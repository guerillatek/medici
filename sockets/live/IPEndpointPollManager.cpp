#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <openssl/err.h>
#include <sys/epoll.h>
#include <thread>
#include <unistd.h>

namespace medici::sockets::live {

IPEndpointPollManager::IPEndpointPollManager(const std::string &name,
                                             const ClockNowT &clock)
    : _clock{clock}, _name{name} {}

IPEndpointPollManager::SSLContextPtr IPEndpointPollManager::initSSL() {

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

  const SSL_METHOD *const method = TLS_client_method();
  if (not method) {
    throw std::runtime_error("Failed to initialize SSL for endpoint poll "
                             "manager on call to TLS_client_method()");
  }

  auto result =
      SSLContextPtr{SSL_CTX_new(method), [](auto *ptr) { SSL_CTX_free(ptr); }};
  if (not _sslContext) {
    throw std::runtime_error("Failed to initialize SSL context poll manager "
                             "name={}, on call to TLS_client_method()");
  }
  return result;
}

IPEndpointPollManager::SSLContextPtr IPEndpointPollManager::_sslContext =
    IPEndpointPollManager::initSSL();

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

Expected
IPEndpointPollManager::registerEndpoint(int fd,
                                        IIPEndpointDispatch &endPointDispatch) {

  if (_registeredEndpoints.contains(fd)) {
    return std::unexpected(std::format("Failed to add endpoint name={} epoll "
                                       "to poll manager name={}, Already added",
                                       endPointDispatch.getConfig().name(),
                                       _name));
  }
  _registeredEndpoints[fd] = &endPointDispatch;
  epoll_event event;
  event.events = EPOLLIN | EPOLLRDHUP | EPOLLRDNORM;
  event.data.ptr = &endPointDispatch;
  if (::epoll_ctl(_epollHandle, EPOLL_CTL_ADD, fd, &event)) {
    return std::unexpected(std::format("Failed to add endpoint name={} epoll "
                                       "to poll manager name={}, errno={}",
                                       endPointDispatch.getConfig().name(),
                                       _name, strerror(errno)));
  }
  return {};
}

// Only one external thread can register endpoints at a time
//  This is used by listener endpoints to register new remote endpoints
Expected
IPEndpointPollManager::listenerRegisterEndpoint(int fd,
                                                IIPEndpointDispatch &dispatch) {
  while (!_pendingRemoteEndpoints.push(PendingRemoteEndpoint{fd, dispatch}))
    ;
  return {};
}

Expected
IPEndpointPollManager::removeEndpoint(int fd,
                                      IIPEndpointDispatch &endPointDispatch) {
  if (!_registeredEndpoints.contains(fd)) {
    return std::unexpected(
        std::format("Attempted remove endpoint name={} with unknown file "
                    "descriptor from poll manager name={}",
                    endPointDispatch.getConfig().name(), _name));
  }
  _registeredEndpoints.erase(fd);
  if (::epoll_ctl(_epollHandle, EPOLL_CTL_DEL, fd, nullptr)) {
    return std::unexpected(std::format(
        "Failed to remove endpoint name={} epoll from poll manager name={}, "
        "errno={}",
        endPointDispatch.getConfig().name(), _name, strerror(errno)));
  }

  return {};
}

Expected IPEndpointPollManager::shutdown() {
  if (_epollHandle == 0) {
    return {};
  }

  for (auto [fd, endpointDispatch] : _registeredEndpoints) {
    if (auto result = endpointDispatch->onShutdown(); !result) {
      return result;
    }

    if (epoll_ctl(_epollHandle, EPOLL_CTL_DEL, fd, NULL)) {
      return std::unexpected(std::format(
          "Shutdown attempted remove endpoint name={} from poll manager "
          "name={}, errno={}",
          endpointDispatch->getConfig().name(), _name, strerror(errno)));
    }

    if (::close(fd)) {
      return std::unexpected(std::format(
          "Shutdown attempted close endpoint name={} in poll manager name={}",
          endpointDispatch->getConfig().name(), _name, strerror(errno)));
    }
  }
  _registeredEndpoints.clear();
  _epollHandle = 0;
  return {};
}

ExpectedEventsCount IPEndpointPollManager::pollAndDispatchEndpointsEvents() {

  // Check to see if there are any pending remote endpoints to register
  //  register by a listener endpoint
  while (!_pendingRemoteEndpoints.empty()) {
    auto pendingRemoteEndpoint = _pendingRemoteEndpoints.front();
    _pendingRemoteEndpoints.pop();
    if (auto result = registerEndpoint(pendingRemoteEndpoint.fd,
                                       pendingRemoteEndpoint.dispatch);
        !result) {
      return std::unexpected(result.error());
    }

    if (auto result = pendingRemoteEndpoint.dispatch.onActive(); !result) {
      return std::unexpected(result.error());
    }
  }

  std::array<epoll_event, 100> events{};
  const auto nready =
      ::epoll_wait(_epollHandle, events.data(), std::size(events), 0);
  if (not nready) [[likely]] {
    return 0;
  }

  if (nready == -1) [[likely]] {
    if (errno == EINTR) [[likely]] {
      return 0;
    }
  }
  auto epollTS = _clock();

  for (int i = 0; i < nready; ++i) {
    const auto &event = events[i];
    if (event.events | EPOLLIN) {
      auto *const endpointDispatch =
          reinterpret_cast<IIPEndpointDispatch *>(event.data.ptr);
      auto handleResult = endpointDispatch->onPayloadReady(epollTS);
      if (!handleResult) {
        return std::unexpected(handleResult.error());
      }
    }
  }
  return nready;
}

std::expected<SSL_CTX *, std::string> IPEndpointPollManager::getSSLContext() {
  if (!_sslContext) {
    return std::unexpected(
        std::format("Failed to get SSL context from endpoint poll manager "
                    "name={}, reason='Not Initialized'",
                    _name, strerror(errno)));
  }
  return _sslContext.get();
}

IPEndpointPollManager::~IPEndpointPollManager() { shutdown(); }

} // namespace medici::sockets::live
