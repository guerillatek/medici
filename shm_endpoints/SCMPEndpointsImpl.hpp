#pragma once

#include "medici/event_queue/EventQueue.hpp"
#include "medici/shm_endpoints/SharedMemPODQueueDefinition.hpp"
#include "medici/shm_endpoints/SharedMemoryObjectManager.hpp"

#include <deque>
#include <vector>

namespace medici::shm_endpoints {

// This endpoint serves as the producer side of the
// multichannel shared memory queue configured for a single producer, multiple
// consumer configuration. As a server endpoint, it is responsible for creating
// and managing the shared memory segment and must be instantiated before any
// consumer endpoints can connect to it.
template <event_queue::EventQueueC EventQueueT, typename HandlerT,
          typename... MessageTypes>
class ServerShmPODConsumerEndpoint : public ISharedMemEndpointConsumerServer {
public:
  using QueueDefinition = SharedMemPODQueueDefinition<MessageTypes...>;
  using MessageQueueEntry = typename QueueDefinition::MessageQueueEntry;
  using SharedMemoryLayout = typename QueueDefinition::SharedMemoryLayout;
  using QueueChannelT = typename QueueDefinition::QueueChannelT;
  using TypeTuple = std::tuple<MessageTypes...>;
  using SharedMemoryObjectManagerT =
      SharedMemoryObjectManager<SharedMemoryLayout>;

  ServerShmPODConsumerEndpoint(const std::string &sharedMemName,
                               const HandlerT &handler,
                               std::uint32_t producerCount,
                               std::uint32_t producerQueueSize,
                               EventQueueT &eventQueue)
      : _sharedMemoryObjectMgr{sharedMemName,
                               std::max(4096UL, sizeof(SharedMemoryLayout) +
                                                    QueueChannelT::getQueueSize(
                                                        producerQueueSize) *
                                                        producerCount),
                               ShmQueueType::MPSCQueue, producerCount,
                               producerQueueSize},
        _eventQueue{eventQueue}, _sharedMemName{sharedMemName},
        _handler{handler} {};

  auto &getIncomingChannel() const  {
    if (!_sharedMemoryObjectMgr) {
      throw std::runtime_error("Shared memory not initialized");
    }
    return _sharedMemoryObjectMgr->getQueueChannel(_activeIncomingChannel);
  }

  std::uint32_t getIncomingChannelIndex() const override {
    return _activeIncomingChannel;
  }

  ExpectedEventsCount pollAndDispatch() {
    if (!_sharedMemoryObjectMgr) {
      return std::unexpected("Shared memory not initialized");
    }

    auto activeChannels =
        _sharedMemoryObjectMgr->_activeChannels.load(std::memory_order_acquire);
    std::uint32_t dispatchedEvents = 0;
    for (std::uint32_t channelIndex = 0; channelIndex < activeChannels;
         ++channelIndex) {
      _activeIncomingChannel = channelIndex;
      QueueChannelT &channel =
          _sharedMemoryObjectMgr->getQueueChannel(channelIndex);
      while (channel.updatesAvailable()) {
        auto result =
            channel.consumeAvailable([this](const MessageQueueEntry &entry) {
              return QueueDefinition::dispatchToHandler(entry, _handler);
            });
        if (!result) {
          return std::unexpected(result.error());
        }
        ++dispatchedEvents;
      }
    }

    return dispatchedEvents;
  }

private:
  Expected onActive() override { return {}; }

  // Interface for IEndpointEventDispatch
  Expected onDisconnected(const std::string &reason) override { return {}; }

  Expected onPayloadReady(TimePoint readTime) override { return {}; }

  Expected onShutdown() override { return {}; }

  Expected registerTimer(const timers::IEndPointTimerPtr &timer) {
    return std::unexpected("Timer registration not supported");
  }

