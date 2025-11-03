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

using ISharedMemEndpointConsumerPtr =
    std::shared_ptr<ISharedMemEndpointConsumer>;

} // namespace medici::shm_endpoints