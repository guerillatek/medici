#pragma once

#include "medici/event_queue/EventQueue.hpp"
#include "medici/event_queue/concepts.hpp"
#include "medici/sockets/interfaces.hpp"
#include "medici/timers/EndpointTimer.hpp"
#include "medici/timers/concepts.hpp"
#include <chrono>
#include <concepts>
#include <memory>

namespace medici::timers {

template <event_queue::EventQueueC EventQueueT>
class TimerFactory : public ITimerFactory {
public:
  TimerFactory(EventQueueT &eventQueue) : _eventQueue{eventQueue} {}

  ITimerPtr createManualTimer(TimerType timerType,
                              std::chrono::nanoseconds duration,
                              event_queue::CallableT action) {

    return make_unique<ManualTimer<EventQueueT, event_queue::CallableT>>(
        timerType, std::move(action), _eventQueue, duration);
  }

  IEndPointTimerPtr createEndpointTimer(TimerType timerType,
                                        sockets::IIPEndpoint &endpoint,
                                        std::chrono::nanoseconds duration,
                                        event_queue::CallableT action) {
    auto newTimer =
        make_shared<EndpointTimer<EventQueueT, event_queue::CallableT>>(
            timerType, std::move(action), _eventQueue, duration);
    if (endpoint.isActive()) {
      newTimer->start();
    }
    endpoint.getDispatchInterface().registerTimer(newTimer);
    return newTimer;
  }

private:
  EventQueueT &_eventQueue;
};
} // namespace medici::timers