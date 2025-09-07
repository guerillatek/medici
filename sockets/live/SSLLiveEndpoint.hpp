#pragma once

#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <functional>
#include <memory>
#include <openssl/err.h>
#include <openssl/ssl.h>

namespace medici::sockets::live {

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class SSLLiveEndpoint : public ITcpIpEndpoint, public IIPEndpointDispatch {

  using SocketEventHandlers = IPEndpointHandlerCapture<SocketPayloadHandlerT>;

public:
  static constexpr bool usingCipher() { return true; }

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
      : IIPEndpointDispatch{config},
        _eventHandlers{
            config,
            std::forward<IncomingPayloadHandlerT>(incomingPayloadHandler),
            outgoingPayloadHandler,
            closeHandler,
            disconnectedHandler,
            onActiveHandler},
        _connectionManager{endpointPollManager, *this, ConnectionType::SSL} {
    _inboundBuffer.resize(config.inBufferKB() * 1024);
  }

  SSLLiveEndpoint(int fd, const IPEndpointConfig &config,
                  IIPEndpointPollManager &endpointPollManager,
                  IncomingPayloadHandlerT &&incomingPayloadHandler,
                  SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                  CloseHandlerT closeHandler,
                  DisconnectedHandlerT disconnectedHandler,
                  OnActiveHandlerT onActiveHandler)
      : IIPEndpointDispatch{config},
        _eventHandlers{
            config,
            std::forward<IncomingPayloadHandlerT>(incomingPayloadHandler),
            outgoingPayloadHandler,
            closeHandler,
            disconnectedHandler,
            onActiveHandler},
        _connectionManager{fd, endpointPollManager, *this, ConnectionType::SSL},
        _clientHandshakePending{true} {
    auto sslContextResult = _connectionManager.getSSLContext();
    if (!sslContextResult) {
      throw std::runtime_error(sslContextResult.error());
    }
    _sslSocket = SSL_SocketPtr{SSL_new(sslContextResult.value()),
                               [](auto *ptr) { SSL_free(ptr); }};
    SSL_set_tlsext_host_name(_sslSocket.get(),
                             IIPEndpointDispatch::getConfig().host().c_str());
    if (SSL_set_fd(_sslSocket.get(), fd) != 1) {
      throw std::runtime_error(
          std::format("SSL context failed to set file decscriptor, name={}",
                      IIPEndpointDispatch::getConfig().name()));
    }
    _connectionManager.registerWithEpoll(fd);
  }

  // ITcpIpEndpoint
  const std::string &name() const {
    return IIPEndpointDispatch::getConfig().name();
  }

  Expected openEndpoint() override {
    if (isActive()) {
      return std::unexpected(
          std::format("Endpoint {} is already open", name()));
    }

    auto sslContextResult = _connectionManager.getSSLContext();
    if (!sslContextResult) {
      return std::unexpected(sslContextResult.error());
    }

    if (auto result = _connectionManager.open(); !result) {
      return result;
    }
    _sslSocket = SSL_SocketPtr{SSL_new(sslContextResult.value()),
                               [](auto *ptr) { SSL_free(ptr); }};
    SSL_set_tlsext_host_name(_sslSocket.get(),
                             IIPEndpointDispatch::getConfig().host().c_str());
    if (SSL_set_fd(_sslSocket.get(), _connectionManager.getSocketHandle()) !=
        1) {
      _connectionManager.close();
      return std::unexpected(
          std::format("SSL context failed to set file decscriptor, name={}",
                      IIPEndpointDispatch::getConfig().name()));
    }

    while (true) {
      auto rc = SSL_connect(_sslSocket.get());
      if (rc == 1) {
        break;
      }
      if (rc <= 0) {
        if (errno == EAGAIN)
          continue;
        std::string errorMessage;
        const auto ssl_rc = SSL_get_error(_sslSocket.get(), rc);
        if (ssl_rc == SSL_ERROR_SSL) {
          char msg[1024];
          ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
          errorMessage = msg;
        } else {
          errorMessage = strerror(errno);
        }
        return std::unexpected(
            std::format("SSL_connect handshake failed on endpoint, name={}, "
                        "host={}, port={}, msg={}",
                        IIPEndpointDispatch::getConfig().name(),
                        IIPEndpointDispatch::getConfig().host(),
                        IIPEndpointDispatch::getConfig().port(), errorMessage));
      }
    }
    _sslConnected = true;
    if (auto result = _connectionManager.registerWithEpoll(); !result) {
      return result;
    }

    return this->onActive();
  }

  Expected closeEndpoint(const std::string &reason) override {

    if (!_sslSocket || !isActive()) {
      return std::unexpected(std::format(
          "Failed to close ssl endpoint name={} reason='Not currently open'",
          IIPEndpointDispatch::getConfig().name()));
    }

    if (auto result = _eventHandlers.onCloseEndpoint(reason); !result) {
      return result;
    }
    return closeRemoteConnection();
  }

  IIPEndpointDispatch &getDispatchInterface() override { return *this; }

  bool isActive() const { return _sslConnected; }

  // IIPEndpointDispatch
  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    _eventHandlers.registerTimer(timer);
    return {};
  }

  Expected onActive() { return _eventHandlers.onActive(); }

  Expected resetConnection() {
    SSL_shutdown(_sslSocket.get());
    _sslSocket.reset();
    _sslConnected = false;
    return _connectionManager.setClosed();
  }

