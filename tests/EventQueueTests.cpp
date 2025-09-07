#include "medici/event_queue/EventQueue.hpp"
#include "medici/timers/EndpointTimer.hpp"

#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <format>
#include <list>
#include <memory>
#include <random>
#include <set>
#include <vector>

namespace utf = boost::unit_test;

namespace medici::tests {

struct MockEndpointEventPollMgr {
  Expected registerEndpoint(IEndpointEventDispatch &) { return {}; }
  ExpectedEventsCount pollAndDispatchEndpointsEvents() {
    return dispatchedEvents;
  }

  std::uint32_t dispatchedEvents{0};
};

struct TestClock {
  TestClock(TimePoint &currentTime) : _currentTime{currentTime} {}

  auto operator()() const { return _currentTime; }

  TimePoint &_currentTime;
};

struct EventQueueTestHarness {
  MockEndpointEventPollMgr endpointPollMgr{};
  using EventQueueT =
      event_queue::EventQueue<MockEndpointEventPollMgr, TestClock, 1024, 1024>;

  EventQueueTestHarness() {}

  Expected insertSorted(int value) {
    auto insertAt =
        std::ranges::find_if(_producerList, [value](int existingEntry) {
          return value < existingEntry;
        });
    _producerList.insert(insertAt, value);
    return {};
  }

  Expected insertSorted(const std::string &value) {
    _producerSet.insert(value);
    return {};
  }

  TimePoint currentTime{parseTimestamp("2024-11-25T04:10:25.000000000")};

  // Adds one microsecond tick to the current time for every event
  // cycle
  void setupTestClock() {
    _eventQueue.postAsyncAction([this]() -> event_queue::AsyncExpected {
      currentTime += std::chrono::microseconds{1};
      return false;
    });
  }

  std::list<int> _producerList{};
  std::multiset<std::string> _producerSet{};
  std::uint32_t _asyncAttempts{0};

  EventQueueT _eventQueue{"UnitTest", endpointPollMgr, TestClock{currentTime},
                          5, std::chrono::microseconds{10}};
};

} // namespace medici::tests

BOOST_FIXTURE_TEST_SUITE(MediciUnitTests, medici::tests::EventQueueTestHarness);

BOOST_AUTO_TEST_CASE(ASYNC_MULTI_PRODUCER_RUN_TEST) {
  //
  using namespace medici::event_queue;

  auto threadProducerFunction = [this]() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 1000);

    // Wait until the consuming queue is active otherwise the action
    // will not post through it's thread specific queue and will post to
    // the local queue in a thread unsafe manner
    while (!_eventQueue.isActive()) {
      std::this_thread::sleep_for(std::chrono::microseconds{1});
    }

    for (int n = 0; n != 1000; ++n) {
      _eventQueue.postAction([&distrib, &gen, this]() {
        // This code runs on the main(consuming) thread
        insertSorted(distrib(gen));
        return Expected{};
      });

      std::this_thread::sleep_for(
          std::chrono::microseconds{distrib(gen) % 100});
    }
  };

  // It's safe to post this action to the queue before starting it
  // because this the thread that will start and run the event loop.
  // The asynchronous action below will run repeatedly in the event
  // until it return true or an error.
  _eventQueue.postAsyncAction([this]() -> AsyncExpected {
    //
    ++_asyncAttempts;
    auto entries = _producerList.size();
    if (entries < 5000) {
      return false;
    }
    // The producers have finished so stop the queue
    // making this the last cycle
    _eventQueue.stop();
    auto activeValue = *_producerList.begin();

    // Validate the content
    for (auto value : _producerList) {
      if (activeValue > value) {
        return std::unexpected("Producer Test failed with thread sync error");
      }
      if (value > activeValue) {
        activeValue = value;
      }
    }
    std::cout << "Producers finished! Terminating the loop" << std::endl;
    // Terminate the async cycle
    return true;
  });

  std::thread p1(threadProducerFunction);
  std::thread p2(threadProducerFunction);
  std::thread p3(threadProducerFunction);
  std::thread p4(threadProducerFunction);
  std::thread p5(threadProducerFunction);

  BOOST_CHECK(_eventQueue.start());

  p1.join();
  p2.join();
  p3.join();
  p4.join();
  p5.join();

  BOOST_CHECK(_asyncAttempts > 0);
}

