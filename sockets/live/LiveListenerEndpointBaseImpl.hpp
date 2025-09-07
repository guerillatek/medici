#pragma once

#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/interfaces.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

namespace medici::sockets::live {

class LiveListenerEndpointBaseImpl : public IIPEndpoint,
                                     public IIPEndpointDispatch {

  using SocketEventHandlers = IPEndpointHandlerCapture<SocketPayloadHandlerT>;
  using NewListenerEndpointHandlerT =
      std::function<Expected(int, const IPEndpointConfig &)>;

public:
  virtual ~LiveListenerEndpointBaseImpl() {
    if (!_connectionManager.isConnected()) {
      return;
    }
    closeRemoteConnection();
  }
  // Client side constructor
  LiveListenerEndpointBaseImpl(const IPEndpointConfig &config,
                               IIPEndpointPollManager &endpointPollManager,
                               NewListenerEndpointHandlerT newEndpointHandler,
                               CloseHandlerT listenerCloseHandler,
                               DisconnectedHandlerT listenerDisconnectedHandler,
                               OnActiveHandlerT listenerOnActiveHandler)
      : IIPEndpointDispatch{config}, _eventHandlers{config,
                                                    SocketPayloadHandlerT{},
                                                    SocketPayloadHandlerT{},
                                                    listenerCloseHandler,
                                                    listenerDisconnectedHandler,
                                                    listenerOnActiveHandler},
        _connectionManager{endpointPollManager, *this, ConnectionType::TCP},
        _newListenerEndpointHandler{std::move(newEndpointHandler)} {}

  const std::string &name() const override {
    return IIPEndpointDispatch::getConfig().name();
  }

  Expected openEndpoint() override {
    if (auto result = _connectionManager.openListener(); !result) {
      return result;
    }

    return this->onActive();
  }

  Expected closeEndpoint(const std::string &reason) override {
    if (auto result = closeRemoteConnection(); !result) {
      return result;
    }
    if (auto result = _eventHandlers.onCloseEndpoint(reason); !result) {
      return result;
    }
    return _connectionManager.close();
  }

  bool isActive() const override { return _connectionManager.isConnected(); }

  IIPEndpointDispatch &getDispatchInterface() override { return *this; }

  // IIPEndpointDispatch
  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    _eventHandlers.registerTimer(timer);
    return {};
  }

  Expected onActive() override { return _eventHandlers.onActive(); }

  Expected onPayloadReady(TimePoint readTime) override {

    sockaddr_in remoteAddress{};
    socklen_t addrLen = sizeof(remoteAddress);
    int clientFd =
        accept(_connectionManager.getSocketHandle(),
               reinterpret_cast<struct sockaddr *>(&remoteAddress), &addrLen);
    if (clientFd == -1) {
      return std::unexpected(std::format(
          "Failed to accept connection on listener endpoint, "
          "name={}, error={}",
          IIPEndpointDispatch::getConfig().name(), strerror(errno)));
    }
    char remoteIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &remoteAddress.sin_addr, remoteIp, sizeof(remoteIp));
    std::uint16_t remotePort = ntohs(remoteAddress.sin_port);
    IPEndpointConfig remoteConfig{
        IIPEndpointDispatch::getConfig().name() + "_remote-" + remoteIp + ":" +
            std::to_string(remotePort),
        remoteIp, remotePort, IIPEndpointDispatch::getConfig().inBufferKB(),
        IIPEndpointDispatch::getConfig().interface()};
    return _newListenerEndpointHandler(clientFd, remoteConfig);
  }

  void resetConnection() { _connectionManager.close(); }

  Expected onDisconnected(const std::string &reason) override {
    resetConnection();
    return _eventHandlers.onDisconnected(reason);
  }

  Expected onShutdown() {
    return _eventHandlers.onCloseEndpoint(
        std::format("Shutdown called, closing endpoint name={}",
                    IIPEndpointDispatch::getConfig().name()));
  }

  const medici::ClockNowT &getClock() const override {
    return _connectionManager.getClock();
  }

protected:
  Expected closeRemoteConnection() {
    if (!_connectionManager.isConnected()) {
      return std::unexpected(
          std::format("Failed to close listening endpoint name={} reason='Not "
                      "currently open'",
                      IIPEndpointDispatch::getConfig().name()));
    }
    return {};
  }

  SocketEventHandlers _eventHandlers;
  IPEndpointConnectionManager _connectionManager;
  NewListenerEndpointHandlerT _newListenerEndpointHandler;
};

} // namespace medici::sockets::live