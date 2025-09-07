#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <boost/test/unit_test.hpp>

#include "medici/application/IPAppRunContext.hpp"
#include "medici/sockets/GroupEndpointCoordinator.hpp"
#include "medici/sockets/live/LiveSocketFactory.hpp"
#include "medici/sockets/live/TcpIpLiveEndpoint.hpp"

namespace medici::sockets::live::tests {
struct TestClock {
  TestClock(TimePoint &currentTime) : _currentTime{currentTime} {}

  auto operator()() const { return _currentTime; }

  TimePoint &_currentTime;
};

struct LiveSocketTestHarness {
    /*
  TimePoint currentTime;
  TestClock clock{currentTime};
  sockets::live::IPEndpointPollManager pollManager{"TestSocketsPollMgr", clock};
  LiveSocketFactory socketFactory{pollManager};
  using RunContext =
      medici::application::IPAppRunContext<LiveSocketFactory, TestClock,
                                           IPEndpointPollManager>;
  RunContext runContext{"TestRunContext", 5, std::chrono::microseconds{10},
                        clock};*/
};

} // namespace medici::sockets::live::tests
using namespace medici;
using namespace medici::sockets;
using namespace medici::sockets::live::tests;


BOOST_FIXTURE_TEST_SUITE(MediciUnitTests, LiveSocketTestHarness);

BOOST_AUTO_TEST_CASE(EMPTY_TEST) { BOOST_CHECK(true); }

/*
BOOST_AUTO_TEST_CASE(TCP_LIVE_ENDPOINT_TEST) {
    
  auto payloadHandler1 = [](std::string_view, TimePoint) { return Expected{}; };

  payloadHandler1("", TimePoint{});
  
  auto payloadHandler2 = [](std::string_view, TimePoint) { return Expected{}; };

  auto payloadHandler3 = [](const http::HttpFields &, std::string_view, int,
                            TimePoint) { return Expected{}; };

  auto payloadHandler4 = [](const http::HttpFields &, std::string_view, int,
                            TimePoint) { return Expected{}; };

  auto payloadHandler5 = [](std::string_view, WSOpCode, TimePoint) {
    return Expected{};
  };

  auto payloadHandler6 = [](std::string_view, WSOpCode, TimePoint) {
    return Expected{};
  };
  */

  /*
  IPEndpointConfig config{"", "", 0};
  auto tcpEndpoint = runContext.getSocketFactory().createTcpIpClientEndpoint(
      config, std::move(payloadHandler1), SocketPayloadHandlerT{},
      CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{});

      
  auto sslEndpoint = runContext.getSocketFactory().createSSLClientEndpoint(
      config, std::move(payloadHandler2), SocketPayloadHandlerT{},
      CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{});
  HttpEndpointConfig httpConfig{"", "", 0, ""};

  auto httpClientEndpoint =
      runContext.getSocketFactory().createHttpClientEndpoint(
          httpConfig, std::move(payloadHandler3), HttpPayloadHandlerT{},
          CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{});

  auto httpsClientEndpoint =
      runContext.getSocketFactory().createHttpsClientEndpoint(
          httpConfig, std::move(payloadHandler4), HttpPayloadHandlerT{},
          CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{});

  auto wsClientEndpoint = runContext.getSocketFactory().createWSClientEndpoint(
      httpConfig, std::move(payloadHandler5), WebSocketPayloadHandlerT{},
      CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{});

  auto wssClientEndpoint =
      runContext.getSocketFactory().createWSSClientEndpoint(
          httpConfig, std::move(payloadHandler6), WebSocketPayloadHandlerT{},
          CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{});

  auto tcpListenerEndpoint =
      runContext.getSocketFactory().createTcpIpListenerEndpoint(
          config, CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{},
          RemoteTcpEndpointHandlerT{});

  auto httpListenerEndpoint =
      runContext.getSocketFactory().createHttpListenerEndpoint(
          config, CloseHandlerT{}, DisconnectedHandlerT{}, OnActiveHandlerT{},
          RemoteHttpEndpointHandlerT{});

  auto closeHandler = CloseHandlerT{};
  auto disconnectedHandler = DisconnectedHandlerT{};
  auto onActiveHandler = OnActiveHandlerT{};

  auto groupTcpEndpointCoordinator =
      GroupEndpointCoordinator<live::TcpEndpoint>(
          SocketPayloadHandlerT{}, SocketPayloadHandlerT{}, closeHandler,
          disconnectedHandler, onActiveHandler);

  auto groupSSlEndpointCoordinator =
      GroupEndpointCoordinator<live::SSLEndpoint>(
          SocketPayloadHandlerT{}, SocketPayloadHandlerT{}, closeHandler,
          disconnectedHandler, onActiveHandler);

  auto groupHttpEndpointCoordinator =
      GroupEndpointCoordinator<live::HttpEndpoint>(
          HttpPayloadHandlerT{}, HttpPayloadHandlerT{}, closeHandler,
          disconnectedHandler, onActiveHandler);

  auto groupHttpsEndpointCoordinator =
      GroupEndpointCoordinator<live::HttpsEndpoint>(
          HttpPayloadHandlerT{}, HttpPayloadHandlerT{}, closeHandler,
          disconnectedHandler, onActiveHandler);

  auto groupWSEndpointCoordinator =
      GroupEndpointCoordinator<live::WebSocketEndpoint>(
          WebSocketPayloadHandlerT{}, WebSocketPayloadHandlerT{}, closeHandler,
          disconnectedHandler, onActiveHandler);
}
*/

BOOST_AUTO_TEST_SUITE_END();
