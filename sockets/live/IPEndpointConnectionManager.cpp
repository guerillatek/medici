#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace medici::sockets::live {

IPEndpointConnectionManager::IPEndpointConnectionManager(
    const IPEndpointConfig &config, IIPEndpointPollManager &endPointPollManager,
    IEndpointEventDispatch &endPointDispatch, ConnectionType connectionType)
    : _config{config}, _endPointPollManager{endPointPollManager},
      _endPointDispatch{endPointDispatch}, _connectionType{connectionType} {}

IPEndpointConnectionManager::IPEndpointConnectionManager(
    const IPEndpointConfig &config, int fd,
    IIPEndpointPollManager &endPointPollManager,
    IEndpointEventDispatch &endPointDispatch, ConnectionType connectionType,
    std::function<Expected()> onActive)
    : _config{config}, _endPointPollManager{endPointPollManager},
      _endPointDispatch{endPointDispatch}, _connectionType{connectionType},
      _fd{fd} {

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

  switch (_connectionType) {
  case ConnectionType::SSL:
  case ConnectionType::TCP: {
    int flag = 1;
    if ((_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
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
    break;
  }
  case ConnectionType::UDP:
  case ConnectionType::MCAST: {
    if ((_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      return std::unexpected(
          std::format("Failed to create UDP socket errno={}, name={}",
                      strerror(errno), _config.name()));
    }
    break;
  }
  };

  int flags = fcntl(_fd, F_GETFL, 0);

  if (fcntl(_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return std::unexpected(
        std::format("Failed to set non blocking, errno={}, name={}",
                    strerror(errno), _config.name()));
  }

  // Set address
  struct sockaddr_in remoteAddress;
  remoteAddress.sin_family = AF_INET;
  remoteAddress.sin_port = htons(_config.port());
  struct hostent *const he = gethostbyname(_config.host().c_str());
  if (not he) {
    return std::unexpected(std::format(
        "Invalid remote host={}:{}, errno={}, name={}", _config.host(),
        _config.port(), strerror(errno), _config.name()));
  }
  memcpy(&remoteAddress.sin_addr.s_addr, he->h_addr_list[0], 4);

  // Bind to interface if specifed in the config
  if (!_config.interface().empty()) {
    if (::bind(_fd, (const sockaddr *)&remoteAddress, sizeof(remoteAddress)) <
        9) {
      return std::unexpected(std::format(
          "Failed to bind socket to interface={}, errno={}, name={}",
          _config.interface(), strerror(errno), _config.name()));
    }
  }

  int inboundBufferSize = static_cast<int>(_config.recvBufferKB() * 1024);
  setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &inboundBufferSize,
             sizeof(inboundBufferSize));

  switch (_connectionType) {
  case ConnectionType::TCP:
  case ConnectionType::SSL: {
    int result =
        connect(_fd, (struct sockaddr *)&remoteAddress, sizeof(remoteAddress));
    while (result < 0) {
      switch (errno) {
      case EINPROGRESS:
      case EAGAIN:
      case EALREADY:
        break;
      default:
        return std::unexpected(std::format(
            "Failed to connect to remote host={}:{}, errno={}, name={}",
            _config.host(), _config.port(), strerror(errno), _config.name()));
      };
      result = connect(_fd, (struct sockaddr *)&remoteAddress,
                       sizeof(remoteAddress));
    }

  } break;
  case ConnectionType::MCAST: {
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
  }
  case ConnectionType::UDP:
    break; // No extra step for these connections
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
  struct hostent *const he = gethostbyname(_config.host().c_str());
  if (not he) {
    return std::unexpected(std::format(
        "Invalid remote host={}:{}, errno={}, name={}", _config.host(),
        _config.port(), strerror(errno), _config.name()));
  }
  memcpy(&listenAddress.sin_addr.s_addr, he->h_addr_list[0], 4);

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