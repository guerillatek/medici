#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <openssl/err.h>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

namespace medici::sockets::live {

bool IPEndpointPollManager::initSSL() {

  if (OpenSSL_add_all_algorithms() != 1) {
    throw std::runtime_error("Failed to initialize SSL for endpoint poll "
                             "manager on call to OpenSSL_add_all_algorithms()");
  }
  if (SSL_load_error_strings() != 1) {
    // throw std::runtime_error("Failed to initialize SSL for endpoint poll
    // manager on call to SSL_load_error_strings()");
  }
  if (ERR_load_crypto_strings() != 1) {
    throw std::runtime_error("Failed to initialize SSL for endpoint poll "
                             "manager on call to ERR_load_crypto_strings()");
  }
  if (SSL_library_init() != 1) {
    throw std::runtime_error("Failed to initialize SSL for endpoint poll "
                             "manager on call to SSL_library_init()");
  }

  const SSL_METHOD *const serverMethod = TLS_client_method();
  if (not serverMethod) {
    throw std::runtime_error("Failed to initialize SSL for endpoint poll "
                             "manager on call to TLS_client_method()");
  }

  return true;
}

SSL_CTX *IPEndpointPollManager::initSSLClientContext() {

  auto *const context = SSL_CTX_new(TLS_client_method());
  if (not context) {
    throw std::runtime_error(
        "Failed to initialize SSL client context for poll manager "
        "name={}, on call to TLS_client_method()");
  }

  return context;
}

SSL_CTX *
IPEndpointPollManager::initSSLServerContext(const std::string &certFile,
                                            const std::string &keyFile) {

  if (certFile.empty() || keyFile.empty()) {
    throw std::runtime_error(
        "Failed to initialize SSL server context for poll manager "
        "reason='Certificate file or key file not provided'");
  }

  auto *const context = SSL_CTX_new(TLS_server_method());
  if (not context) {
    throw std::runtime_error(
        "Failed to initialize SSL server context for poll manager "
        "name={}, on call to TLS_server_method()");
  }

  if (SSL_CTX_use_certificate_file(context, certFile.c_str(),
                                   SSL_FILETYPE_PEM) <= 0) {
    throw std::runtime_error(
        std::format("Failed to load certificate file for SSL server context {}",
                    strerror(errno)));
  }

  if (SSL_CTX_use_PrivateKey_file(context, keyFile.c_str(), SSL_FILETYPE_PEM) <=
      0) {
    throw std::runtime_error(
        std::format("Failed to load private key file for SSL server context {}",
                    strerror(errno)));
  }

  return context;
}

bool IPEndpointPollManager::_initialized = IPEndpointPollManager::initSSL();

Expected IPEndpointPollManager::initialize() {

  _epollHandle = epoll_create1(EPOLL_CLOEXEC);
  if (_epollHandle == -1) {
    return std::unexpected(
        std::format("Failed to create epoll handle for endpoint poll manager "
                    "name={}, errno={}",
                    _name, strerror(errno)));
  }
  return {};
}

Expected IPEndpointPollManager::registerEndpoint(
    int fd, IEndpointEventDispatch &endPointDispatch,
    const IPEndpointConfig &config) {

  if (_registeredEndpoints.contains(fd)) {
    return std::unexpected(std::format("Failed to add endpoint name={} epoll "
                                       "to poll manager name={}, Already added",
                                       config.name(), _name));
  }
  auto registered = _registeredEndpoints.emplace(
      fd, RegisteredEndpointEntry{endPointDispatch, config});
  epoll_event event;
  event.events = EPOLLIN | EPOLLRDHUP | EPOLLRDNORM;
  event.data.ptr = &(registered.first->second);
  if (::epoll_ctl(_epollHandle, EPOLL_CTL_ADD, fd, &event)) {
    return std::unexpected(std::format("Failed to add endpoint name={} epoll "
                                       "to poll manager name={}, errno={}",
                                       config.name(), _name, strerror(errno)));
  }
  return {};
}

Expected IPEndpointPollManager::listenerRegisterEndpoint(
    int fd, IEndpointEventDispatch &dispatch, const IPEndpointConfig &config) {
  return _eventQueue.postAction([this, fd, &dispatch, config]() -> Expected {
    _pendingRemoteEndpoints.push_back(
        PendingRemoteEndpointEntry{fd, dispatch, config});
    return {};
  });
}

Expected
IPEndpointPollManager::removeEndpoint(int fd,
                                      IEndpointEventDispatch &endPointDispatch,
                                      const IPEndpointConfig &config) {
  if (!_registeredEndpoints.contains(fd)) {
    return std::unexpected(
        std::format("Attempted remove endpoint name={} with unknown file "
                    "descriptor from poll manager name={}",
                    config.name(), _name));
  }
  _registeredEndpoints.erase(fd);
  if (::epoll_ctl(_epollHandle, EPOLL_CTL_DEL, fd, nullptr)) {
    return std::unexpected(std::format(
        "Failed to remove endpoint name={} epoll from poll manager name={}, "
        "errno={}",
        config.name(), _name, strerror(errno)));
  }

  return {};
}

Expected IPEndpointPollManager::shutdown() {
  if (_epollHandle == 0) {
    return {};
  }

  for (auto [fd, endpoint] : _registeredEndpoints) {
    if (auto result = endpoint.dispatch.onShutdown(); !result) {
      return result;
    }

    if (epoll_ctl(_epollHandle, EPOLL_CTL_DEL, fd, NULL)) {
      return std::unexpected(std::format(
          "Shutdown attempted remove endpoint name={} from poll manager "
          "name={}, errno={}",
          endpoint.config.name(), _name, strerror(errno)));
    }

    if (::close(fd)) {
      return std::unexpected(std::format(
          "Shutdown attempted close endpoint name={} in poll manager name={}",
          endpoint.config.name(), _name, strerror(errno)));
    }
  }
  _registeredEndpoints.clear();
  _epollHandle = 0;
  return {};
}

ExpectedEventsCount IPEndpointPollManager::pollAndDispatchEndpointsEvents() {

  // Check to see if there are any pending remote endpoints to register
  //  register by a listener endpoint
  for (auto &[fd, dispatch, config] : _pendingRemoteEndpoints) {

    if (auto result = registerEndpoint(fd, dispatch, config); !result) {
      return std::unexpected(result.error());
    }

    if (auto result = dispatch.onActive(); !result) {
      return std::unexpected(result.error());
    }
  }
  _pendingRemoteEndpoints.clear();

  std::array<epoll_event, 100> events{};
  const auto nready =
      ::epoll_wait(_epollHandle, events.data(), std::size(events), 0);
  if (not nready) [[likely]] {
    return 0;
  }

  if (nready == -1) {
    switch (errno) {
    case EINTR:
      // Interrupted by signal - retry is safe
      return 0;

    case EBADF:
    case EINVAL:
      // Check for invalid fds in registered endpoints and dispatch disconnects
      for (const auto &[fd, endpoint] : _registeredEndpoints) {
        struct epoll_event dummy;
        int result = ::epoll_ctl(fd, EPOLL_CTL_ADD, -1, &dummy);

        if (result == -1) {
          if (errno == EBADF) {
            auto result =
                endpoint.dispatch.onDisconnected("Connection dropped");
            if (!result) {
              return std::unexpected(result.error());
            }
            return 0;
          }
          // Other errors (like EINVAL for fd -1) are expected
        }
      }
      break;
    case EFAULT:
      return std::unexpected(std::format(
          "epoll_wait failed: Invalid events buffer in poll manager {}",
          _name));

    default:
      return std::unexpected(
          std::format("epoll_wait failed in poll manager {}: {} (errno={})",
                      _name, strerror(errno), errno));
    }
  }
  auto epollTS = _clock();

  for (int i = 0; i < nready; ++i) {
    const auto &event = events[i];
    auto registeredEndpoint =
        reinterpret_cast<RegisteredEndpointEntry *>(event.data.ptr);
    if (event.events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
      // Handle disconnect
      auto disconnectResult =
          registeredEndpoint->dispatch.onDisconnected("Connection dropped");
      if (!disconnectResult) {
        return std::unexpected(disconnectResult.error());
      }
      continue;
    }
    if (event.events | EPOLLIN) {

      auto handleResult = registeredEndpoint->dispatch.onPayloadReady(epollTS);
      if (!handleResult) {
        return std::unexpected(handleResult.error());
      }
    }
  }
  return nready;
}

IPEndpointPollManager::~IPEndpointPollManager() { shutdown(); }

IPEndpointPollManager::SSLContextPtr &
IPEndpointPollManager::getSSLClientContextStatic() {
  if (!_initialized) {
    throw std::runtime_error(
        std::format("Failed to get SSL context from endpoint poll manager "
                    "reason='SSL Not Initialized'",
                    strerror(errno)));
  }
  static SSLContextPtr sslClientContext{initSSLClientContext(),
                                        [](SSL_CTX *ctx) {
                                          if (ctx) {
                                            SSL_CTX_free(ctx);
                                          }
                                        }};
  return sslClientContext;
}

//  This will only be called during construction of the IPEndpointPollManager.
//  While multiple poll managers can exist and run on multiple threads, because
//  of the absence of thread safety in this function they must all be
//  constructed on the same thread. This is not an issue when using the
//  AppRunConfigManager. However, in test environments and other special use
//  cases where AppRunConfigManager is not used, care must be taken to ensure
//  this restriction and that only one poll manager is configured with a
//  certFile and keyFile. Once it is set any attempts to set it again will be
//  ignored.
IPEndpointPollManager::SSLContextPtr &
IPEndpointPollManager::getSSLServerContextStatic(const std::string &certFile,
                                                 const std::string &keyFile) {
  if (!_initialized) {
    throw std::runtime_error(std::format(
        "Failed to get SSL context from endpoint poll manager  reason ="
        "'SSL Not Initialized' ",
        strerror(errno)));
  }
  static SSLContextPtr sslServerContext{nullptr, [](SSL_CTX *ctx) {
                                          if (ctx) {
                                            SSL_CTX_free(ctx);
                                          }
                                        }};
  if (sslServerContext || (certFile.empty() && keyFile.empty())) {
    return sslServerContext;
  }
  sslServerContext.reset(initSSLServerContext(certFile, keyFile));
  return sslServerContext;
}

} // namespace medici::sockets::live
