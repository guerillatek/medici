#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace medici::sockets::live {

namespace {

// gethostbyname()/gethostbyname2() return a pointer into a static buffer
// that is MT-Unsafe (race:hostbyname) - concurrent resolutions on other
// threads (e.g. one per exchange connection) can overwrite it while this
// thread is still reading it, silently swapping in another host's address.
// getaddrinfo() is reentrant, so copy the resolved addresses out up front
// rather than holding a pointer to shared state across the connect loop.
std::expected<std::vector<in_addr>, std::string>
resolveHostAddresses(const std::string &host) {
  struct addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *result = nullptr;
  int rc = getaddrinfo(host.c_str(), nullptr, &hints, &result);
  if (rc != 0) {
    return std::unexpected(
        std::format("Invalid remote host={}, {}", host, gai_strerror(rc)));
  }

  std::vector<in_addr> addresses;
  for (struct addrinfo *ai = result; ai != nullptr; ai = ai->ai_next) {
    addresses.push_back(reinterpret_cast<sockaddr_in *>(ai->ai_addr)->sin_addr);
  }
  freeaddrinfo(result);

  if (addresses.empty()) {
    return std::unexpected(std::format("Invalid remote host={}", host));
  }
  return addresses;
}

// Bound on how long a single address connect attempt may block waiting for
// the TCP handshake to complete, so an unroutable address (e.g. one not
// reachable over a bound VPN interface) fails fast instead of hanging on the
// kernel's default SYN retry timeout (~130s) before other addresses can be
// tried.
constexpr std::chrono::milliseconds kConnectTimeout{5000};

// A resolved config "interface" value: either a literal local IPv4 address
// to bind() to, or the name of a device to bind via SO_BINDTODEVICE. Device
// binding matters for interfaces like a split-tunnel VPN, where the routing
// table only sends traffic for the tunnel's own subnet out that device -
// binding merely to the device's address doesn't change the kernel's
// destination-based route selection, so traffic for anywhere else still
// silently egresses through the default interface instead.
struct ResolvedInterface {
  std::optional<in_addr> address;
  std::string deviceName; // empty when interfaceValue was a literal IP
};

std::expected<ResolvedInterface, std::string>
resolveInterfaceValue(const std::string &interfaceValue) {
  in_addr addr{};
  if (inet_pton(AF_INET, interfaceValue.c_str(), &addr) == 1) {
    return ResolvedInterface{addr, {}};
  }

  struct ifaddrs *ifaddrList = nullptr;
  if (getifaddrs(&ifaddrList) != 0) {
    return std::unexpected(std::format(
        "Failed to enumerate network interfaces, errno={}", strerror(errno)));
  }

  bool foundInterface = false;
  std::optional<in_addr> foundAddress;
  for (struct ifaddrs *ifa = ifaddrList; ifa != nullptr; ifa = ifa->ifa_next) {
    if (interfaceValue != ifa->ifa_name) {
      continue;
    }
    foundInterface = true;
    if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
      foundAddress = reinterpret_cast<sockaddr_in *>(ifa->ifa_addr)->sin_addr;
      break;
    }
  }
  freeifaddrs(ifaddrList);

  if (!foundInterface) {
    return std::unexpected(std::format("Unknown interface={}", interfaceValue));
  }
  return ResolvedInterface{foundAddress, interfaceValue};
}

// Binds `fd` to `deviceName` via SO_BINDTODEVICE if possible so packets
// egress that device regardless of the destination-based routing table;
// falls back to binding `fallbackAddress` (if any) when the process lacks
// the privilege for SO_BINDTODEVICE.
std::expected<void, std::string>
bindToInterface(int fd, const ResolvedInterface &interfaceInfo) {
  if (!interfaceInfo.deviceName.empty() &&
      setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
                 interfaceInfo.deviceName.c_str(),
                 interfaceInfo.deviceName.size() + 1) == 0) {
    return {};
  }
  if (!interfaceInfo.address) {
    return std::unexpected(
        std::format("Failed to bind to device, errno={}", strerror(errno)));
  }

  struct sockaddr_in localAddress{};
  localAddress.sin_family = AF_INET;
  localAddress.sin_addr = *interfaceInfo.address;
  localAddress.sin_port = 0;
  if (::bind(fd, reinterpret_cast<const sockaddr *>(&localAddress),
             sizeof(localAddress)) < 0) {
    return std::unexpected(std::format("errno={}", strerror(errno)));
  }
  return {};
}

