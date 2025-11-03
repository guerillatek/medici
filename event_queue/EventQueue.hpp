#pragma once

#include <boost/lockfree/spsc_queue.hpp>
#include <concepts>
#include <deque>
#include <expected>
#include <iostream>
#include <optional>
#include <queue>
#include <ranges>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "medici/sockets/concepts.hpp"
#include "medici/time.hpp"

#include "concepts.hpp"
#include "medici/IEndpointEventDispatch.hpp"

namespace medici::event_queue {

struct PrioritizedCallable {
  PrioritizedCallable(TimePoint eventTime, CallableC auto &&callable)
      : _eventTime{eventTime}, _action{std::move(callable)} {}

  auto eventTime() const { return _eventTime; }

  auto applyAction() const { return _action(); }

private:
  TimePoint _eventTime;
  CallableT _action;

  friend bool operator>(const PrioritizedCallable &lhs,
                        const PrioritizedCallable &rhs) {
    return lhs._eventTime > rhs._eventTime;
  }
};

using PayloadPtr = std::uint8_t *;
using ExpectedBuffer = std::expected<PayloadPtr, std::string>;
template <typename T> using ExpectedObject = std::expected<T *, std::string>;

template <std::uint32_t PayloadSize, std::uint32_t MaxProducerQueueSize>
class ThreadSpecificQueue {
public:
  using QueueT = boost::lockfree::spsc_queue<
      CallableT, boost::lockfree::capacity<MaxProducerQueueSize>>;
  using PayloadEntry = std::array<std::uint8_t, PayloadSize>;

  void push(CallableC auto &&action) {
    while (!(_queue.push(action)))
      ;
    _payloadEngaged = false;
  }
  auto empty() { return _queue.empty(); }

  void pop(CallableT &poppedEntry) {
    while (!_queue.pop(poppedEntry))
      ;
  }

  auto &front() { return _queue.front(); }

  ExpectedBuffer getNextActionPayload() {
    if (_payloadEngaged) {
      return std::unexpected(
          "Action payload already engaged for next queue entry");
    }
    auto dataPtr = _localPayloads[_nextActionPayload].data();
    _payloadEngaged = true;
    ++_nextActionPayload %= MaxProducerQueueSize;
    return dataPtr;
  }

  template <typename T, typename... ConstructorArgs>
  ExpectedObject<T> getNextActionObject(ConstructorArgs &&...args) {
    if (sizeof(T) > PayloadSize) {
      return std::unexpected(
          std::format("Request for next action object exceeds size={} of queue "
                      "entry payload",
                      PayloadSize));
    }
    auto bufferResult = getNextActionPayload();
    if (!bufferResult) {
      return std::unexpected(bufferResult.error());
    }
    T *object =
        new (bufferResult.value()) T(std::forward<ConstructorArgs>(args)...);
    return object;
  }

private:
  size_t _nextActionPayload{};
  bool _payloadEngaged;
  QueueT _queue;
  std::array<PayloadEntry, MaxProducerQueueSize> _localPayloads;
};

template <typename EndpointEventPollMgrT, ClockNowC ClockT,
          std::uint32_t PayloadSize = 1024,
          std::uint32_t MaxProducerQueueSize = 1024 * 64>
class EventQueue : public IEventQueue {
  using ActiveThreadId = std::optional<std::thread::id>;
  using ThreadSpecificQueueT =
      ThreadSpecificQueue<PayloadSize, MaxProducerQueueSize>;
  struct ThreadQueuePair {
    std::thread::id threadId{};
    ThreadSpecificQueueT producerQueue{};
  };

  using ExternalThreadProducerQueue = std::vector<ThreadQueuePair>;
  using LocalAsyncQueue = std::deque<CallableT>;
  using TimedEventQueue =
      std::priority_queue<PrioritizedCallable, std::vector<PrioritizedCallable>,
                          std::greater<PrioritizedCallable>>;

public:
  EventQueue(const std::string &sessionName,
             EndpointEventPollMgrT &endpointEventPollMgr, const ClockT &clock,
             std::uint32_t maxProducerThreads,
             std::chrono::microseconds inActivitySleepDuration)
      : _externalProducerQueue{maxProducerThreads},
        _endpointEventPollMgr{endpointEventPollMgr}, _clock{clock},
        _maxProducerThreads{maxProducerThreads},
        _inActivitySleepDuration{inActivitySleepDuration} {}

  ExpectedBuffer getNextActionPayload() {
    if (!_activeThreadId || (std::this_thread::get_id() == *_activeThreadId)) {
      if (_payloadEngaged) {
        return std::unexpected(
            "Action payload already engaged for next queue entry");
      }
      auto dataPtr = _localPayloads[_nextActionPayload].data();
      _payloadEngaged = true;
      ++_nextActionPayload %= MaxProducerQueueSize;
      return dataPtr;
    }
    auto findResult = getThreadProducerQueueEntry();
    if (!findResult) {
      return std::unexpected(findResult.error());
    }
    auto &targetQueueEntry = findResult.value();
    return targetQueueEntry->producerQueue.getNextActionPayload();
  }