  SharedMemoryObjectManagerT _sharedMemoryObjectMgr;
  EventQueueT &_eventQueue;
  std::string _sharedMemName{};
  HandlerT _handler;
  std::uint32_t _activeIncomingChannel{0};
};

// This endpoint serves as the producer side of the
// multichannel shared memory queue configured for a single producer, multiple
// producer configuration. As a client endpoint, it connects to an existing
// shared memory segment created by a ServerShmPODProducerEndpoint and
// consumes messages from its dedicated channel.
template <typename EventQueueT, typename... MessageTypes>
class ClientShmPODProducerEndpoint {
public:
  using QueueDefinition = SharedMemPODQueueDefinition<MessageTypes...>;
  using MessageQueueEntry = typename QueueDefinition::MessageQueueEntry;
  using SharedMemoryLayout = typename QueueDefinition::SharedMemoryLayout;
  using QueueChannelT = typename QueueDefinition::QueueChannelT;
  using TypeTuple = std::tuple<MessageTypes...>;
  using PayloadVariantT = std::variant<MessageTypes...>;
  using ProducerBackPressureQueue = std::deque<PayloadVariantT>;
  using SharedMemoryObjectManagerT =
      SharedMemoryObjectManager<SharedMemoryLayout>;

  ClientShmPODProducerEndpoint(const std::string &queueName,
                               const std::string &producerId,
                               EventQueueT &eventQueue)
      : _sharedMemoryObjectMgr{queueName},
        _channel{_sharedMemoryObjectMgr->AllocQueueChannel(producerId)},
        _eventQueue{eventQueue} {
    if (_sharedMemoryObjectMgr->_queueType != ShmQueueType::MPSCQueue) {
      throw std::runtime_error(
          std::format("Shared memory queue type mismatch for producer "
                      "endpoint accessing queue '{}'",
                      queueName));
    }
    auto value = typeid(std::tuple<MessageTypes...>).hash_code();
    if (_sharedMemoryObjectMgr->_messageTypesHash != value) {
      throw std::runtime_error(
          std::format("Supported message type mismatch for producer endpoint "
                      "accessing queue '{}'",
                      queueName));
    }
  }

  Expected pushMessage(const auto &message, auto &&backPressureHandler) {
    if (!_sharedMemoryObjectMgr) {
      return std::unexpected("Shared memory not initialized");
    }
    auto typeIndex = GetTypeIndex<std::decay_t<decltype(message)>>(TypeTuple{});
    if (typeIndex == -1) {
      return std::unexpected("Message type not supported");
    }

    MessageQueueEntry entry;
    entry._typeIndex = typeIndex;
    std::memcpy(entry.messageData, &message,
                sizeof(std::decay_t<decltype(message)>));

    if (!_backPressureQueue.empty()) {
      // content in backpressure queue so call handler
      auto result = backPressureHandler(message);
      if (!result) {
        return result;
      }
      return {};
    }

    if (_channel.enqueue(entry)) {
      return {};
    }
    auto result = backPressureHandler(message);
    if (!result) {
      return result;
    }

    return {};
  }

  Expected applyDefaultBackPressureHandler(const auto &message) {

    if (_backPressureQueue.empty()) {
      // Back queue is empty, need to start async
      // processing
      startBackPressureQueue(message);
      return {};
    }
    // Queue already active so just amend it
    _backPressureQueue.push_back(message);
    return {};
  }

  // This is queue based push method that allows for back pressure buffering
  // per producer channel. If a channel is full, the message is buffered in a
  // per-channel back pressure queue.
  Expected pushMessage(const auto &message) {
    return pushMessage(message, [this](const auto &message) -> Expected {
      return applyDefaultBackPressureHandler(message);
    });
  }

  auto &getBackPressureQueue() { return _backPressureQueue; }

  auto &getQueueChannel() const { return _channel; }

private:
  Expected startBackPressureQueue(const auto &message) {
    _backPressureQueue.push_back(message);
    return _eventQueue.postAsyncAction([this]() -> AsyncExpected {
      while (!_backPressureQueue.empty()) {
        const auto &bufferedMessage = _backPressureQueue.front();

        bool enqueuedFront = std::visit(
            [this](const auto &message) {
              auto typeIndex =
                  GetTypeIndex<std::decay_t<decltype(message)>>(TypeTuple{});
              MessageQueueEntry entry;
              entry._typeIndex = typeIndex;
              std::memcpy(entry.messageData, &message,
                          sizeof(std::decay_t<decltype(message)>));
              if (_channel.enqueue(entry)) {
                _backPressureQueue.pop_front();
                return true;
              }
              return false;
            },
            bufferedMessage);
        if (!enqueuedFront) {
          return false;
        }
      }
      return true;
    });
  }

protected:
  ProducerBackPressureQueue _backPressureQueue;
  SharedMemoryObjectManagerT _sharedMemoryObjectMgr;
  std::string _queueName{};
  QueueChannelT &_channel;
  EventQueueT &_eventQueue;
};

} // namespace medici::shm_endpoints