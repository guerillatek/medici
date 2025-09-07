#pragma once
#include "medici/event_queue/EventQueue.hpp"
#include "medici/timers/interfaces.hpp"
#include <chrono>
#include <concepts>

namespace medici::timers {

template <event_queue::EventQueueC EventQueueT, event_queue::CallableC ActionT>
class ManualTimer : public ITimer {
public:
  ManualTimer(TimerType timerType, ActionT &&action, EventQueueT &eventQueue,
              auto duration)
      : _eventQueue{eventQueue}, _action{std::move(action)},
        _duration{duration}, _timerType{timerType} {}

  void start() override {
    if (_active) {
      return;
    }
    _active = true;
    if (_timerType == TimerType::Precision) {
      _eventQueue.postPrecisionTimedAction(_eventQueue.getClock()() + _duration,
                                           [this]() { return runAndRePost(); });
    } else {
      _eventQueue.postIdleTimedAction(_eventQueue.getClock()() + _duration,
                                      [this]() { return runAndRePost(); });
    }
  }
  void stop() override { _active = false; }

private:
  Expected runAndRePost() {
    if (_active == false) {
      return {};
    }
    auto result = _action();
    if (!result) {
      _active = false;
      return result;
    }
    if (!_active) {
      return result;
    }

    if (_timerType == TimerType::Precision) {
      _eventQueue.postPrecisionTimedAction(_eventQueue.getClock()() + _duration,
                                           [this]() { return runAndRePost(); });
    } else {
      _eventQueue.postIdleTimedAction(_eventQueue.getClock()() + _duration,
                                      [this]() { return runAndRePost(); });
    }
    return result;
  }

  EventQueueT &_eventQueue;
  ActionT _action;
  std::chrono::nanoseconds _duration;
  TimerType _timerType;
  bool _active{false};
};

} // namespace medici::timers
