#pragma once

#include "medici/sockets/live/EndpointBase.hpp"

namespace medici::sockets::live {

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class TcpIpLiveEndpoint : public ITcpIpEndpoint,
                          protected EndpointBase<IncomingPayloadHandlerT> {
  using EndpointBaseT = EndpointBase<IncomingPayloadHandlerT>;

public:
  ~TcpIpLiveEndpoint() {
    if (!this->getConnectionManager().isConnected()) {
      return;
    }
    closeRemoteConnection();
  }
  // Client side constructor
  TcpIpLiveEndpoint(const IPEndpointConfig &config,
                    IIPEndpointPollManager &endpointPollManager,
                    IncomingPayloadHandlerT &&incomingPayloadHandler,
                    SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                    CloseHandlerT closeHandler,
                    DisconnectedHandlerT DisconnectedHandlerT,
                    OnActiveHandlerT onActiveHandler)
      : EndpointBaseT{
            ConnectionType::TCP,
            config,
            endpointPollManager,
            std::forward<IncomingPayloadHandlerT>(incomingPayloadHandler),
            outgoingPayloadHandler,
            closeHandler,
            DisconnectedHandlerT,
            onActiveHandler} {}

  // Server side constructor
  TcpIpLiveEndpoint(int fd, const IPEndpointConfig &config,
                    IIPEndpointPollManager &endpointPollManager,
                    IncomingPayloadHandlerT &&incomingPayloadHandler,
                    SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                    CloseHandlerT closeHandler,
                    DisconnectedHandlerT DisconnectedHandlerT,
                    OnActiveHandlerT onActiveHandler)
      : EndpointBaseT{
            fd,
            ConnectionType::TCP,
            config,
            endpointPollManager,
            std::forward<IncomingPayloadHandlerT>(incomingPayloadHandler),
            outgoingPayloadHandler,
            closeHandler,
            DisconnectedHandlerT,
            onActiveHandler} {}

  // ITcpIpEndpoint
  const std::string &name() const { return this->getConfig().name(); }
  Expected openEndpoint() {
    if (auto result = this->getConnectionManager().open(); !result) {
      return result;
    }

    return this->onActive();
  }

  Expected closeEndpoint(const std::string &reason) {
    if (auto result = closeRemoteConnection(); !result) {
      return result;
    }
    if (auto result =
            this->getEventHandlers().onCloseEndpoint(reason, this->getConfig());
        !result) {
      return result;
    }
    return this->getConnectionManager().close();
  }

  bool isActive() const { return this->getConnectionManager().isConnected(); }

  Expected send(std::string_view payload) override {
    if (!this->getOutboundBuffer().empty()) {
      return std::unexpected(
          std::format("Endpoint name={} already has an async send in progress",
                      this->getConfig().name()));
    }
    if (auto result = this->getEventHandlers().onPayloadSend(
            payload, this->getConnectionManager().getClock()());
        !result) {
      return result;
    }
    auto bytesSentResult = this->getConnectionManager().send(payload);
    if (!bytesSentResult) {
      return this->onDisconnected(
          std::format(
              "No bytes sent to {}\n TCP connection appears to have dropped "
              "connection",
              this->getConfig().name()),
          this->getConfig());
    }
    return {};
  }

  Expected sendAsync(std::string_view buffer, CallableT &&finishCB) override {
    if (!this->getOutboundBuffer().empty()) {
      return std::unexpected(
          std::format("Endpoint name={} already has an async send in progress",
                      this->getConfig().name()));
    }
    std::copy(buffer.begin(), buffer.end(),
              std::back_inserter(this->getOutboundBuffer()));

    if (auto result = this->getEventHandlers().onPayloadSend(
            buffer, this->getConnectionManager().getClock()());
        !result) {
      return std::unexpected{result.error()};
    }

    return this->getConnectionManager().getEventQueue().postAsyncAction(
        [this, finishCB]() -> AsyncExpected {
          auto sendResult = sendAsyncCont();
          if (!sendResult) {
            return std::unexpected(sendResult.error());
          }
          if (sendResult.value()) {
            if (finishCB) {
              auto result = finishCB();
              if (!result) {
                return std::unexpected(result.error());
              }
            }
            return true;
          }
          return false;
        });
  }

  AsyncExpected sendAsyncCont() {
    if (this->getOutboundBuffer().empty()) {
      return std::unexpected(
          std::format("Endpoint name={} has no async send in progress",
                      this->getConfig().name()));
    }
    auto result = this->getConnectionManager().sendAsync(
        std::string_view{this->getOutboundBuffer().data() + _asyncBytesSent,
                         this->getOutboundBuffer().size() - _asyncBytesSent});
    if (!result) {
      return std::unexpected(result.error());
    }
    _asyncBytesSent += result.value();
    if (_asyncBytesSent == this->getOutboundBuffer().size()) {
      this->clearOutboundBuffer();
      _asyncBytesSent = 0;
      return true;
    }
    return false;
  }

  void resetConnection() { this->getConnectionManager().close(); }

  Expected onPayloadReady(TimePoint readTime) override {

    auto bytesRead = read(this->getConnectionManager().getSocketHandle(),
                          this->getInboundBufferWritePos(),
                          this->getInboundBufferAvailableSize());
    if (bytesRead <= 0) {
      if (!this->getConnectionManager().isConnected()) {
        return {};
      }
      return onDisconnected(strerror(errno), this->getConfig());
    }
    return this->getEventHandlers().onPayloadRead(
        std::string_view{this->getInboundBufferWritePos(),
                         static_cast<size_t>(bytesRead)},
        readTime);
  }

  Expected onDisconnected(const std::string &reason,
                          const IPEndpointConfig &endpointConfig) {
    resetConnection();
    return this->getEventHandlers().onDisconnected(reason, endpointConfig);
  }

  Expected onShutdown() {
    return this->getEventHandlers().onCloseEndpoint(
        std::format("Shutdown called, closing endpoint name={}",
                    this->getConfig().name()),
        this->getConfig());
  }

  const medici::ClockNowT &getClock() const override {
    return this->getConnectionManager().getClock();
  }

  int getEndpointUniqueId() const override {
    return this->getConnectionManager().getSocketHandle();
  }

  IEndpointEventDispatch &getDispatchInterface() override { return *this; }

protected:
  Expected closeRemoteConnection() {
    if (!this->getConnectionManager().isConnected()) {
      return std::unexpected(
          std::format("Failed to close tcpIp endpoint name={} reason='Not "
                      "currently open'",
                      this->getConfig().name()));
    }
    return {};
  }
  size_t _asyncBytesSent{0};
};
} // namespace medici::sockets::live