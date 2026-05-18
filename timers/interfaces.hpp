#pragma once

#include "medici/event_queue/concepts.hpp"
#include "medici/sockets/EndpointConfig.hpp"

namespace medici::sockets {
class IIPEndpoint;
} // namespace medici::sockets

#include <memory>

namespace medici::timers {

using CallableT = std::function<event_queue::Expected()>;

enum class TimerType {
  Precision, // Attempts to fire as close to designate time as possible
  Idle       // Fire after designated time only if there is no other activity
};

class ITimer {
public:
  virtual void start() = 0;
  virtual void stop() = 0;
};
using ITimerPtr = std::unique_ptr<ITimer>;

class IEndPointTimer : public ITimer {
public:
  virtual void pause() = 0;
  virtual void resume() = 0;
};
using IEndPointTimerPtr = std::shared_ptr<IEndPointTimer>;

class ITimerFactory {
public:
  virtual ~ITimerFactory() = default;
  virtual ITimerPtr createManualTimer(TimerType timerType,
                                      std::chrono::nanoseconds duration,
                                      event_queue::CallableT action) = 0;
  virtual IEndPointTimerPtr
  createEndpointTimer(TimerType timerType, sockets::IIPEndpoint &endpoint,
                      std::chrono::nanoseconds duration,
                      event_queue::CallableT action) = 0;
};

} // namespace medici::timers