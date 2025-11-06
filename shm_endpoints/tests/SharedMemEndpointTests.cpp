#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "medici/shm_endpoints/testUtils.hpp"
#include <boost/test/unit_test.hpp>
#include <concepts>

namespace medici::tests {

struct SharedMemEndpointTestHarness {

  void startClientConsumers(std::uint32_t numConsumers) {
    // Start in-process consumers for debugging
    std::vector<std::jthread> consumerThreads;
    for (std::uint32_t i = 0; i < numConsumers; ++i) {
      consumerThreads.emplace_back([i]() {
        runClientConsumerFunction(std::format("consumer{}", i + 1));
      });
    }
  }

  void startClientProducers(std::uint32_t numProducers) {
    // Start in-process producers for debugging
    std::vector<std::jthread> producerThreads;
    for (std::uint32_t i = 0; i < numProducers; ++i) {
      producerThreads.emplace_back([i]() {
        runClientProducerFunction(std::format("producer{}", i + 1));
      });
    }
  }

  void startClientFullDuplexEndpoints(std::uint32_t numClients) {
    // Start in-process full duplex clients for debugging
    std::vector<std::jthread> clientThreads;
    for (std::uint32_t i = 0; i < numClients; ++i) {
      clientThreads.emplace_back([i]() {
        runFullDuplexClientFunction(std::format("client{}", i + 1));
      });
    }
  }

  static const int NUM_CLIENTS = 2;
};

} // namespace medici::tests

BOOST_FIXTURE_TEST_SUITE(MediciUnitTests,
                         medici::tests::SharedMemEndpointTestHarness);


BOOST_AUTO_TEST_CASE(SERVER_PRODUCER_TEST) {
//Run in process consumer for debugging
std::thread producerThread([this]() {
medici::tests::runServerProducerFunction(
NUM_CLIENTS, [this]() { startClientConsumers(NUM_CLIENTS); });
});

producerThread.join();
}

BOOST_AUTO_TEST_CASE(SERVER_CONSUMER_TEST) {

// Run in process producer for debugging
std::thread consumerThread([this]() {
medici::tests::runServerConsumerFunction(
NUM_CLIENTS, [this]() { startClientProducers(NUM_CLIENTS); },
NUM_CLIENTS * 200);
});

consumerThread.join();
}


BOOST_AUTO_TEST_CASE(FULL_DUPLEX_TEST) {
  // Run in process full duplex server for debugging
  std::thread serverThread([this]() {
    medici::tests::runFullDuplexServerFunction(
        NUM_CLIENTS, [this]() { startClientFullDuplexEndpoints(NUM_CLIENTS); });
  });

  serverThread.join();
}


BOOST_AUTO_TEST_SUITE_END();
