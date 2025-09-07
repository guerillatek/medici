#pragma once

#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

namespace medici::sockets::live {

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class TcpIpLiveEndpoint : public ITcpIpEndpoint, public IIPEndpointDispatch {

  using SocketEventHandlers = IPEndpointHandlerCapture<IncomingPayloadHandlerT>;

public:
  ~TcpIpLiveEndpoint() {
    if (!_connectionManager.isConnected()) {
      return;
    }
    closeRemoteConnection();
  }
  static constexpr bool usingCipher() { return false; }
  // Client side constructor
  TcpIpLiveEndpoint(const IPEndpointConfig &config,
                    IIPEndpointPollManager &endpointPollManager,
                    IncomingPayloadHandlerT &&incomingPayloadHandler,
                    SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                    CloseHandlerT closeHandler,
                    DisconnectedHandlerT DisconnectedHandlerT,
                    OnActiveHandlerT onActiveHandler)
      : IIPEndpointDispatch{config},
        _eventHandlers{
            config,
            std::forward<IncomingPayloadHandlerT>(incomingPayloadHandler),
            outgoingPayloadHandler,
            closeHandler,
            DisconnectedHandlerT,
            onActiveHandler},
        _connectionManager{endpointPollManager, *this, ConnectionType::TCP} {
    _inboundBuffer.resize(config.inBufferKB() * 1024);
  }

  // Server side constructor
  TcpIpLiveEndpoint(int fd, const IPEndpointConfig &config,
                    IIPEndpointPollManager &endpointPollManager,
                    IncomingPayloadHandlerT &&incomingPayloadHandler,
                    SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                    CloseHandlerT closeHandler,
                    DisconnectedHandlerT DisconnectedHandlerT,
                    OnActiveHandlerT onActiveHandler)
      : IIPEndpointDispatch{config},
        _eventHandlers{
            config,
            std::forward<IncomingPayloadHandlerT>(incomingPayloadHandler),
            outgoingPayloadHandler,
            closeHandler,
            DisconnectedHandlerT,
            onActiveHandler},
        _connectionManager{fd, endpointPollManager, *this, ConnectionType::TCP,
                           [this]() { return onActive(); }} {
    _inboundBuffer.resize(config.inBufferKB() * 1024);
  }

  // ITcpIpEndpoint
  const std::string &name() const {
    return IIPEndpointDispatch::getConfig().name();
  }
  Expected openEndpoint() {
    if (auto result = _connectionManager.open(); !result) {
      return result;
    }

    return this->onActive();
  }

  Expected closeEndpoint(const std::string &reason) {
    if (auto result = closeRemoteConnection(); !result) {
      return result;
    }
    if (auto result = _eventHandlers.onCloseEndpoint(reason); !result) {
      return result;
    }
    return _connectionManager.close();
  }

  bool isActive() const { return _connectionManager.isConnected(); }

  IIPEndpointDispatch &getDispatchInterface() override { return *this; }

  // IIPEndpointDispatch
  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    _eventHandlers.registerTimer(timer);
    return {};
  }

  Expected onActive() { return _eventHandlers.onActive(); }

  Expected send(std::string_view payload) {
    if (auto result = _eventHandlers.onPayloadSend(
            payload, _connectionManager.getClock()());
        !result) {
      return result;
    }
    return _connectionManager.send(payload);
  }

  Expected onPayloadReady(TimePoint readTime) override {
    return onPayloadReady(readTime, _inboundBuffer);
  }

  void resetConnection() { _connectionManager.close(); }

  Expected onPayloadReady(TimePoint readTime, auto &inboundBuffer) {
    auto bytesRead = read(_connectionManager.getSocketHandle(),
                          inboundBuffer.data(), inboundBuffer.size());
    if (bytesRead <= 0) {
      if (!_connectionManager.isConnected()) {
        return {};
      }
      return onDisconnected(strerror(errno));
    }
    return _eventHandlers.onPayloadRead(
        std::string_view{inboundBuffer.data(), static_cast<size_t>(bytesRead)},
        readTime);
  }

  Expected onDisconnected(const std::string &reason) {
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
      return std::unexpected(std::format(
          "Failed to close tcpIp endpoint name={} reason='Not currently open'",
          IIPEndpointDispatch::getConfig().name()));
    }
    return {};
  }

  auto &getInboundBuffer() const { return _inboundBuffer; }

  std::vector<char> _inboundBuffer{};
  SocketEventHandlers _eventHandlers;
  IPEndpointConnectionManager _connectionManager;
};
} // namespace medici::sockets::live