// Drives a non-blocking connect() to completion (or failure) bounded by
// `timeout`, using select() to wait for the socket to become writable rather
// than busy-spinning connect() until the kernel gives up on its own.
std::expected<void, std::string>
connectWithTimeout(int fd, const sockaddr_in &remoteAddress,
                   std::chrono::milliseconds timeout) {
  if (connect(fd, reinterpret_cast<const sockaddr *>(&remoteAddress),
              sizeof(remoteAddress)) == 0) {
    return {};
  }
  if (errno != EINPROGRESS) {
    return std::unexpected(
        std::format("connect() failed, errno={}", strerror(errno)));
  }

  fd_set writeSet;
  FD_ZERO(&writeSet);
  FD_SET(fd, &writeSet);
  struct timeval tv;
  tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
  tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);

  int selectResult = select(fd + 1, nullptr, &writeSet, nullptr, &tv);
  if (selectResult == 0) {
    return std::unexpected("connect() timed out");
  }
  if (selectResult < 0) {
    return std::unexpected(std::format(
        "select() failed while connecting, errno={}", strerror(errno)));
  }

  int socketError = 0;
  socklen_t socketErrorLen = sizeof(socketError);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &socketErrorLen) < 0) {
    return std::unexpected(
        std::format("getsockopt(SO_ERROR) failed, errno={}", strerror(errno)));
  }
  if (socketError != 0) {
    return std::unexpected(
        std::format("connect() failed, errno={}", strerror(socketError)));
  }
  return {};
}

} // namespace

IPEndpointConnectionManager::IPEndpointConnectionManager(
    const IPEndpointConfig &config, IIPEndpointPollManager &endPointPollManager,
    IEndpointEventDispatch &endPointDispatch, ConnectionType connectionType)
    : _config{config}, _endPointPollManager{endPointPollManager},
      _endPointDispatch{endPointDispatch}, _connectionType{connectionType},
      _createTime{endPointPollManager.getClock()()} {}

IPEndpointConnectionManager::IPEndpointConnectionManager(
    const IPEndpointConfig &config, int fd,
    IIPEndpointPollManager &endPointPollManager,
    IEndpointEventDispatch &endPointDispatch, ConnectionType connectionType,
    std::function<Expected()> onActive)
    : _config{config}, _endPointPollManager{endPointPollManager},
      _endPointDispatch{endPointDispatch}, _connectionType{connectionType},
      _createTime{endPointPollManager.getClock()()}, _fd{fd} {

  int flag = 1;
  if (setsockopt(_fd, IPPROTO_TCP, SO_KEEPALIVE, (char *)&flag, sizeof(int)) <
      0) {
    throw std::runtime_error(std::format(
        "Failed to set socket 'Keep Alive' option errno={}, name={}",
        strerror(errno), _config.name()));
  }

  if (setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) <
      0) {
    throw std::runtime_error(
        std::format("Failed to set socket 'No Delay' option errno={}, name={}",
                    strerror(errno), _config.name()));
  }

  if (onActive) {
    endPointPollManager.listenerRegisterEndpoint(fd, endPointDispatch, _config);
    if (auto result = onActive(); !result) {
      throw std::runtime_error(result.error());
    }
  }
}

