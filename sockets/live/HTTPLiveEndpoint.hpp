#pragma once

#include "medici/http/CompressionUtils.hpp"
#include "medici/http/HttpFields.hpp"
#include "medici/http/MultipartPayload.hpp"
#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/concepts.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <spanstream>

namespace medici::sockets::live {

enum class HttpEndpointState {
  AwaitingUserRequest,   // Client only
  AwaitingClientRequest, // Server only
  AwaitingResponse,      // Client only
  ReadingHeaders,
  ReadingChunks,
  ReadingContent,
  WebsocketPassthrough
};

struct HttpResponseHeader {
  int responseCode{200};
  std::string message{"OK"};
};
using HttpContentT = std::variant<std::string, std::filesystem::path,
                                  http::HeaderFields, http::MultipartPayload>;
struct HttpSendEntry {
  std::optional<http::HTTPAction> action;
  http::HeaderFields headersValues;
  std::optional<HttpResponseHeader> responseHeader; // For server-side responses
  HttpContentT content;
  http::SupportedCompression compression{http::SupportedCompression::None};
  std::string uriPath{};
  HttpResponsePayloadOptions responsePayloadOptions{};
};

template <HttpPayloadHandlerC IncomingPayloadHandlerT,
          template <class> class BaseSocketEndpoint, typename EndpointInterface>
class HTTPLiveEndpoint : public EndpointInterface,
                         protected BaseSocketEndpoint<SocketPayloadHandlerT> {

  using BaseSocketEndpointT = BaseSocketEndpoint<SocketPayloadHandlerT>;
  using ParseExpected = std::expected<std::string, std::string>;

public:
  HTTPLiveEndpoint(const HttpEndpointConfig &config,
                   IIPEndpointPollManager &endpointPollManager,
                   IncomingPayloadHandlerT &&incomingPayloadHandler,
                   SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                   CloseHandlerT closeHandler,
                   DisconnectedHandlerT DisconnectedHandlerT,
                   OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{config,
                            endpointPollManager,
                            [this](auto payload, auto tp) {
                              return handleBaseSocketInboundPayload(payload,
                                                                    tp);
                            },
                            std::forward<decltype(outgoingPayloadHandler)>(
                                outgoingPayloadHandler),
                            closeHandler,
                            DisconnectedHandlerT,
                            onActiveHandler},
        _uriPath{config.uriPath()},
        _payLoadHandler{std::move(incomingPayloadHandler)} {
    _serverSide = false;
    _state = HttpEndpointState::AwaitingUserRequest; // Client only
  }

  HTTPLiveEndpoint(int fd, const HttpEndpointConfig &config,
                   IIPEndpointPollManager &endpointPollManager,
                   IncomingPayloadHandlerT &&incomingPayloadHandler,
                   SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                   CloseHandlerT closeHandler,
                   DisconnectedHandlerT DisconnectedHandlerT,
                   OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{fd,
                            static_cast<const IPEndpointConfig &>(config),
                            endpointPollManager,
                            [this](auto payload, auto tp) {
                              return handleBaseSocketInboundPayload(payload,
                                                                    tp);
                            },
                            std::forward<decltype(outgoingPayloadHandler)>(
                                outgoingPayloadHandler),
                            closeHandler,
                            DisconnectedHandlerT,
                            onActiveHandler},
        _uriPath{config.uriPath()},
        _payLoadHandler{std::move(incomingPayloadHandler)} {
    _serverSide = true;
    _state = HttpEndpointState::AwaitingClientRequest;
  }

  ~HTTPLiveEndpoint() = default;

  const std::string &name() const override {
    return BaseSocketEndpointT::name();
  }
  Expected openEndpoint() override {
    return BaseSocketEndpointT::openEndpoint();
  }

  Expected closeEndpoint(const std::string &reason) override {
    BaseSocketEndpointT::closeEndpoint(reason);
    resetIncomingHttpState();
    return {};
  }

  bool isActive() const override { return BaseSocketEndpointT::isActive(); }
  const medici::ClockNowT &getClock() const override {
    return BaseSocketEndpointT::getClock();
  }

  std::uint64_t getEndpointUniqueId() const override {
    return BaseSocketEndpointT::getEndpointUniqueId();
  }

  IEndpointEventDispatch &getDispatchInterface() override { return *this; }
  // Overrides value set in config
  void setURIPath(const std::string &path) { _uriPath = path; }

protected:
  Expected clarifySendHeaders(http::HeaderFields &headersValues,
                              std::string_view content,
                              http::ContentType contentType,
                              http::SupportedCompression compression) {
    if (contentType != http::ContentType::Unspecified) {
      if (headersValues.HasField("Content-Type")) {
        return std::unexpected(
            "Passing headers with 'Content-Type' header set in tandem "
            "'contentType' parameter on "
            "send is ambiguous. Set 'Content-Type' header manually when passed "
            "content has proprietary type not covered by the enumerated "
            "values ");
      }
      // Set the Content-Type header using the passed parameter
      headersValues.addFieldValue("Content-Type",
                                  std::format("{}", contentType));
    } else if ((!content.empty()) && !headersValues.HasField("Content-Type")) {
      return std::unexpected(
          "Cannot send non empty content without setting "
          "'Content-Type' header or 'contentType' parameter.");
    }

    if (compression != http::SupportedCompression::None) {
      if (headersValues.HasField("Content-Encoding")) {
        return std::unexpected(
            "Passing headers with 'Content-Encoding' header set in tandem "
            "'compression' parameter on"
            "send is ambiguous. Set 'Content-Encoding' header manually when "
            "passed "
            "content is already compressed");
      }

      std::string contentEncoding;
      switch (compression) {
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
            std::format("Unsupported compression type '{}'", compression));
      }
      headersValues.addFieldValue("Content-Encoding", contentEncoding);
    }
    return {};
  }

  Expected sendHttpData(HttpSendEntry activeSendEntry) {
    auto &headersValues = activeSendEntry.headersValues;
    auto &responseHeader = activeSendEntry.responseHeader;
    auto &content = activeSendEntry.content;
    auto &compressionEncoding = activeSendEntry.compression;
    auto action = activeSendEntry.action;
    _uriPathWithQueryParams = activeSendEntry.uriPath;
    _compressedData.clear();

    auto compressStringContent =
        [&](const std::string &targetContent) -> Expected {
      auto compressionResult = http::compressPayload(
          targetContent, compressionEncoding, _compressedData);
      if (!compressionResult) {
        return std::unexpected(
            std::format("Failed to compress outgoing payload: error={}",
                        compressionResult.error()));
      }
      headersValues.addFieldValue("Content-Length",
                                  std::to_string(_compressedData.size()));
      headersValues.addFieldValue("Content-Encoding",
                                  to_encoding_value(compressionEncoding));
      return {};
    };

    Expected updateContentLengthResult = std::visit(
        [&, this](auto &targetContent) -> Expected {
          using T = std::decay_t<decltype(targetContent)>;
          if constexpr (std::is_same_v<T, std::string>) {
            if (std::size(targetContent) > 0) {
              if (compressionEncoding != http::SupportedCompression::None) {
                return compressStringContent(targetContent);
              } else {
                headersValues.addFieldValue(
                    "Content-Length", std::to_string(targetContent.size()));
              }
            }
          } else if constexpr (std::is_same_v<T, http::HttpFields>) {
            auto encodedFormResult = targetContent.encodeAsQueryString();
            if (!encodedFormResult) {
              return std::unexpected(std::format(
                  "Failed to encode form content as URL encoded string: "
                  "error={}",
                  encodedFormResult.error()));
            }
            auto encodedForm = encodedFormResult.value();
            if (!encodedForm.empty()) {

              content = encodedForm; // reassign as std::string
              if (compressionEncoding != http::SupportedCompression::None) {
                return compressStringContent(encodedForm);
              }
              headersValues.addFieldValue("Content-Length",
                                          std::to_string(encodedForm.size()));
            }
          } else if constexpr (std::is_same_v<T, http::MultipartPayload>) {
            auto encodedMPartFormResult = targetContent.encodeToString();
            if (!encodedMPartFormResult) {
              return std::unexpected(
                  std::format("Failed to encode multipart form content: "
                              "error={}",
                              encodedMPartFormResult.error()));
            }
            auto encodedForm = encodedMPartFormResult.value();
            if (encodedForm.empty()) {
              return {};
            }
            if (compressionEncoding != http::SupportedCompression::None) {
              return compressStringContent(encodedForm);
            }
            content = encodedForm;
            headersValues.addFieldValue("Content-Length",
                                        std::to_string(encodedForm.size()));
            return {};
          } else if constexpr (std::is_same_v<T, std::filesystem::path>) {

            if (compressionEncoding != http::SupportedCompression::None) {
              auto compressionResult = http::compressFile(
                  targetContent, compressionEncoding, _compressedData);
              if (!compressionResult) {
                return std::unexpected(std::format(
                    "Failed to compress outgoing file payload: error={}",
                    compressionResult.error()));
              }
              headersValues.addFieldValue(
                  "Content-Length", std::to_string(_compressedData.size()));
              headersValues.addFieldValue(
                  "Content-Encoding", to_encoding_value(compressionEncoding));
            } else {
              auto fileSize = std::filesystem::file_size(targetContent);
              if (fileSize > 0) {
                headersValues.addFieldValue("Content-Length",
                                            std::to_string(fileSize));
              }
            }
          }
          return {};
        },
        content);

    if (!updateContentLengthResult) {
      return updateContentLengthResult;
    }

    std::string payload;
    if (responseHeader) {
      // Send a Response
      auto result = headersValues.encodeFieldsToResponseHeader(
          responseHeader->responseCode, responseHeader->message);
      if (!result) {
        return std::unexpected(std::format(
            "Failed to encode HTTP response header: error={}", result.error()));
      }
      payload = result.value();
    } else {
      // Send a Request
      auto result = headersValues.encodeFieldsToRequestHeader(
          *action, _uriPathWithQueryParams, this->getConfig().host());
      if (!result) {
        return std::unexpected(std::format(
            "Failed to encode HTTP request header: error={}", result.error()));
      }
      payload = result.value();
    }

    // If we have compressed data set, then just send that
    // as any relevant content would have been compressed
    // into this buffer at this point
    if (_compressedData.empty() == false) {
      std::copy(_compressedData.begin(), _compressedData.end(),
                std::back_inserter(payload));
      return BaseSocketEndpointT::sendAsync(
          payload, [this]() { return onPayloadSent(); });
    }
    if (!_serverSide) {
      // FIFO: response options are applied in the order requests were sent
      _pendingResponseOptions.push_back(activeSendEntry.responsePayloadOptions);
    }
    return std::visit(
        [&, this](auto &targetContent) -> Expected {
          using T = std::decay_t<decltype(targetContent)>;
          if constexpr (std::is_same_v<T, std::filesystem::path>) {
            return BaseSocketEndpointT::sendAsync(
                payload, [this, targetContent]() {
                  return sendFileContent(targetContent,
                                         [this]() { return onPayloadSent(); });
                });
          } else if constexpr (std::is_same_v<T, std::string>) {
            payload += targetContent;
            return BaseSocketEndpointT::sendAsync(
                payload, [this]() { return onPayloadSent(); });
          }
          return std::unexpected("Unsupported content type in send queue");
        },
        content);
  }

  Expected sendFileContent(const std::filesystem::path &filePath,
                           CallableT &&onSendComplete) {
    _activeFileStream =
        std::make_unique<std::ifstream>(filePath, std::ios::binary);
    if (!_activeFileStream->is_open()) {
      return std::unexpected("Failed to open file");
    }

    return sendFileChunk(std::move(onSendComplete));
  }

  Expected sendFileChunk(CallableT &&onSendComplete) {
    char buffer[8192];

    if (_activeFileStream->read(buffer, sizeof(buffer)) ||
        _activeFileStream->gcount() > 0) {
      auto bytesRead = _activeFileStream->gcount();

      return BaseSocketEndpointT::sendAsync(
          std::string_view(buffer, bytesRead),
          [this, onSendComplete = std::move(onSendComplete)]() mutable
              -> Expected { return sendFileChunk(std::move(onSendComplete)); });
    }

    // File complete
    return onSendComplete();
  }

  Expected sendMultipartFiles(http::MultipartPayload mpPayload,
                              CallableT onComplete) {
    if (mpPayload.hasFileContent() == false) {
      return BaseSocketEndpointT::sendAsync(
          mpPayload.getTailBoundary(),
          [this, onComplete]() { return onComplete(); });
    }
    return BaseSocketEndpointT::sendAsync(
        mpPayload.getActiveFileBoundaryHeader(),
        [this, mpPayload, onComplete]() {
          auto expectedPath = mpPayload.getActiveFile();
          return sendFileContent(
              expectedPath.value(),
              [this, mpPayload, onComplete]() mutable -> Expected {
                mpPayload.removeActiveFile();
                return sendMultipartFiles(std::move(mpPayload), onComplete);
              });
        });
  }

  Expected dispatchPayload(auto epollTime) {
    http::SupportedCompression compression = http::SupportedCompression::None;

    if (_activeHeaders.HasField("Content-Encoding")) {
      auto encodingStr = _activeHeaders.getField("Content-Encoding").value();
      if (encodingStr == to_encoding_value(http::SupportedCompression::GZip)) {
        compression = http::SupportedCompression::GZip;
      } else if (encodingStr ==
                 to_encoding_value(http::SupportedCompression::HttpDeflate)) {
        compression = http::SupportedCompression::HttpDeflate;
      } else if (encodingStr ==
                 to_encoding_value(http::SupportedCompression::Brotli)) {
        compression = http::SupportedCompression::Brotli;
      } else {
        return std::unexpected(
            std::format("Unsupported content encoding '{}'", encodingStr));
      }
    }
    if (_serverSide) {
      _state = HttpEndpointState::AwaitingClientRequest;
    } else {
      if (!_pendingResponseOptions.empty()) {
        _responsePayloadOptions = _pendingResponseOptions.front();
        _pendingResponseOptions.pop_front();
      }
      _state = _pendingResponseOptions.empty()
                   ? HttpEndpointState::AwaitingUserRequest
                   : HttpEndpointState::AwaitingResponse;
    }
    if (compression != http::SupportedCompression::None) {
      if (_serverSide || (_responsePayloadOptions.decompressBeforeDispatch)) {
        auto decompressionResult = decompressPayload(
                  _httpBody, compression);
        if (!decompressionResult) {
          return std::unexpected(
              std::format("Failed to decompress incoming payload: error={}",
                          decompressionResult.error()));
        }
        return _payLoadHandler(_activeHeaders, _decompressedBody, epollTime);
      }
    }
    else {
      if (_compressionContext) {
        closeCompressionContext();
      }
    }

    return _payLoadHandler(_activeHeaders, _httpBody, epollTime);
  }

  Expected onPayloadSent() {
    if (_state == HttpEndpointState::WebsocketPassthrough) {
      return {};
    }
    if (_serverSide) {
      if (_state != HttpEndpointState::ReadingHeaders && _state != HttpEndpointState::ReadingContent &&
          _state != HttpEndpointState::ReadingChunks) {
        _state = HttpEndpointState::AwaitingClientRequest;
      }
    } else {
      _state = HttpEndpointState::AwaitingResponse;
    }
    return {};
  }

  Expected readContent(std::ispanstream &payloadStream, TimePoint epollTime) {
    auto streamSpan = payloadStream.span();
    auto remainingStreamSize =
        payloadStream.span().size() - payloadStream.tellg();
    auto streamCopyStart = streamSpan.begin() + payloadStream.tellg();

    if ((_httpBody.size() + remainingStreamSize) <= (*_contentSize)) {
      std::copy(streamCopyStart, streamSpan.end(),
                std::back_inserter(_httpBody));
      payloadStream.seekg(payloadStream.span().size()); // Move eof
    } else {
      auto remainingToRead = (*_contentSize - _httpBody.size());
      std::copy(streamCopyStart, streamCopyStart + remainingToRead,
                std::back_inserter(_httpBody));
      payloadStream.seekg(remainingToRead, std::ios_base::cur);
    }

    if (_httpBody.size() >= (*_contentSize)) {
      return dispatchPayload(epollTime);
    }
    return {};
  }

  Expected readChunks(std::ispanstream &payloadStream, TimePoint epollTime) {
    while (!payloadStream.eof()) {
      if (!_chunkSize) {
        auto expectedLine = getline_expected(payloadStream);
        if (!expectedLine) {
          return this->prependPartialContent(expectedLine.error().c_str(),
                                             expectedLine.error().size());
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
        // Last chunk marker: must still consume any trailer header lines and
        // the final blank line terminating the chunked message before
        // dispatching, otherwise that trailing CRLF leaks into the next
        // payload processed under the post-dispatch state. _chunkSize stays
        // at 0 so a terminator split across payloads resumes here.
        ParseExpected trailerLine;
        bool foundTerminator = false;
        while ((trailerLine = getline_expected(payloadStream))) {
          if (trim(trailerLine.value()).empty()) {
            foundTerminator = true;
            break;
          }
          // Trailer header fields are currently ignored.
        }
        if (!foundTerminator) {
          return this->prependPartialContent(trailerLine.error().c_str(),
                                             trailerLine.error().size());
        }
        _chunkedBody.clear();
        _chunkSize.reset();
        _chunkedResponse = false;
        return dispatchPayload(epollTime);
      }

      // Positions are absolute within the stream span, not within
      // _activePayload, which has already had consumed bytes trimmed off.
      auto streamSpan = payloadStream.span();
      std::uint64_t positionInBuff = payloadStream.tellg();
      auto remainingPayloadSize = streamSpan.size() - positionInBuff;
      if (remainingPayloadSize < *_chunkSize) {
        std::copy(streamSpan.begin() + positionInBuff, streamSpan.end(),
                  std::back_inserter(_chunkedBody));
        *_chunkSize -= remainingPayloadSize;
        payloadStream.seekg(streamSpan.size());
        return {};

      } else {
        std::copy(streamSpan.begin() + positionInBuff,
                  streamSpan.begin() + positionInBuff + *_chunkSize,
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

  Expected readHeaders(std::ispanstream &payloadStream, TimePoint epollTime) {
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
        // or this is the end of the content. Check the headers values
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
          _state = HttpEndpointState::ReadingContent;
          return readContent(payloadStream, epollTime);
        } else if (_activeHeaders.HasField("Transfer-Encoding") &&
                   _activeHeaders.getField("Transfer-Encoding").value() ==
                       "chunked") {
          // Handle chunked transfer encoding
          _chunkedResponse = true;
          _state = HttpEndpointState::ReadingChunks;
          return readChunks(payloadStream, epollTime);
        }
        // There's no content body so close the response
        // dispatch the headers
        return dispatchPayload(epollTime);
      }
    }
    _state = HttpEndpointState::ReadingHeaders;
    // We read a partial header line because the buffer
    // read didn't contain all the expected content
    return this->prependPartialContent(result.error().c_str(),
                                       result.error().size());
  }

  Expected handleBaseSocketInboundPayload(std::string_view payload,
                                          TimePoint epollTime) {

    auto totalSize = payload.size();
    if (_state == HttpEndpointState::WebsocketPassthrough) {
      return _payLoadHandler(_activeHeaders, payload, epollTime);
    }
    auto payloadStream = std::ispanstream{
        std::span{const_cast<char *>(payload.data()),
                  const_cast<char *>(payload.data()) + payload.size()}};

    while (payloadStream.tellg() < totalSize) {
      _activePayload = payload;
      _activePayload.remove_prefix(payloadStream.tellg());
      if (auto result =
              handleBaseSocketInboundPayload(payloadStream, epollTime);
          !result) {
        return result;
      }
    }
    return {}; // All data processed successfully
  }

  Expected handleBaseSocketInboundPayload(std::ispanstream &payloadStream,
                                          TimePoint epollTime) {

    auto hasFullHeaderLine = [&]() {
      return _activePayload.find("\r\n") != std::string::npos;
    };

    switch (_state) {
    case HttpEndpointState::WebsocketPassthrough:
      return std::unexpected(
          "WebsocketPassthrough state should not reach this point");
    case HttpEndpointState::AwaitingUserRequest:
      return {}; // Ignore unexpected data ... some http servers may send
                 // unsolicited data.
    case HttpEndpointState::AwaitingClientRequest: {
      if (!hasFullHeaderLine()) {
        this->prependPartialContent(_activePayload.data(),
                                    _activePayload.size());
        return {};
      }
      resetIncomingHttpState();
      std::string httpMethod;
      std::string httpVersion;
      payloadStream >> httpMethod >> _incomingURIPath;
      std::getline(payloadStream, httpVersion);
      if (httpMethod.empty() || httpVersion.empty()) {
        return this->closeEndpoint(
            "Invalid HTTP request line: Missing action method and/or version");
      }
      auto actionResult = http::to_HTTPAction(httpMethod);
      if (!actionResult) {
        return this->closeEndpoint(std::format(
            "Invalid HTTP request line: Unsupported action method '{}'",
            httpMethod));
      }
      _incomingAction = actionResult.value();
      return readHeaders(payloadStream, epollTime);
    }

    case HttpEndpointState::AwaitingResponse: {
      if (!hasFullHeaderLine()) {
        return this->prependPartialContent(_activePayload.data(),
                                           _activePayload.size());
      }
      resetIncomingHttpState();
      std::string httpVersion;
      std::string responsePhrase;
      payloadStream >> httpVersion >> _responseCode;
      std::getline(payloadStream, responsePhrase);
      // Start reading headers
      return readHeaders(payloadStream, epollTime);
    }
    case HttpEndpointState::ReadingHeaders:
      return readHeaders(payloadStream, epollTime);
    case HttpEndpointState::ReadingChunks:
      return readChunks(payloadStream, epollTime);
    case HttpEndpointState::ReadingContent:
      return readContent(payloadStream, epollTime);
    };
    return {};
  }

  ParseExpected getline_expected(std::ispanstream &payloadStream) {
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

 auto compressionActive() const {
    return _compressionContext.has_value() && (_activeCompressionFormat == http::SupportedCompression::Brotli);
  }

 auto &getCompressionContext() { return *_compressionContext; }

 auto initializeDecompressionCompressionContext(http::SupportedCompression compressionFormat) ->Expected {
    if (_compressionContext.has_value() && _activeCompressionFormat == compressionFormat) {
      return {};
    }
    _activeCompressionFormat = compressionFormat;
    if (compressionFormat == http::SupportedCompression::Brotli){
      _compressionContext.reset();
      return {};
    }
    _compressionContext = z_stream{};
    if (auto result = http::openZStreamDecompression(
            *_compressionContext, compressionFormat);
        !result) {
      return std::unexpected(
          std::format("Failed to initialize z_stream context for "
                      "endpoint, error={}",
                      result.error()));
    }
    return {};
  }

  Expected initializeCompressionContext(http::SupportedCompression compressionFormat) {
    _compressionContext = z_stream{};
    if (auto result = http::openZStreamCompression(
            *_compressionContext, compressionFormat);
        !result) {
      return std::unexpected(
          std::format("Failed to initialize z_stream context for websocket "
                      "endpoint, error={}",
                      result.error()));
    }
    return {};
  }

  Expected closeCompressionContext() {
    if (_compressionContext) {
      if (auto result = http::closeZStream(*_compressionContext); !result) {
        return std::unexpected(
            std::format("Failed to close z_stream context for websocket "
                        "endpoint, error={}",
                        result.error()));
      }
      _compressionContext.reset();
    }
    return {};
  }



  void setPassThrough(bool passThroughState) {
    if (passThroughState)
      _state = HttpEndpointState::WebsocketPassthrough;
    else if (_serverSide) {
      _state = HttpEndpointState::AwaitingClientRequest;
    } else {
      _state = HttpEndpointState::AwaitingUserRequest;
    }
  }

  Expected onDisconnected(const std::string &reason) override {
    resetIncomingHttpState();
    return BaseSocketEndpointT::onDisconnected(reason);
  }

  void resetIncomingHttpState() {
    _contentSize.reset();
    _compression.reset();
    _httpBody.clear();
    _decompressedBody.clear();
    _chunkedResponse = false;
    _responseCode = 0;
    _chunkSize.reset();
    _activeHeaders.clear();
    if (_serverSide) {
      _state = HttpEndpointState::AwaitingClientRequest;
    } else {
      _state = _pendingResponseOptions.empty()
                   ? HttpEndpointState::AwaitingUserRequest
                   : HttpEndpointState::AwaitingResponse;
    }
  }

  void resetHttpState() {
    resetIncomingHttpState();
    _pendingResponseOptions.clear();
  }

  auto &getCompressedDataBuffer() { return _compressedData; }
  auto &getDecompressedBodyBuffer() { return _decompressedBody; }

  // Server side only
  auto getIncomingAction() const { return _incomingAction; }

  // Client side only
  auto getResponseCode() const { return _responseCode; }

  auto &getRequestURIPath() const { return _incomingURIPath; }

  auto hasCompressionBlockTail(std::string_view messagePayload) {
    if (messagePayload.size() < 4) {
      return false;
    }
    const std::array<char, 4> deflateBlockTail{
        0x00, 0x00, static_cast<char>(0xff), static_cast<char>(0xff)};
    return std::equal(deflateBlockTail.begin(), deflateBlockTail.end(),
                      messagePayload.end() - 4);
  }

  auto amendCompressionBlockTailIfMissing(std::string_view messagePayload) {
    if (hasCompressionBlockTail(messagePayload)) {
      return messagePayload;
    }

    _preDecompressionBuffer.clear();
    std::copy(messagePayload.begin(), messagePayload.end(),
              std::back_inserter(_preDecompressionBuffer));
    // Append the 0x00, 0x00, 0xff, 0xff tail to indicate end of
    // compressed block as per RFC 7692 for permessage-deflate compressed
    // messages with no context takeover
    const std::array<char, 4> deflateBlockTail{
        0x00, 0x00, static_cast<char>(0xff), static_cast<char>(0xff)};
    std::copy(deflateBlockTail.begin(), deflateBlockTail.end(),
              std::back_inserter(_preDecompressionBuffer));

    return std::string_view{_preDecompressionBuffer.data(),
                            _preDecompressionBuffer.size()};
  }

  auto decompressPayload(std::string_view payload,
                                 http::SupportedCompression compression) {
    _decompressedBody.clear();
    // Only RFC 7692 permessage-deflate frames are shipped without the
    // terminating empty block; gzip/deflate HTTP bodies are complete streams.
    if (compression != http::SupportedCompression::WSDeflate) {
      return http::decompressPayloadToBuffer(payload, compression,
                                             _decompressedBody);
    }
    return http::decompressPayloadToBuffer(amendCompressionBlockTailIfMissing(payload), compression,
                                           _decompressedBody,
                                           *_compressionContext);
  }



  std::unique_ptr<std::ifstream> _activeFileStream{};
  std::string _uriPath;
  std::string _uriPathWithQueryParams;
  std::string _incomingURIPath;
  IncomingPayloadHandlerT _payLoadHandler;
  http::HeaderFields _activeHeaders{};
  std::optional<size_t> _contentSize{};
  std::optional<size_t> _chunkSize{};
  bool _chunkedResponse{false};
  std::string _httpBody{};
  std::string _decompressedBody{};
  std::string _chunkedBody{};
  std::string_view _activePayload{};
  std::optional<http::SupportedCompression> _compression;
  std::vector<uint8_t> _compressedData;

  HttpResponsePayloadOptions _responsePayloadOptions{};
  int _responseCode{0};
  HttpEndpointState _state{};
  http::HTTPAction _incomingAction{};
  bool _serverSide{false};
  std::deque<HttpResponsePayloadOptions> _pendingResponseOptions{};
  std::optional<z_stream> _compressionContext{};
  http::SupportedCompression _activeCompressionFormat{http::SupportedCompression::None};
  std::vector<char> _preDecompressionBuffer{};

};
} // namespace medici::sockets::live