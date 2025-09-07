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
  { t.ioLogging() } -> std::convertible_to<IOloggingConfig>;
};

template <typename T>
concept HTTPEndpointConfigC = IPEndpointConfigC<T> && requires(T t) {
  { t.uriPath() } -> std::convertible_to<std::string>;
};

class IPEndpointConfig {
  std::string _name{};
  std::string _host{};
  std::uint16_t _port{0};
  std::string _interface{};
  std::uint32_t _inBufferKB{1};
  IOloggingConfig _ioLogging{};

public:
  IPEndpointConfig() {}
  IPEndpointConfig(const std::string &name, const std::string &host,
                   std::uint16_t port, std::uint32_t inBufferKB = 1,
                   std::string interface = "", IOloggingConfig ioLogging = {})
      : _name{name}, _host{host}, _port{port}, _interface{interface},
        _inBufferKB{inBufferKB}, _ioLogging{ioLogging}

  {}

  IPEndpointConfig(const IPEndpointConfigC auto &config)
      : _name{config.name()}, _host{config.host()}, _port{config.port()},
        _interface{config.interface()}, _ioLogging{config.ioLogging()} {}

  auto &name() const { return _name; }

  auto &host() const { return _host; }

  auto port() const { return _port; }

  auto interface() const { return _interface; }

  auto inBufferKB() const { return _inBufferKB; }

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

} // namespace  medici::sockets