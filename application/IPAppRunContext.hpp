#include <concepts>
#include <expected>
#include <iostream>
#include <string_view>

#include "medici/IEndpointEventDispatch.hpp"
#include "medici/application/AppRunContextConfig.hpp"
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
  IPAppRunContext(const std::string &sessionName,
                  std::uint32_t maxProducerThreads,
                  std::chrono::microseconds inActivitySleepDuration,
                  ClockNowT clock)
      : _clock{clock}, _endpointPollManager{sessionName, _clock},
        _socketFactory{_endpointPollManager},
        _eventQueue{sessionName, _endpointPollManager, _clock,
                    maxProducerThreads, inActivitySleepDuration},
        _timerFactory{_eventQueue} {}

  IPAppRunContext(const AppRunContextConfig &config, ClockNowT clock)
      : IPAppRunContext{config.name(), config.producers(),
                        config.inactivityMicros(), _clock} {}

  auto &getSocketFactory() { return _socketFactory; }

  auto &getEventQueue() { return _eventQueue; }

  auto &getTimerFactory() { return _timerFactory; }

  auto &getEndpointPollManager() { return _endpointPollManager; }

  Expected start() override {
    _endpointPollManager.initialize();
    return _eventQueue.start();
  }

  Expected stop() override { return _eventQueue.stop(); }

  auto &getClock() const { return _clock; }

  ClockNowT _clock;
  EndpointEventPollMgrT _endpointPollManager;
  SocketFactoryT _socketFactory;
  EventQueueT _eventQueue;
  TimerFactoryT _timerFactory;
};

} // namespace medici::application