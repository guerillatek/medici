#include "ServerClientTestHarness.hpp"
namespace medici::tests {

template <medici::sockets::IsWebSocketEndpoint EndpointT>
struct WSClientServerTestHarness : ServerClientTestHarness {

  WSClientServerTestHarness() : ServerClientTestHarness(remoteClient) {}

  Expected echoPayload(std::string_view payload) {
    std::copy(payload.begin(), payload.end(),
              std::back_inserter(serverCapturedPayload));
    if (serverCapturedPayload.size() >= largeContent.size()) {
      return remoteListener.getEndpointCoordinator()
          .getActiveContext()
          .getEndpoint()
          .sendText(serverCapturedPayload);
    }
    return {};
  }

  medici::sockets::IWebSocketEndpointPtr remoteClient;

  using RemoteListener =
      medici::sockets::RemoteEndpointListener<EndpointT, RunContextT>;

  RemoteListener remoteListener{
      *(ServerClientTestHarness::serverThreadContext),
      ServerClientTestHarness::listenEndpoint,
      [this](const std::string &reason) {
        BOOST_TEST_MESSAGE(std::format(" {} Listener Closing: reason={}",
                                       endpointType, reason));
        return serverThreadContext->stop();
      },
      listenDisconnectHandler,
      [this]() {
        BOOST_TEST_MESSAGE("Remote Listener Active");

        return clientRunFunc();
      },
      [this](std::string_view payload, medici::sockets::WSOpCode,
             medici::TimePoint) { return echoPayload(payload); },
      [this](std::string_view payload, medici::sockets::WSOpCode,
             medici::TimePoint) { return Expected{}; },
      clientCloseHandler,
      [this](const std::string &reason) {
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
    // Wait for OS to flush sockets
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  void RunWSTest() {
    endpointType = "WS Endpoint";
    remoteClient =
        clientThreadContext->getSocketFactory().createWSClientEndpoint(
            listenEndpoint,
            [this](std::string_view payload, medici::sockets::WSOpCode,
                   medici::TimePoint) {
              serverResponse = payload;
              return remoteClient->closeEndpoint(std::format("Closing client"));
            },
            [this](std::string_view payload, medici::sockets::WSOpCode,
                   medici::TimePoint) {
              clientOutgoingPayload = payload;
              return medici::Expected{};
            },
            [this](const std::string &reason) {
              BOOST_TEST_MESSAGE(
                  std::format("Closing client: reason={}", reason));
              return clientThreadContext->stop();
            },
            clientDisconnectHandler,
            [this]() {
              BOOST_TEST_MESSAGE(
                  std::format(" {} client Active", endpointType));
              return remoteClient->sendText(largeContent);
            });

    RunTest();
  }

  void RunWSSTest() {
    endpointType = "WSS Endpoint";
    remoteClient =
        clientThreadContext->getSocketFactory().createWSSClientEndpoint(
            listenEndpoint,
            [this](std::string_view payload, medici::sockets::WSOpCode op,
                   medici::TimePoint) {
              BOOST_CHECK(op == medici::sockets::WSOpCode::Text);
              serverResponse =
                  payload; // Full payload should be assembled already
              if (serverResponse.size() >= largeContent.size()) {
                return remoteClient->closeEndpoint(
                    std::format("Closing client"));
              }
              return medici::Expected{};
            },
            [this](std::string_view payload, medici::sockets::WSOpCode,
                   medici::TimePoint) {
              clientOutgoingPayload = payload;
              return medici::Expected{};
            },
            [this](const std::string &reason) {
              BOOST_TEST_MESSAGE(
                  std::format("Closing client: reason={}", reason));
              return clientThreadContext->stop();
            },
            clientDisconnectHandler,
            [this]() {
              BOOST_TEST_MESSAGE(
                  std::format(" {} client Active", endpointType));
              return remoteClient->sendText(largeContent);
            });
    RunTest();
  }
};

} // namespace medici::tests

BOOST_FIXTURE_TEST_SUITE(
    MediciUnitWSClearTests,
    medici::tests::WSClientServerTestHarness<
        medici::sockets::live::WebSocketLiveServerEndpoint>);

BOOST_AUTO_TEST_CASE(WS_TEST) { RunWSTest(); };

BOOST_AUTO_TEST_CASE(WS_PMDeflate_Test) {
  listenEndpoint = medici::sockets::WSEndpointConfig{"listenHost", "127.0.0.1",
                                                     12345, "/", true};
  RunWSTest();
};

BOOST_AUTO_TEST_SUITE_END();

BOOST_FIXTURE_TEST_SUITE(
    MediciUnitWSSTests,
    medici::tests::WSClientServerTestHarness<
        medici::sockets::live::WebSSocketLiveServerEndpoint>);

BOOST_AUTO_TEST_CASE(WSS_TEST) { RunWSSTest(); };

BOOST_AUTO_TEST_CASE(WSS_PMDeflate_TEST) {
  listenEndpoint = medici::sockets::WSEndpointConfig{"listenHost", "127.0.0.1",
                                                     12345, "/", true};
  RunWSSTest();
};

BOOST_AUTO_TEST_SUITE_END();
