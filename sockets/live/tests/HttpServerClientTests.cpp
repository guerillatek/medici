#include "ServerClientTestHarness.hpp"

namespace medici::tests {

template <medici::sockets::IsHttpServerEndpoint EndpointT>
struct HttpServerClientTestHarness : ServerClientTestHarness {
  bool largeContentSuccessful = false;
  bool LargeContentGzippedSuccessful = false;
  bool LargeContentBrotliSuccessful = false;
  bool LargeContentDeflateSuccessful = false;

  bool formGetSuccessful = false;
  bool formPostSuccessful = false;
  bool formMultiPartSuccessful = false;
  bool formMultiPartCompressedSuccessful = false;

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
                                           content, contentType, compression);
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

  bool ValidateFormData(const http::HttpFields &payload) {
    if (payload.getFieldCount() != 3) {
      BOOST_FAIL(
          std::format("Multipart field count mismatch: expected 3 got {}",
                      payload.getFieldCount()));
      return false;
    }
    auto field1 = payload.getField("field1");
    if (!field1 || field1.value() != "value1") {
      BOOST_FAIL(
          std::format("field1 value mismatch: expected 'value1' got '{}'",
                      field1 ? field1.value() : "NOT FOUND"));
      return false;
    }
    auto field2 = payload.getField("field2");
    if (!field2 || field2.value() != "value2") {
      BOOST_FAIL(
          std::format("field2 value mismatch: expected 'value2' got '{}'",
                      field2 ? field2.value() : "NOT FOUND"));
      return false;
    }
    auto field3 = payload.getField("field3");
    if (!field3 || field3.value() != "value3") {
      BOOST_FAIL(
          std::format("field3 value mismatch: expected 'value3' got '{}'",
                      field3 ? field3.value() : "NOT FOUND"));
      return false;
    }
    return true;
  }

  Expected HandleClientRequests(http::HTTPAction action,
                                const std::string &requestURI,
                                const http::HttpFields &requestHeaders,
                                const sockets::HttpServerPayloadT &payload,
                                TimePoint) {
    if (action == http::HTTPAction::GET &&
        (requestURI == "/testing/largeContentRequest")) {
      if (requestHeaders.HasField("Accept-Encoding")) {
        auto encoding = requestHeaders.getField("Accept-Encoding");
        if (encoding && encoding.value() == "gzip") {
          http::HttpFields headers;
          headers.addFieldValue("Custom-Header", "LargeContentGzipped");
          return sendHttpResponse(headers, 200, "OK", largeContent,
                                  http::ContentType::AppJSON,
                                  http::SupportedCompression::GZip);
        } else if (encoding && encoding.value() == "br") {
          http::HttpFields headers;
          headers.addFieldValue("Custom-Header", "LargeContentBrotli");
          return sendHttpResponse(headers, 200, "OK", largeContent,
                                  http::ContentType::AppJSON,
                                  http::SupportedCompression::Brotli);
        } else if (encoding && encoding.value() == "deflate") {
          http::HttpFields headers;
          headers.addFieldValue("Custom-Header", "LargeContentDeflate");
          return sendHttpResponse(headers, 200, "OK", largeContent,
                                  http::ContentType::AppJSON,
                                  http::SupportedCompression::HttpDeflate);
        }
      } else {
        http::HttpFields headers;
        headers.addFieldValue("Custom-Header", "LargeContentTest");
        return sendHttpResponse(headers, 200, "OK", largeContent,
                                http::ContentType::AppJSON,
                                http::SupportedCompression::None);
      }
    }
    if (requestURI == "/testing/formTestPost" &&
        action == http::HTTPAction::POST) {
      if (std::get_if<http::HttpFields>(&payload) == nullptr) {
        BOOST_FAIL("Expected HttpFields payload");
        return std::unexpected("Expected HttpFields payload");
      }
      auto &formPayload = std::get<http::HttpFields>(payload);

      if (!ValidateFormData(formPayload)) {
        return std::unexpected("Form data validation failed");
      }
      http::HttpFields headers;
      headers.addFieldValue("Custom-Header", "formPostResponse");
      return sendHttpResponse(headers, 200, "OK", "SUCCESS",
                              http::ContentType::TextPlain,
                              http::SupportedCompression::None);
    }
    if (requestURI == "/testing/formTestGet" &&
        action == http::HTTPAction::GET) {
      if (std::get_if<http::HttpFields>(&payload) == nullptr) {
        BOOST_FAIL("Expected HttpFields payload");
        return std::unexpected("Expected HttpFields payload");
      }
      auto &formPayload = std::get<http::HttpFields>(payload);
      if (!ValidateFormData(formPayload)) {
        return std::unexpected("Form data validation failed");
      }
      http::HttpFields headers;
      headers.addFieldValue("Custom-Header", "formGetResponse");
      return sendHttpResponse(headers, 200, "OK", "SUCCESS",
                              http::ContentType::TextPlain,
                              http::SupportedCompression::None);
    }
    return Expected{};
  }

