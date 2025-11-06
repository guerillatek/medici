#pragma once
#include "medici/IEndpointEventDispatch.hpp"
#include "medici/event_queue/EventQueue.hpp"

#include <memory>

namespace medici::shm_endpoints {

class ISharedMemEndpointConsumer : public IEndpointEventDispatch {
private:
  virtual medici::ExpectedEventsCount pollAndDispatch() = 0;
  template <event_queue::EventQueueC EventQueueT>
  friend class SharedMemEndpointFactory;
};

class ISharedMemEndpointConsumerServer : public ISharedMemEndpointConsumer {
public:
  virtual std::uint32_t getIncomingChannelIndex() const = 0;
  const char *getIncomingChannelName() const noexcept;
};

using ISharedMemEndpointConsumerPtr =
    std::shared_ptr<ISharedMemEndpointConsumer>;

using ISharedMemEndpointConsumerServerPtr =
    std::shared_ptr<ISharedMemEndpointConsumerServer>;

} // namespace medici::shm_endpoints