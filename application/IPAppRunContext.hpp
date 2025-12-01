#pragma once

#include <concepts>
#include <expected>
#include <iostream>
#include <string_view>

#include "medici/IEndpointEventDispatch.hpp"
#include "medici/application/ContextThreadConfig.hpp"
#include "medici/application/IAppContext.hpp"
#include "medici/application/concepts.hpp"
#include "medici/event_queue/EventQueue.hpp"
#include "medici/event_queue/concepts.hpp"
#include "medici/sockets/concepts.hpp"
#include "medici/time.hpp"
#include "medici/timers/TimerFactory.hpp"

namespace medici::application {

class IPContextThreadConfig : public ContextThreadConfig {
public:
  auto producers() const { return _producers; }
  auto inactivityMicros() const {
    return std::chrono::microseconds{_inactivityMicros};
  }
  auto certFile() const { return _certFile; }
  auto keyFile() const { return _keyFile; }
  auto keyPassword() const { return _keyPassword; }

  IPContextThreadConfig(const std::string name, std::uint32_t producers = 1,
                        std::uint32_t inactivityMicros = 100,
                        const std::string &certFile = "",
                        const std::string &keyFile = "",
                        const std::string &keyPassword = "",
                        std::optional<std::uint32_t> cpu = {},
                        std::optional<std::uint32_t> schedPolicy = {},
                        std::optional<std::uint32_t> schedPriority = {})

      : ContextThreadConfig{}, _producers{producers},
        _inactivityMicros{inactivityMicros}, _certFile{certFile},
        _keyFile{keyFile}, _keyPassword{keyPassword} {
    _runContextType = "LiveIPEndpointContext";
    _name = name;
    _cpu = cpu;
    _schedPolicy = schedPolicy;
    _schedPriority = schedPriority;
  }

  std::uint32_t _producers{1};
  std::uint32_t _inactivityMicros{100};
  std::string _certFile{};
  std::string _keyFile{};
  std::string _keyPassword{};
};

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