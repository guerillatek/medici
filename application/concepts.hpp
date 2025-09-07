#pragma once
#include <concepts>
#include <expected>
#include <iostream>
#include <string_view>

#include "medici/event_queue/concepts.hpp"
#include "medici/sockets/concepts.hpp"
#include "medici/sockets/interfaces.hpp"
#include "medici/time.hpp"
#include "medici/timers/concepts.hpp"

namespace medici::application {

template <typename T>
concept AppRunContextC = requires(T t) {
  { t.getSocketFactory() } -> sockets::SocketFactoryC;
  { t.getEventQueue() } -> event_queue::EventQueueC;
  { t.getClock() } -> ClockNowC;
  { t.getTimerFactory() } -> timers::TimerFactoryC;
};

template <typename T>
concept AppRunContextManagerC = requires(T t) {
  t.stopAllThreads();
  t.startRunContext(std::uint32_t{});
};

} // namespace medici::application