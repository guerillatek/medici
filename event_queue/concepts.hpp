
#pragma once
#include "medici/time.hpp"
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

namespace medici::event_queue {

using Expected = std::expected<void, std::string>;
using AsyncExpected = std::expected<bool, std::string>;
using CallableT = std::function<Expected()>;
using AsyncCallableT = std::function<AsyncExpected()>;

class IEventQueue {
public:
  virtual ~IEventQueue() = default;
  virtual Expected postAsyncAction(const AsyncCallableT &action) = 0;
  virtual Expected postAction(const CallableT &action) = 0;
  virtual Expected postPrecisionTimedAction(TimePoint timePoint,
                                            const CallableT &action) = 0;
  virtual Expected postIdleTimedAction(TimePoint timePoint,
                                       const CallableT &action) = 0;
  virtual Expected start() = 0;
  virtual Expected stop() = 0;
  virtual Expected pumpEvents() = 0;
  virtual bool isActive() const = 0;
};

template <typename T>
concept CallableC = requires(T t) {
  { t() } -> std::same_as<Expected>;
};

template <typename T>
concept AsyncCallableC = requires(T t) {
  { t() } -> std::same_as<AsyncExpected>;
};

template <typename T>
concept EventQueueC = requires(T t) {
  { t.postAsyncAction(AsyncCallableT{}) } -> std::same_as<Expected>;
  { t.postAction(CallableT{}) } -> std::same_as<Expected>;
  {
    t.postPrecisionTimedAction(TimePoint{}, CallableT{})
  } -> std::same_as<Expected>;
  { t.postIdleTimedAction(TimePoint{}, CallableT{}) } -> std::same_as<Expected>;
  { t.start() } -> std::same_as<Expected>;
  { t.stop() } -> std::same_as<Expected>;
  { t.pumpEvents() } -> std::same_as<Expected>;
  { t.isActive() } -> std::same_as<bool>;
};

} // namespace medici::event_queue