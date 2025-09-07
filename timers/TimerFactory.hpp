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

template <event_queue::EventQueueC EventQueueT> class TimerFactory {
public:
  TimerFactory(EventQueueT &eventQueue) : _eventQueue{eventQueue} {}

  ITimerPtr createManualTimer(TimerType timerType, auto duration,
                              event_queue::CallableC auto &&action) {
    using TimerActionT = std::decay_t<decltype(action)>;
    return make_unique<ManualTimer<EventQueueT, TimerActionT>>(
        timerType, std::forward<TimerActionT>(action), _eventQueue, duration);
  }

  IEndPointTimerPtr createEndpointTimer(TimerType timerType,
                                        sockets::IIPEndpoint &endpoint,
                                        auto duration,
                                        event_queue::CallableC auto &&action) {
    using TimerActionT = std::decay_t<decltype(action)>;
    auto newTimer = make_shared<EndpointTimer<EventQueueT, TimerActionT>>(
        timerType, std::forward<TimerActionT>(action), _eventQueue, duration);
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