  template <typename T, typename... ConstructorArgs>
  ExpectedObject<T> getNextActionObject(ConstructorArgs &&...args) {
    if (!_activeThreadId || (std::this_thread::get_id() == *_activeThreadId)) {
      if (sizeof(T) > PayloadSize) {
        return std::unexpected(
            std::format("Request for next object on local async queue exceeds "
                        "size={} of queue entry payload",
                        PayloadSize));
      }
      auto bufferResult = getNextActionPayload();
      if (!bufferResult) {
        return std::unexpected(bufferResult.error());
      }
      T *object =
          new (bufferResult.value()) T(std::forward<ConstructorArgs>(args)...);
      return object;
    }
    auto findResult = getThreadProducerQueueEntry();
    if (!findResult) {
      return std::unexpected(findResult.error());
    }
    auto &targetQueueEntry = findResult.value();
    return targetQueueEntry->producerQueue.template getNextActionObject<T>(
        std::forward<ConstructorArgs>(args)...);
  }

  template <CallableC T> Expected postAction(T &&action) {
    if (!_activeThreadId || (std::this_thread::get_id() == *_activeThreadId)) {
      if (_localEventQueue.size() == MaxProducerQueueSize) {
        return std::unexpected("Local Async event queue has reached capacity");
      }
      _localEventQueue.emplace_back(CallableT{std::move(action)});
      return {};
    }
    auto findResult = getThreadProducerQueueEntry();
    if (!findResult) {
      return std::unexpected(findResult.error());
    }
    auto &targetQueueEntry = findResult.value();
    targetQueueEntry->producerQueue.push(CallableT{std::move(action)});
    return {};
  }

  template <AsyncCallableC T> Expected postAsyncAction(T &&action) {
    auto asyncCallableWrapper = [action = std::move(action),
                                 this]() mutable -> Expected {
      auto result = action();
      if (!result) {
        return std::unexpected(result.error());
      }
      bool finished = result.value();
      if (finished) {
        // action completed
        return {};
      }
      // Action did not complete and needs to attempted again
      return postAsyncAction<T>(std::move(action));
    };

    if (!_activeThreadId || (std::this_thread::get_id() == *_activeThreadId)) {
      if (_localEventQueue.size() == MaxProducerQueueSize) {
        return std::unexpected("Local Async event queue has reached capacity");
      }
      _localEventQueue.emplace_back(std::move(asyncCallableWrapper));
      return {};
    }
    return postAction<decltype(asyncCallableWrapper)>(
        std::move(asyncCallableWrapper));
  }

  template <CallableC T>
  Expected postPrecisionTimedAction(TimePoint timePoint, T &&action) {
    if (_clock() > timePoint) {
      return std::unexpected(std::format(
          "Posting timed action for expired time={}, currentTime={}", timePoint,
          _clock()));
    }

    if (!_activeThreadId || (std::this_thread::get_id() == *_activeThreadId)) {
      _precisionTimedEvents.push(
          PrioritizedCallable{timePoint, std::move(action)});
      return Expected{};
    }

    auto actionToPost = [timePoint, action = std::move(action), this] mutable {
      _precisionTimedEvents.push(
          PrioritizedCallable{timePoint, std::move(action)});
      return Expected{};
    };
    return postAction<decltype(actionToPost)>(std::move(actionToPost));
  }

  template <CallableC T>
  Expected postIdleTimedAction(TimePoint timePoint, T &&action) {
    if (_clock() > timePoint) {
      return std::unexpected(std::format(
          "Cannot post timed action for expired time={}, currentTime={}",
          timePoint, _clock()));
    }

    if (!_activeThreadId || (std::this_thread::get_id() == *_activeThreadId)) {
      _idleTimedEvents.push(PrioritizedCallable{timePoint, std::move(action)});
    }
    auto actionToPost = [timePoint, action = std::move(action), this] mutable {
      _idleTimedEvents.push(PrioritizedCallable{timePoint, std::move(action)});
      return Expected{};
    };
    return postAction<decltype(actionToPost)>(std::move(actionToPost));
  }

  Expected start() {
    if (_activeThreadId) {
      return std::unexpected("Event queue already active cannot be started");
    }
    _activeThreadId = std::this_thread::get_id();
    _isActive = true;
    Expected result;
    while (_activeThreadId && result) {
      result = runEventCycle();
      if (!result) {
        std::cerr << "Event queue encountered error: "
                  << result.error() << std::endl;
        return result;
      }
    }
    return result;
  }
  Expected stop() {
    if (!_activeThreadId) {
      return std::unexpected("Invalid stop request. Event queue is not active");
    }
    _activeThreadId.reset();
    _isActive = false;
    return {};
  }

  Expected pumpEvents() {
    if (_activeThreadId) {
      return std::unexpected("Cannot pump events on an active event queue");
    }
    return runEventCycle();
  }

  // This should only used by external threads checking
  // activity at startup periods otherwise they should assume
  // the event loop is active during normal operations and avoid
  // incurring an atomic memory barrier.
  bool isActive() const { return _isActive; }

