#pragma once

#include "medici/sockets/live/HTTPLiveEndpoint.hpp"

namespace medici::sockets::live {

template <template <class> class BaseSocketEndpoint>
class HTTPLiveServerEndpoint
    : public HTTPLiveEndpoint<HttpPayloadHandlerT, BaseSocketEndpoint,
                              IHttpServerEndpoint> {
  using BaseSocketEndpointT =
      HTTPLiveEndpoint<HttpPayloadHandlerT, BaseSocketEndpoint,
                       IHttpServerEndpoint>;
  using ParseExpected = std::expected<std::string, std::string>;
  using HttpSendQueue = std::deque<HttpSendQueueEntry>;

public:
  HTTPLiveServerEndpoint(int fd, const HttpEndpointConfig &config,
                         IIPEndpointPollManager &endpointPollManager,
                         HttpServerPayloadHandlerC auto &&serverPayloadHandler,
                         SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                         CloseHandlerT closeHandler,
                         DisconnectedHandlerT DisconnectedHandlerT,
                         OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{fd,
                            config,
                            endpointPollManager,
                            [this](const http::HttpFields &requestHeaders,
                                   std::string_view payload, TimePoint tp) {
                              return demuxPayloadToVariantTypes(requestHeaders,
                                                                payload, tp);
                            },
                            std::forward<decltype(outgoingPayloadHandler)>(
                                outgoingPayloadHandler),
                            closeHandler,
                            DisconnectedHandlerT,
                            onActiveHandler},
        _serverPayloadHandler{std::move(serverPayloadHandler)} {}

  Expected sendHttpResponse(
      http::HttpFields headersValues, int responseCode = 200,
      const std::string &message = "OK", std::string_view content = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      http::SupportedCompression compression =
          http::SupportedCompression::None) override {
    auto canSend = this->_sendQueue.empty();
    this->_sendQueue.emplace_back(
        std::nullopt, headersValues, HttpResponseHeader{responseCode, message},
        std::string{content}, compression, "", this->_uriPath, std::nullopt);
    if (!canSend) {
      return {};
    }
    return this->sendQueuedHttpData();
    // queue was empty so we can send immediately
  }

  Expected sendFileResponse(
      http::HttpFields headersValues, int responseCode = 200,
      const std::string &message = "OK", std::string filePath = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      http::SupportedCompression compressed =
          http::SupportedCompression::None) override {
    auto canSend = this->_sendQueue.empty();
    auto targetContent = std::filesystem::path{filePath};
    if (!std::filesystem::exists(targetContent) ||
        !std::filesystem::is_regular_file(targetContent)) {
      return std::unexpected(
          std::format("Attempted to send non-existent file '{}'", filePath));
    }
    this->_sendQueue.emplace_back(
        std::nullopt, headersValues, HttpResponseHeader{responseCode, message},
        targetContent, compressed, this->_uriPath, "", std::nullopt);
    if (!canSend) {
      return {};
    }
    return this->sendQueuedHttpData();
    // queue was empty so we can send immediately
  }

private:
  Expected demuxPayloadToVariantTypes(const http::HttpFields &requestHeaders,
                                      std::string_view payload, TimePoint tp) {
    if (requestHeaders.HasField("Content-Type")) {
      if (requestHeaders.getField("Content-Type")
              .value()
              .find("multipart/form-data") != std::string::npos) {
        // Multipart form data
        http::MultipartPayload multiPartPayload;
        if (auto result = multiPartPayload.decodePayload(payload); !result) {
          return result;
        }
        http::HttpFields formFields{multiPartPayload};
        return _serverPayloadHandler(BaseSocketEndpointT::getIncomingAction(),
                                     BaseSocketEndpointT::getRequestURIPath(),
                                     requestHeaders, formFields, tp);
      } else if (requestHeaders.getField("Content-Type")
                     .value()
                     .find("application/x-www-form-urlencoded") !=
                 std::string::npos) {
        // URL Encoded form data
        http::HttpFields formFields;
        auto loadResult = formFields.loadFromURLString(std::string{payload});
        if (!loadResult) {
          return std::unexpected(
              std::format("Failed to parse URL encoded form data: error={}",
                          loadResult.error()));
        }
        return _serverPayloadHandler(BaseSocketEndpointT::getIncomingAction(),
                                     BaseSocketEndpointT::getRequestURIPath(),
                                     requestHeaders, formFields, tp);
      }
    }
    return _serverPayloadHandler(BaseSocketEndpointT::getIncomingAction(),
                                 BaseSocketEndpointT::getRequestURIPath(),
                                 requestHeaders, payload, tp);
  }
  HttpServerPayloadHandlerT _serverPayloadHandler;
};

} // namespace medici::sockets::live