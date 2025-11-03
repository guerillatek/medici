#include "medici/event_queue/EventQueue.hpp"
#include "medici/shm_endpoints/SCMPEndpointsImpl.hpp"
#include "medici/shm_endpoints/SPMCEndpointsImpl.hpp"
#include "medici/shm_endpoints/shmUtils.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace medici::shm_endpoints {

template <event_queue::EventQueueC EventQueueT> class SharedMemEndpointFactory {
public:
  template <typename... MessageTypes>
  using ProducerServerEndpointPtr = std::unique_ptr<
      ServerShmPODProducerEndpoint<EventQueueT, MessageTypes...>>;

  template <typename... MessageTypes>
  using ExpectedProducerServerEndpointPtr =
      std::expected<ProducerServerEndpointPtr<MessageTypes...>, std::string>;

  SharedMemEndpointFactory(EventQueueT &eventQueue) : _eventQueue{eventQueue} {}

  // SPMC Factory Methods

  // Server i.e. a market data server sending data to shared memory clients
  template <typename... MessageTypes>
  ExpectedProducerServerEndpointPtr<MessageTypes...>
  createServerProducerEndpoint(const std::string &queueName,
                               std::uint32_t allowedConsumers,
                               std::uint32_t consumerQueueSize

  ) {
    try {
      auto serverProducer = std::make_unique<
          ServerShmPODProducerEndpoint<EventQueueT, MessageTypes...>>(
          queueName, allowedConsumers, consumerQueueSize, _eventQueue);
      return serverProducer;
    } catch (const std::exception &ex) {
      return std::unexpected(
          std::format("Failed to create shared memory server producer endpoint "
                      "queueName='{}', error='{}'",
                      queueName.c_str(), ex.what()));
    }
    return {};
  }

  // Client i.e. a market data client receiving data from a shared memory server
  template <typename... MessageTypes>
  std::expected<ISharedMemEndpointConsumerPtr, std::string>
  createClientConsumerEndpoint(const std::string &queueName,
                               const std::string &consumerId, auto &&handler) {
    using HandlerT = std::decay_t<decltype(handler)>;

    try {
      auto newClientConsumer = std::make_shared<
          ClientShmPODConsumerEndpoint<EventQueueT, HandlerT, MessageTypes...>>(
          queueName, consumerId, std::move(handler), _eventQueue);

      _registeredConsumers.push_back(newClientConsumer);
      return newClientConsumer;
    } catch (const std::exception &ex) {
      return std::unexpected(std::format(
          "Failed to create shared memory client consumer endpoint  "
          "queueName = '{}'\n{}",
          queueName.c_str(), ex.what()));
    }
    return {};
  }

  // SCMP Factory Methods

  // Server Consumer i.e. a market data server receiving subscription requests
  // from shared memory clients
  template <typename... MessageTypes>
  std::expected<ISharedMemEndpointConsumerPtr, std::string>
  createServerConsumerEndpoint(const std::string &queueName,
                               std::uint32_t allowedProducers,
                               std::uint32_t producerQueueSize, auto &&handler

  ) {

    using HandlerT = std::decay_t<decltype(handler)>;
    try {
      auto serverConsumer = std::make_shared<
          ServerShmPODConsumerEndpoint<EventQueueT, HandlerT, MessageTypes...>>(
          queueName, std::move(handler), allowedProducers, producerQueueSize,
          _eventQueue);
      _registeredConsumers.push_back(serverConsumer);
      return serverConsumer;
    } catch (const std::exception &ex) {
      return std::unexpected(
          std::format("Failed to create shared memory server consumer endpoint "
                      "queueName='{}', error='{}'",
                      queueName.c_str(), ex.what()));
    }
    return {};
  }

  template <typename... MessageTypes>
  using ProducerClientEndpointPtr = std::unique_ptr<
      ClientShmPODProducerEndpoint<EventQueueT, MessageTypes...>>;

  template <typename... MessageTypes>
  using ExpectedProducerClientEndpointPtr =
      std::expected<ProducerClientEndpointPtr<MessageTypes...>, std::string>;

  // Client  i.e. a market data client sending a subscription request to a
  // server
  template <typename... MessageTypes>
  ExpectedProducerClientEndpointPtr<MessageTypes...>
  createClientProducerEndpoint(const std::string &queueName,
                               const std::string &producerId) {
    try {
      auto newClientProducer = std::make_unique<
          ClientShmPODProducerEndpoint<EventQueueT, MessageTypes...>>(
          queueName, producerId, _eventQueue);

      return newClientProducer;
    } catch (const std::exception &ex) {
      return std::unexpected(std::format(
          "Failed to create shared memory client producer endpoint  "
          "queueName = '{}'\n{}",
          queueName.c_str(), ex.what()));
    }
    return {};
  }

  ExpectedEventsCount pollAndDispatchEndpointsEvents() {
    ExpectedEventsCount totalDispatchedEvents{0};
    for (auto &consumer : _registeredConsumers) {
      auto result = consumer->pollAndDispatch();
      if (!result) {
        return std::unexpected(result.error());
      }
      totalDispatchedEvents.value() += result.value();
    }
    return totalDispatchedEvents;
  }

private:
  EventQueueT &_eventQueue;
  std::vector<ISharedMemEndpointConsumerPtr> _registeredConsumers;
};

} // namespace medici::shm_endpoints