BOOST_AUTO_TEST_CASE(PRECISION_TIMED_EVENT_TEST) {
  using namespace medici::event_queue;
  medici::TimePoint eventTime{
      medici::parseTimestamp("2024-11-25T04:10:25.000010000")};
  bool testPassed = false;
  setupTestClock();
  _eventQueue.postPrecisionTimedAction(eventTime,
                                       [&, this]() -> medici::Expected {
                                         _eventQueue.stop();
                                         testPassed = eventTime == currentTime;
                                         return {};
                                       });
  auto result = _eventQueue.start();
  BOOST_CHECK(result);
  BOOST_CHECK(testPassed);
  // Because the fake clock in this test case is using the aynch event queue to
  // update the time when we stop the queue the event loop it will have added an
  // extra tick because the precision timed events, have the highest priority in
  // the event cycle
  BOOST_CHECK_EQUAL(eventTime + std::chrono::microseconds{1}, currentTime);
}

BOOST_AUTO_TEST_CASE(IDLE_TIMED_EVENT_TEST) {
  using namespace medici::event_queue;
  medici::TimePoint eventTime{
      medici::parseTimestamp("2024-11-25T04:10:25.000010000")};
  bool testPassed = false;
  _eventQueue.postIdleTimedAction(eventTime, [&, this]() -> medici::Expected {
    _eventQueue.stop();
    testPassed = eventTime == currentTime;
    return {};
  });
  // Can't fake idleness with active event loop so we pump the event loop
  // manually
  _eventQueue.pumpEvents();

  // Time hasn't occurred
  BOOST_CHECK(!testPassed);

  currentTime = eventTime;
  endpointPollMgr.dispatchedEvents = 1;
  _eventQueue.pumpEvents();
  // Time has occurred but we have endpoint activity
  BOOST_CHECK(!testPassed);

  // No endpoint activity in event cycle
  // so idle events should fire
  endpointPollMgr.dispatchedEvents = 0;
  _eventQueue.pumpEvents();
  BOOST_CHECK(testPassed);
}

BOOST_AUTO_TEST_CASE(MULTI_PRODUCER_PAYLOAD_RUN_TEST) {

  //
  using namespace medici::event_queue;

  auto threadProducerFunction = [this]() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 1000);

    // Wait until the consuming queue is active otherwise the action
    // will not post through it's thread specific queue and will post to
    // the local queue in a thread unsafe manner
    while (!_eventQueue.isActive()) {
      std::this_thread::sleep_for(std::chrono::microseconds{1});
    }

    for (int n = 0; n != 1000; ++n) {
      auto expectedObject =
          _eventQueue.getNextActionObject<std::array<char, 20>>();
      sprintf(expectedObject.value()->data(), "%d", distrib(gen));
      _eventQueue.postAction([&payload = *expectedObject.value(), this]() {
        // This code runs on the main(consuming) thread
        insertSorted(payload.data());
        return Expected{};
      });
      std::this_thread::sleep_for(
          std::chrono::microseconds{distrib(gen) % 100});
    }
  };

  // It's safe to post this action to the queue before starting it
  // because this the thread that will start and run the event loop.
  // The asynchronous action below will run repeatedly in the event
  // until it return true or an error.
  _eventQueue.postAsyncAction([this]() -> AsyncExpected {
    //
    ++_asyncAttempts;
    auto entries = _producerSet.size();
    if (entries < 5000) {
      return false;
    }
    // The producers have finished so stop the queue
    // making this the last cycle
    _eventQueue.stop();
    auto activeValue = *_producerSet.begin();

    // Validate the content
    for (auto value : _producerSet) {
      if (activeValue > value) {
        return std::unexpected("Producer Test failed with thread sync error");
      }
      if (value > activeValue) {
        activeValue = value;
      }
    }
    std::cout << "Producers finished! Terminating the loop" << std::endl;
    // Terminate the async cycle
    return true;
  });

  std::thread p1(threadProducerFunction);
  std::thread p2(threadProducerFunction);
  std::thread p3(threadProducerFunction);
  std::thread p4(threadProducerFunction);
  std::thread p5(threadProducerFunction);

  BOOST_CHECK(_eventQueue.start());

  p1.join();
  p2.join();
  p3.join();
  p4.join();
  p5.join();

  BOOST_CHECK(_asyncAttempts > 0);
}

BOOST_AUTO_TEST_SUITE_END();
