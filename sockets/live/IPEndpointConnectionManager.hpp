#pragma once

#include "medici/sockets/interfaces.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>

namespace medici::sockets::live {
enum class ConnectionType { TCP, SSL, UDP, MCAST };

class IPEndpointConnectionManager {
public:
  IPEndpointConnectionManager(const IPEndpointConfig &,
                              IIPEndpointPollManager &,
                              IEndpointEventDispatch &, ConnectionType);

  IPEndpointConnectionManager(const IPEndpointConfig &, int fd,
                              IIPEndpointPollManager &,
                              IEndpointEventDispatch &, ConnectionType,
                              std::function<Expected()> onActive = {});
  // used by client endpoints
  Expected open();

  // used by listener endpoints
  Expected openListener();

  Expected send(std::string_view);
  ExpectedSize sendAsync(std::string_view);

  Expected setClosed();
  Expected close();
  bool isConnected() const { return _fd != 0; }

  auto getSocketHandle() const { return _fd; }

  auto &getConfig() const { return _config; }
  auto &getClock() const { return _endPointPollManager.getClock(); }

  auto &getEventQueue() { return _endPointPollManager.getEventQueue(); }

  auto getSSLCLientContext() {
    return _endPointPollManager.getSSLClientContext();
  }
  auto getSSLServerContext() {
    return _endPointPollManager.getSSLServerContext();
  }

  Expected registerWithEpoll() {
    ++_fdRegistrations;
    return _endPointPollManager.registerEndpoint(_fd, _endPointDispatch,
                                                 _config);
  }

  Expected registerWithEpoll(int fd) {
    _fd = fd;
    ++_fdRegistrations;
    return _endPointPollManager.listenerRegisterEndpoint(_fd, _endPointDispatch,
                                                         _config);
  }

  auto fdRegistrations() const { return _fdRegistrations; }

  ~IPEndpointConnectionManager() {
    if (_fd) {
      _endPointPollManager.removeEndpoint(_fd, _endPointDispatch, _config);
      ::close(_fd);
    }
  }

private:
  IPEndpointConfig _config;
  IIPEndpointPollManager &_endPointPollManager;
  IEndpointEventDispatch &_endPointDispatch;
  ConnectionType _connectionType;
  int _fd;
  int _fdRegistrations{0};
};

}; // namespace medici::sockets::live
