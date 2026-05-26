#pragma once

#include "medici/cryptoUtils/CryptoUtils.hpp"
#include "medici/http/CompressionUtils.hpp"
#include "medici/http/HttpFields.hpp"
#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/live/HTTPLiveClientEndpoint.hpp"
#include "medici/sockets/live/HTTPLiveServerEndpoint.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <array>
#include <random>
#include <sstream>

namespace medici::sockets::live {

struct DecryptedPayloadFrameBuffer {
  std::string_view _data;
  auto data() { return const_cast<char *>(_data.data()); }

  auto size() { return _data.size(); }
};

template <template <template <class> class> class BaseSocketEndpoint,
          template <class> class CoreSocketEndpoint>
class WebSocketLiveEndpoint : public IWebSocketEndpoint,
                              protected BaseSocketEndpoint<CoreSocketEndpoint> {
  using BaseSocketEndpointT = BaseSocketEndpoint<CoreSocketEndpoint>;
  using ServerSideEndpointT = HTTPLiveServerEndpoint<CoreSocketEndpoint>;
  using ClientSideEndpointT = HTTPLiveClientEndpoint<CoreSocketEndpoint>;

public:
  ~WebSocketLiveEndpoint() {
    if (BaseSocketEndpointT::isActive()) {
      BaseSocketEndpointT::closeRemoteConnection();
    }
  };

  WebSocketLiveEndpoint(const medici::sockets::WSEndpointConfigC auto &config,
                        IIPEndpointPollManager &endpointPollManager,
                        WebSocketPayloadHandlerC auto &&incomingPayloadHandler,
                        WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
                        CloseHandlerT closeHandler,
                        DisconnectedHandlerT disconnectedHandler,
                        OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{config,
                            endpointPollManager,
                            [this](const http::HeaderFields &headers,
                                   std::string_view payload, int rc,
                                   TimePoint tp) {
                              return handleBaseSocketInboundPayload(
                                  headers, payload, tp);
                            },
                            SocketPayloadHandlerT{},
                            closeHandler,
                            disconnectedHandler,
                            onActiveHandler},
        _incomingPayloadHandler{std::move(incomingPayloadHandler)},
        _outgoingHandler{std::move(outgoingPayloadHandler)},
        _deflateRequested{config.perMessageDeflate()} {
    _sendBuffer.reserve(2048);
  }

  WebSocketLiveEndpoint(int clientFd, const HttpEndpointConfig &config,
                        IIPEndpointPollManager &endpointPollManager,
                        WebSocketPayloadHandlerC auto &&incomingPayloadHandler,
                        WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
                        CloseHandlerT closeHandler,
                        DisconnectedHandlerT disconnectedHandler,
                        OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{
            clientFd,
            config,
            endpointPollManager,
            [this](http::HTTPAction action, const std::string &uri,
                   const http::HeaderFields &headers,
                   const HttpServerPayloadT &payload, TimePoint tp) {
              return handleBaseSocketInboundPayload(
                  headers, std::get<std::string_view>(payload), tp);
            },
            [this](auto payload, auto tp) { return Expected(); },
            closeHandler,
            disconnectedHandler,
            onActiveHandler},
        _incomingPayloadHandler{std::move(incomingPayloadHandler)},
        _outgoingHandler{std::move(outgoingPayloadHandler)} {
    _sendBuffer.reserve(2048);
  }

  const std::string &name() const override {
    return BaseSocketEndpointT::name();
  }
  Expected openEndpoint() override {
    return BaseSocketEndpointT::openEndpoint();
  }

  IEndpointEventDispatch &getDispatchInterface() override { return *this; }

  const medici::ClockNowT &getClock() const override {
    return BaseSocketEndpointT::getClock();
  }

  std::uint64_t getEndpointUniqueId() const override {
    return BaseSocketEndpointT::getEndpointUniqueId();
  }

  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    return BaseSocketEndpointT::registerTimer(timer);
  }

  Expected onDisconnected(const std::string &reason) override {
    resetWebsocketState();
    return BaseSocketEndpointT::onDisconnected(reason);
  }

  bool isActive() const override {
    return _upgraded && BaseSocketEndpointT::isActive();
  }

  Expected sendText(std::string_view payload) override {
    return sendFramedPayload(WSOpCode::Text, payload);
  }

  Expected sendBinary(std::string_view payload) override {
    return sendFramedPayload(WSOpCode::Binary, payload);
  }

  Expected sendPayload(WSOpCode opCode, std::string_view payload) override {
    return sendFramedPayload(opCode, payload);
  }

  auto generateMaskingKey() {
    std::array<char, 4> key;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);

    for (int i = 0; i < 4; ++i) {
      key[i] = static_cast<char>(distrib(gen));
    }
    return key;
  }

  Expected sendFramedPayload(WSOpCode opCode, std::string_view payload) {
    if (!_upgraded) {
      return std::unexpected("WebSocket endpoint not upgraded");
    }

    if constexpr (std::is_same_v<ServerSideEndpointT, BaseSocketEndpointT>) {
      return serverSendFramedPayload(opCode, payload);
    }
    if constexpr (std::is_same_v<ClientSideEndpointT, BaseSocketEndpointT>) {
      return clientSendFramedPayload(opCode, payload);
    }
    return std::unexpected("Unknown endpoint type");
  }

  Expected clientSendFramedPayload(WSOpCode opCode, std::string_view payload) {
    _sendBuffer.clear();
    auto uncompressed = payload;
    const bool compressed = _compressionContext && (opCode == WSOpCode::Text ||
                                                    opCode == WSOpCode::Binary);
    if (compressed) {
      if (auto deflatedResult = http::compressPayload(
              payload, http::SupportedCompression::WSDeflate,
              this->getCompressedDataBuffer());
          !deflatedResult) {
        return std::unexpected(std::format(
            "Failed to deflate websocket payload, endpoint name={}, error={}",
            this->name(), deflatedResult.error()));
      }
      payload = std::string_view(reinterpret_cast<const char *>(
                                     this->getCompressedDataBuffer().data()),
                                 this->getCompressedDataBuffer().size());
    }

    auto payloadSize = static_cast<uint16_t>(std::size(payload));

    _sendBuffer.push_back(static_cast<char>(0x80 | (compressed ? 0x40 : 0x00) |
                                            static_cast<int>(opCode)));

    if (payloadSize <= 125) {
      _sendBuffer.push_back(
          static_cast<char>(0x80 | static_cast<uint8_t>(payloadSize)));
    } else if (payloadSize <= 65536) {
      _sendBuffer.push_back(
          static_cast<char>(0x80 | 126)); // Mask bit set + 126 indicator
      _sendBuffer.push_back(static_cast<char>((payloadSize >> 8) &
                                              0xFF)); // Extended length (MSB)
      _sendBuffer.push_back(static_cast<char>(payloadSize & 0xFF));
    } else {
      _sendBuffer.push_back(
          static_cast<char>(0x80 | 127)); // Mask bit set + 127 indicator
      for (int i = 7; i >= 0; --i) {
        _sendBuffer.push_back(static_cast<char>(
            (payloadSize >> (i * 8)) & 0xFF)); // Extended length (64-bit)
      }
    }
    auto maskingKey = generateMaskingKey();
    std::copy(maskingKey.begin(), maskingKey.end(),
              std::back_inserter(_sendBuffer));
    auto maskOffset = _sendBuffer.size();
    std::copy(std::begin(payload), std::end(payload),
              std::back_inserter(_sendBuffer));
    // mask the payload
    for (size_t i = maskOffset; i < _sendBuffer.size(); ++i) {
      auto payloadIndex = i - maskOffset;
      _sendBuffer[i] ^= maskingKey[payloadIndex % 4]; // XOR with repeating key
    }

#ifdef FNXDEBUG
    // BIO_dump_fp(stdout, _sendBuffer.data(), _sendBuffer.size());
#endif
    if (auto result = BaseSocketEndpointT::send(
            std::string_view(_sendBuffer.data(), _sendBuffer.size()));
        !result) {
      return result;
    }
    _outgoingHandler(uncompressed, opCode, this->getClock()());
    return {};
  }

  Expected serverSendFramedPayload(WSOpCode opCode, std::string_view payload) {
    _sendBuffer.clear();
    auto uncompressed = payload;
    const bool compressed = _compressionContext && (opCode == WSOpCode::Text ||
                                                    opCode == WSOpCode::Binary);
    if (compressed) {
      if (auto deflatedResult = http::compressZStream(
              *_compressionContext, payload, this->getCompressedDataBuffer());
          !deflatedResult) {
        return std::unexpected(std::format(
            "Failed to deflate websocket payload, endpoint name={}, error={}",
            this->name(), deflatedResult.error()));
      }
      payload = std::string_view(reinterpret_cast<const char *>(
                                     this->getCompressedDataBuffer().data()),
                                 this->getCompressedDataBuffer().size());
    }

    auto payloadSize = static_cast<uint16_t>(std::size(payload));

    _sendBuffer.push_back(static_cast<char>(0x80 | (compressed ? 0x40 : 0x00) |
                                            static_cast<int>(opCode)));

    // ✅ Server-side: NO MASK bit (remove 0x80)
    if (payloadSize <= 125) {
      _sendBuffer.push_back(
          static_cast<char>(static_cast<uint8_t>(payloadSize)));
      //                                     ^^^^ No 0x80 mask bit
    } else if (payloadSize <= 65536) {
      _sendBuffer.push_back(
          static_cast<char>(126)); // No mask bit + 126 indicator
      _sendBuffer.push_back(static_cast<char>((payloadSize >> 8) & 0xFF));
      _sendBuffer.push_back(static_cast<char>(payloadSize & 0xFF));
    } else {
      _sendBuffer.push_back(
          static_cast<char>(127)); // No mask bit + 127 indicator
      for (int i = 7; i >= 0; --i) {
        _sendBuffer.push_back(
            static_cast<char>((payloadSize >> (i * 8)) & 0xFF));
      }
    }

    // ✅ Server-side: NO masking key or payload masking
    // Just append payload directly
    std::copy(std::begin(payload), std::end(payload),
              std::back_inserter(_sendBuffer));

#ifdef FNXDEBUG
    // BIO_dump_fp(stdout, _sendBuffer.data(), _sendBuffer.size());
#endif
    if (auto result = BaseSocketEndpointT::send(
            std::string_view(_sendBuffer.data(), _sendBuffer.size()));
        !result) {
      return result;
    }
    _outgoingHandler(uncompressed, opCode, this->getClock()());
    return {};
  }

  Expected closeEndpoint(const std::string &reason) override {
    auto result = BaseSocketEndpointT::closeEndpoint(reason);
    resetWebsocketState();
    return result;
  }

  Expected disconnectEndpoint(const std::string &reason) override {
    auto result = BaseSocketEndpointT::closeRemoteConnection();
    return onDisconnected(reason);
  }

  bool supportsDeflatedPayloads() const {
    return BaseSocketEndpointT::supportsDeflatedPayloads();
  }

private:
  Expected onActive() override {
    // This onActive dispatch is from the base socket endpoint establishing
    // the connection on the server. We don't want to dispatch the passed
    // handler until after the websocket upgrade is complete

    if constexpr (std::is_same_v<ServerSideEndpointT, BaseSocketEndpointT>) {
      return {};
    }

    if constexpr (std::is_same_v<ClientSideEndpointT, BaseSocketEndpointT>) {
      // On the client side we use this dispatch to so send the upgrade request
      http::HeaderFields upgradeHeaders;
      const auto key = crypto_utils::generateKey();
      upgradeHeaders.addFieldValue("Upgrade", "websocket");
      upgradeHeaders.addFieldValue("Connection", "Upgrade");
      upgradeHeaders.addFieldValue("Sec-WebSocket-Version", "13");
      upgradeHeaders.addFieldValue("Sec-WebSocket-Key", key);

      // Add RFC 7692 deflate extension request
      if (_deflateRequested) {
        upgradeHeaders.addFieldValue(
            "Sec-WebSocket-Extensions",
            "permessage-deflate; client_max_window_bits");
      }

      return BaseSocketEndpointT::sendHttpRequest(
          http::HTTPAction::GET, std::move(upgradeHeaders), "");
    }
    return std::unexpected("Unknown endpoint type for websocket");
  }

  Expected onPayloadReady(TimePoint readTime) override {
    if (!_upgraded) {
      return BaseSocketEndpointT::onPayloadReady(readTime);
    }
    if (!_frameReadTime) {
      // Time stamp for raw socket epoll
      // prior to decrypting the ssl data
      _frameReadTime = readTime;
    }
    return BaseSocketEndpointT::onPayloadReady(readTime);
  }

  Expected handleBaseSocketInboundPayload(const http::HeaderFields &headers,
                                          std::string_view payload,
                                          TimePoint tp) {
    if (_upgraded) [[likely]] {
      return handleWebsocketPayload(payload.size());
    }

    // Handle Server Side HTTP Upgrade Request
    if constexpr (std::is_same_v<ServerSideEndpointT, BaseSocketEndpointT>) {
      if (headers.getField("Upgrade") &&
          headers.getField("Upgrade").value() == "websocket") {
        _upgraded = true;
        // Tell the base socket to stop parsing headers and body
        // and just deliver the raw payload
        BaseSocketEndpointT::setPassThrough(true);
      } else {
        const auto &config = BaseSocketEndpointT::getConfig();
        auto response =
            std::format("{} CONNECTION from '{}:{}{}' NOT UPGRADED\n "
                        "payload={}\n Missing 'Upgrade: websocket' header",
                        config.name(), config.host(), config.port(),
                        this->_uriPath, payload);
        return BaseSocketEndpointT::sendHttpResponse(
            http::HeaderFields{}, 400, response, "",
            http::ContentType::TextPlain);
      }

      // Validate the client key and send the upgrade response
      if (auto secKey = headers.getField("Sec-WebSocket-Key"); secKey) {
        std::string acceptKey =
            crypto_utils::generateWebSocketAcceptKey(secKey.value());
        http::HeaderFields responseHeaders;
        responseHeaders.addFieldValue("Upgrade", "websocket");
        responseHeaders.addFieldValue("Connection", "Upgrade");
        responseHeaders.addFieldValue("Sec-WebSocket-Accept", acceptKey);

        // Check for permessage-deflate extension request
        if (auto extField = headers.getField("Sec-WebSocket-Extensions");
            extField) {
          auto extValue = extField.value();
          if (extValue.find("permessage-deflate") != std::string::npos) {
            // Client requested permessage-deflate extension
            if (auto result = initializeCompressionContext(); !result) {
              return result;
            }
            responseHeaders.addFieldValue(
                "Sec-WebSocket-Extensions",
                "permessage-deflate; server_no_context_takeover; "
                "client_no_context_takeover; client_max_window_bits");
          }
        }

        if (auto result = BaseSocketEndpointT::sendHttpResponse(
                responseHeaders, 101, "Switching Protocols");
            !result) {
          return result;
        }
      } else {
        return std::unexpected(
            "WebSocket upgrade failed: Missing Sec-WebSocket-Key header");
      }
      // Dispatch the onActive handler now that the upgrade is complete
      return BaseSocketEndpointT::onActive();
    }

    // Handle Client Side HTTP Upgrade Response
    if constexpr (std::is_same_v<ClientSideEndpointT, BaseSocketEndpointT>) {
      if (headers.getField("Upgrade") &&
          headers.getField("Upgrade").value() == "websocket") {
        _upgraded = true;
        // Tell the base socket to stop parsing headers and body
        // and just deliver the raw payload
        BaseSocketEndpointT::setPassThrough(true);
      } else {
        const auto &config = BaseSocketEndpointT::getConfig();
        return std::unexpected(
            std::format("{} CONNECTION to '{}:{}{}' NOT UPGRADED\n payload={}",
                        config.name(), config.host(), config.port(),
                        this->_uriPath, payload));
      }
      // Client side endpoint so just check for permessage-deflate
      if (auto extField = headers.getField("Sec-WebSocket-Extensions");
          extField) {
        auto extValue = extField.value();
        if (extValue.find("permessage-deflate") != std::string::npos) {
          // Server accepted permessage-deflate extension
          if (auto result = initializeDecompressionCompressionContext();
              !result) {
            return result;
          }
        }
      }
      // Dispatch the onActive handler now that the upgrade is complete
      return BaseSocketEndpointT::onActive();
    }

    return std::unexpected("Unknown endpoint type for websocket");
  }

  Expected handleWebsocketPayload(std::uint32_t networkBytesRead) {
    // nextMessageOffset is an offset used to establish the start of the next
    // message in the currently read buffer after one more messages have been
    // read/dispatched.This value monotonically increases by the size of current
    // frame once it's established assuming that remaining content in the buffer
    // is large enough.
    std::uint32_t nextMessageOffset{0};
    std::uint32_t currentBufferOffset{0};
    std::uint32_t messagesRead{0};
    while (true) {
      // wsFramePayload is a pointer to the start of the active websocket frame
      // in the buffer that we're about to process for this cycle . We establish
      // the start of this buffer based on the nextMessageOffset which is zero
      // for the first cycle. We update this offset once establish size of the
      // current frame we're processing to prep it for the next cycle.
      auto wsFramePayload =
          BaseSocketEndpointT::getInboundBuffer().data() + nextMessageOffset;
      currentBufferOffset = nextMessageOffset;
      ++messagesRead;
      // wsFramePayloadSize below represents the size of the content starting
      // from the wsFramePayload ptr to the end of the current buffer. We use
      // this value to determine whether the current buffer contains all or part
      // of the current frame after we process the frame headers establish the
      // frame size
      auto wsFramePayloadSize = networkBytesRead - nextMessageOffset;

      // Values to be retrieved from the header
      uint32_t expectedFramePayloadLength = 0;
      uint8_t headerSize = 0;

      std::string_view messagePayload{};
      std::uint32_t currentFramePayloadSize =
          0; // The length of content in this current frame payload complete or
             // otherwise as opposed to expectedFramePayloadLength which is the
             // value expected base based on what was in the header

      if (_pendingWSFrameContent == 0) {
        // If there is no pending content for an active web socket frame so we
        // start a new frame and inspect the headers to get FIN bit, opCode and
        // the length of the frame currently being sent by the remote source

        if (wsFramePayloadSize < 2) [[unlikely]] {
          // websocket payload with size less than header length(2) exit for
          // next read
          return shiftIncompleteHdrToFrontForNextRead(wsFramePayloadSize,
                                                      nextMessageOffset);
        }

        expectedFramePayloadLength = wsFramePayload[1] & WS_LENGTH;

        size_t maskingKeyLength = 0;
        if (expectedFramePayloadLength < 126) {
          headerSize = 2;
        } else {
          if (expectedFramePayloadLength == 126) {
            headerSize = 4; // 16 bit length
            expectedFramePayloadLength = __builtin_bswap16(
                *reinterpret_cast<const uint16_t *>(wsFramePayload + 2));

          } else {
            headerSize = 10; // 64 bit length
            expectedFramePayloadLength = __builtin_bswap64(
                *reinterpret_cast<const uint64_t *>(wsFramePayload + 2));
          }

          if (wsFramePayloadSize < headerSize) {
            // websocket payload with size less than header length exit for next
            // read
            return shiftIncompleteHdrToFrontForNextRead(wsFramePayloadSize,
                                                        nextMessageOffset);
          }
        }
        const bool hasMask = wsFramePayload[1] & WS_MASK;
        if (hasMask) {
          if (wsFramePayloadSize < (headerSize + 4)) {
            // websocket payload with size less than header and mask keylength
            // exit for next read
            return shiftIncompleteHdrToFrontForNextRead(wsFramePayloadSize,
                                                        nextMessageOffset);
          }
          // Extract the 4-byte masking key from the header
          maskingKeyLength = 4;
          _maskingKey = *reinterpret_cast<const std::array<char, 4> *>(
              wsFramePayload + headerSize);
        } else {
          // According to RFC 6455, client-to-server frames MUST be masked
          // Server-to-client frames MUST NOT be masked
          if constexpr (std::is_same_v<ServerSideEndpointT,
                                       BaseSocketEndpointT>) {
            return std::unexpected(
                "WebSocket protocol violation: Client frame is not masked");
          }
        }

        if (wsFramePayloadSize <
            (expectedFramePayloadLength + headerSize + maskingKeyLength)) {
          _pendingWSFrameContent =
              (expectedFramePayloadLength + headerSize + maskingKeyLength) -
              wsFramePayloadSize;
          // current read does not contain the entire message so set the
          // current payload length to everything after the header
          currentFramePayloadSize =
              wsFramePayloadSize - headerSize - maskingKeyLength;
          nextMessageOffset = 0;
        } else {
          // Current SSL read does contain the end of message so set the next
          // message offset
          nextMessageOffset += (expectedFramePayloadLength + headerSize);
          // and set current payload length to the message length
          currentFramePayloadSize = expectedFramePayloadLength;
        }

        _finalFrame = wsFramePayload[0] & WS_FIN;
        const bool rsv1 =
            (static_cast<std::uint8_t>(wsFramePayload[0]) & 0x40U) != 0;

        messagePayload =
            std::string_view{wsFramePayload + headerSize + maskingKeyLength,
                             currentFramePayloadSize};

        const auto opCode =
            static_cast<WSOpCode>(wsFramePayload[0] & WS_OPCODE);

        if (opCode == WSOpCode::Text || opCode == WSOpCode::Binary) {
          _compressedMessage = rsv1;
        } else if (opCode != WSOpCode::Continuation) {
          _compressedMessage = false;
        }

        if (!_firstOp)
          _firstOp = opCode;
      } else {
        if (wsFramePayloadSize < _pendingWSFrameContent) {
          _pendingWSFrameContent -= wsFramePayloadSize;
          // The incoming payload did not contain the end of the pending content
          // so set the current pay load length to the entire read length
          currentFramePayloadSize = wsFramePayloadSize;
          nextMessageOffset = 0;
        } else {
          nextMessageOffset = _pendingWSFrameContent;
          // Current read contained the end of the pending content
          currentFramePayloadSize = _pendingWSFrameContent;
          _pendingWSFrameContent = 0; // No more pending content
        }

        messagePayload =
            std::string_view{wsFramePayload, currentFramePayloadSize};
      }

      if (_pendingWSFrameContent > 0) {
        // This is a network read continuation. Update the continuation content
        // and exit keeping the current frame active
        updateContinuationPayload(messagePayload);
        return {}; // exiting for the network read
      }

      if (!_finalFrame) {
        // We're done with the current frame content but we're handling
        // continuation frames with the remote source so update the continuation
        // buffer and prepare for the next frame but do not dispatch
        updateContinuationPayload(messagePayload);
        if ((nextMessageOffset == 0) ||
            (nextMessageOffset == networkBytesRead)) {
          return {}; // exit to read more
        }
        continue;
      }

      // We have a complete frame to dispatch and no pending content

      if (!_continuationBuffer.empty()) {
        // We have no pending frame content but we're handling content assembly
        // either from an ssl or remote continuation frames so add the current
        // payload to the continuation buffer
        updateContinuationPayload(messagePayload);
        // and the redirect the dispatch payload to the continuation buffer
        messagePayload = std::string_view{_continuationBuffer.begin(),
                                          _continuationBuffer.end()};
      }

      // DISPATCH THE MESSAGE PAYLOAD
      Expected handleResult;
      switch (*_firstOp) {
      case WSOpCode::ClosedConnection:
        return onDisconnected("Connection was closed by the remote server");
      case WSOpCode::Continuation:
        return onDisconnected(
            "Received continuation on message start ... expected content type");
      case WSOpCode::Ping:
        if (auto pingResult = sendFramedPayload(WSOpCode::Pong, messagePayload);
            !pingResult) {
          return pingResult;
        }
        [[fallthrough]];
      case WSOpCode::Pong:
        handleResult =
            _incomingPayloadHandler(messagePayload, *_firstOp, *_frameReadTime);
        break;
      case WSOpCode::Text:
      case WSOpCode::Binary: {
        if (_maskingKey) {
          // Unmask the payload in place
          auto *messagePayloadPtr = const_cast<char *>(messagePayload.data());

          for (size_t i = 0; i < messagePayload.size(); ++i) {
            messagePayloadPtr[i] ^=
                (*_maskingKey)[i % 4]; // XOR with repeating key
          }
          _maskingKey.reset();
        }
        if (_compressedMessage) {
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

          messagePayload = std::string_view{_preDecompressionBuffer.data(),
                                            _preDecompressionBuffer.size()};

          auto &deflateBuffer = this->getCompressedDataBuffer();
          deflateBuffer.clear();
          if (auto deflatedResult = http::decompressPayloadToBuffer(
                  messagePayload, http::SupportedCompression::WSDeflate,
                  BaseSocketEndpointT::getDecompressedBodyBuffer(),
                  *_compressionContext);
              !deflatedResult) {
            return std::unexpected(std::format(
                "Failed to inflate websocket payload, endpoint_name='{}', "
                "networkBytesRead={}, "
                "wsFramePayloadSize={},"
                "currentBufferOffset={},"
                "messagesRead={}, "
                "currentFramePayloadSize={},"
                "error={}",
                this->name(), networkBytesRead, wsFramePayloadSize,
                currentBufferOffset, messagesRead, currentFramePayloadSize,
                deflatedResult.error()));
          } else {
            messagePayload = std::string_view(
                reinterpret_cast<const char *>(
                    BaseSocketEndpointT::getDecompressedBodyBuffer().data()),
                BaseSocketEndpointT::getDecompressedBodyBuffer().size());
          }
        }
        handleResult =
            _incomingPayloadHandler(messagePayload, *_firstOp, *_frameReadTime);
        break;
      }
      default:
        _firstOp.reset();
        _continuationBuffer.clear();
        return onDisconnected(
            "Decoding error ... Received unknown Websocket frame type");
        break;
      };
      _firstOp.reset();
      _continuationBuffer.clear();
      _compressedMessage = false;
      if (!handleResult) {
        return handleResult;
      }

      if ((nextMessageOffset == 0) || (nextMessageOffset == networkBytesRead)) {
        _frameReadTime.reset();
        return {}; // exit to read more
      }
      // There's partial payload for another message in this read so update the
      // frame read time
      _frameReadTime = BaseSocketEndpointT::getClock()();
    }
  }

  Expected shiftIncompleteHdrToFrontForNextRead(
      std::uint32_t shiftContentSize,
      std::uint32_t currentFrameOffsetInBuffer) {
    // There was an incomplete header while attempting to process the current
    // frame. So prepend so the incoming data data buffer with this partial
    // content so that the contents of the next network read will be placed
    // immediately after this allowing us to process the complete header next
    // time
    _pendingWSFrameContent = 0;
    return this->prependPartialContent(this->getInboundBuffer().data() +
                                           currentFrameOffsetInBuffer,
                                       shiftContentSize);
  }

  void updateContinuationPayload(std::string_view messagePayload) {
    std::copy(messagePayload.begin(), messagePayload.end(),
              std::back_inserter(_continuationBuffer));
  }

  void resetWebsocketState() {
    this->resetConnection();
    // Downgrade the connection
    _upgraded = false;
    // Reset frame handling state
    _frameReadTime.reset();
    _maskingKey.reset();
    _continuationBuffer.clear();
    _pendingWSFrameContent = 0;
    _finalFrame = 0;
    _readBufferOffset = 0;
    _compressedMessage = false;
    closeCompressionContext();
    BaseSocketEndpointT::resetHttpState();
  }

  Expected initializeDecompressionCompressionContext() {
    _compressionContext = z_stream{};
    if (auto result = http::openZStreamDecompression(
            *_compressionContext, http::SupportedCompression::WSDeflate);
        !result) {
      return std::unexpected(
          std::format("Failed to initialize z_stream context for websocket "
                      "endpoint, error={}",
                      result.error()));
    }
    return {};
  }

  Expected initializeCompressionContext() {
    _compressionContext = z_stream{};
    if (auto result = http::openZStreamCompression(
            *_compressionContext, http::SupportedCompression::WSDeflate);
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

  std::vector<char> _sendBuffer;
  // proprietary websocket handlers
  WebSocketPayloadHandlerT _incomingPayloadHandler;
  WebSocketPayloadHandlerT _outgoingHandler;
  bool _upgraded{false};
  std::optional<TimePoint> _frameReadTime;
  std::optional<std::array<char, 4>> _maskingKey;
  // Websocket buffer assembly/continuation
  std::vector<char> _continuationBuffer{};
  std::vector<char> _preDecompressionBuffer{};

  std::optional<WSOpCode> _firstOp;
  std::uint32_t _pendingWSFrameContent{0};
  bool _finalFrame{false};
  std::uint32_t _readBufferOffset{0};
  bool _deflateRequested{false};
  std::optional<z_stream> _compressionContext{};
  bool _compressedMessage{false};
};

} // namespace medici::sockets::live