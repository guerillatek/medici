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

  WebSocketLiveEndpoint(const HttpEndpointConfig &config,
                        IIPEndpointPollManager &endpointPollManager,
                        WebSocketPayloadHandlerC auto &&incomingPayloadHandler,
                        WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
                        CloseHandlerT closeHandler,
                        DisconnectedHandlerT disconnectedHandler,
                        OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{config,
                            endpointPollManager,
                            [this](const auto &headers, auto payload, int rc,
                                   auto tp) {
                              return handleBaseSocketInboundPayload(
                                  headers, payload, rc, tp);
                            },
                            SocketPayloadHandlerT{},
                            closeHandler,
                            disconnectedHandler,
                            onActiveHandler},
        _incomingPayloadHandler{std::move(incomingPayloadHandler)},
        _outgoingHandler{std::move(outgoingPayloadHandler)} {
    _sendBuffer.reserve(2048);
  }

  WebSocketLiveEndpoint(int clientFd, const HttpEndpointConfig &config,
                        IIPEndpointPollManager &endpointPollManager,
                        WebSocketPayloadHandlerC auto &&incomingPayloadHandler,
                        WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
                        CloseHandlerT closeHandler,
                        DisconnectedHandlerT disconnectedHandler,
                        OnActiveHandlerT onActiveHandler)
      : BaseSocketEndpointT{clientFd,
                            config,
                            endpointPollManager,
                            [this](const auto &headers, auto payload, auto tp) {
                              return handleBaseSocketInboundPayload(
                                  headers, payload, 0, tp);
                            },
                            SocketPayloadHandlerT{},
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

  int getEndpointUniqueId() const override {
    return BaseSocketEndpointT::getEndpointUniqueId();
  }

  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    return BaseSocketEndpointT::registerTimer(timer);
  }

  Expected onDisconnected(
      const std::string &reason,
      const medici::sockets::IPEndpointConfig &endpointConfig) override {
    resetWebsocketState();
    return BaseSocketEndpointT::onDisconnected(reason, endpointConfig);
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
    _sendBuffer.clear();

    if (_deflateEnabled) {
      if (auto deflatedResult =
              compressPayload(payload, http::SupportedCompression::WSDeflate,
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

    _sendBuffer.push_back(static_cast<char>(0x80 | static_cast<int>(opCode)));

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
      return std::unexpected("Support for sending payloads larger than 64K is "
                             "not supported at this time");
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
    _outgoingHandler(payload, opCode, this->getClock()());
    return {};
  }

  Expected closeEndpoint(const std::string &reason) override {
    auto result = BaseSocketEndpointT::closeEndpoint(reason);
    resetWebsocketState();
    return result;
  }

  Expected disconnectEndpoint(const std::string &reason) override {
    auto result = BaseSocketEndpointT::closeRemoteConnection();
    return onDisconnected(reason, this->getConfig());
  }

  bool supportsDeflatedPayloads() const {
    return BaseSocketEndpointT::supportsDeflatedPayloads();
  }

private:
  Expected onActive() override {
    if constexpr (std::is_same_v<ServerSideEndpointT, BaseSocketEndpointT>) {
      // Server side endpoint so od nothing and wait for client to send upgrade
      // request
      return {};
    }

    if constexpr (std::is_same_v<ClientSideEndpointT, BaseSocketEndpointT>) {
      // Client side endpoint so send the upgrade request
      http::HttpFields upgradeHeaders;
      const auto key = crypto_utils::generateKey();
      upgradeHeaders.addFieldValue("Upgrade", "websocket");
      upgradeHeaders.addFieldValue("Connection", "Upgrade");
      upgradeHeaders.addFieldValue("Sec-WebSocket-Version", "13");
      upgradeHeaders.addFieldValue("Sec-WebSocket-Key", key);

      // Add RFC 7692 deflate extension request
      upgradeHeaders.addFieldValue(
          "Sec-WebSocket-Extensions",
          "permessage-deflate; client_max_window_bits");

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

  Expected handleBaseSocketInboundPayload(const http::HttpFields &headers,
                                          std::string_view payload,
                                          int responseCode,
                                          TimePoint epollTime) {
    if (!_upgraded) [[unlikely]] {

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

      if constexpr (std::is_same_v<ServerSideEndpointT, BaseSocketEndpointT>) {
        // Server side endpoint so validate the client key and send the
        // upgrade response
        if (auto secKey = headers.getField("Sec-WebSocket-Key"); secKey) {
          std::string acceptKey =
              crypto_utils::generateWebSocketAcceptKey(secKey.value());
          http::HttpFields responseHeaders;
          responseHeaders.addFieldValue("Upgrade", "websocket");
          responseHeaders.addFieldValue("Connection", "Upgrade");
          responseHeaders.addFieldValue("Sec-WebSocket-Accept", acceptKey);

          // Check for permessage-deflate extension request
          if (auto extField = headers.getField("Sec-WebSocket-Extensions");
              extField) {
            auto extValue = extField.value();
            if (extValue.find("permessage-deflate") != std::string::npos) {
              // Client requested permessage-deflate extension
              _deflateEnabled = true;
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
      }

      if constexpr (std::is_same_v<ClientSideEndpointT, BaseSocketEndpointT>) {
        // Client side endpoint so just check for permessage-deflate
        if (auto extField = headers.getField("Sec-WebSocket-Extensions");
            extField) {
          auto extValue = extField.value();
          if (extValue.find("permessage-deflate") != std::string::npos) {
            // Server accepted permessage-deflate extension
            _deflateEnabled = true;
          }
        }
        return BaseSocketEndpointT::onActive();
      }

      return std::unexpected("Unknown endpoint type for websocket");
    }
    return handleWebsocketPayload(payload.size());
  }

  Expected handleWebsocketPayload(std::uint32_t networkBytesRead) {
    // nextMessageOffset is an offset used to establish the start of the next
    // message in the currently read buffer after one more messages have been
    // read/dispatched.This value monotonically increases by the size of current
    // frame once it's established assuming that remaining content in the buffer
    // is large enough.
    std::uint32_t nextMessageOffset{0};

    // If the previous network read had remaining payload data that was
    // incomplete That data would have been copied to the front network payload
    // buffer and the _writeBufferOffset would been set to reflect the size of
    // that shifted data. That offset also served as the starting point in our
    // buffer that we we passed for the current socket read that we are now
    // handling. This ensures that the newly read data was appended to the
    // previous leftover data. For this reason we augment the actual
    // networkBytesRead with that offset to reflect the actual data in the
    // buffer starting from the beginning that needs to be processed
    if (_writeBufferOffset) {
      // conflate leftover content with current read
      networkBytesRead += _writeBufferOffset;
      _writeBufferOffset = 0;
    }

    while (true) {
      // wsFramePayload is a pointer to the start of the active websocket frame
      // in the buffer that we're about to process for this cycle . We establish
      // the start of this buffer based on the nextMessageOffset which is zero
      // for the first cycle. We update this offset once establish size of the
      // current frame we're processing to prep it for the next cycle.
      auto wsFramePayload =
          BaseSocketEndpointT::getInboundBuffer().data() + nextMessageOffset;

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

        const bool MASK = wsFramePayload[1] & WS_MASK;
        if (MASK) [[unlikely]] {
          return onDisconnected(
              "Received masked frame ... not handled or expected at this time. "
              "Frame corruption has likely occurred",
              this->getConfig());
        }

        expectedFramePayloadLength = wsFramePayload[1] & WS_LENGTH;

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
        if (wsFramePayloadSize < (expectedFramePayloadLength + headerSize)) {
          _pendingWSFrameContent =
              (expectedFramePayloadLength + headerSize) - wsFramePayloadSize;
          // current SSL read does not contain the entire message so set the
          // current payload length to everything after the header
          currentFramePayloadSize = wsFramePayloadSize - headerSize;
          nextMessageOffset = 0;
        } else {
          // Current SSL read does contain the end of message so set the next
          // message offset
          nextMessageOffset += (expectedFramePayloadLength + headerSize);
          // and set current payload length to the message length
          currentFramePayloadSize = expectedFramePayloadLength;
        }

        _finalFrame = wsFramePayload[0] & WS_FIN;
        messagePayload = std::string_view{wsFramePayload + headerSize,
                                          currentFramePayloadSize};
        const auto opCode =
            static_cast<WSOpCode>(wsFramePayload[0] & WS_OPCODE);

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
      switch (_firstOp) {
      case WSOpCode::ClosedConnection:
        return onDisconnected("Connection was closed by the remote server",
                              this->getConfig());
      case WSOpCode::Continuation:
        return onDisconnected(
            "Received continuation on message start ... expected content type",
            this->getConfig());
      case WSOpCode::Ping:
        if (auto pingResult = sendFramedPayload(WSOpCode::Pong, messagePayload);
            !pingResult) {
          return pingResult;
        }
        [[fallthrough]];
      case WSOpCode::Pong:
        handleResult =
            _incomingPayloadHandler(messagePayload, _firstOp, *_frameReadTime);
        break;
      case WSOpCode::Text:
      case WSOpCode::Binary:
        handleResult =
            _incomingPayloadHandler(messagePayload, _firstOp, *_frameReadTime);
        break;
      default:
        return onDisconnected(
            "Decoding error ... Received unknown Websocket frame type",
            this->getConfig());
        break;
      };

      _continuationBuffer.clear();
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
    // frame shift it to the front and set the _writeBufferOffset so that the
    // incoming data on the next network read will be placed immediately after
    // the shifted data
    _pendingWSFrameContent = 0;
    _writeBufferOffset = shiftContentSize;
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
    _continuationBuffer.clear();
    _pendingWSFrameContent = 0;
    _finalFrame = 0;
    _writeBufferOffset = 0;
    _readBufferOffset = 0;
    BaseSocketEndpointT::resetHttpState();
  }

  std::vector<char> _sendBuffer;

  // proprietary websocket handlers
  WebSocketPayloadHandlerT _incomingPayloadHandler;
  WebSocketPayloadHandlerT _outgoingHandler;
  bool _upgraded{false};
  std::optional<TimePoint> _frameReadTime;

  // Websocket buffer assembly/continuation
  std::vector<char> _continuationBuffer{};
  WSOpCode _firstOp;
  std::uint32_t _pendingWSFrameContent{0};
  bool _finalFrame{false};
  std::uint32_t _writeBufferOffset{0};
  std::uint32_t _readBufferOffset{0};
  bool _deflateEnabled{false};
};

} // namespace medici::sockets::live