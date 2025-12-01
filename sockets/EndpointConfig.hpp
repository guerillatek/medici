#pragma once
#include "medici/http/HttpFields.hpp"

#include <concepts>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace medici::sockets {
struct IOloggingConfig {
  bool inbound{false};
  bool outbound{false};
};

template <typename T>
concept IPEndpointConfigC = requires(T t) {
  { t.name() } -> std::convertible_to<std::string>;
  { t.host() } -> std::convertible_to<std::string>;
  { t.port() } -> std::convertible_to<std::uint16_t>;
  { t.interface() } -> std::convertible_to<std::string>;
  { t.recvBufferKB() } -> std::convertible_to<std::uint32_t>;
  { t.ioLogging() } -> std::convertible_to<IOloggingConfig>;
};

template <typename T>
concept HTTPEndpointConfigC = IPEndpointConfigC<T> && requires(T t) {
  { t.uriPath() } -> std::convertible_to<std::string>;
};

template <typename T>
concept WSEndpointConfigC = HTTPEndpointConfigC<T> && requires(T t) {
  { t.perMessageDeflate() } -> std::convertible_to<bool>;
};

class IPEndpointConfig {
  std::string _name{};
  std::string _host{};
  std::uint16_t _port{0};
  std::string _interface{};
  std::uint32_t _recvBufferKB{1};
  IOloggingConfig _ioLogging{};

public:
  IPEndpointConfig() {}
  IPEndpointConfig(const std::string &name, const std::string &host,
                   std::uint16_t port, std::uint32_t recvBufferKB = 16,
                   std::string interface = "", IOloggingConfig ioLogging = {})
      : _name{name}, _host{host}, _port{port}, _interface{interface},
        _recvBufferKB{recvBufferKB}, _ioLogging{ioLogging}

  {}

  IPEndpointConfig(const IPEndpointConfigC auto &config)
      : _name{config.name()}, _host{config.host()}, _port{config.port()},
        _interface{config.interface()}, _ioLogging{config.ioLogging()} {}

  auto &name() const { return _name; }

  auto &host() const { return _host; }

  auto port() const { return _port; }

  auto interface() const { return _interface; }

  auto recvBufferKB() const { return _recvBufferKB; }

  auto &ioLogging() const { return _ioLogging; }
};

class HttpEndpointConfig : public IPEndpointConfig {
  std::string _uriPath{};

public:
  HttpEndpointConfig() {};
  HttpEndpointConfig(IPEndpointConfig &&ipEndpointConfig,
                     const std::string uriPath)
      : IPEndpointConfig{std::move(ipEndpointConfig)}, _uriPath{uriPath} {};
  HttpEndpointConfig(const std::string &name, const std::string &host,
                     std::uint16_t port, const std::string &uri)
      : IPEndpointConfig(name, host, port), _uriPath{uri} {}

  HttpEndpointConfig(const HTTPEndpointConfigC auto &config)
      : IPEndpointConfig{config}, _uriPath{config.uriPath()} {}

  HttpEndpointConfig(const IPEndpointConfigC auto &config,
                     const std::string &uri = "")
      : IPEndpointConfig{config}, _uriPath{uri} {}

  auto uriPath() const { return _uriPath; }

  void uriPath(const std::string value) { _uriPath = value; }
};

class WSEndpointConfig : public HttpEndpointConfig {
  bool _perMessageDeflate{false};

public:
  WSEndpointConfig() {};
  WSEndpointConfig(HttpEndpointConfig &&httpEndpointConfig,
                   bool perMessageDeflate = false)
      : HttpEndpointConfig{std::move(httpEndpointConfig)},
        _perMessageDeflate{perMessageDeflate} {};
  WSEndpointConfig(const std::string &name, const std::string &host,
                   std::uint16_t port, const std::string &uri,
                   bool perMessageDeflate = false)
      : HttpEndpointConfig(name, host, port, uri),
        _perMessageDeflate{perMessageDeflate} {}

  WSEndpointConfig(const WSEndpointConfigC auto &config)
      : HttpEndpointConfig{config},
        _perMessageDeflate{config.perMessageDeflate()} {}

  WSEndpointConfig(const HTTPEndpointConfigC auto &config,
                   const std::string &uri = "")
      : HttpEndpointConfig{config}, _perMessageDeflate{uri} {}

  bool perMessageDeflate() const { return _perMessageDeflate; }
};

} // namespace  medici::sockets