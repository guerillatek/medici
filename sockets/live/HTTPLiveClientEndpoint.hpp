#pragma once

#include "medici/sockets/live/HTTPLiveEndpoint.hpp"

namespace medici::sockets::live {

template <template <class> class BaseSocketEndpoint>
class HTTPLiveClientEndpoint
    : public HTTPLiveEndpoint<HttpPayloadHandlerT, BaseSocketEndpoint,
                              IHttpClientEndpoint> {
  using BaseSocketEndpointT =
      HTTPLiveEndpoint<HttpPayloadHandlerT, BaseSocketEndpoint,
                       IHttpClientEndpoint>;
  using ParseExpected = std::expected<std::string, std::string>;
  using HttpSendQueue = std::deque<HttpSendQueueEntry>;

  Expected applyAcceptedCompressions(
      const HttpResponsePayloadOptions &responsePayloadOptions,
      http::HttpFields &headersValues) {
    if (responsePayloadOptions.acceptedCompressionsFlag != 0) {
      std::string acceptedCompressionEncodings;
      if (responsePayloadOptions.acceptedCompressionsFlag &
          std::to_underlying(http::SupportedCompression::GZip)) {
        acceptedCompressionEncodings = "gzip";
      }

      if (responsePayloadOptions.acceptedCompressionsFlag &
          std::to_underlying(http::SupportedCompression::HttpDeflate)) {
        if (!acceptedCompressionEncodings.empty()) {
          acceptedCompressionEncodings += ", ";
        }
        acceptedCompressionEncodings += "deflate";
      }

      if (responsePayloadOptions.acceptedCompressionsFlag &
          std::to_underlying(http::SupportedCompression::Brotli)) {
        if (!acceptedCompressionEncodings.empty()) {
          acceptedCompressionEncodings += ", ";
        }
        acceptedCompressionEncodings += "br";
      }

      if (!acceptedCompressionEncodings.empty()) {
        headersValues.addFieldValue("Accept-Encoding",
                                    acceptedCompressionEncodings);
      }

    } else {
      if (responsePayloadOptions.decompressBeforeDispatch) {
        return std::unexpected(
            "Decompressing before dispatching requires accepting compression "
            "encodings");
      }
    }
    return {};
  }
  HttpClientPayloadHandlerT _payloadHandler;

public:
  HTTPLiveClientEndpoint(
      const HttpEndpointConfig &config,
      IIPEndpointPollManager &endpointPollManager,
      HttpClientPayloadHandlerC auto &&incomingPayloadHandler,
      SocketPayloadHandlerC auto &&outgoingPayloadHandler,
      CloseHandlerT closeHandler, DisconnectedHandlerT DisconnectedHandlerT,
      OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{config,
                            endpointPollManager,
                            [this](const http::HttpFields &httpFields,
                                   std::string_view payload, TimePoint tp) {
                              return _payloadHandler(
                                  httpFields, payload,
                                  BaseSocketEndpointT::getResponseCode(), tp);
                            },
                            std::forward<decltype(outgoingPayloadHandler)>(
                                outgoingPayloadHandler),
                            closeHandler,
                            DisconnectedHandlerT,
                            onActiveHandler},
        _payloadHandler{std::move(incomingPayloadHandler)} {}

  Expected sendHttpRequest(
      http::HTTPAction action, http::HttpFields &&headersValues,
      std::string_view content = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      http::SupportedCompression compression = http::SupportedCompression::None,
      HttpResponsePayloadOptions responsePayloadOptions = {}) override {
    if (auto result = this->clarifySendHeaders(headersValues, content,
                                               contentType, compression);
        !result) {
      return result;
    }
    if (auto result =
            applyAcceptedCompressions(responsePayloadOptions, headersValues);
        !result) {
      return result;
    }
    auto canSend = this->_sendQueue.empty();
    this->_sendQueue.emplace_back(action, headersValues, std::nullopt,
                                  std::string{content}, compression,
                                  this->_uriPath, "", responsePayloadOptions);
    if (canSend) {
      return this->sendQueuedHttpData();
    }
    return {};
  }

  Expected sendFormRequest(
      http::HTTPAction action, http::HttpFields &&headersValues,
      const http::HttpFields &formContent,
      http::SupportedCompression compression = http::SupportedCompression::None,
      HttpResponsePayloadOptions responsePayloadOptions = {}) override {

    auto canSend = this->_sendQueue.empty();
    if (action != http::HTTPAction::POST && action != http::HTTPAction::GET) {
      return std::unexpected(
          "Form submissions must use POST or GET HTTP actions");
    }

    if (formContent.hasFilePathFields()) {
      if (action == http::HTTPAction::GET) {
        return std::unexpected(
            "Form submissions with file content must use POST HTTP action");
      }

      auto multipartPayload = http::MultipartPayload{formContent};
      if (auto result = this->clarifySendHeaders(
              headersValues, "", http::ContentType::Unspecified, compression);
          !result) {
        return result;
      }
      if (headersValues.HasField("Content-Type")) {
        return std::unexpected("Content-Type header cannot be set by the user "
                               "when sending multipart forms "
                               "as it contains dynamic boundary information.");
      }
      headersValues.addFieldValue("Content-Type",
                                  multipartPayload.getContentType());

      if (auto result =
              applyAcceptedCompressions(responsePayloadOptions, headersValues);
          !result) {
        return result;
      }

      this->_sendQueue.emplace_back(action, headersValues, std::nullopt,
                                    multipartPayload, compression,
                                    this->_uriPath, "", responsePayloadOptions);

    } else {
      // No file content so encode as URL encoded form
      auto expectedEncodedForm = formContent.encodeAsQueryString();
      if (!expectedEncodedForm) {
        return std::unexpected(std::format(
            "Failed to encode form content as URL encoded string: error={}",
            expectedEncodedForm.error()));
      }
      std::string payload;
      std::string queryString;
      if (action == http::HTTPAction::GET) {
        queryString = expectedEncodedForm.value();
        if (auto result = this->clarifySendHeaders(
                headersValues, "", http::ContentType::Unspecified, compression);
            !result) {
          return result;
        }
      } else {
        payload = expectedEncodedForm.value();
        if (auto result = this->clarifySendHeaders(
                headersValues, "", http::ContentType::URLEncodedForm,
                compression);
            !result) {
          return result;
        }
      }

      payload = expectedEncodedForm.value();
      if (auto result =
              applyAcceptedCompressions(responsePayloadOptions, headersValues);
          !result) {
        return result;
      }

      this->_sendQueue.emplace_back(action, headersValues, std::nullopt,
                                    payload, compression, this->_uriPath,
                                    queryString, responsePayloadOptions);
    }

    if (canSend) {
      return this->sendQueuedHttpData();
    }

    return {};
  }
};
} // namespace medici::sockets::live