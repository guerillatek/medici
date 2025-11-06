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
template <event_queue::EventQueueC EventQueueT, typename... MessageTypes>
class ServerShmPODProducerEndpoint {
public:
  using QueueDefinition = SharedMemPODQueueDefinition<MessageTypes...>;
  using MessageQueueEntry = typename QueueDefinition::MessageQueueEntry;
  using SharedMemoryLayout = typename QueueDefinition::SharedMemoryLayout;
  using QueueChannelT = typename QueueDefinition::QueueChannelT;
  using TypeTuple = std::tuple<MessageTypes...>;
  using PayloadVariantT = std::variant<MessageTypes...>;
  using ConsumerBackPressureQueue = std::deque<PayloadVariantT>;
  using ConsumerBackPressureChannels = std::vector<ConsumerBackPressureQueue>;
  using SharedMemoryObjectManagerT =
      SharedMemoryObjectManager<SharedMemoryLayout>;

  ServerShmPODProducerEndpoint(const std::string &sharedMemName,
                               std::uint32_t consumerCount,
                               std::uint32_t consumerQueueSize,
                               EventQueueT &eventQueue)
      : _sharedMemoryObjectMgr{sharedMemName,
                               std::max(4096UL, sizeof(SharedMemoryLayout) +
                                                    QueueChannelT::getQueueSize(
                                                        consumerQueueSize) *
                                                        consumerCount),
                               ShmQueueType::SPMCQueue, consumerCount,
                               consumerQueueSize},
        _eventQueue{eventQueue}, _sharedMemName{sharedMemName} {
    _backPressureChannels.resize(consumerCount);
  };

  // push message to all active consumer channels with custom back pressure
  // handler
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

