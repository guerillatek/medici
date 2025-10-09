#include "ServerClientTestHarness.hpp"

namespace medici::tests {

template <medici::sockets::IsHttpServerEndpoint EndpointT>
struct HttpServerClientTestHarness : ServerClientTestHarness {

  Expected sendHttpResponse(
      http::HttpFields headersValues, int responseCode = 200,
      const std::string &message = "OK", std::string_view content = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      http::SupportedCompression compression =
          http::SupportedCompression::None) {

    auto &targetEndpoint = remoteListener.getEndpointCoordinator()
                               .getActiveContext()
                               .getEndpoint();

    return targetEndpoint.sendHttpResponse(headersValues, responseCode, message,
                                           content, contentType,
                                           compression);
  }

  Expected sendFileResponse(
      http::HttpFields headersValues, int responseCode = 200,
      const std::string &message = "OK", std::string filePath = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      http::SupportedCompression compression =
          http::SupportedCompression::None) {

    auto &targetEndpoint = remoteListener.getEndpointCoordinator()
                               .getActiveContext()
                               .getEndpoint();

    return targetEndpoint.sendFileResponse(200, "OK", headersValues,
                                           responseCode, message, filePath,
                                           contentType, compression);
  }

  http::HttpFields headersReceivedOnServer{};
  medici::sockets::IHttpClientEndpointPtr remoteClient;

  using RemoteListener =
      medici::sockets::RemoteEndpointListener<EndpointT, RunContextT>;

  RemoteListener remoteListener;

  HttpServerClientTestHarness()
      : ServerClientTestHarness(remoteClient),
        remoteListener{
            *serverThreadContext,
            listenEndpoint,
            [this](const std::string &reason,
                   const sockets::IPEndpointConfig &) {
              BOOST_TEST_MESSAGE(std::format(" {} Listener Closing: reason={}",
                                             endpointType, reason));
              return serverThreadContext->stop();
            },
            listenDisconnectHandler,
            [this]() {
              BOOST_TEST_MESSAGE("Remote Listener Active");

              return clientRunFunc();
            },
            [this](http::HTTPAction action, const std::string &requestURI,
                   const http::HttpFields &fields,
                   const sockets::HttpServerPayloadT &payload, TimePoint tp) {
              return HandleClientRequests(action, requestURI, fields, payload,
                                            tp);
            },
            [this](std::string_view payload, medici::TimePoint) {
              return Expected{};
            },
            clientCloseHandler,
            [this](const std::string &reason,
                   const sockets::IPEndpointConfig &endpointConfig) {
              BOOST_TEST_MESSAGE(std::format(
                  " {} client disconnected: reason={}", endpointType, reason));
              return remoteListener.stop("Ending Test");
            },
            [this]() {
              serverDetectedClientActive = true;
              return Expected{};
            }} {}

  Expected HandleClientRequests(http::HTTPAction action,
                                  const std::string &requestURI,
                                  const http::HttpFields &,
                                  const sockets::HttpServerPayloadT &,
                                  TimePoint) {
    if (action == http::HTTPAction::GET &&
        (requestURI == "/testing/largeContentRequest")) {
      http::HttpFields headers;
      headers.addFieldValue("Custom-Header", "LargeContentTest");
      return sendHttpResponse(headers, 200, "OK", largeContent,
                              http::ContentType::AppJSON,
                              http::SupportedCompression::None);
    }

    return Expected{};
  }

  Expected HandleServerResponse(const http::HttpFields &headers,
                                std::string_view payload, int responseCode, TimePoint) {
    if(headers.HasField("Custom-Header")){
      auto headerValue = headers.getField("Custom-Header");
      if(headerValue && headerValue.value() == "LargeContentTest"){
        BOOST_CHECK(responseCode == 200);
        BOOST_CHECK(payload == largeContent);
    
      } else {
        BOOST_FAIL("Custom-Header value mismatch");
      }
    } else {
      BOOST_FAIL("Custom-Header not found in response");
    }
    return Expected{};
  }

  Expected StartTests() {
    remoteClient->setURIPath("/testing/largeContentRequest");
    remoteClient->sendHttpRequest(http::HTTPAction::GET, http::HttpFields{}, "",
                                  http::ContentType::Unspecified,
                                  http::SupportedCompression::None,
                                  sockets::HttpResponsePayloadOptions{});

    return Expected{};
  }

  void ValidateResults() {}

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
        std::cerr << std::format("ERROR: Server thread exited with error: {}",
                                 result.error())
                  << std::endl;
      }
    });
    while (serverRunning && !clientThread)
      ;
    p1.join();
    if (clientThread)
      clientThread->join();
    ValidateResults();
  }

  void RunHTTPUnsecureTest() {
    endpointType = "HTTP Unsecure";
    remoteClient =
        clientThreadContext->getSocketFactory().createHttpClientEndpoint(
            listenEndpoint,
            [this](const http::HttpFields &headers, std::string_view payload,
                   int responseCode, medici::TimePoint tp) {
              return HandleServerResponse(headers, payload, responseCode, tp);
            },
            [this](std::string_view payload, medici::TimePoint tp) {
              return Expected{};
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
              return StartTests();
            });

    RunTest();
  }
};

} // namespace medici::tests

BOOST_FIXTURE_TEST_SUITE(MediciUnitHTTPUnsecureTests,
                         medici::tests::HttpServerClientTestHarness<
                             medici::sockets::live::HttpServerEndpoint>);
BOOST_AUTO_TEST_CASE(HTTP_UNSECURE_TEST) { RunHTTPUnsecureTest(); };

BOOST_AUTO_TEST_SUITE_END();
/*
BOOST_FIXTURE_TEST_SUITE(
    MediciUnitTCPSSLTests,
    medici::tests::HttpServerClientTestHarness<medici::sockets::live::SSLEndpoint>);

BOOST_AUTO_TEST_CASE(SSL_TEST) { RunSSLTest(); };

BOOST_AUTO_TEST_SUITE_END();*/