  auto &getClock() const { return _clock; }

private:
  Expected runEventCycle() {
    // Fire all precision timed events that are ready
    size_t dispatchedEvents = 0;
    while (!_precisionTimedEvents.empty() &&
           (_precisionTimedEvents.top().eventTime() <= _clock())) {
      auto eventResult = _precisionTimedEvents.top().applyAction();
      if (!eventResult) {
        return eventResult;
      }
      _precisionTimedEvents.pop();
      ++dispatchedEvents;
    }

    // Check and dispatch for endpoint events
    auto expectedEventCount =
        _endpointEventPollMgr.pollAndDispatchEndpointsEvents();
    if (!expectedEventCount) {
      return std::unexpected{expectedEventCount.error()};
    }
    dispatchedEvents +=
        expectedEventCount.value(); // FIXME: YO : potential decrease and
                                    // overflow. Value should be unsigned(?)

    // Check for async operations on local queue
    if (!_localEventQueue.empty()) {
      if (!_localEventQueue.front()) {
        return std::unexpected("Empty action placed on local async queue");
      }
      auto result = _localEventQueue.front()();
      _localEventQueue.pop_front();
      ++dispatchedEvents;
      if (!result) {
        return result;
      }
    }

    // poll the external thread producers
    for (size_t queueIndex = 0;
         queueIndex < _activeProducers.load(std::memory_order_relaxed);
         ++queueIndex) {

      auto &producerQueueEntry = _externalProducerQueue[queueIndex];
      auto &producerQueue = producerQueueEntry.producerQueue;

      if (!producerQueue.empty()) {
        CallableT queueEntry{};
        producerQueue.pop(queueEntry);
        if (!queueEntry) {
          return std::unexpected(
              "Empty action placed on external producer queue");
        }
        auto result = queueEntry();
        ++dispatchedEvents;
        if (!result) {
          return result;
        }
      }
    }

    // Check for idleness
    if (dispatchedEvents == 0) {
      // Fire all idle timed events that are ready
      while (!_idleTimedEvents.empty() &&
             (_idleTimedEvents.top().eventTime() <= _clock())) {
        auto eventResult = _idleTimedEvents.top().applyAction();
        _idleTimedEvents.pop();
        if (!eventResult) {
          return eventResult;
        }
        ++dispatchedEvents;
      }
    }

    // Check for inactivity
    if (dispatchedEvents == 0) {
      std::this_thread::sleep_for(_inActivitySleepDuration);
    }
    return {};
  }

  ExternalThreadProducerQueue _externalProducerQueue{};
  using ProducerQueueEntry = typename ExternalThreadProducerQueue::iterator;

  std::expected<ProducerQueueEntry, std::string> getThreadProducerQueueEntry() {
    auto isTargetThread = [](const ThreadQueuePair &x) {
      return x.threadId == std::this_thread::get_id();
    };

    auto entry = std::ranges::find_if(_externalProducerQueue, isTargetThread);

    if (entry != _externalProducerQueue.end()) {
      return entry;
    }

    if (_externalProducerQueue.size() == _activeProducers) {
      return std::unexpected(
          std::format("No producers can be registered for this event queue",
                      _maxProducerThreads));
    }

    auto targetIndex = _activeProducers.fetch_add(1, std::memory_order_relaxed);
    _externalProducerQueue[targetIndex].threadId = std::this_thread::get_id();
    return _externalProducerQueue.begin() + targetIndex;
  }

  // Public interface implementation

  Expected postAsyncAction(const AsyncCallableT &action) override {
    return postAsyncAction<AsyncCallableT>(AsyncCallableT{action});
  }

  Expected postAction(const CallableT &action) override {
    return postAction<CallableT>(CallableT{action});
  }

  Expected postPrecisionTimedAction(TimePoint timePoint,
                                    const CallableT &action) override {
    return postPrecisionTimedAction<CallableT>(timePoint, CallableT{action});
  }

  Expected postIdleTimedAction(TimePoint timePoint,
                               CallableT &action) override {
    return postIdleTimedAction<CallableT>(timePoint, CallableT{action});
  }

  // TODO: Put members in order of use in poll cycle to have better
  // cache locality
  TimedEventQueue _precisionTimedEvents{};
  TimedEventQueue _idleTimedEvents{};
  LocalAsyncQueue _localEventQueue{};
  EndpointEventPollMgrT &_endpointEventPollMgr;
  ClockT _clock;
  std::uint32_t _maxProducerThreads;
  ActiveThreadId _activeThreadId{};
  std::atomic<bool> _isActive{false};
  std::atomic<size_t> _activeProducers{0};
  std::chrono::microseconds _inActivitySleepDuration;
  size_t _nextActionPayload{};
  using PayloadEntry = std::array<std::uint8_t, PayloadSize>;
  std::array<PayloadEntry, MaxProducerQueueSize> _localPayloads;
  bool _payloadEngaged{false};
};

} // namespace medici::event_queue