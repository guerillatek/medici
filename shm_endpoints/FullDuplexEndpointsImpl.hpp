#pragma once

#include "medici/shm_endpoints/SCMPEndpointsImpl.hpp"
#include "medici/shm_endpoints/SPMCEndpointsImpl.hpp"

#include <memory>
#include <string>
#include <tuple>

namespace medici::shm_endpoints {

struct ConsumerProducerMapEntry {
  std::uint32_t consumerIndex{};
  std::uint32_t producerIndex{};
};

struct DuplexSegmentMemoryLayout {
  // Metadata stored in shared memory for consumers to discover
  const ShmQueueType _queueType{ShmQueueType::FullDuplexQueue};
  const std::uint32_t _allowedClients;
  std::uint32_t _activeClients{0};
  char _serverProducerQueueName[128]{};
  char _serverConsumerQueueName[128]{};
  pthread_mutex_t _allocationMutex;
  ConsumerProducerMapEntry _consumerProducerMap[];

  DuplexSegmentMemoryLayout(std::uint32_t allowedClients)
      : _allowedClients{allowedClients} {

    // Initialize multiprocess mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);

    int result = pthread_mutex_init(&_allocationMutex, &attr);
    pthread_mutexattr_destroy(&attr);
    if (result != 0) {
      throw std::runtime_error("Failed to initialize multiprocess mutex: " +
                               std::string(strerror(result)));
    }

    for (std::uint32_t i = 0; i < allowedClients; ++i) {
      _consumerProducerMap[i] = ConsumerProducerMapEntry{};
    }
  }

  void AddMapEntry(std::uint32_t consumerIndex, std::uint32_t producerIndex) {
    // Lock the mutex for exclusive access
    int lockResult = pthread_mutex_lock(&_allocationMutex);
    if (lockResult != 0) {
      throw std::runtime_error("Failed to acquire allocation map mutex: " +
                               std::string(strerror(lockResult)));
    }

    // Ensure we unlock even if an exception is thrown
    MutexGuard guard{&_allocationMutex};
    if (_activeClients >= _allowedClients) {
      throw std::runtime_error(
          "Maximum number of allowed clients exceeded in duplex segment");
    }
    auto &entry = _consumerProducerMap[_activeClients];
    entry.consumerIndex = consumerIndex;
    entry.producerIndex = producerIndex;
    ++_activeClients;
  }
};

using DuplexMemoryLayoutMgrT =
    SharedMemoryObjectManager<DuplexSegmentMemoryLayout>;

template <event_queue::EventQueueC EventQueueT, typename ConsumerHandlerT,
          typename ProducerMessageTypesT, typename ConsumerMessageTypesT>
class FullDuplexServerShmEndpoint {

  template <typename... MessageTypes>
  static constexpr auto GetServerConsumerT(std::tuple<MessageTypes...>)
      -> ServerShmPODConsumerEndpoint<EventQueueT, ConsumerHandlerT,
                                      MessageTypes...> {
    return {};
  }

  template <typename... MessageTypes>
  static constexpr auto GetServerProducerT(std::tuple<MessageTypes...>)
      -> ServerShmPODProducerEndpoint<EventQueueT, MessageTypes...> {
    return {};
  }

  template <typename... MessageTypes>
  auto GetServerConsumer(std::tuple<MessageTypes...>, auto &shmEndpointFactory,
                         const std::string &queueName, std::uint32_t producers,
                         std::uint32_t producerQueueSize, auto &&handler) {
    return shmEndpointFactory
        .template createServerConsumerEndpoint<MessageTypes...>(
            queueName, producers, producerQueueSize,
            std::forward<decltype(handler)>(handler));
  }

  template <typename... MessageTypes>
  auto GetServerProducer(std::tuple<MessageTypes...>, auto &shmEndpointFactory,
                         const std::string &queueName, std::uint32_t consumers,
                         std::uint32_t consumerQueueSize) {
    return shmEndpointFactory
        .template createServerProducerEndpoint<MessageTypes...>(
            queueName, consumers, consumerQueueSize);
  }

public:
  using ServerProducerT =
      decltype(FullDuplexServerShmEndpoint::GetServerProducerT(
          ProducerMessageTypesT{}));