  Expected send(std::string_view payload) {
    if (!_sslSocket) {
      return std::unexpected(
          std::format("Failed send payload {} on inactive endpoint, name={}, "
                      "host={}, port={}",
                      payload, IIPEndpointDispatch::getConfig().name(),
                      IIPEndpointDispatch::getConfig().host(),
                      IIPEndpointDispatch::getConfig().port()));
    }
    if (auto result = _eventHandlers.onPayloadSend(
            payload, _connectionManager.getClock()());
        !result) {
      return result;
    }
    std::size_t offset = 0;
    while (true) {
      auto bytesWritten = SSL_write(_sslSocket.get(), payload.data() + offset,
                                    std::size(payload) - offset);
      if (bytesWritten < 0) [[unlikely]] {
        return onDisconnected("No bytes written on ssl read ... server likely "
                              "dropped connection");
      }
      offset += bytesWritten;
      if (offset < std::size(payload))
        continue;
      return {};
    }
    return {};
  }

  Expected onPayloadReady(TimePoint readTime) override {
    return onPayloadReady(readTime, _inboundBuffer);
  }

  Expected onPayloadReady(TimePoint readTime, auto &inboundBuffer) {
    if (_clientHandshakePending) [[unlikely]] {
      if (int ret = SSL_do_handshake(_sslSocket.get()); ret != 1) [[unlikely]] {
        if (ret < 0) {
          int sslError = SSL_get_error(_sslSocket.get(), ret);
          if (sslError == SSL_ERROR_WANT_READ ||
              sslError == SSL_ERROR_WANT_WRITE) {
            return {};
          }
          _connectionManager.close();
          _sslSocket.reset();
          _sslConnected = false;
          return onDisconnected(
              std::format("SSL handshake failed for endpoint, name={}, "
                          "error={}",
                          IIPEndpointDispatch::getConfig().name(),
                          ERR_reason_error_string(ERR_get_error())));
        }
      }
      _clientHandshakePending = false;
      return onActive();
    }
    size_t bytesRead = 0;
    size_t bufferSize = inboundBuffer.size();

    const auto rc = SSL_read_ex(_sslSocket.get(), inboundBuffer.data(),
                                bufferSize, &bytesRead);
    if (rc == 1) [[likely]] {
      if (bytesRead == 0) {
        if (_sslConnected == false) {
          return {};
        }
        SSL_shutdown(_sslSocket.get());
        _connectionManager.close();
        _sslSocket.reset();
        _sslConnected = false;
        return onDisconnected(
            "No bytes read on ssl read ... server likely dropped connection");
      }

      // BIO *bio = BIO_new_fp(stdout, BIO_NOCLOSE);// Use stdout for output
      // if (bio) {
      //  Dump the buffer content in hex and ASCII
      // BIO_dump(bio, _inboundBuffer.data(), static_cast<int>(bytesRead));
      // Free the BIO
      //  BIO_free(bio);
      //  }

      return _eventHandlers.onPayloadRead(
          std::string_view{_inboundBuffer.data(), bytesRead}, readTime);
    }

    std::string errorMessage;
    const auto ssl_rec = SSL_get_error(_sslSocket.get(), rc);
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
    _connectionManager.close();
    _sslSocket.reset();
    _sslConnected = false;
    auto message = std::format(
        "SSL_read_ex failed on endpoint, name={}, host={}, port={}, msg={}",
        IIPEndpointDispatch::getConfig().name(),
        IIPEndpointDispatch::getConfig().host(),
        IIPEndpointDispatch::getConfig().port(), errorMessage);
    return onDisconnected(message);
  }

  Expected onDisconnected(const std::string &reason) override {
    auto result = resetConnection();
    if (!result) {
      if (!result) {
        return std::unexpected(
            std::format("Failed to reset connection on endpoint, name={}, "
                        "reason={}",
                        IIPEndpointDispatch::getConfig().name(), reason));
      }
    }

    return _eventHandlers.onDisconnected(reason);
  }

  Expected onShutdown() {
    return closeEndpoint(
        std::format("Shutdown called, closing endpoint name={}",
                    IIPEndpointDispatch::getConfig().name()));
  }

  const medici::ClockNowT &getClock() const override {
    return _connectionManager.getClock();
  }

protected:
  auto &getInboundBuffer() const { return _inboundBuffer; }

  Expected closeRemoteConnection() {
    if (!_sslSocket) {
      return std::unexpected(std::format(
          "Failed to close ssl endpoint name={} reason='Not currently open'",
          IIPEndpointDispatch::getConfig().name()));
    }
    _sslConnected = false;

    while (true) {
      int shutdownResult = SSL_shutdown(_sslSocket.get());
      if ((shutdownResult == 0) && (errno == EAGAIN)) {
        continue;
      }
      if (shutdownResult < 0) {
        _sslSocket.reset();
        _connectionManager.close();
        const auto ssl_rc = SSL_get_error(_sslSocket.get(), shutdownResult);
        if (ssl_rc == SSL_ERROR_SSL) {
          char msg[1024];
          ERR_error_string_n(ERR_get_error(), msg, sizeof(msg));
          return std::unexpected(
              std::format("SSL_shutdown failed on endpoint, name={}, msg={}",
                          IIPEndpointDispatch::getConfig().name(), msg));
        }
      }
    }
    _sslSocket.reset();
    return _connectionManager.close();
  }

  bool _sslConnected{false};
  using SSL_SocketPtr = std::unique_ptr<ssl_st, std::function<void(ssl_st *)>>;
  SSL_SocketPtr _sslSocket;
  std::vector<char> _inboundBuffer{};
  SocketEventHandlers _eventHandlers;
  IPEndpointConnectionManager _connectionManager;
  bool _clientHandshakePending{false};
};
} // namespace medici::sockets::live