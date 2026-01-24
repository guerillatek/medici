#pragma once

#include <concepts>
#include <expected>
#include <iostream>
#include <string_view>

#include "medici/IEndpointEventDispatch.hpp"
#include "medici/application/IPContextThreadConfig.hpp"
#include "medici/application/IAppContext.hpp"
#include "medici/application/concepts.hpp"
#include "medici/event_queue/EventQueue.hpp"
#include "medici/event_queue/concepts.hpp"
#include "medici/sockets/concepts.hpp"
#include "medici/time.hpp"
#include "medici/timers/TimerFactory.hpp"

namespace medici::application {



template <sockets::SocketFactoryC SocketFactoryT, ClockNowC ClockNowT,
          EndpointEventPollMgrC EndpointEventPollMgrT,
          std::uint32_t MaxProducerQueueSize = 2048>
class IPAppRunContext : public IAppContext {
public:
  using EventQueueT = event_queue::EventQueue<EndpointEventPollMgrT, ClockNowT,
                                              MaxProducerQueueSize>;
  using TimerFactoryT = timers::TimerFactory<EventQueueT>;
  template <typename... Args>
  IPAppRunContext(const std::string &sessionName,
                  std::uint32_t maxProducerThreads,
                  std::chrono::microseconds inActivitySleepDuration,
                  ClockNowT clock, Args &&...args)
      : _clock{clock}, _endpointPollManager{sessionName, _clock, _eventQueue,
                                            std::forward<Args>(args)...},
        _socketFactory{_endpointPollManager},
        _eventQueue{sessionName, _endpointPollManager, _clock,
                    maxProducerThreads, inActivitySleepDuration},
        _timerFactory{_eventQueue} {}

  IPAppRunContext(const IPContextThreadConfig &config, ClockNowT clock)
      : IPAppRunContext{
            config.name(), config.producers(), config.inactivityMicros(),
            _clock,        config.certFile(),  config.keyFile()} {}

  auto &getSocketFactory() { return _socketFactory; }

  auto &getEventQueue() { return _eventQueue; }

  auto &getTimerFactory() { return _timerFactory; }

  auto &getEndpointPollManager() { return _endpointPollManager; }

  auto &getShmEndpointFactory() {
    if constexpr (requires {
                    _endpointPollManager.getSharedMemEndpointFactory();
                  }) {
      // Endpoint poll manager supports shared memory endpoint factory
      return _endpointPollManager.getSharedMemEndpointFactory();
    } else {
      static_assert(
          false,
          "Endpoint poll manager does not support shared memory endpoint "
          "factory");
    }
  }

  Expected start() override {
    if (auto result = _endpointPollManager.initialize(); !result) {
      return result;
    }
    return _eventQueue.start();
  }

  Expected stop() override { return _eventQueue.stop(); }

  auto &getClock() const { return _clock; }

  ClockNowT _clock;
  EndpointEventPollMgrT _endpointPollManager;
  SocketFactoryT _socketFactory;
  EventQueueT _eventQueue;
  TimerFactoryT _timerFactory;
  std::string _certFile;
  std::string _keyFile;
};

} // namespace medici::application