Expected IPEndpointConnectionManager::open() {

  auto resolvedAddresses = resolveHostAddresses(_config.host());
  if (!resolvedAddresses) {
    return std::unexpected(std::format("{}:{}, name={}",
                                       resolvedAddresses.error(),
                                       _config.port(), _config.name()));
  }

  std::optional<ResolvedInterface> interfaceInfo;
  if (!_config.interface().empty()) {
    auto resolved = resolveInterfaceValue(_config.interface());
    if (!resolved) {
      return std::unexpected(
          std::format("Failed to bind socket to interface={}, {}, name={}",
                      _config.interface(), resolved.error(), _config.name()));
    }
    interfaceInfo = *resolved;
  }

  // Creates a socket appropriate for `_connectionType`, non-blocking, bound
  // to the resolved interface address (if any) and sized per config.
  auto createSocket = [&]() -> std::expected<int, std::string> {
    int fd = -1;
    switch (_connectionType) {
    case ConnectionType::SSL:
    case ConnectionType::TCP: {
      if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
        return std::unexpected(
            std::format("Failed to create socket errno={}", strerror(errno)));
      }
      int flag = 1;
      if (setsockopt(fd, IPPROTO_TCP, SO_KEEPALIVE, &flag, sizeof(flag)) < 0) {
        ::close(fd);
        return std::unexpected(
            std::format("Failed to set socket 'Keep Alive' option errno={}",
                        strerror(errno)));
      }
      if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        ::close(fd);
        return std::unexpected(
            std::format("Failed to set socket 'No Delay' option errno={}",
                        strerror(errno)));
      }
      break;
    }
    case ConnectionType::UDP:
    case ConnectionType::MCAST: {
      if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        return std::unexpected(std::format(
            "Failed to create UDP socket errno={}", strerror(errno)));
      }
      break;
    }
    };

    int flags = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      ::close(fd);
      return std::unexpected(
          std::format("Failed to set non blocking, errno={}", strerror(errno)));
    }

    if (interfaceInfo) {
      if (auto bound = bindToInterface(fd, *interfaceInfo); !bound) {
        ::close(fd);
        return std::unexpected(
            std::format("Failed to bind socket to interface={}, {}",
                        _config.interface(), bound.error()));
      }
    }

    int inboundBufferSize = static_cast<int>(_config.recvBufferKB() * 1024);
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &inboundBufferSize,
               sizeof(inboundBufferSize));
    return fd;
  };

  switch (_connectionType) {
  case ConnectionType::TCP:
  case ConnectionType::SSL: {
    struct sockaddr_in remoteAddress{};
    remoteAddress.sin_family = AF_INET;
    remoteAddress.sin_port = htons(_config.port());

    // Try every resolved address in turn (bounded by kConnectTimeout each)
    // rather than only the first, since a given address may be unroutable
    // over a bound interface (e.g. a split-tunnel VPN) while another isn't.
    std::string lastError;
    _fd = 0;
    for (const auto &address : *resolvedAddresses) {
      remoteAddress.sin_addr = address;

      auto socketResult = createSocket();
      if (!socketResult) {
        lastError =
            std::format("{}, name={}", socketResult.error(), _config.name());
        continue;
      }

      if (auto connectResult =
              connectWithTimeout(*socketResult, remoteAddress, kConnectTimeout);
          !connectResult) {
        lastError =
            std::format("Failed to connect to remote host={}:{}, {}, name={}",
                        _config.host(), _config.port(), connectResult.error(),
                        _config.name());
        ::close(*socketResult);
        continue;
      }

      _fd = *socketResult;
      break;
    }

    if (_fd == 0) {
      return std::unexpected(lastError);
    }
  } break;
  case ConnectionType::MCAST: {
    auto socketResult = createSocket();
    if (!socketResult) {
      return std::unexpected(
          std::format("{}, name={}", socketResult.error(), _config.name()));
    }
    _fd = *socketResult;

    struct ip_mreq mreq;
    // Join the multicast group
    mreq.imr_multiaddr.s_addr = inet_addr(_config.host().c_str());
    mreq.imr_interface.s_addr =
        htonl(INADDR_ANY); // Use default network interface

    if (setsockopt(_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) ==
        -1) {
      return std::unexpected(
          std::format("Failed to set membership options on multicast "
                      "connection, errno={}, name={}",
                      strerror(errno), _config.name()));
    }
  } break;
  case ConnectionType::UDP: {
    auto socketResult = createSocket();
    if (!socketResult) {
      return std::unexpected(
          std::format("{}, name={}", socketResult.error(), _config.name()));
    }
    _fd = *socketResult;
  } break;
  };

  if (_connectionType != ConnectionType::SSL) {
    return _endPointPollManager.registerEndpoint(_fd, _endPointDispatch,
                                                 _config);
  }
  return {};
}

