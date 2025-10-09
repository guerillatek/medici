#pragma once
#include <memory>

#include "medici/event_queue/EventQueue.hpp"
#include "medici/sockets/live/HTTPLiveClientEndpoint.hpp"
#include "medici/sockets/live/HTTPLiveServerEndpoint.hpp"
#include "medici/sockets/live/HttpLiveListenEndpoints.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"
#include "medici/sockets/live/SSLLiveEndpoint.hpp"
#include "medici/sockets/live/TcpIpLiveEndpoint.hpp"
#include "medici/sockets/live/TcpIpLiveListenEndpoints.hpp"
#include "medici/sockets/live/WebSocketLiveEndpoint.hpp"
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <format>
#include <list>
#include <memory>
#include <random>
#include <vector>

namespace medici::sockets::live {

class LiveSocketFactory {
public:
  LiveSocketFactory(IIPEndpointPollManager &endpointPollManager)
      : _endpointPollManager{endpointPollManager} {}

  std::unique_ptr<IMulticastEndpoint>
  createMulticastEndpoint(const IPEndpointConfig &config,
                          SocketPayloadHandlerC auto &&payloadHandler1,
                          CloseHandlerT closeHandler,
                          DisconnectedHandlerT disconnectHandler,
                          OnActiveHandlerT onActiveHandler) {
    throw std::runtime_error("Multicast endpoints not supported at this time");
  }

  std::unique_ptr<IUdpEndpoint>
  createUdpEndpoint(const IPEndpointConfig &config,
                    SocketPayloadHandlerC auto &&payloadHandler1,
                    SocketPayloadHandlerT outgoingPayloadHandler,
                    CloseHandlerT closeHandler,
                    DisconnectedHandlerT disconnectHandler,
                    OnActiveHandlerT onActiveHandler) {
    throw std::runtime_error("UDP endpoints not supported at this time");
  }

  std::unique_ptr<ITcpIpEndpoint>
  createTcpIpClientEndpoint(const IPEndpointConfig &config,
                            SocketPayloadHandlerC auto &&payloadHandler,
                            SocketPayloadHandlerT outgoingPayloadHandler,
                            CloseHandlerT closeHandler,
                            DisconnectedHandlerT disconnectHandler,
                            OnActiveHandlerT onActiveHandler) {

    using IncomingHandlerT = std::decay_t<decltype(payloadHandler)>;
    return std::make_unique<TcpIpLiveEndpoint<IncomingHandlerT>>(
        config, _endpointPollManager,
        std::forward<IncomingHandlerT>(payloadHandler), outgoingPayloadHandler,
        closeHandler, disconnectHandler, onActiveHandler);
  }

  ITcpIpEndpointPtr
  createSSLClientEndpoint(const IPEndpointConfig &config,
                          SocketPayloadHandlerC auto &&payloadHandler,
                          SocketPayloadHandlerT outgoingPayloadHandler,
                          CloseHandlerT closeHandler,
                          DisconnectedHandlerT disconnectHandler,
                          OnActiveHandlerT onActiveHandler) {

    using IncomingHandlerT = std::decay_t<decltype(payloadHandler)>;
    return std::make_unique<SSLLiveEndpoint<IncomingHandlerT>>(
        config, _endpointPollManager,
        std::forward<IncomingHandlerT>(payloadHandler), outgoingPayloadHandler,
        closeHandler, disconnectHandler, onActiveHandler);
  }

  IHttpClientEndpointPtr
  createHttpClientEndpoint(const HttpEndpointConfig &config,
                           HttpClientPayloadHandlerC auto &&payloadHandler,
                           SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                           CloseHandlerT closeHandler,
                           DisconnectedHandlerT disconnectHandler,
                           OnActiveHandlerT onActiveHandler) {
    return createHttpClientEndpointImpl<TcpIpLiveEndpoint>(
        config, payloadHandler, outgoingPayloadHandler, closeHandler,
        disconnectHandler, onActiveHandler);
  }

  IHttpClientEndpointPtr
  createHttpsClientEndpoint(const HttpEndpointConfig &config,
                            HttpClientPayloadHandlerC auto &&payloadHandler,
                            SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                            CloseHandlerT closeHandler,
                            DisconnectedHandlerT disconnectHandler,
                            OnActiveHandlerT onActiveHandler) {
    return createHttpClientEndpointImpl<SSLLiveEndpoint>(
        config, payloadHandler, outgoingPayloadHandler, closeHandler,
        disconnectHandler, onActiveHandler);
  }

  IWebSocketEndpointPtr
  createWSClientEndpoint(const HttpEndpointConfig &config,
                         WebSocketPayloadHandlerC auto &&payloadHandler,
                         WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
                         CloseHandlerT closeHandler,
                         DisconnectedHandlerT disconnectHandler,
                         OnActiveHandlerT onActiveHandler) {
    return createWSClientEndpointImpl<TcpIpLiveEndpoint>(
        config, payloadHandler, outgoingPayloadHandler, closeHandler,
        disconnectHandler, onActiveHandler);
  }