  FullDuplexServerShmEndpoint(const std::string &name, std::uint32_t maxClients,
                              size_t consumerQueueSize,
                              size_t producerQueueSize,
                              ConsumerHandlerT &&handler,
                              auto &shmEndpointFactory)
      : _serverSharedMemMgr{name,
                            sizeof(DuplexSegmentMemoryLayout) +
                                sizeof(ConsumerProducerMapEntry) * maxClients,
                            maxClients},
        _handler{std::move(handler)} {

    auto consumerQueueName =
        std::format("{}_consumer_{}", name, std::rand()); // Unique name
    auto producerQueueName =
        std::format("{}_producer_{}", name, std::rand()); // Unique name
    // Initialize names
    strncpy(_serverSharedMemMgr->_serverConsumerQueueName,
            consumerQueueName.c_str(),
            sizeof(_serverSharedMemMgr->_serverConsumerQueueName) - 1);
    strncpy(_serverSharedMemMgr->_serverProducerQueueName,
            producerQueueName.c_str(),
            sizeof(_serverSharedMemMgr->_serverProducerQueueName) - 1);
    auto producerResult =
        GetServerProducer(ProducerMessageTypesT{}, shmEndpointFactory,
                          producerQueueName, maxClients, consumerQueueSize);
    if (!producerResult) {
      throw std::runtime_error(producerResult.error());
    }
    _serverProducerEndpoint = std::move(*producerResult);
    auto consumerResult = GetServerConsumer(
        ConsumerMessageTypesT{}, shmEndpointFactory, consumerQueueName,
        maxClients, producerQueueSize, [this](const auto &payload) -> Expected {
          auto consumerChannelIndex =
              _serverConsumerEndpoint->getIncomingChannelIndex();
          // Find the client index from the channel consumer producer map.
          for (std::uint32_t i = 0; i < _serverSharedMemMgr->_activeClients;
               ++i) {
            if (_serverSharedMemMgr->_consumerProducerMap[i].consumerIndex ==
                consumerChannelIndex) {
              _activeClientProducerIndex =
                  _serverSharedMemMgr->_consumerProducerMap[i].producerIndex;
              break;
            }
          }
          return _handler(payload);
        });
    if (!consumerResult) {
      throw std::runtime_error(consumerResult.error());
    }
    _serverConsumerEndpoint = consumerResult.value();
  }

