#pragma once

#include "medici/sockets/RemoteEndpointListener.hpp"
#include "medici/sockets/live/WebSocketLiveEndpoint.hpp"

namespace medici::sockets {

template <template <class> class SocketEndpointT,
          application::IPAppRunContextC ThreadRunContextT,
          typename ExtendedContextData = NoExtendedContextData>
class WSServerHandler {
public:
  using WSServerEndpointT =
      live::WebSocketLiveEndpoint<live::HTTPLiveServerEndpoint,
                                  SocketEndpointT>;
  using RemoteEndpointListenerT =
      RemoteEndpointListener<WSServerEndpointT, ThreadRunContextT,
                             ExtendedContextData>;
  using RemoteEndpointContextT =
      typename RemoteEndpointListenerT::RemoteEndpointContextT;

  WSServerHandler(ThreadRunContextT &serverThreadContext,
                  const WSEndpointConfig &listenEndpoint)
      : _serverEndpointListener{
            serverThreadContext,
            listenEndpoint,
            [this](const std::string &reason) {
              return handleClosedListener(reason, _listenEndpointConfig);
            },
            [this](const std::string &reason) {
              return handleDisconnectedListener(reason, _listenEndpointConfig);
            },
            [this]() { return handleListenerActive(); },
            [this](std::string_view payload, WSOpCode opCode, TimePoint tp) {
              return handleRemoteWebSocketPayload(
                  payload, opCode, tp,
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            },
            [this](std::string_view payload, WSOpCode opCode, TimePoint tp) {
              return handleOutgoingWebSocketPayload(
                  payload, opCode, tp,
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            },
            [this](const std::string &reason) {
              auto &endpointCoordinator =
                  _serverEndpointListener.getEndpointCoordinator();
              auto result = handleClosedRemoteListener(
                  reason, endpointCoordinator.getActiveContext());
              endpointCoordinator.removeEndpoint(
                  endpointCoordinator.getActiveContext()
                      .getEndpoint()
                      .getEndpointUniqueId());
              return result;
            },
            [this](const std::string &reason) {
              auto &endpointCoordinator =
                  _serverEndpointListener.getEndpointCoordinator();
              auto result = handleDisconnectedRemoteListener(
                  reason, endpointCoordinator.getActiveContext());
              endpointCoordinator.removeEndpoint(
                  endpointCoordinator.getActiveContext()
                      .getEndpoint()
                      .getEndpointUniqueId());
              return result;
            },
            [this]() {
              return handleRemoteListenerActive(
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            }},
        _listenEndpointConfig{listenEndpoint} {}

  Expected start() { return _serverEndpointListener.start(); }

  auto &getListenEndpointConfig() const { return _listenEndpointConfig; }

  auto &getThreadRunContext() {
    return _serverEndpointListener.getThreadRunContext();
  }

  Expected forEachRemoteEndpoint(auto &&callback) {
    return _serverEndpointListener.forEachRemoteEndpoint(
        std::forward<decltype(callback)>(callback));
  }

protected:
  virtual Expected handleListenerActive() { return {}; }

  virtual Expected handleClosedListener(const std::string &reason,
                                        const IPEndpointConfig &endpoint) {
    return {};
  }

  virtual Expected
  handleDisconnectedListener(const std::string &reason,
                             const IPEndpointConfig &endpoint) {
    return {};
  }

  virtual Expected
  handleRemoteWebSocketPayload(std::string_view payload, WSOpCode opCode,
                               TimePoint tp,
                               RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  virtual Expected handleOutgoingWebSocketPayload(
      std::string_view payload, WSOpCode opCode, TimePoint tp,
      RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  virtual Expected
  handleClosedRemoteListener(const std::string &reason,
                             RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  virtual Expected handleDisconnectedRemoteListener(
      const std::string &reason,
      RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  virtual Expected
  handleRemoteListenerActive(RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  RemoteEndpointListenerT _serverEndpointListener;
  WSEndpointConfig _listenEndpointConfig;
};

} // namespace medici::sockets
