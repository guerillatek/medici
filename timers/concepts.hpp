#pragma once

#include "medici/sockets/interfaces.hpp"
#include "medici/time.hpp"
#include "medici/timers/interfaces.hpp"
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace medici::timers {

template <typename T>
concept TimerFactoryC = requires(T t) {
  {
    t.createManualTimer(TimerType{}, std::chrono::nanoseconds{}, CallableT{})
  } -> std::same_as<ITimerPtr>;
  {
    t.createEndpointTimer(TimerType{}, std::declval<sockets::IIPEndpoint &>(),
                          std::chrono::nanoseconds{}, CallableT{})
  } -> std::same_as<IEndPointTimerPtr>;
};

} // namespace medici::timers