  IWebSocketEndpointPtr createWSSClientEndpoint(
      const HttpEndpointConfig &config,
      WebSocketPayloadHandlerC auto &&payloadHandler,
      WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
      CloseHandlerT closeHandler, DisconnectedHandlerT disconnectHandler,
      OnActiveHandlerT onActiveHandler) {

    return createWSClientEndpointImpl<SSLLiveEndpoint>(
        config, payloadHandler, outgoingPayloadHandler, closeHandler,
        disconnectHandler, onActiveHandler);
  }

  IIPEndpointPtr createTcpIpListenerEndpoint(
      const IPEndpointConfig &config, CloseHandlerT closeHandler,
      DisconnectedHandlerT disconnectedHandler,
      OnActiveHandlerT onActiveHandler,
      RemoteTcpEndpointHandlerT remoteTcpEndpointHandler) {
    return std::make_unique<TcpIpLiveListenEndpoint>(
        config, _endpointPollManager, closeHandler, disconnectedHandler,
        onActiveHandler, remoteTcpEndpointHandler);
  }

  IIPEndpointPtr createHttpListenerEndpoint(
      const IPEndpointConfig &config, CloseHandlerT closeHandler,
      DisconnectedHandlerT disconnectedHandler,
      OnActiveHandlerT onActiveHandler,
      RemoteHttpEndpointHandlerT remoteHttpEndpointHandler) {
    return std::make_unique<HttpLiveListenEndpoint>(
        config, _endpointPollManager, closeHandler, disconnectedHandler,
        onActiveHandler, remoteHttpEndpointHandler);
  }

private:
  template <template <class> class LiveEndpointT>
  IHttpClientEndpointPtr createHttpClientEndpointImpl(
      const HttpEndpointConfig &config,
      HttpClientPayloadHandlerC auto &&payloadHandler,
      SocketPayloadHandlerC auto &&outgoingPayloadHandler,
      CloseHandlerT closeHandler, DisconnectedHandlerT disconnectHandler,
      OnActiveHandlerT onActiveHandler) {
    using IncomingHandlerT = std::decay_t<decltype(payloadHandler)>;
    using OutgoingHandlerT = std::decay_t<decltype(outgoingPayloadHandler)>;
    return std::make_unique<HTTPLiveClientEndpoint<LiveEndpointT>>(
        config, _endpointPollManager,
        std::forward<IncomingHandlerT>(payloadHandler),
        std::forward<OutgoingHandlerT>(outgoingPayloadHandler), closeHandler,
        disconnectHandler, onActiveHandler);
  }

  template <template <class> class LiveEndpointT>
  IWebSocketEndpointPtr createWSClientEndpointImpl(
      const HttpEndpointConfig &config,
      WebSocketPayloadHandlerC auto &&payloadHandler,
      WebSocketPayloadHandlerC auto &&outgoingPayloadHandler,
      CloseHandlerT closeHandler, DisconnectedHandlerT disconnectHandler,
      OnActiveHandlerT onActiveHandler) {

    using IncomingHandlerT = std::decay_t<decltype(payloadHandler)>;
    using OutgoingHandlerT = std::decay_t<decltype(outgoingPayloadHandler)>;
    return std::make_unique<
        WebSocketLiveEndpoint<HTTPLiveClientEndpoint, LiveEndpointT>>(
        config, _endpointPollManager,
        std::forward<IncomingHandlerT>(payloadHandler),
        std::forward<OutgoingHandlerT>(outgoingPayloadHandler), closeHandler,
        disconnectHandler, onActiveHandler);
  }

  sockets::IIPEndpointPollManager &_endpointPollManager;
};

// Stand alone generic constructable definitions of sockets that are hard coded
// with their std::function callable types to provide concrete
// definitions simplifying the creation of complex endpoint frameworks like
// connection pools, servers ...

using TcpEndpoint = live::TcpIpLiveEndpoint<SocketPayloadHandlerT>;

using SSLEndpoint = live::SSLLiveEndpoint<SocketPayloadHandlerT>;

using HttpClientEndpoint = HTTPLiveClientEndpoint<live::TcpIpLiveEndpoint>;

using HttpsClientEndpoint = HTTPLiveClientEndpoint<live::SSLLiveEndpoint>;

using HttpServerEndpoint = HTTPLiveServerEndpoint<live::TcpIpLiveEndpoint>;

using HttpsServerEndpoint = HTTPLiveServerEndpoint<live::SSLLiveEndpoint>;

template <template <class> class BaseHTTPEndpoint>
using HTTPLiveClientEndpointT = HTTPLiveClientEndpoint<BaseHTTPEndpoint>;

using WebSocketLiveClientEndpoint =
    WebSocketLiveEndpoint<HTTPLiveClientEndpointT, live::TcpIpLiveEndpoint>;
using WebSSocketLiveClientEndpoint =
    WebSocketLiveEndpoint<HTTPLiveClientEndpointT, live::SSLLiveEndpoint>;



using WebSocketLiveServerEndpoint =
    WebSocketLiveEndpoint<HTTPLiveServerEndpoint, live::TcpIpLiveEndpoint>;

using WebSSocketLiveServerEndpoint =
    WebSocketLiveEndpoint<HTTPLiveServerEndpoint, live::SSLLiveEndpoint>;
} // namespace medici::sockets::live