#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>

#include "medici/http/HttpFields.hpp"
#include "medici/sockets/EndpointConfig.hpp"
#include "medici/sockets/WSOpCode.hpp"
#include "medici/sockets/interfaces.hpp"

#include "medici/time.hpp"

namespace medici::sockets {

using OnActiveHandlerT = std::function<sockets::Expected()>;
using SocketPayloadHandlerT =
    std::function<sockets::Expected((std::string_view, TimePoint))>;
using HttpPayloadHandlerT = std::function<sockets::Expected(
    (const http::HttpFields &, std::string_view, int, TimePoint))>;
using WebSocketPayloadHandlerT =
    std::function<sockets::Expected((std::string_view, WSOpCode, TimePoint))>;
using CloseHandlerT = std::function<sockets::Expected(const std::string &)>;
using DisconnectedHandlerT =
    std::function<sockets::Expected((const std::string &))>;

using SourceEndpointPollMangerT = std::function<IIPEndpointPollManager &()>;

template <typename T>
concept RemoteTcpEndpointHandlerC = requires(T t) {
  { t(std::declval<IPEndpointConfig>(), int{}) };
};

using RemoteTcpEndpointHandlerT =
    std::function<sockets::Expected(const IPEndpointConfig &, int)>;

template <typename T>
concept RemoteHttpEndpointHandlerC = requires(T t) {
  { t(std::declval<HttpEndpointConfig>(), int{}) };
};

using RemoteHttpEndpointHandlerT =
    std::function<sockets::Expected(const HttpEndpointConfig &, int)>;

template <typename T>
concept SocketFactoryC = requires(T t) {
  {
    t.createMulticastEndpoint(std::declval<IPEndpointConfig>(),
                              SocketPayloadHandlerT{}, CloseHandlerT{},
                              DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<IMulticastEndpointPtr>;

  {
    t.createUdpEndpoint(std::declval<IPEndpointConfig>(),
                        SocketPayloadHandlerT{}, SocketPayloadHandlerT{},
                        CloseHandlerT{}, DisconnectedHandlerT{},
                        OnActiveHandlerT{})
  } -> std::same_as<IUdpEndpointPtr>;

  {
    t.createTcpIpClientEndpoint(std::declval<IPEndpointConfig>(),
                                SocketPayloadHandlerT{},
                                SocketPayloadHandlerT{}, CloseHandlerT{},
                                DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<ITcpIpEndpointPtr>;

  {
    t.createSSLClientEndpoint(std::declval<IPEndpointConfig>(),
                              SocketPayloadHandlerT{}, SocketPayloadHandlerT{},
                              CloseHandlerT{}, DisconnectedHandlerT{},
                              OnActiveHandlerT{})
  } -> std::same_as<ITcpIpEndpointPtr>;

  {
    t.createHttpClientEndpoint(std::declval<HttpEndpointConfig>(),
                               HttpPayloadHandlerT{}, HttpPayloadHandlerT{},
                               CloseHandlerT{}, DisconnectedHandlerT{},
                               OnActiveHandlerT{})
  } -> std::same_as<IHttpEndpointPtr>;

  {
    t.createHttpsClientEndpoint(std::declval<HttpEndpointConfig>(),
                                HttpPayloadHandlerT{}, HttpPayloadHandlerT{},
                                CloseHandlerT{}, DisconnectedHandlerT{},
                                OnActiveHandlerT{})
  } -> std::same_as<IHttpEndpointPtr>;

  {
    t.createWSClientEndpoint(std::declval<HttpEndpointConfig>(),
                             WebSocketPayloadHandlerT{},
                             WebSocketPayloadHandlerT{}, CloseHandlerT{},
                             DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<IWebSocketEndpointPtr>;

  {
    t.createWSSClientEndpoint(std::declval<HttpEndpointConfig>(),
                              WebSocketPayloadHandlerT{},
                              WebSocketPayloadHandlerT{}, CloseHandlerT{},
                              DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<IWebSocketEndpointPtr>;

  {
    t.createTcpIpListenerEndpoint(IPEndpointConfig{}, CloseHandlerT{},
                                  DisconnectedHandlerT{}, OnActiveHandlerT{},
                                  RemoteTcpEndpointHandlerT{})
  } -> std::same_as<IIPEndpointPtr>;

  {
    t.createHttpListenerEndpoint(IPEndpointConfig{}, CloseHandlerT{},
                                 DisconnectedHandlerT{}, OnActiveHandlerT{},
                                 RemoteHttpEndpointHandlerT{})
  } -> std::same_as<IIPEndpointPtr>;
};

template <typename T>
concept SocketPayloadHandlerC = requires(T t) {
  { t(std::string_view{}, TimePoint{}) } -> std::same_as<Expected>;
};

template <typename T>
concept HttpPayloadHandlerC = requires(T t) {
  {
    t(http::HttpFields{}, std::string_view{}, int{}, TimePoint{})
  } -> std::same_as<Expected>;
};

template <typename T>
concept WebSocketPayloadHandlerC = requires(T t) {
  { t(std::string_view{}, WSOpCode{}, TimePoint{}) } -> std::same_as<Expected>;
};

// Concepts for identifying socket categories for the interface types

template <typename T>
concept IsTcpEndpoint = std::derived_from<T, ITcpIpEndpoint> &&
                        !std::derived_from<T, IWebSocketEndpoint> &&
                        !std::derived_from<T, IHttpEndpoint>;

template <typename T>
concept IsHttpEndpoint = std::derived_from<T, IHttpEndpoint> &&
                         !std::derived_from<T, IWebSocketEndpoint>;

template <typename T>
concept IsWebSocketEndpoint = std::derived_from<T, IWebSocketEndpoint>;

template <typename EndpointT> static auto constexpr getEndpointHandlerType() {
  if constexpr (IsTcpEndpoint<EndpointT>) {
    return SocketPayloadHandlerT{};
  } else if constexpr (IsHttpEndpoint<EndpointT>) {
    return HttpPayloadHandlerT{};
  } else if constexpr (IsWebSocketEndpoint<EndpointT>) {
    return WebSocketPayloadHandlerT{};
  }
};

template <typename HandlerT> static auto constexpr getStdFunctionHandlerType() {
  if constexpr (SocketPayloadHandlerC<HandlerT>) {
    return SocketPayloadHandlerT{};
  } else if constexpr (HttpPayloadHandlerC<HandlerT>) {
    return HttpPayloadHandlerT{};
  } else if constexpr (WebSocketPayloadHandlerC<HandlerT>) {
    return WebSocketPayloadHandlerT{};
  }
};

} // namespace medici::sockets