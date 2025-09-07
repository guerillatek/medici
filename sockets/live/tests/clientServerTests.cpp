#include "medici/application/IPAppRunContext.hpp"
#include "medici/application/concepts.hpp"
#include "medici/sockets/RemoteEndpointListener.hpp"
#include "medici/sockets/live/LiveSocketFactory.hpp"
#include "medici/cryptoUtils/OpenSSLUtils.hpp"
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <format>
#include <list>
#include <memory>
#include <random>
#include <set>
#include <vector>

static auto sslInitialization = crypto_utils::OpenSSLUtils::initSSL();

namespace utf = boost::unit_test;

namespace medici::tests {

using RunContextT =
    medici::application::IPAppRunContext<sockets::live::LiveSocketFactory,
                                         SystemClockNow,
                                         sockets::live::IPEndpointPollManager>;

struct ServerClientTestHarness {

  RunContextT serverThreadContext{"Server", 1, std::chrono::microseconds{1000},
                                  SystemClockNow{}};

  RunContextT clientThreadContext{"Client", 1, std::chrono::microseconds{1000},
                                  SystemClockNow{}};

  medici::sockets::IPEndpointConfig listenEndpoint{"listenHost", "127.0.0.1",
                                                   12345};

  std::string endpointType;

  medici::sockets::CloseHandlerT listenCloseHandler =
      [this](const std::string &reason) {
        BOOST_TEST_MESSAGE(
            std::format(" {} listener close: reason={}", endpointType, reason));
        return Expected{};
      };
  medici::sockets::CloseHandlerT listenDisconnectHandler =
      [this](const std::string &reason) {
        BOOST_TEST_MESSAGE(std::format(" {} listener disconnected: reason={}",
                                       endpointType, reason));
        return Expected{};
      };

  medici::sockets::CloseHandlerT clientCloseHandler =
      [this](const std::string &reason) {
        BOOST_TEST_MESSAGE(
            std::format(" {} client close: reason={}", endpointType, reason));
        return Expected{};
      };

  medici::sockets::DisconnectedHandlerT clientDisconnectHandler =
      [this](const std::string &reason) {
        BOOST_TEST_MESSAGE(std::format(" {} client disconnected: reason={}",
                                       endpointType, reason));
        return Expected{};
      };

  std::string testPayload = "test payload";
  std::string serverResponse;
  std::string clientOutgoingPayload;
  bool serverDetectedClientActive = false;
  std::function<Expected()> startClient;
};

template <medici::sockets::IsTcpEndpoint EndpointT>
struct TcpClientServerTestHarness : ServerClientTestHarness {

  Expected echoPayload(auto &remoteListener, std::string_view payload) {
    return remoteListener.getEndpointCoordinator()
        .getActiveContext()
        .getEndpoint()
        .send(payload);
    return {};
  }

  medici::sockets::ITcpIpEndpointPtr remoteClient{
      clientThreadContext.getSocketFactory().createTcpIpClientEndpoint(
          listenEndpoint,
          [this](std::string_view payload, medici::TimePoint) {
            serverResponse = payload;
            return remoteClient->closeEndpoint(std::format("Closing client"));
          },
          [this](std::string_view payload, medici::TimePoint) {
            clientOutgoingPayload = payload;
            return Expected{};
          },
          [this](const std::string &reason) {
            BOOST_TEST_MESSAGE(
                std::format("Closing client: reason={}", reason));
            return clientThreadContext.stop();
          },
          clientDisconnectHandler,
          [this]() {
            BOOST_TEST_MESSAGE(std::format(" {} client Active", endpointType));
            return remoteClient->send(testPayload);
          })};

  using RemoteListener =
      medici::sockets::RemoteEndpointListener<EndpointT, RunContextT>;

  TcpClientServerTestHarness() {

    startClient = [this]() {
      BOOST_TEST_MESSAGE("Starting client");
      if (auto result = remoteClient->openEndpoint(); !result) {
        BOOST_TEST_MESSAGE(
            std::format("Failed to open client endpoint: {}", result.error()));
        return result;
      }
      BOOST_TEST_MESSAGE("Client endpoint opened successfully");
      return clientThreadContext.start();
    };
  }
};

} // namespace medici::tests
BOOST_FIXTURE_TEST_SUITE(MediciUnitTests,
                         medici::tests::TcpClientServerTestHarness<
                             medici::sockets::live::TcpEndpoint>);

BOOST_AUTO_TEST_CASE(TCP_TEST) {
  BOOST_CHECK(sslInitialization);
  using namespace medici;
  using namespace medici::tests;
  RemoteListener remoteListener(
      serverThreadContext, listenEndpoint,
      [this](const std::string &reason) {
        BOOST_TEST_MESSAGE(std::format(" {} Listener Closing: reason={}",
                                       endpointType, reason));
        return serverThreadContext.stop();
      },
      listenDisconnectHandler,
      [this]() {
        BOOST_TEST_MESSAGE("Remote Listener Active");
        return startClient();
      },
      [this, &remoteListener](std::string_view payload, medici::TimePoint) {
        return echoPayload(remoteListener, payload);
      },
      [this](std::string_view payload, medici::TimePoint) {
        serverResponse = payload;
        return Expected{};
      },
      clientCloseHandler,
      [this, &remoteListener](const std::string &reason) {
        BOOST_TEST_MESSAGE(std::format(" {} client disconnected: reason={}",
                                       endpointType, reason));
        return remoteListener.stop("Ending Test");
      },
      [this]() {
        serverDetectedClientActive = true;
        return Expected{};
      });

  serverThreadContext.getEventQueue().postAction([this, &remoteListener]() {
    BOOST_TEST_MESSAGE("Opening remote listener");
    return remoteListener.start();
  });

  endpointType = "TcpIpClear";
  std::thread p1([this]() { serverThreadContext.start(); });
  p1.join();
  BOOST_CHECK_EQUAL(testPayload, serverResponse);
  BOOST_CHECK_EQUAL(testPayload, clientOutgoingPayload);
};

BOOST_AUTO_TEST_SUITE_END();