  Expected HandleServerResponse(const http::HttpFields &headers,
                                std::string_view payload, int responseCode,
                                TimePoint) {
    if (!headers.HasField("Custom-Header")) {
      return std::unexpected("Custom-Header not found in response");
    }
    auto headerValue = headers.getField("Custom-Header");
    if (headerValue && headerValue.value() == "LargeContentTest") {
      BOOST_CHECK(responseCode == 200);
      BOOST_CHECK(payload == largeContent);
      largeContentSuccessful = true;
    } else if (headerValue && headerValue.value() == "LargeContentGzipped") {
      BOOST_CHECK(responseCode == 200);
      BOOST_CHECK(payload == largeContent);
      LargeContentGzippedSuccessful = true;
    } else if (headerValue && headerValue.value() == "LargeContentBrotli") {
      BOOST_CHECK(responseCode == 200);
      BOOST_CHECK(payload == largeContent);
      LargeContentBrotliSuccessful = true;
    } else if (headerValue && headerValue.value() == "LargeContentDeflate") {
      BOOST_CHECK(responseCode == 200);
      BOOST_CHECK(payload == largeContent);
      LargeContentDeflateSuccessful = true;
    } else if (headerValue == "formPostResponse") {
      BOOST_CHECK(responseCode == 200);
      BOOST_CHECK(payload == "SUCCESS");
      formPostSuccessful = true;
    } else if (headerValue == "formGetResponse") {
      BOOST_CHECK(responseCode == 200);
      BOOST_CHECK(payload == "SUCCESS");
      formGetSuccessful = true;
      remoteClient->closeEndpoint("Test complete");
    }

    return Expected{};
  }

  Expected StartTests() {
    remoteClient->setURIPath("/testing/largeContentRequest");
    remoteClient->sendHttpRequest(http::HTTPAction::GET, http::HttpFields{}, "",
                                  http::ContentType::Unspecified,
                                  http::SupportedCompression::None,
                                  sockets::HttpResponsePayloadOptions{});
    remoteClient->setURIPath("/testing/largeContentRequest");
    remoteClient->sendHttpRequest(
        http::HTTPAction::GET, http::HttpFields{}, "",
        http::ContentType::Unspecified, http::SupportedCompression::None,
        sockets::HttpResponsePayloadOptions{
            std::to_underlying(http::SupportedCompression::GZip), true, false});
    remoteClient->sendHttpRequest(
        http::HTTPAction::GET, http::HttpFields{}, "",
        http::ContentType::Unspecified, http::SupportedCompression::None,
        sockets::HttpResponsePayloadOptions{
            std::to_underlying(http::SupportedCompression::Brotli), true,
            false});
    remoteClient->sendHttpRequest(
        http::HTTPAction::GET, http::HttpFields{}, "",
        http::ContentType::Unspecified, http::SupportedCompression::None,
        sockets::HttpResponsePayloadOptions{
            std::to_underlying(http::SupportedCompression::HttpDeflate), true,
            false});
    http::HttpFields formData;
    formData.addFieldValue("field1", "value1");
    formData.addFieldValue("field2", "value2");
    formData.addFieldValue("field3", "value3");
    remoteClient->setURIPath("/testing/formTestPost");
    remoteClient->sendFormRequest(http::HTTPAction::POST, http::HttpFields{},
                                  formData, http::SupportedCompression::None,
                                  sockets::HttpResponsePayloadOptions{});
    remoteClient->setURIPath("/testing/formTestGet");
    remoteClient->sendFormRequest(http::HTTPAction::GET, http::HttpFields{},
                                  formData, http::SupportedCompression::None,
                                  sockets::HttpResponsePayloadOptions{});
    return Expected{};
  }

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
    BOOST_CHECK(largeContentSuccessful);
    BOOST_CHECK(LargeContentGzippedSuccessful);
    BOOST_CHECK(LargeContentBrotliSuccessful);
    BOOST_CHECK(LargeContentDeflateSuccessful);
    BOOST_CHECK(formGetSuccessful);
    BOOST_CHECK(formPostSuccessful);
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
