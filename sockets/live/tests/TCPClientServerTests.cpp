#include "ServerClientTestHarness.hpp"
namespace medici::tests {

template <medici::sockets::IsTcpEndpoint EndpointT>
struct TcpClientServerTestHarness : ServerClientTestHarness {

  TcpClientServerTestHarness() : ServerClientTestHarness(remoteClient) {
    useAsyncSend = false;
  }

  Expected echoPayload(std::string_view payload) {
    std::copy(payload.begin(), payload.end(),
              std::back_inserter(serverCapturedPayload));
    if (serverCapturedPayload.size() >= largeContent.size()) {
      return remoteListener.getEndpointCoordinator()
          .getActiveContext()
          .getEndpoint()
          .send(serverCapturedPayload);
    }
    return {};
  }

  Expected echoPayloadAsyncSend(std::string_view payload) {
    std::copy(payload.begin(), payload.end(),
              std::back_inserter(serverCapturedPayload));
    if (serverCapturedPayload.size() >= largeContent.size()) {
      auto &targetEndpoint = remoteListener.getEndpointCoordinator()
                                 .getActiveContext()
                                 .getEndpoint();

      return targetEndpoint.sendAsync(serverCapturedPayload, [this]() {
        BOOST_TEST_MESSAGE("Async send completed");
        serverFinishedResponse = true;
        return Expected{};
      });
    }
    return {};
  }

  medici::sockets::ITcpIpEndpointPtr remoteClient;

  using RemoteListener =
      medici::sockets::RemoteEndpointListener<EndpointT, RunContextT>;

  RemoteListener remoteListener{
      *(ServerClientTestHarness::serverThreadContext),
      ServerClientTestHarness::listenEndpoint,
      [this](const std::string &reason, const sockets::IPEndpointConfig &) {
        BOOST_TEST_MESSAGE(std::format(" {} Listener Closing: reason={}",
                                       endpointType, reason));
        return serverThreadContext->stop();
      },
      listenDisconnectHandler,
      [this]() {
        BOOST_TEST_MESSAGE("Remote Listener Active");

        return clientRunFunc();
      },
      [this](std::string_view payload, medici::TimePoint) {
        if (useAsyncSend)
          return echoPayloadAsyncSend(payload);
        else
          return echoPayload(payload);
      },
      [this](std::string_view payload, medici::TimePoint) {
        return Expected{};
      },
      clientCloseHandler,
      [this](const std::string &reason,
             const sockets::IPEndpointConfig &endpointConfig) {
        BOOST_TEST_MESSAGE(std::format(" {} client disconnected: reason={}",
                                       endpointType, reason));
        return remoteListener.stop("Ending Test");
      },
      [this]() {
        serverDetectedClientActive = true;
        return Expected{};
      }};

  void RunTest() {
    BOOST_CHECK(sslInitialization);
    using namespace medici;
    using namespace medici::tests;

    serverThreadContext->getEventQueue().postAction([this]() {
      BOOST_TEST_MESSAGE("Opening remote listener");
      return remoteListener.start();
    });
    bool serverRunning = true;

    std::thread p1([this, &serverRunning]() {
      auto result = serverThreadContext->start();
      serverRunning = false;
      if (!result) {
        BOOST_ERROR(
            std::format("Server thread exited with error: {}", result.error()));
      }
    });
    while (serverRunning && !clientThread)
      ;
    p1.join();
    if (clientThread)
      clientThread->join();
    BOOST_CHECK_EQUAL(largeContent, serverResponse);
    BOOST_CHECK_EQUAL(largeContent, clientOutgoingPayload);
    BOOST_CHECK_EQUAL(largeContent, serverCapturedPayload);
  }

  void RunTCPTest() {
    endpointType = "TcpIpClear";
    remoteClient =
        clientThreadContext->getSocketFactory().createTcpIpClientEndpoint(
            listenEndpoint,
            [this](std::string_view payload, medici::TimePoint) {
              std::copy(payload.begin(), payload.end(),
                        std::back_inserter(serverResponse));
              auto srs = serverResponse.size();
              auto lcs = largeContent.size();
              if (srs >= lcs) {
                return remoteClient->closeEndpoint(
                    std::format("Closing client"));
              }
              return medici::Expected{};
            },
            [this](std::string_view payload, medici::TimePoint) {
              clientOutgoingPayload = payload;
              return medici::Expected{};
            },
            [this](const std::string &reason,
                   const medici::sockets::IPEndpointConfig &) {
              BOOST_TEST_MESSAGE(
                  std::format("Closing client: reason={}", reason));
              return clientThreadContext->stop();
            },
            clientDisconnectHandler,
            [this]() {
              BOOST_TEST_MESSAGE(
                  std::format(" {} client Active", endpointType));
              return remoteClient->send(largeContent);
            });

    RunTest();
  }

  void RunSSLTest() {
    endpointType = "SSLTcpIp";
    remoteClient =
        clientThreadContext->getSocketFactory().createSSLClientEndpoint(
            listenEndpoint,
            [this](std::string_view payload, medici::TimePoint) {
              std::copy(payload.begin(), payload.end(),
                        std::back_inserter(serverResponse));
              if (serverResponse.size() >= largeContent.size()) {
                return remoteClient->closeEndpoint(
                    std::format("Closing client"));
              }
              return medici::Expected{};
            },
            [this](std::string_view payload, medici::TimePoint) {
              clientOutgoingPayload = payload;
              return medici::Expected{};
            },
            [this](const std::string &reason,
                   const medici::sockets::IPEndpointConfig &) {
              BOOST_TEST_MESSAGE(
                  std::format("Closing client: reason={}", reason));
              return clientThreadContext->stop();
            },
            clientDisconnectHandler,
            [this]() {
              BOOST_TEST_MESSAGE(
                  std::format(" {} client Active", endpointType));
              return remoteClient->send(largeContent);
            });
    RunTest();
  }
};

} // namespace medici::tests
BOOST_FIXTURE_TEST_SUITE(MediciUnitTCPClearTests,
                         medici::tests::TcpClientServerTestHarness<
                             medici::sockets::live::TcpEndpoint>);

BOOST_AUTO_TEST_CASE(TCP_TEST) { RunTCPTest(); };
BOOST_AUTO_TEST_CASE(TCP_TEST_ASYNC_SEND) {
  useAsyncSend = true;
  RunTCPTest();
  BOOST_CHECK(serverFinishedResponse);
};

BOOST_AUTO_TEST_SUITE_END();

BOOST_FIXTURE_TEST_SUITE(MediciUnitTCPSSLTests,
                         medici::tests::TcpClientServerTestHarness<
                             medici::sockets::live::SSLEndpoint>);

BOOST_AUTO_TEST_CASE(SSL_TEST) { RunSSLTest(); };
BOOST_AUTO_TEST_CASE(SSL_TEST_ASYNC_SEND) {
  useAsyncSend = true;
  RunSSLTest();
  BOOST_CHECK(serverFinishedResponse);
};
BOOST_AUTO_TEST_SUITE_END();
