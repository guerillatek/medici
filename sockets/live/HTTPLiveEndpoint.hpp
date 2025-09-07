#pragma once

#include "medici/http/CompressionUtils.hpp"
#include "medici/http/HttpFields.hpp"
#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/concepts.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <deque>
#include <iostream>
#include <spanstream>

namespace medici::sockets::live {

struct HttpResponseHeader {
  int responseCode{200};
  std::string message{"OK"};
};

struct HttpSendQueueEntry {
  std::optional<http::HTTPAction> action;
  http::HttpFields headersValues;
  std::optional<HttpResponseHeader>
      responseHeader; // Optional message for response
  std::string content;
  http::ContentType contentType;
  std::string uriPath;
  HttpResponsePayloadOptions responsePayloadOptions;
};

template <HttpPayloadHandlerC IncomingPayloadHandlerT,
          template <class> class BaseSocketEndpoint>
class HTTPLiveEndpoint : public IHttpEndpoint,
                         public BaseSocketEndpoint<SocketPayloadHandlerT> {
  using BaseSocketEndpointT = BaseSocketEndpoint<SocketPayloadHandlerT>;
  using ParseExpected = std::expected<std::string, std::string>;
  using HttpSendQueue = std::deque<HttpSendQueueEntry>;

public:
  HTTPLiveEndpoint(const HttpEndpointConfig &config,
                   IIPEndpointPollManager &endpointPollManager,
                   IncomingPayloadHandlerT &&incomingPayloadHandler,
                   HttpPayloadHandlerC auto &&outgoingPayloadHandler,
                   CloseHandlerT closeHandler,
                   DisconnectedHandlerT DisconnectedHandlerT,
                   OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{
            config,
            endpointPollManager,
            [this](auto payload, auto tp) {
              return handleBaseSocketInboundPayload(payload, tp);
            },
            [this](auto payload, auto tp) { return Expected{}; },
            closeHandler,
            DisconnectedHandlerT,
            onActiveHandler},
        _uriPath{config.uriPath()},
        _payLoadHandler{std::move(incomingPayloadHandler)} {}

  HTTPLiveEndpoint(int fd, const HttpEndpointConfig &config,
                   IIPEndpointPollManager &endpointPollManager,
                   IncomingPayloadHandlerT &&incomingPayloadHandler,
                   SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                   CloseHandlerT closeHandler,
                   DisconnectedHandlerT DisconnectedHandlerT,
                   OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{fd,
                            config,
                            endpointPollManager,
                            [this](auto payload, auto tp) {
                              return handleBaseSocketInboundPayload(payload,
                                                                    tp);
                            },
                            outgoingPayloadHandler,
                            closeHandler,
                            DisconnectedHandlerT,
                            onActiveHandler},
        _uriPath{config.uriPath()},
        _payLoadHandler{std::move(incomingPayloadHandler)} {}
  ~HTTPLiveEndpoint() = default;

  const std::string &name() const override {
    return BaseSocketEndpointT::name();
  }
  Expected openEndpoint() override {
    return BaseSocketEndpointT::openEndpoint();
  }

  Expected closeEndpoint(const std::string &reason) override {
    BaseSocketEndpointT::closeEndpoint(reason);
    resetHttpSocketState();
    return {};
  }

  bool isActive() const override { return BaseSocketEndpointT::isActive(); }
  const medici::ClockNowT &getClock() const override {
    return BaseSocketEndpointT::getClock();
  }

  IIPEndpointDispatch &getDispatchInterface() override { return *this; }
  // Overrides value set in config
  void setURIPath(const std::string &path) { _uriPath = path; }

  Expected
  sendHttpRequest(http::HTTPAction action, http::HttpFields headersValues,
                  std::string_view content, http::ContentType contentType,
                  HttpResponsePayloadOptions responsePayloadOptions) override {

    if (responsePayloadOptions.acceptedCompressionEncodings) {
      std::string acceptedCompressionEncodings;
      if (responsePayloadOptions.acceptedCompressionEncodings &
          std::to_underlying(http::SupportedCompression::GZip)) {
        acceptedCompressionEncodings = "gzip";
      }

      if (responsePayloadOptions.acceptedCompressionEncodings &
          std::to_underlying(http::SupportedCompression::HttpDeflate)) {
        if (!acceptedCompressionEncodings.empty()) {
          acceptedCompressionEncodings += ", ";
        }
        acceptedCompressionEncodings += "deflate";
      }

      if (responsePayloadOptions.acceptedCompressionEncodings &
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

      if (responsePayloadOptions.decompressBeforeDispatch &&
          responsePayloadOptions.dispatchPartialPayloads) {
        return std::unexpected(
            "Partial payloads cannot be decompressed before dispatching");
      }
    } else {
      if (responsePayloadOptions.decompressBeforeDispatch) {
        return std::unexpected(
            "Decompressing before dispatching requires accepting compression "
            "encodings");
      }
    }

    auto canSend = _sendQueue.empty();
    _sendQueue.emplace_back(action, headersValues, std::nullopt,
                            std::string{content}, contentType, _uriPath,
                            responsePayloadOptions);
    if (canSend) {
      return sendQueuedHttpData();
    }
    return {};
  }

  Expected sendHttpResponse(
      http::HttpFields headersValues, int responseCode,
      const std::string &message, std::string_view content = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      std::optional<http::SupportedCompression> compressed = {}) override {
    auto canSend = _sendQueue.empty();
    _sendQueue.emplace_back(std::nullopt, headersValues,
                            HttpResponseHeader{responseCode, message},
                            std::string{content}, contentType, _uriPath);
    if (canSend) {
      if (compressed) {
        std::string contentEncoding;
        switch (*compressed) {
        case http::SupportedCompression::GZip:
          contentEncoding = "gzip";
          break;
        case http::SupportedCompression::HttpDeflate:
          contentEncoding = "deflate";
          break;
        case http::SupportedCompression::Brotli:
          contentEncoding = "br";
          break;
        default:
          return std::unexpected(
              std::format("Unsupported compression type '{}'", *compressed));
        }
        _sendQueue.back().headersValues.addFieldValue("Content-Encoding",
                                                      contentEncoding);
      }
    }

    return sendQueuedHttpData();
  }

private:
  Expected sendQueuedHttpData() {

    auto &activeQueueEntry = _sendQueue.back();
    auto &headersValues = activeQueueEntry.headersValues;
    auto &responseHeader = activeQueueEntry.responseHeader;
    auto &content = activeQueueEntry.content;
    auto action = activeQueueEntry.action;
    _uriPath = activeQueueEntry.uriPath;
    if (std::size(content) > 0) {
      headersValues.addFieldValue(
          "Content-Type", std::format("{}", activeQueueEntry.contentType));
      headersValues.addFieldValue(
          "Content-Length", std::to_string(activeQueueEntry.content.size()));
    }
    std::string payload;
    if (responseHeader) {
      payload = headersValues.encodeFieldsToResponseHeader(
          responseHeader->responseCode, responseHeader->message);
    } else {
      payload = headersValues.encodeFieldsToRequestHeader(
          *action, _uriPath, this->getConfig().host());
    }

    if (headersValues.HasField("Content-Encoding")) {
      auto encodingStr = headersValues.getField("Content-Encoding").value();
      http::SupportedCompression compressionEncoding =
          http::SupportedCompression::None;
      if (encodingStr == "gzip") {
        compressionEncoding = http::SupportedCompression::GZip;
      } else if (encodingStr == "deflate") {
        compressionEncoding = http::SupportedCompression::HttpDeflate;
      } else if (encodingStr == "br") {
        compressionEncoding = http::SupportedCompression::Brotli;
      }
      _compressedData.clear();
      auto compressionResult =
          http::compressPayload(content, compressionEncoding, _compressedData);
      if (!compressionResult) {
        return std::unexpected(
            std::format("Failed to compress outgoing payload: error={}",
                        compressionResult.error()));
      }
      std::copy(_compressedData.begin(), _compressedData.end(),
                std::back_inserter(payload));
    } else {
      std::copy(content.begin(), content.end(), std::back_inserter(payload));
    }

    _pendingResponse = true;
    return BaseSocketEndpointT::send(payload);
  }

  Expected dispatchPayloadAndCheckQueue(auto epollTime) {

    if (auto result = _payLoadHandler(_activeHeaders, _httpBody, _responseCode,
                                      epollTime);
        !result) {
      return result;
    }

    if (!_sendQueue.empty()) {
      auto entry = _sendQueue.front();
      _sendQueue.pop_front();
      _pendingResponse = true;
      return sendQueuedHttpData();
    }
    _pendingResponse = false;
    return {};
  }

  Expected loadBody(std::spanstream &payloadStream, TimePoint epollTime) {
    auto positionInBuff = payloadStream.tellg();
    auto remainingSize = _activePayload.size() - positionInBuff;
    if ((_httpBody.size() + remainingSize) <= (*_contentSize)) {
      std::copy(_activePayload.begin() + positionInBuff, _activePayload.end(),
                std::back_inserter(_httpBody));
    } else {
      std::copy(_activePayload.begin() + positionInBuff,
                _activePayload.begin() + positionInBuff +
                    (*_contentSize - _httpBody.size()),
                std::back_inserter(_httpBody));
    }

    if (_httpBody.size() >= (*_contentSize)) {
      return dispatchPayloadAndCheckQueue(epollTime);
    }
    return {};
  }

  Expected loadChunks(std::spanstream &payloadStream, TimePoint epollTime) {

    while (!payloadStream.eof()) {
      if (!_chunkSize) {
        auto posB4Line = payloadStream.tellg();
        auto expectedLine = getline_expected(payloadStream);
        if (!expectedLine) {
          _partialChunkLength = expectedLine.error();
          if (trim(_partialChunkLength).empty()) {
            _partialChunkLength.clear();
          }
          continue;
        }
        if (trim(expectedLine.value()).empty()) {
          continue;
        }
        auto &line = expectedLine.value();
        size_t chunkSize;
        std::from_chars(line.data(), line.data() + line.size(), chunkSize, 16);
        _chunkSize = chunkSize;
      }

      if (*_chunkSize == 0) {
        // We're done with chunking so clear the chunked state and dispatch
        _chunkedBody.clear();
        _chunkSize.reset();
        _chunkedResponse = false;
        return dispatchPayloadAndCheckQueue(epollTime);
      }

      auto positionInBuff = payloadStream.tellg();
      auto remainingPayloadSize = _activePayload.size() - positionInBuff;
      if (remainingPayloadSize < *_chunkSize) {
        std::copy(_activePayload.begin() + positionInBuff, _activePayload.end(),
                  std::back_inserter(_chunkedBody));
        *_chunkSize -= remainingPayloadSize;
        return {};

      } else {
        std::copy(_activePayload.begin() + positionInBuff,
                  _activePayload.begin() + positionInBuff + *_chunkSize,
                  std::back_inserter(_chunkedBody));

        _httpBody += _chunkedBody;
        payloadStream.seekg(positionInBuff + *_chunkSize);
        _chunkedBody.clear();
        _chunkSize.reset();
      }
    }
    return {};
  }

  auto trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t");
    size_t last = str.find_last_not_of(" \t\r");
    return (first == std::string::npos || last == std::string::npos)
               ? ""
               : str.substr(first, last - first + 1);
  }

  Expected loadHeaders(std::spanstream &payloadStream, TimePoint epollTime) {
    ParseExpected result;
    _httpBody.clear();

    while ((result = getline_expected(payloadStream))) {
      auto line = result.value();
      size_t delimiterPos = line.find(':');
      if (delimiterPos != std::string::npos) {
        std::string key = line.substr(0, delimiterPos);
        std::string value = line.substr(delimiterPos + 1);

        // Trim spaces
        key = trim(key);
        value = trim(value);
        _activeHeaders.addFieldValue(key, value);
        if (payloadStream.eof()) {
          return {}; // edge case where the buffer read ends exactly
                     // at the eol for a header line but there is more
                     // content to handle
        }
      } else if (trim(line).empty()) {
        // We've read an empty line so either the body is starting
        // order this is the end of the content. Check the headers values
        // to verify

        if (_activeHeaders.HasField("Content-Encoding")) {
          auto encodingStr =
              _activeHeaders.getField("Content-Encoding").value();
          if (encodingStr == "gzip") {
            _compression = http::SupportedCompression::GZip;
          } else if (encodingStr == "deflate") {
            _compression = http::SupportedCompression::HttpDeflate;
          } else if (encodingStr == "br") {
            _compression = http::SupportedCompression::Brotli;
          } else {
            return std::unexpected(
                std::format("Unsupported content encoding '{}'", encodingStr));
          }
        }

        if (_activeHeaders.HasField("Content-Length")) {
          auto contentLenStr =
              _activeHeaders.getField("Content-Length").value();
          size_t contentSize;
          std::from_chars(contentLenStr.data(),
                          contentLenStr.data() + contentLenStr.size(),
                          contentSize);
          _contentSize = contentSize;
          return loadBody(payloadStream, epollTime);
        } else if (_activeHeaders.HasField("Transfer-Encoding") &&
                   _activeHeaders.getField("Transfer-Encoding").value() ==
                       "chunked") {
          // Handle chunked transfer encoding
          _chunkedResponse = true;
          return loadChunks(payloadStream, epollTime);
        }
        // There's no content body so close the response
        // dispatch the headers
        _inResponse = false;
        return dispatchPayloadAndCheckQueue(epollTime);
      }
    }

    // We read a partial header line because the buffer
    // read didn't contain all the expected content
    _partialHeaderLine = result.error();
    return {};
  }

  Expected handleBaseSocketInboundPayload(std::string_view payload,
                                          TimePoint epollTime) {
    if (_passThrough) {
      // Used by upgraded connections to deliver raw payloads
      return _payLoadHandler(_activeHeaders, payload, _responseCode, epollTime);
    }
    if (!_pendingResponse) {
      return {}; // Ignore unexpected data ... some http servers are sending
                 // unsolicited data.
    }
    if (_inResponse) {
      if (!_partialHeaderLine.empty()) {
        auto prependedPayload = _partialHeaderLine + std::string{payload};
        _activePayload = prependedPayload;
        auto payloadStream = std::spanstream{
            std::span{prependedPayload.begin(), prependedPayload.end()}};
        _partialHeaderLine.clear();
        return loadHeaders(payloadStream, epollTime);
      } else if (_chunkedResponse) {
        if (!_partialChunkLength.empty()) {
          // append the left over payload with the incoming
          auto prependedPayload = _partialChunkLength + std::string{payload};
          _activePayload = prependedPayload;
          _partialChunkLength.clear();
          auto payloadStream = std::spanstream{
              std::span{prependedPayload.begin(), prependedPayload.end()}};
          return loadChunks(payloadStream, epollTime);
        } else {
          _activePayload = payload;
          auto payloadStream = std::spanstream{
              std::span{const_cast<char *>(payload.data()),
                        const_cast<char *>(payload.data()) + payload.size()}};
          return loadChunks(payloadStream, epollTime);
        }
      }
    }
    _activePayload = payload;
    auto payloadStream = std::spanstream{
        std::span{const_cast<char *>(payload.data()),
                  const_cast<char *>(payload.data()) + payload.size()}};
    if (!_inResponse) {
      _sendQueue.pop_front();
      _activeHeaders.clear();
      _contentSize.reset();
      _compression.reset();
      std::string httpVersion;
      std::string responsePhrase;
      payloadStream >> httpVersion >> _responseCode;
      std::getline(payloadStream, responsePhrase);
      _inResponse = true;
      // Start reading headers
      return loadHeaders(payloadStream, epollTime);
    }

    if (!_contentSize) {
      return std::unexpected(
          "Size information not available for current payload");
    }

    if (_chunkedResponse) {
      // Handle chunked transfer encoding
      return loadChunks(payloadStream, epollTime);
    }

    return loadBody(payloadStream, epollTime);
  }

  ParseExpected getline_expected(std::spanstream &payloadStream) {
    std::string line;
    char ch;
    bool hasEOL = false;

    while (payloadStream.get(ch)) {
      if (ch == '\n') {
        hasEOL = true;
        break;
      }
      line += ch;
    }

    if (hasEOL) {
      return line; // Expected case: Line before EOL
    } else if (!line.empty()) {
      return std::unexpected(
          line); // Unexpected case: Content before EOF without EOL
    }

    return std::unexpected(
        ""); // If EOF with nothing read, return empty unexpected value
  }

protected:
  void setPassThrough(bool passThroughState) {
    _passThrough = passThroughState;
  }

  Expected onDisconnected(const std::string &reason) override {
    resetHttpSocketState();
    return BaseSocketEndpointT::onDisconnected(reason);
  }

  void resetHttpSocketState() {
    _contentSize.reset();
    _compression.reset();
    _httpBody.clear();
    _partialChunkLength.clear();
    _activePayload = {};
    _partialHeaderLine.clear();
    _inResponse = false;
    _passThrough = false;
    _sendQueue.clear();
    _pendingResponse = false;
    _chunkedResponse = false;
    _responseCode = 0;
    _chunkSize.reset();
  }

  HttpSendQueue _sendQueue{};
  std::string _uriPath;
  IncomingPayloadHandlerT _payLoadHandler;
  HttpPayloadHandlerT _outgoingPayloadHandler;
  http::HttpFields _activeHeaders{};
  std::optional<size_t> _contentSize{};
  std::optional<size_t> _chunkSize{};
  bool _chunkedResponse{false};
  std::string _httpBody{};
  std::string _partialChunkLength{};
  std::string _chunkedBody{};
  std::string _partialHeaderLine{};
  std::string_view _activePayload{};
  std::optional<http::SupportedCompression> _compression;
  std::vector<uint8_t> _compressedData;

  HttpResponsePayloadOptions _responsePayloadOptions{};

  bool _inResponse{false};
  bool _passThrough{false};
  bool _pendingResponse{false};
  int _responseCode{0};
};
} // namespace medici::sockets::live