Expected IPEndpointConnectionManager::openListener() {

  switch (_connectionType) {
  case ConnectionType::SSL:
  case ConnectionType::TCP: {
    int flag = 1;
    if ((_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_IP)) < 0) {
      return std::unexpected(
          std::format("Failed to create socket errno={}, name={}",
                      strerror(errno), _config.name()));
    }
    if (setsockopt(_fd, IPPROTO_TCP, SO_KEEPALIVE, (char *)&flag, sizeof(int)) <
        0) {
      return std::unexpected(std::format(
          "Failed to set socket 'Keep Alive' option errno={}, name={}",
          strerror(errno), _config.name()));
    }

    if (setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int)) <
        0) {
      return std::unexpected(std::format(
          "Failed to set socket 'No Delay' option errno={}, name={}",
          strerror(errno), _config.name()));
    }

    // Set SO_REUSEADDR to avoid "address already in use" errors on restart
    int opt = 1;
    if (setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
      return std::unexpected(std::format(
          "Failed to set socket 'Reuse Address' option errno={}, name={}",
          strerror(errno), _config.name()));
    }
    break;
  }
  default: {
    return std::unexpected(std::format(
        "Cannot open listener endpoint for  name={}, must be TCP or SSL",
        _config.name()));
  }
  };

  int flags = fcntl(_fd, F_GETFL, 0);

  if (fcntl(_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return std::unexpected(
        std::format("Failed to set non blocking, errno={}, name={}",
                    strerror(errno), _config.name()));
  }

  // Set address
  struct sockaddr_in listenAddress;
  listenAddress.sin_family = AF_INET;
  listenAddress.sin_port = htons(_config.port());
  listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
  auto resolvedAddresses = resolveHostAddresses(_config.host());
  if (!resolvedAddresses) {
    return std::unexpected(std::format("{}:{}, name={}",
                                       resolvedAddresses.error(),
                                       _config.port(), _config.name()));
  }
  listenAddress.sin_addr = resolvedAddresses->front();

  // Bind to listen address and port
  if (bind(_fd, reinterpret_cast<sockaddr *>(&listenAddress),
           sizeof(listenAddress)) == -1) {
    return std::unexpected(
        std::format("Failed to bind socket to interface={}, errno={}, name={}",
                    _config.interface(), strerror(errno), _config.name()));
  }

  if (listen(_fd, SOMAXCONN) == -1) {
    return std::unexpected(
        std::format("Failed to listen on socket, errno={}, name={}",
                    strerror(errno), _config.name()));
  }

  return _endPointPollManager.registerEndpoint(_fd, _endPointDispatch, _config);
}

Expected IPEndpointConnectionManager::send(std::string_view payload) {
  while (payload.size() > 0) {
    ssize_t bytes_sent = ::send(_fd, payload.data(), payload.size(), 0);
    if (bytes_sent <= 0) {
      return std::unexpected(
          std::format("Failed to send payload on endpoint name={} ",
                      _config.name(), strerror(errno)));
    }
    payload.remove_prefix(bytes_sent);
  }
  return {};
}

ExpectedSize IPEndpointConnectionManager::sendAsync(std::string_view payload) {
  ssize_t bytes_sent = ::send(_fd, payload.data(), payload.size(), 0);
  if (bytes_sent <= 0) {
    return std::unexpected(
        std::format("Failed to send payload on endpoint name={} ",
                    _config.name(), strerror(errno)));
  }
  return bytes_sent;
}

Expected IPEndpointConnectionManager::setClosed() {
  _endPointPollManager.removeEndpoint(_fd, _endPointDispatch, _config);
  _fd = 0;
  return {};
}

Expected IPEndpointConnectionManager::close() {
  if (::close(_fd)) {
    return setClosed();
    return std::unexpected(
        std::format("Shutdown attempted close endpoint name={} ",
                    _config.name(), strerror(errno)));
  }

  return setClosed();
}

} // namespace medici::sockets::live