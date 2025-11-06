#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "medici/shm_endpoints/testUtils.hpp"
#include <boost/test/unit_test.hpp>
#include <concepts>
#include <filesystem>
#include <format>

namespace medici::tests {

struct SharedMemPublishTestHarness {

  SystemClockNow clock{};
  TestRunContext producerRunContext{"ProducerEventQueue", clock};

  void startOutOfProcClientConsumers(int consumers) {
    // Start 3 out-of-process consumers using the testConsumer executable
    std::string thisExecutableFolder =
        std::filesystem::canonical("/proc/self/exe").parent_path().string();
    for (int i = 0; i < consumers; ++i) {
      std::string command = std::format("{}/testConsumer consumer{} &",
                                        thisExecutableFolder, i + 1);
      std::system(command.c_str());
    }
  }

  void startOutOfProcClientProducers(int producers) {
    std::string thisExecutableFolder =
        std::filesystem::canonical("/proc/self/exe").parent_path().string();
    for (int i = 0; i < producers; ++i) {
      std::string command = std::format("{}/testProducer producer{} &",
                                        thisExecutableFolder, i + 1);
      std::system(command.c_str());
    }
  }

  void startOutOfProcDuplexClients(int clients) {
    std::string thisExecutableFolder =
        std::filesystem::canonical("/proc/self/exe").parent_path().string();
    for (int i = 0; i < clients; ++i) {
      std::string command = std::format("{}/testDuplex client{} &",
                                        thisExecutableFolder, i + 1);
      std::system(command.c_str());
    }
  }
};
} // namespace medici::tests

BOOST_FIXTURE_TEST_SUITE(MediciUnitTests,
                         medici::tests::SharedMemPublishTestHarness);

BOOST_AUTO_TEST_CASE(PRODUCER_SERVER_TEST) {

  const int numConsumers = 5;
  // Run in process consumer for debugging
  medici::tests::runServerProducerFunction(
      numConsumers, [this]() { startOutOfProcClientConsumers(numConsumers); });
}

BOOST_AUTO_TEST_CASE(CONSUMER_SERVER_TEST) {

  const int numProducers = 5;
  // Run in process producer for debugging
  medici::tests::runServerConsumerFunction(
      numProducers, [this]() { startOutOfProcClientProducers(numProducers); },
      numProducers * 200);
}

BOOST_AUTO_TEST_CASE(DUPLEX_SERVER_TEST) {
  const int numClients = 5;
  // Run in process full duplex server for debugging
  medici::tests::runFullDuplexServerFunction(
      numClients, [this]() { startOutOfProcDuplexClients(numClients); });
}

BOOST_AUTO_TEST_SUITE_END();
