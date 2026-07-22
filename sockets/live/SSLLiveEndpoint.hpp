#pragma once

#include "medici/sockets/live/EndpointBase.hpp"

#include <deque>
#include <functional>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace medici::sockets::live {

enum class SSLState {
  Disconnected,
  ConnectingToRemote,
  RemoteClientConnecting,
  Connected,
};

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class SSLLiveEndpoint : public ITcpIpEndpoint,
                        protected EndpointBase<IncomingPayloadHandlerT> {
  using EndpointBaseT = EndpointBase<IncomingPayloadHandlerT>;
  static constexpr size_t kMaxBackpressureBytes = 10 * 1024 * 1024;

  struct PendingAsyncSend {
    std::string payload;
    CallableT finishCB;
  };

public:
  ~SSLLiveEndpoint() {
    if (!_sslSocket) {
      return;
    }
    closeRemoteConnection();
  }

  SSLLiveEndpoint(const IPEndpointConfig &config,
                  IIPEndpointPollManager &endpointPollManager,
                  IncomingPayloadHandlerT &&incomingPayloadHandler,
                  SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                  CloseHandlerT closeHandler,
                  DisconnectedHandlerT disconnectedHandler,
                  OnActiveHandlerT onActiveHandler)
      : EndpointBaseT{ConnectionType::SSL,
                      config,
                      endpointPollManager,
                      std::forward<IncomingPayloadHandlerT>(
                          incomingPayloadHandler),
                      outgoingPayloadHandler,
                      closeHandler,
                      disconnectedHandler,
                      onActiveHandler},
        _sslState{SSLState::Disconnected} {
    EndpointBaseT::resizeInboundBuffer(1024 *
                                       17); // SSL_Read only delivers 16KB at a
                                            // time. Extra 1K for prepended data
  }

  SSLLiveEndpoint(int fd, const IPEndpointConfig &config,
                  IIPEndpointPollManager &endpointPollManager,
                  IncomingPayloadHandlerT &&incomingPayloadHandler,
                  SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                  CloseHandlerT closeHandler,
                  DisconnectedHandlerT disconnectedHandler,
                  OnActiveHandlerT onActiveHandler)
      : EndpointBaseT{fd,
                      ConnectionType::SSL,
                      config,
                      endpointPollManager,
                      std::forward<IncomingPayloadHandlerT>(
                          incomingPayloadHandler),
                      outgoingPayloadHandler,
                      closeHandler,
                      disconnectedHandler,
                      onActiveHandler},
        _sslState{SSLState::RemoteClientConnecting} {
    EndpointBaseT::resizeInboundBuffer(1024 *
                                       17); // SSL_Read only delivers 16KB at a
                                            // time. Extra 1K for prepended data
    auto sslContextResult = this->getConnectionManager().getSSLServerContext();
    if (!sslContextResult) {
      throw std::runtime_error(sslContextResult.error());
    }

    _sslSocket = SSL_SocketPtr{SSL_new(sslContextResult.value()),
                               [](auto *ptr) { SSL_free(ptr); }};
    SSL_set_tlsext_host_name(_sslSocket.get(),
                             this->getConfig().host().c_str());
    if (SSL_set_fd(_sslSocket.get(), fd) != 1) {
      throw std::runtime_error(
          std::format("SSL context failed to set file descriptor, name={}",
                      this->getConfig().name()));
    }
    if (auto result = this->getConnectionManager().registerWithEpoll();
        !result) {
      throw std::runtime_error(result.error());
    }
    SSL_set_accept_state(_sslSocket.get());
    auto result = this->getConnectionManager().getEventQueue().postAsyncAction(
        [this]() { return HandleSSLHandshake(); });
    if (!result) {
      throw std::runtime_error(result.error());
    }
    _sslState = SSLState::RemoteClientConnecting;
  }

  // ITcpIpEndpoint
  const std::string &name() const { return this->getConfig().name(); }

  event_queue::AsyncExpected HandleSSLHandshake() {
    if (!_sslSocket) {
      return true;
    }
    if (int ret = SSL_do_handshake(_sslSocket.get()); ret != 1) [[unlikely]] {
      int sslError = SSL_get_error(_sslSocket.get(), ret);
      if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
        return {};
      }
      std::string activeTask;
      switch (_sslState) {
      case SSLState::ConnectingToRemote:
        activeTask = "connecting to remote server";
        break;
      case SSLState::RemoteClientConnecting:
        activeTask = "accepting incoming client connection";
        break;
      default:
        break;
      };
      this->getConnectionManager().close();
      _sslState = SSLState::Disconnected;
      auto sslErrReason = ERR_reason_error_string(ERR_get_error());
      std::string failReason;
      if (sslErrReason) {
        failReason = sslErrReason;
      }
      std::string failStatement =
          std::format("SSL handshake failed {} for endpoint, name={}, "
                      "error={}",
                      activeTask, this->getConfig().name(),
                      (!failReason.empty() ? failReason : "unknown"));
      _sslSocket.reset();
      if (auto result = onDisconnected(failStatement); !result) {
        return std::unexpected(result.error());
      }
      return true;
    }
    _sslState = SSLState::Connected;
    this->getConnectionManager().registerWithEpoll();
    if (auto result = this->onActive(); !result) {
      return std::unexpected(result.error());
    }
    return true;
  }

  Expected openEndpoint() override {
    if (isActive()) {
      return std::unexpected(
          std::format("Endpoint {} is already open", name()));
    }

    auto sslContextResult = this->getConnectionManager().getSSLCLientContext();
    if (!sslContextResult) {
      return std::unexpected(sslContextResult.error());
    }

    if (auto result = this->getConnectionManager().open(); !result) {
      return result;
    }
    _sslSocket = SSL_SocketPtr{SSL_new(sslContextResult.value()),
                               [](auto *ptr) { SSL_free(ptr); }};
    SSL_set_tlsext_host_name(_sslSocket.get(),
                             this->getConfig().host().c_str());
    if (SSL_set_fd(_sslSocket.get(),
                   this->getConnectionManager().getSocketHandle()) != 1) {
      this->getConnectionManager().close();
      return std::unexpected(
          std::format("SSL context failed to set file decscriptor, name={}",
                      this->getConfig().name()));
    }
    SSL_set_connect_state(_sslSocket.get());
    _sslState = SSLState::ConnectingToRemote;
    return this->getConnectionManager().getEventQueue().postAsyncAction(
        [this]() { return HandleSSLHandshake(); });
  }

  Expected closeEndpoint(const std::string &reason) override {

    if (!_sslSocket || !isActive()) {
      return std::unexpected(std::format(
          "Failed to close ssl endpoint name={} reason='Not currently open'",
          this->getConfig().name()));
    }

    if (auto result = this->getEventHandlers().onCloseEndpoint(reason);
        !result) {
      return result;
    }
    return closeRemoteConnection();
  }

  bool isActive() const { return _sslState == SSLState::Connected; }
  // IEndpointDispatch

  Expected resetConnection() {
    SSL_shutdown(_sslSocket.get());
    _sslSocket.reset();
    _sslConnected = false;
    return this->getConnectionManager().setClosed();
  }

  Expected send(std::string_view payload) {
    if (!_sslSocket) {
      return std::unexpected(
          std::format("Failed send payload {} on inactive endpoint, name={}, "
                      "host={}, port={}",
                      payload, this->getConfig().name(),
                      this->getConfig().host(), this->getConfig().port()));
    }
    if (_asyncSendInProgress) {
      return sendAsync(payload, []() { return Expected{}; });
    }

    if (auto result = this->getEventHandlers().onPayloadSend(
            payload, this->getConnectionManager().getClock()());
        !result) {
      return result;
    }
    std::size_t offset = 0;
    while (offset < std::size(payload)) {
      const int bytesWritten =
          SSL_write(_sslSocket.get(), payload.data() + offset,
                    static_cast<int>(std::size(payload) - offset));
      if (bytesWritten > 0) {
        offset += static_cast<std::size_t>(bytesWritten);
        continue;
      }

      const int sslError = SSL_get_error(_sslSocket.get(), bytesWritten);
      if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
        // Non-blocking socket backpressure: keep retrying until write can
        // progress.
        continue;
      }

      char msg[1024];
      ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
      return onDisconnected(std::format(
          "SSL_write failed on endpoint, name={}, host={}, port={}, msg={}",
          this->getConfig().name(), this->getConfig().host(),
          this->getConfig().port(), msg));
    }
    return {};
  }

  Expected sendAsync(std::string_view buffer, CallableT &&finishCB) {
    if (auto backpressureCheck = verifyBackpressureLimit(buffer.size());
        !backpressureCheck) {
      return closeEndpoint(backpressureCheck.error());
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

    int bytesWritten = SSL_write(
        _sslSocket.get(), this->getOutboundBuffer().data() + _asyncBytesSent,
        this->getOutboundBuffer().size() - _asyncBytesSent);

    if (bytesWritten <= 0) [[unlikely]] {
      const int sslError = SSL_get_error(_sslSocket.get(), bytesWritten);
      if (sslError == SSL_ERROR_WANT_READ || sslError == SSL_ERROR_WANT_WRITE) {
        return false;
      }

      char msg[1024];
      ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
      auto result = onDisconnected(std::format(
          "SSL_write failed on endpoint, name={}, host={}, port={}, msg={}",
          this->getConfig().name(), this->getConfig().host(),
          this->getConfig().port(), msg));
      if (!result) {
        return std::unexpected(result.error());
      }
      return true;
    }

    _asyncBytesSent += bytesWritten;
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

  Expected onPayloadReady(TimePoint readTime) override {

    int readRc = 0;
    while (true) {
      size_t bytesRead = 0;
      readRc = SSL_read_ex(_sslSocket.get(), this->getInboundBufferWritePos(),
                           this->getInboundBufferAvailableSize(), &bytesRead);
      if (readRc == 1) [[likely]] {
        if (bytesRead == 0) {
          SSL_shutdown(_sslSocket.get());
          this->getConnectionManager().close();
          _sslSocket.reset();
          _sslState = SSLState::Disconnected;
          return onDisconnected(
              "No bytes read on ssl read ... remote connection "
              "dropped connection");
        }

        // BIO *bio = BIO_new_fp(stdout, BIO_NOCLOSE);// Use stdout for output
        // if (bio) {
        //  Dump the buffer content in hex and ASCII
        // BIO_dump(bio, _inboundBuffer.data(), static_cast<int>(bytesRead));
        // Free the BIO
        //  BIO_free(bio);
        //  }
        auto preparedPayload = std::string_view{
            this->getInboundBuffer().data(),
            bytesRead + EndpointBaseT::getAndClearPrependSize()};
        auto result =
            this->getEventHandlers().onPayloadRead(preparedPayload, readTime);
        if (!result) {
          return result;
        }
        if (_sslSocket.get() == nullptr) {
          return {}; // Disconnected during payload processing
        }
        int pending = SSL_pending(_sslSocket.get());
        if (pending == 0) {
          return {}; // No more data to read
        }
      } else {
        // Handle SSL return codes
        break;
      }
    }

    std::string errorMessage;
    const auto ssl_rec = SSL_get_error(_sslSocket.get(), readRc);
    switch (ssl_rec) {
    case SSL_ERROR_WANT_READ: {
      return {};
    } break;

    case SSL_ERROR_WANT_RETRY_VERIFY: {
      errorMessage = "SSL_ERROR_WANT_RETRY_VERIFY";
    } break;

    case SSL_ERROR_ZERO_RETURN: {
      errorMessage = "SSL_ERROR_ZERO_RETURN";
    } break;

    case SSL_ERROR_SYSCALL: {
      errorMessage = "SSL_ERROR_SYSCALL";
    } break;

    default: {
      char msg[1024];
      ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
      errorMessage = msg;
    } break;

    case SSL_ERROR_NONE: {
      return {};
    } break;
    };
    this->getConnectionManager().close();
    _sslSocket.reset();
    _sslConnected = false;
    auto message = std::format(
        "SSL_read_ex failed on endpoint, name={}, host={}, port={}, msg={}",
        this->getConfig().name(), this->getConfig().host(),
        this->getConfig().port(), errorMessage);
    return onDisconnected(message);
  }

  Expected onDisconnected(const std::string &reason) override {
    clearAsyncSendState();
    auto result = resetConnection();
    if (!result) {
      if (!result) {
        return std::unexpected(
            std::format("Failed to reset connection on endpoint, name={}, "
                        "reason={}",
                        this->getConfig().name(), reason));
      }
    }

    return this->getEventHandlers().onDisconnected(reason);
  }

  Expected onShutdown() {
    return closeEndpoint(std::format(
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
    if (!_sslSocket) {
      return std::unexpected(std::format(
          "Failed to close ssl endpoint name={} reason='Not currently open'",
          this->getConfig().name()));
    }
    _sslConnected = false;

    return this->getConnectionManager().getEventQueue().postAsyncAction(
        [this]() -> event_queue::AsyncExpected {
          int shutdownResult = SSL_shutdown(_sslSocket.get());
          if ((shutdownResult == 0) && (errno == EAGAIN)) {
            return false; // Keep trying
          }
          if (shutdownResult < 0) {
            const auto ssl_rc = SSL_get_error(_sslSocket.get(), shutdownResult);
            _sslSocket.reset();
            if (ssl_rc == SSL_ERROR_SSL) {
              char msg[1024];
              ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
              return std::unexpected(std::format(
                  "SSL_shutdown failed on endpoint, name={}, msg={}",
                  this->getConfig().name(), msg));
            }
          }
          _sslState = SSLState::Disconnected;
          _sslSocket.reset();
          if (auto result = this->getConnectionManager().close(); !result) {
            return std::unexpected(result.error());
          }
          return true; // Done
        });
  }

  bool _sslConnected{false};
  SSLState _sslState{SSLState::Disconnected};
  using SSL_SocketPtr = std::unique_ptr<ssl_st, std::function<void(ssl_st *)>>;
  SSL_SocketPtr _sslSocket;
  bool _clientHandshakePending{false};
  size_t _asyncBytesSent{0};
  size_t _queuedBackpressureBytes{0};
  bool _asyncSendInProgress{false};
  CallableT _activeFinishCB{};
  std::deque<PendingAsyncSend> _pendingAsyncSends{};
};
} // namespace medici::sockets::live