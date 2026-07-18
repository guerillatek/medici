#pragma once

#include "medici/sockets/live/EndpointBase.hpp"

#include <deque>

namespace medici::sockets::live {

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class TcpIpLiveEndpoint : public ITcpIpEndpoint,
                          protected EndpointBase<IncomingPayloadHandlerT> {
  using EndpointBaseT = EndpointBase<IncomingPayloadHandlerT>;
  static constexpr size_t kMaxBackpressureBytes = 100 * 1024;

  struct PendingAsyncSend {
    std::string payload;
    CallableT finishCB;
  };

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
            onActiveHandler} {
    EndpointBaseT::resizeInboundBuffer(config.recvBufferKB() * 1024);
  }

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
            onActiveHandler} {
    EndpointBaseT::resizeInboundBuffer(config.recvBufferKB() * 1024);
  }

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
    if (auto result = this->getEventHandlers().onCloseEndpoint(reason);
        !result) {
      return result;
    }
    return this->getConnectionManager().close();
  }

  bool isActive() const { return this->getConnectionManager().isConnected(); }

  Expected send(std::string_view payload) override {
    if (_asyncSendInProgress || !_pendingAsyncSends.empty()) {
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
      return this->onDisconnected(std::format(
          "No bytes sent to {}\n TCP connection appears to have dropped "
          "connection",
          this->getConfig().name()));
    }
    return {};
  }

  Expected sendAsync(std::string_view buffer, CallableT &&finishCB) override {
    if (auto backpressureCheck = verifyBackpressureLimit(buffer.size());
        !backpressureCheck) {
      return std::unexpected(backpressureCheck.error());
    }

    if (auto result = this->getEventHandlers().onPayloadSend(
            buffer, this->getConnectionManager().getClock()());
        !result) {
      return std::unexpected{result.error()};
    }

    if (_asyncSendInProgress) {
      _queuedBackpressureBytes += buffer.size();
      _pendingAsyncSends.emplace_back(
          PendingAsyncSend{std::string{buffer}, std::move(finishCB)});
      return {};
    }

    this->clearOutboundBuffer();
    std::copy(buffer.begin(), buffer.end(),
              std::back_inserter(this->getOutboundBuffer()));
    _activeFinishCB = std::move(finishCB);
    _asyncBytesSent = 0;
    _asyncSendInProgress = true;

    return this->getConnectionManager().getEventQueue().postAsyncAction(
        [this]() -> AsyncExpected { return processAsyncSendQueue(); });
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

  AsyncExpected processAsyncSendQueue() {
    if (!_asyncSendInProgress) {
      return true;
    }

    auto sendResult = sendAsyncCont();
    if (!sendResult) {
      return std::unexpected(sendResult.error());
    }
    if (!sendResult.value()) {
      return false;
    }

    if (_activeFinishCB) {
      auto result = _activeFinishCB();
      if (!result) {
        return std::unexpected(result.error());
      }
      _activeFinishCB = {};
    }
    _asyncSendInProgress = false;

    if (_pendingAsyncSends.empty()) {
      return true;
    }

    auto nextSend = std::move(_pendingAsyncSends.front());
    _pendingAsyncSends.pop_front();
    _queuedBackpressureBytes -= nextSend.payload.size();

    this->clearOutboundBuffer();
    std::copy(nextSend.payload.begin(), nextSend.payload.end(),
              std::back_inserter(this->getOutboundBuffer()));
    _activeFinishCB = std::move(nextSend.finishCB);
    _asyncBytesSent = 0;
    _asyncSendInProgress = true;
    return false;
  }

  Expected verifyBackpressureLimit(size_t incomingBytes) {
    const auto activeBackpressureBytes =
        _asyncSendInProgress
            ? (this->getOutboundBuffer().size() >= _asyncBytesSent
                   ? this->getOutboundBuffer().size() - _asyncBytesSent
                   : 0)
            : 0;
    const auto totalBackpressureBytes =
        _queuedBackpressureBytes + activeBackpressureBytes + incomingBytes;
    if (totalBackpressureBytes <= kMaxBackpressureBytes) {
      return {};
    }

    auto reason = std::format(
        "Backpressure queue limit exceeded on endpoint name={} total={} "
        "limit={}",
        this->getConfig().name(), totalBackpressureBytes,
        kMaxBackpressureBytes);
    clearAsyncSendState();
    if (auto disconnectResult = onDisconnected(reason); !disconnectResult) {
      return std::unexpected(disconnectResult.error());
    }
    return std::unexpected(reason);
  }

  void clearAsyncSendState() {
    this->clearOutboundBuffer();
    _pendingAsyncSends.clear();
    _activeFinishCB = {};
    _queuedBackpressureBytes = 0;
    _asyncBytesSent = 0;
    _asyncSendInProgress = false;
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
      return onDisconnected(strerror(errno));
    }
    return this->getEventHandlers().onPayloadRead(
        std::string_view{this->getInboundBuffer().data(),
                         static_cast<size_t>(bytesRead) +
                             EndpointBaseT::getAndClearPrependSize()},
        readTime);
  }

  Expected onDisconnected(const std::string &reason) {
    clearAsyncSendState();
    resetConnection();
    return this->getEventHandlers().onDisconnected(reason);
  }

  Expected onShutdown() {
    return this->getEventHandlers().onCloseEndpoint(std::format(
        "Shutdown called, closing endpoint name={}", this->getConfig().name()));
  }

  const medici::ClockNowT &getClock() const override {
    return this->getConnectionManager().getClock();
  }

  std::uint64_t getEndpointUniqueId() const override {
    return EndpointBaseT::getConnectionManager()
        .getCreationTime()
        .time_since_epoch()
        .count();
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
  size_t _queuedBackpressureBytes{0};
  bool _asyncSendInProgress{false};
  CallableT _activeFinishCB{};
  std::deque<PendingAsyncSend> _pendingAsyncSends{};
};
} // namespace medici::sockets::live