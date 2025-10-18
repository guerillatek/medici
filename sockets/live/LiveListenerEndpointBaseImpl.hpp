#pragma once

#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/interfaces.hpp"
#include "medici/sockets/live/EndpointBase.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"
namespace medici::sockets::live {

class LiveListenerEndpointBaseImpl
    : public IIPEndpoint,
      public EndpointBase<SocketPayloadHandlerT> {

  using NewListenerEndpointHandlerT =
      std::function<Expected(int, const IPEndpointConfig &)>;
  using EndpointBaseT = EndpointBase<SocketPayloadHandlerT>;

public:
  virtual ~LiveListenerEndpointBaseImpl() {
    if (!this->getConnectionManager().isConnected()) {
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
      : EndpointBaseT{ConnectionType::TCP,         config,
                      endpointPollManager,         SocketPayloadHandlerT{},
                      SocketPayloadHandlerT{},     listenerCloseHandler,
                      listenerDisconnectedHandler, listenerOnActiveHandler},
        _newListenerEndpointHandler{newEndpointHandler} {}

  const std::string &name() const override {
    return this->getConnectionManager().getConfig().name();
  }

  Expected openEndpoint() override {
    if (auto result = this->getConnectionManager().openListener(); !result) {
      return result;
    }

    return this->onActive();
  }

  Expected closeEndpoint(const std::string &reason) override {
    if (auto result = closeRemoteConnection(); !result) {
      return result;
    }
    if (auto result = _eventHandlers.onCloseEndpoint(
            reason, this->getConnectionManager().getConfig());
        !result) {
      return result;
    }
    return this->getConnectionManager().close();
  }

  bool isActive() const override {
    return this->getConnectionManager().isConnected();
  }

  IEndpointEventDispatch &getDispatchInterface() override { return *this; }

  // IEndpointEventDispatch
  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    _eventHandlers.registerTimer(timer);
    return {};
  }

  Expected onActive() override { return _eventHandlers.onActive(); }

  Expected onPayloadReady(TimePoint readTime) override {

    sockaddr_in remoteAddress{};
    socklen_t addrLen = sizeof(remoteAddress);
    int clientFd =
        accept(this->getConnectionManager().getSocketHandle(),
               reinterpret_cast<struct sockaddr *>(&remoteAddress), &addrLen);
    if (clientFd == -1) {
      return std::unexpected(std::format(
          "Failed to accept connection on listener endpoint, "
          "name={}, error={}",
          this->getConnectionManager().getConfig().name(), strerror(errno)));
    }
    char remoteIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &remoteAddress.sin_addr, remoteIp, sizeof(remoteIp));
    std::uint16_t remotePort = ntohs(remoteAddress.sin_port);
    IPEndpointConfig remoteConfig{
        this->getConnectionManager().getConfig().name() + "_remote-" +
            remoteIp + ":" + std::to_string(remotePort),
        remoteIp, remotePort,
        this->getConnectionManager().getConfig().recvBufferKB(),
        this->getConnectionManager().getConfig().interface()};
    return _newListenerEndpointHandler(clientFd, remoteConfig);
  }

  void resetConnection() { this->getConnectionManager().close(); }

  Expected onDisconnected(
      const std::string &reason,
      const medici::sockets::IPEndpointConfig &endpointConfig) override {
    resetConnection();
    return _eventHandlers.onDisconnected(reason, endpointConfig);
  }

  Expected onShutdown() {
    return _eventHandlers.onCloseEndpoint(
        std::format("Shutdown called, closing endpoint name={}",
                    this->getConnectionManager().getConfig().name()),
        this->getConfig());
  }

  const medici::ClockNowT &getClock() const override {
    return this->getConnectionManager().getClock();
  }

  std::uint64_t getEndpointUniqueId() const override {
    return *reinterpret_cast<const std::uint64_t *>(this);
  }

protected:
  Expected closeRemoteConnection() {
    if (!this->getConnectionManager().isConnected()) {
      return std::unexpected(
          std::format("Failed to close listening endpoint name={} reason='Not "
                      "currently open'",
                      this->getConnectionManager().getConfig().name()));
    }
    return {};
  }

  NewListenerEndpointHandlerT _newListenerEndpointHandler;
};

} // namespace medici::sockets::live