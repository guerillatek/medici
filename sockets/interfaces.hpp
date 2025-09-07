#pragma once

#include "medici/IEndpointEventDispatch.hpp"
#include "medici/sockets/EndpointConfig.hpp"
#include "medici/sockets/WSOpCode.hpp"

#include "medici/http/ContentType.hpp"
#include "medici/http/HTTPAction.hpp"
#include "medici/http/HttpFields.hpp"

#include <memory>
#include <openssl/ssl.h>
#include <utility>

namespace medici::sockets {
using Expected = std::expected<void, std::string>;

using ExpectedContext = std::expected<SSL_CTX *, std::string>;

class IIPEndpointDispatch : public IEndpointEventDispatch {
public:
  IIPEndpointDispatch(const IPEndpointConfig &config) : _config{config} {}
  auto &getConfig() const { return _config; }

protected:
  IPEndpointConfig _config;
};

class IIPEndpoint {
public:
  virtual const std::string &name() const = 0;
  virtual Expected openEndpoint() = 0;
  virtual Expected closeEndpoint(const std::string &reason = "") = 0;
  virtual bool isActive() const = 0;
  virtual const medici::ClockNowT &getClock() const = 0;
  virtual IIPEndpointDispatch &getDispatchInterface() = 0;
  virtual ~IIPEndpoint() = default;
};

using IIPEndpointPtr = std::unique_ptr<IIPEndpoint>;

class IMulticastEndpoint : public IIPEndpoint {};

using IMulticastEndpointPtr = std::unique_ptr<IMulticastEndpoint>;

class IUdpEndpoint : public IIPEndpoint {
public:
  virtual Expected send(std::string_view buffer) = 0;
};

using IUdpEndpointPtr = std::unique_ptr<IUdpEndpoint>;

class ITcpIpEndpoint : public IIPEndpoint {
public:
  virtual Expected send(std::string_view buffer) = 0;
};

using ITcpIpEndpointPtr = std::unique_ptr<ITcpIpEndpoint>;

struct HttpResponsePayloadOptions {
  static constexpr uint8_t AllCompressionEncodings =
      (std::to_underlying(http::SupportedCompression::GZip) |
       std::to_underlying(http::SupportedCompression::HttpDeflate) |
       std::to_underlying(http::SupportedCompression::Brotli));
  std::uint8_t acceptedCompressionEncodings{0};
  bool decompressBeforeDispatch{false};
  bool dispatchPartialPayloads{false};
};

class IHttpEndpoint : public IIPEndpoint {
public:
  // Overrides value set in config
  virtual void setURIPath(const std::string &) = 0;
  virtual Expected
  // Client side sendHttp
  sendHttpRequest(
      http::HTTPAction action, http::HttpFields headersValues,
      std::string_view content = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      HttpResponsePayloadOptions responsePayloadOptions = {}) = 0;

  // Server side sendHttp
  virtual Expected sendHttpResponse(
      http::HttpFields headersValues, int responseCode = 200,
      const std::string &message = "OK", std::string_view content = "",
      http::ContentType contentType = http::ContentType::Unspecified,
      std::optional<http::SupportedCompression> compressed = {}) = 0;
};

using IHttpEndpointPtr = std::unique_ptr<IHttpEndpoint>;

class IWebSocketEndpoint : public IIPEndpoint {
public:
  virtual Expected sendText(std::string_view) = 0;
  virtual Expected sendBinary(std::string_view) = 0;
  virtual Expected sendPayload(WSOpCode opCode, std::string_view) = 0;
  // Manual disconnect that closes the session but also fires the disconnect
  // handlers
  virtual Expected disconnectEndpoint(const std::string &reason) = 0;
};

using IWebSocketEndpointPtr = std::unique_ptr<IWebSocketEndpoint>;

class IListenerEndpoint : public IIPEndpoint {
public:
  virtual Expected startListening() = 0;
  virtual Expected stopListening() = 0;
};

class ITcpIpListenerEndpoint : public IIPEndpoint {};
using ITcpIpListenerEndpointPtr = std::unique_ptr<ITcpIpListenerEndpoint>;

class ISSLListenerEndpoint : public IIPEndpoint {};
using ISSLListenerEndpointPtr = std::unique_ptr<ISSLListenerEndpoint>;

class IHttpListenerEndpoint : public IIPEndpoint {};
using IHttpListenerEndpointPtr = std::unique_ptr<IHttpListenerEndpoint>;

class IWebSocketListenerEndpoint : public IHttpListenerEndpoint {};
using IWebSocketListenerEndpointPtr =
    std::unique_ptr<IWebSocketListenerEndpoint>;

class IIPEndpointPollManager {
public:
  // On thread local registration of endpoints
  virtual Expected registerEndpoint(int fd, IIPEndpointDispatch &) = 0;

  // Off thread local registration of endpoints with a callback for endpoints
  // created by listeners on another thread
  virtual Expected listenerRegisterEndpoint(int fd,
                                            IIPEndpointDispatch &dispatch) = 0;
  virtual Expected removeEndpoint(int fd, IIPEndpointDispatch &) = 0;
  virtual Expected initialize() = 0;
  virtual Expected shutdown() = 0;
  virtual ExpectedContext getSSLContext() = 0;
  virtual const ClockNowT &getClock() const = 0;
};

} // namespace  medici::sockets
