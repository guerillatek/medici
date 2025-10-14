#include "medici/application/IPAppRunContext.hpp"
#include "medici/application/concepts.hpp"
#include "medici/cryptoUtils/OpenSSLUtils.hpp"
#include "medici/sockets/RemoteEndpointListener.hpp"
#include "medici/sockets/live/LiveSocketFactory.hpp"
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <format>
#include <list>
#include <memory>
#include <random>
#include <set>
#include <vector>

#include "largeContent.hpp"

namespace utf = boost::unit_test;

namespace medici::tests {

using RunContextT =
    medici::application::IPAppRunContext<sockets::live::LiveSocketFactory,
                                         SystemClockNow,
                                         sockets::live::IPEndpointPollManager>;

using RunContextTPtr = std::unique_ptr<RunContextT>;

struct ServerClientTestHarness {

  static Expected sslInitialization;

  ServerClientTestHarness(auto &remoteClient) {
    serverThreadContext = std::make_unique<RunContextT>(
        "Server", 1, std::chrono::microseconds{1000}, SystemClockNow{},
        serverCertFilePath, serverKeyFilePath);
    clientThreadContext = std::make_unique<RunContextT>(
        "Client", 1, std::chrono::microseconds{1000}, SystemClockNow{});

    clientRunFunc = [this, &remoteClient]() {
      clientThreadContext->getEventQueue().postAction([this, &remoteClient]() {
        BOOST_TEST_MESSAGE("Starting client");
        if (auto result = remoteClient->openEndpoint(); !result) {
          BOOST_ERROR(std::format("Failed to open client endpoint: {}",
                                  result.error()));
          return result;
        }
        BOOST_TEST_MESSAGE("Client endpoint opened successfully");
        return Expected{};
      });
      clientThread = std::make_unique<std::thread>(
          [this]() { clientThreadContext->start(); });
      return Expected{};
    };
  }

  RunContextTPtr serverThreadContext;

  RunContextTPtr clientThreadContext;


  medici::sockets::WSEndpointConfig listenEndpoint{"listenHost", "127.0.0.1",
                                                   12345, "/",false};
  std::string endpointType;

  medici::sockets::CloseHandlerT listenCloseHandler =
      [this](const std::string &reason, const sockets::IPEndpointConfig &) {
        BOOST_TEST_MESSAGE(
            std::format(" {} listener close: reason={}", endpointType, reason));
        return Expected{};
      };
  medici::sockets::CloseHandlerT listenDisconnectHandler =
      [this](const std::string &reason, const sockets::IPEndpointConfig &) {
        BOOST_TEST_MESSAGE(std::format(" {} listener disconnected: reason={}",
                                       endpointType, reason));
        return Expected{};
      };

  medici::sockets::CloseHandlerT clientCloseHandler =
      [this](const std::string &reason, const sockets::IPEndpointConfig &) {
        BOOST_TEST_MESSAGE(
            std::format(" {} client close: reason={}", endpointType, reason));
        return Expected{};
      };

  medici::sockets::DisconnectedHandlerT clientDisconnectHandler =
      [this](const std::string &reason, const sockets::IPEndpointConfig &) {
        BOOST_TEST_MESSAGE(std::format(" {} client disconnected: reason={}",
                                       endpointType, reason));
        return Expected{};
      };

  std::string serverResponse;
  std::string serverCapturedPayload;
  std::string clientOutgoingPayload;
  bool serverDetectedClientActive = false;
  bool serverFinishedResponse = false;
  std::function<Expected()> clientRunFunc;
  std::unique_ptr<std::thread> clientThread;
  static std::string serverCertFilePath;
  static std::string serverKeyFilePath;
  bool useAsyncSend{false};
};

} // namespace medici::tests