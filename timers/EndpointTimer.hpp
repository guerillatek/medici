#pragma once
#include "medici/timers/ManualTimer.hpp"

#include <chrono>
#include <concepts>

namespace medici::timers {

template <event_queue::EventQueueC EventQueueT, event_queue::CallableC ActionT>
class EndpointTimer : private ManualTimer<EventQueueT, ActionT>,
                      public IEndPointTimer {
public:
  using ManualBase = ManualTimer<EventQueueT, ActionT>;
  EndpointTimer(TimerType timerType, ActionT &&action, EventQueueT &eventQueue,
                auto duration)
      : ManualBase{timerType, std::forward<ActionT>(action), eventQueue,
                   duration} {}

  void start() override {
    if (_paused) {
      return;
    }
    ManualBase::start();
  }

  void stop() override { ManualBase::stop(); }

  void pause() override {
    ManualBase::stop();
    _paused = true;
  }

  void resume() override { _paused = false; }

private:
  bool _paused{false};
};

} // namespace medici::timers