    auto activeChannels =
        _sharedMemoryObjectMgr->_activeChannels.load(std::memory_order_acquire);
    for (std::uint32_t channelIndex = 0; channelIndex < activeChannels;
         ++channelIndex) {

      auto result = pushMessageToChannel(
          channelIndex, entry, message,
          std::forward<decltype(backPressureHandler)>(backPressureHandler));
      if (!result) {
        return result;
      }
    }
    return {};
  }

  // push message to particular channel with custom back pressure handler
  Expected pushMessage(std::uint32_t channelIndex, const auto &message,
                       auto &&backPressureHandler) {
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

    return pushMessageToChannel(
        channelIndex, entry, message,
        std::forward<decltype(backPressureHandler)>(backPressureHandler));
  }

  // push message to all active consumer channels with default back pressure
  // handler
  Expected pushMessage(const auto &message) {
    return pushMessage(message, [this](const auto &message) -> Expected {
      return applyDefaultBackPressureHandler(message);
    });
  }

  // push message to particular channel with default back pressure handler
  Expected pushMessage(std::uint32_t channelIndex, const auto &message) {
    return pushMessage(channelIndex, message,
                       [this](const auto &message) -> Expected {
                         return applyDefaultBackPressureHandler(message);
                       });
  }

  Expected applyDefaultBackPressureHandler(const auto &message) {

    auto &backPressureQueue =
        _backPressureChannels[_sharedMemoryObjectMgr->_activeOutgoingChannel];
    if (backPressureQueue.empty()) {
      // Back queue is empty, need to start async
      // processing
      startBackPressureQueue(message,
                             _sharedMemoryObjectMgr->_activeOutgoingChannel);
      return {};
    }
    // Queue already active so just amend it
    backPressureQueue.push_back(message);
    return {};
  }

  void detachSharedMemoryOnDestruct(bool enableCleanup) {
    _cleanupOnDestruct = enableCleanup;
  }

  ~ServerShmPODProducerEndpoint() {
    if (_cleanupOnDestruct) {
      boost::interprocess::shared_memory_object::remove(_sharedMemName.c_str());
    }
  }

  std::uint32_t getActiveConsumerChannelCount() const {
    if (!_sharedMemoryObjectMgr) {
      return 0;
    }
    return _sharedMemoryObjectMgr->_activeChannels.load(
        std::memory_order_acquire);
  }

  Expected startBackPressureQueue(const auto &message,
                                  std::uint32_t channelIndex) {
    auto &backPressureQueue = _backPressureChannels[channelIndex];
    backPressureQueue.push_back(message);
    auto &channel = _sharedMemoryObjectMgr->getQueueChannel(channelIndex);
    return _eventQueue.postAsyncAction([&channel,
                                        &backPressureQueue]() -> AsyncExpected {
      while (!backPressureQueue.empty()) {
        const auto &bufferedMessage = backPressureQueue.front();

        bool enqueuedFront = std::visit(
            [&backPressureQueue, &channel](const auto &message) {
              auto typeIndex =
                  GetTypeIndex<std::decay_t<decltype(message)>>(TypeTuple{});
              MessageQueueEntry entry;
              entry._typeIndex = typeIndex;
              std::memcpy(entry.messageData, &message,
                          sizeof(std::decay_t<decltype(message)>));
              if (channel.enqueue(entry)) {
                backPressureQueue.pop_front();
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

  auto &getBackPressureChannels() { return _backPressureChannels; }

private:
  Expected pushMessageToChannel(std::uint32_t channelIndex,
                                const MessageQueueEntry &entry,
                                const auto &message,
                                auto &&backPressureHandler) {
    auto activeChannels =
        _sharedMemoryObjectMgr->_activeChannels.load(std::memory_order_acquire);
    if (channelIndex >= activeChannels) {
      return std::unexpected("Invalid channel index for pushMessage");
    }

    auto &backPressureQueue = _backPressureChannels[channelIndex];
    _sharedMemoryObjectMgr->_activeOutgoingChannel = channelIndex;
    if (!backPressureQueue.empty()) {
      // content in backpressure queue so call handler
      auto result = backPressureHandler(message);
      if (!result) {
        return result;
      }
      return {};
    }
    QueueChannelT &channel =
        _sharedMemoryObjectMgr->getQueueChannel(channelIndex);

    if (channel.enqueue(entry)) {
      return {};
    }
    auto result = backPressureHandler(message);
    if (!result) {
      return result;
    }

    return {};
  }

  SharedMemoryObjectManagerT _sharedMemoryObjectMgr;
  ConsumerBackPressureChannels _backPressureChannels{};
  EventQueueT &_eventQueue;
  bool _cleanupOnDestruct{true};
  std::string _sharedMemName{};
};

// This endpoint serves as the consumer side of the
// multichannel shared memory queue configured for a single producer, multiple
// consumer configuration. As a client endpoint, it connects to an existing
// shared memory segment created by a ServerShmPODProducerEndpoint and
// consumes messages from its dedicated channel.
template <typename EventQueueT, typename HandlerT, typename... MessageTypes>
class ClientShmPODConsumerEndpoint : public ISharedMemEndpointConsumer {
public:
  using QueueDefinition = SharedMemPODQueueDefinition<MessageTypes...>;
  using MessageQueueEntry = typename QueueDefinition::MessageQueueEntry;
  using SharedMemoryLayout = typename QueueDefinition::SharedMemoryLayout;
  using QueueChannelT = typename QueueDefinition::QueueChannelT;
  using TypeTuple = std::tuple<MessageTypes...>;
  using SharedMemoryObjectManagerT =
      SharedMemoryObjectManager<SharedMemoryLayout>;

      ClientShmPODConsumerEndpoint(const std::string &queueName,
                                   const std::string &consumerId,
                                   const HandlerT &handler,
                                   EventQueueT &eventQueue)
      : _sharedMemoryObjectMgr{queueName}, _handler{handler},
        _channel{_sharedMemoryObjectMgr->AllocQueueChannel(consumerId)},
        _eventQueue{eventQueue} {
    if (_sharedMemoryObjectMgr->_queueType != ShmQueueType::SPMCQueue) {
      throw std::runtime_error(
          std::format("Shared memory queue type mismatch for consumer "
                      "endpoint accessing queue '{}'",
                      queueName));
    }
    auto value = typeid(std::tuple<MessageTypes...>).hash_code();
    if (_sharedMemoryObjectMgr->_messageTypesHash != value) {
      throw std::runtime_error(
          std::format("Supported message type mismatch for consumer endpoint "
                      "accessing queue '{}'",
                      queueName));
    }
  }

  ExpectedEventsCount pollAndDispatch() {
    if (!_sharedMemoryObjectMgr) {
      return std::unexpected("Shared memory not initialized");
    }
    std::uint32_t dispatchedEvents = 0;
    while (_channel.updatesAvailable()) {
      auto result = onPayloadReady(_eventQueue.getClock()());
      if (!result) {
        return std::unexpected(result.error());
      }
      ++dispatchedEvents;
    }
    return dispatchedEvents;
  }

  auto &getQueueChannel() const { return _channel; }

private:
  Expected onActive() override { return {}; }

  // Interface for IEndpointEventDispatch
  Expected onDisconnected(const std::string &reason) override { return {}; }

  Expected onPayloadReady(TimePoint readTime) override {
    return _channel.consumeAvailable([this](const MessageQueueEntry &entry) {
      return QueueDefinition::dispatchToHandler(entry, _handler);
    });
  }

  Expected onShutdown() override { return {}; }

  Expected registerTimer(const timers::IEndPointTimerPtr &timer) {
    return std::unexpected("Timer registration not supported");
  }

protected:
  SharedMemoryObjectManagerT _sharedMemoryObjectMgr;
  std::string _queueName{};
  HandlerT _handler;
  QueueChannelT &_channel;
  EventQueueT &_eventQueue;
};

} // namespace medici::shm_endpoints