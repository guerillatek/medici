#pragma once

#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <type_traits>

#include "medici/http/HTTPAction.hpp"
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
    (const http::HttpFields &, std::string_view, TimePoint))>;

using HttpClientPayloadHandlerT = std::function<sockets::Expected(
    (const http::HttpFields &, std::string_view, int, TimePoint))>;

using HttpServerPayloadHandlerT = std::function<sockets::Expected(
    (http::HTTPAction, const std::string &, const http::HttpFields &,
     const HttpServerPayloadT &, TimePoint))>;

using WebSocketPayloadHandlerT =
    std::function<sockets::Expected((std::string_view, WSOpCode, TimePoint))>;
using CloseHandlerT = std::function<sockets::Expected(
    const std::string &, const IPEndpointConfig &)>;
using DisconnectedHandlerT = CloseHandlerT;

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
                               HttpClientPayloadHandlerT{},
                               SocketPayloadHandlerT{}, CloseHandlerT{},
                               DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<IHttpClientEndpointPtr>;

  {
    t.createHttpsClientEndpoint(std::declval<HttpEndpointConfig>(),
                                HttpClientPayloadHandlerT{},
                                SocketPayloadHandlerT{}, CloseHandlerT{},
                                DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<IHttpClientEndpointPtr>;

  {
    t.createWSClientEndpoint(std::declval<WSEndpointConfig>(),
                             WebSocketPayloadHandlerT{},
                             WebSocketPayloadHandlerT{}, CloseHandlerT{},
                             DisconnectedHandlerT{}, OnActiveHandlerT{})
  } -> std::same_as<IWebSocketEndpointPtr>;

  {
    t.createWSSClientEndpoint(std::declval<WSEndpointConfig>(),
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
    t(http::HttpFields{}, std::string_view{}, TimePoint{})
  } -> std::same_as<Expected>;
};

template <typename T>
concept HttpClientPayloadHandlerC = requires(T t) {
  {
    t(http::HttpFields{}, std::string_view{}, int{}, TimePoint{})
  } -> std::same_as<Expected>;
};

template <typename T>
concept HttpServerPayloadHandlerC = requires(T t) {
  {
    t(http::HTTPAction{}, std::string{}, http::HttpFields{},
      HttpServerPayloadT{}, TimePoint{})
  } -> std::same_as<Expected>;
};

template <typename T>
concept WebSocketPayloadHandlerC = requires(T t) {
  { t(std::string_view{}, WSOpCode{}, TimePoint{}) } -> std::same_as<Expected>;
};

// Concepts for identifying socket categories for the interface types

template <typename T>
concept IsTcpEndpoint = std::derived_from<T, ITcpIpEndpoint>;

template <typename T>
concept IsHttpServerEndpoint = std::derived_from<T, IHttpServerEndpoint>;

template <typename T>
concept IsHttpClientEndpoint = std::derived_from<T, IHttpClientEndpoint>;

template <typename T>
concept IsWebSocketEndpoint = std::derived_from<T, IWebSocketEndpoint>;

template <typename EndpointT> static auto constexpr getEndpointHandlerType() {
  if constexpr (IsTcpEndpoint<EndpointT>) {
    return SocketPayloadHandlerT{};
  } else if constexpr (IsHttpServerEndpoint<EndpointT>) {
    return HttpServerPayloadHandlerT{};
  } else if constexpr (IsHttpClientEndpoint<EndpointT>) {
    return HttpClientPayloadHandlerT{};
  } else if constexpr (IsWebSocketEndpoint<EndpointT>) {
    return WebSocketPayloadHandlerT{};
  }
};

template <typename EndpointT> static auto constexpr getOutgoingHandlerType() {
  if constexpr (IsTcpEndpoint<EndpointT>) {
    return SocketPayloadHandlerT{};
  } else if constexpr (IsHttpServerEndpoint<EndpointT>) {
    return SocketPayloadHandlerT{};
  } else if constexpr (IsHttpClientEndpoint<EndpointT>) {
    return SocketPayloadHandlerT{};
  } else if constexpr (IsWebSocketEndpoint<EndpointT>) {
    return WebSocketPayloadHandlerT{};
  }
};
} // namespace medici::sockets