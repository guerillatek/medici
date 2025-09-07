
#pragma once
#include "time.hpp"

#include "medici/timers/interfaces.hpp"
#include <expected>
#include <string>
#include <string_view>
namespace medici {
using Expected = std::expected<void, std::string>;

class IEndpointEventDispatch {
public:
  virtual Expected onActive() = 0;
  virtual Expected onPayloadReady(TimePoint readTime) = 0;
  virtual Expected onDisconnected(const std::string &reason) = 0;
  virtual Expected onShutdown() = 0;
  virtual Expected registerTimer(const timers::IEndPointTimerPtr &timer) = 0;
};

using Expected = std::expected<void, std::string>;
using ExpectedEventsCount = std::expected<int, std::string>;

template <typename T>
concept EndpointEventPollMgrC = requires(T t) {
  { t.pollAndDispatchEndpointsEvents() } -> std::same_as<ExpectedEventsCount>;
};

} // namespace medici