  // Push message to active client producer channel
  Expected pushResponseMessage(const auto &message,
                               auto &&backPressureHandler) {
    if (!_serverProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _serverProducerEndpoint->pushMessage(
        _activeClientProducerIndex, message,
        std::forward<decltype(backPressureHandler)>(backPressureHandler));
  }

  Expected pushResponseMessage(const auto &message) {
    if (!_serverProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _serverProducerEndpoint->pushMessage(_activeClientProducerIndex,
                                                message);
  }

  // Push message to specific client producer channel
  Expected pushMessage(std::uint32_t channelIndex, const auto &message,
                       auto &&backPressureHandler) {
    if (!_serverProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _serverProducerEndpoint->pushMessage(
        channelIndex, message,
        std::forward<decltype(backPressureHandler)>(backPressureHandler));
  }

  Expected pushMessage(std::uint32_t channelIndex, const auto &message) {
    if (!_serverProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _serverProducerEndpoint->pushMessage(channelIndex, message);
  }

  // Push message to all client producer channels
  Expected pushMessage(const auto &message, auto &&backPressureHandler) {
    if (!_serverProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _serverProducerEndpoint->pushMessage(
        message,
        std::forward<decltype(backPressureHandler)>(backPressureHandler));
  }

  Expected pushMessage(const auto &message) {
    if (!_serverProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _serverProducerEndpoint->pushMessage(message);
  }

  std::uint32_t getIncomingChannelIndex() const {
    return _serverConsumerEndpoint->getIncomingChannelIndex();
  }

private:
  ConsumerHandlerT _handler;
  DuplexMemoryLayoutMgrT _serverSharedMemMgr;
  std::unique_ptr<ServerProducerT> _serverProducerEndpoint{};
  ISharedMemEndpointConsumerServerPtr _serverConsumerEndpoint{};
  std::uint32_t _activeClientProducerIndex{0};
  std::uint32_t _activeClientConsumerIndex{0};
};

template <event_queue::EventQueueC EventQueueT, typename ConsumerHandlerT,
          typename ConsumerMessageTypes, typename ProducerMessageTypes>
class FullDuplexClientShmEndpoint {

  template <typename... MessageTypes>
  static constexpr auto GetConsumerClientT(std::tuple<MessageTypes...>)
      -> ClientShmPODConsumerEndpoint<EventQueueT, ConsumerHandlerT,
                                      MessageTypes...> {
    return {};
  }

  template <typename... MessageTypes>
  static constexpr auto GetProducerClientT(std::tuple<MessageTypes...>)
      -> ClientShmPODProducerEndpoint<EventQueueT, MessageTypes...> {
    return {};
  }

  template <typename... MessageTypes>
  auto GetConsumerClient(std::tuple<MessageTypes...>,
                         const std::string &queueName,
                         const std::string &clientId, auto &shmEndpointFactory,
                         auto &&handler) {
    return shmEndpointFactory
        .template createClientConsumerEndpoint<MessageTypes...>(
            queueName, clientId, std::forward<decltype(handler)>(handler));
  }

  template <typename... MessageTypes>
  auto GetProducerClient(std::tuple<MessageTypes...>, auto &shmEndpointFactory,
                         const std::string &queueName,
                         const std::string &clientId) {
    return shmEndpointFactory
        .template createClientProducerEndpoint<MessageTypes...>(queueName,
                                                                clientId);
  }

public:
  using ClientConsumerT = decltype(GetConsumerClientT(ConsumerMessageTypes{}));
  using ClientProducerT = decltype(GetProducerClientT(ProducerMessageTypes{}));

  FullDuplexClientShmEndpoint(const std::string &name,
                              const std::string &clientId,
                              ConsumerHandlerT &&handler,
                              auto &shmEndpointFactory)
      : _serverSharedMemMgr{name} {

    if (_serverSharedMemMgr->_queueType != ShmQueueType::FullDuplexQueue) {
      throw std::runtime_error(
          std::format("Shared memory queue type mismatch for duplex client "
                      "endpoint accessing queue '{}'",
                      name));
    }

    auto clientProducerResult = GetProducerClient(
        ProducerMessageTypes{}, shmEndpointFactory,
        _serverSharedMemMgr->_serverConsumerQueueName, clientId);
    if (!clientProducerResult) {
      throw std::runtime_error(clientProducerResult.error());
    }
    _clientProducerEndpoint = std::move(*clientProducerResult);

    auto clientConsumerResult = GetConsumerClient(
        ConsumerMessageTypes{}, _serverSharedMemMgr->_serverProducerQueueName,
        clientId, shmEndpointFactory, std::forward<decltype(handler)>(handler));
    if (!clientConsumerResult) {
      throw std::runtime_error(clientConsumerResult.error());
    }
    _clientConsumerEndpoint =
        std::static_pointer_cast<ClientConsumerT>(clientConsumerResult.value());
    _serverSharedMemMgr->AddMapEntry(
        _clientConsumerEndpoint->getQueueChannel().getChannelIndex(),
        _clientProducerEndpoint->getQueueChannel().getChannelIndex());
  }

  // Push message to all client producer channels
  Expected pushMessage(const auto &message, auto &&backPressureHandler) {
    if (!_clientProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _clientProducerEndpoint->pushMessage(
        message,
        std::forward<decltype(backPressureHandler)>(backPressureHandler));
  }

  Expected pushMessage(const auto &message) {
    if (!_clientProducerEndpoint) {
      return std::unexpected("Server producer endpoint not initialized");
    }
    return _clientProducerEndpoint->pushMessage(message);
  }

  auto &getBackPressureQueue() const {
    return _clientProducerEndpoint->getBackPressureQueue();
  }

private:
  ConsumerHandlerT _handler;
  DuplexMemoryLayoutMgrT _serverSharedMemMgr;
  std::unique_ptr<ClientProducerT> _clientProducerEndpoint{};
  std::shared_ptr<ClientConsumerT> _clientConsumerEndpoint{};
  std::uint32_t _activeClientProducerIndex{0};
};

} // namespace medici::shm_endpoints
