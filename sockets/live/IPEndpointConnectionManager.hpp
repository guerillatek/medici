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
  IPEndpointConnectionManager(IIPEndpointPollManager &, IIPEndpointDispatch &,
                              ConnectionType);

  IPEndpointConnectionManager(int fd, IIPEndpointPollManager &,
                              IIPEndpointDispatch &, ConnectionType,
                              std::function<Expected()> onActive = {});
  // used by client endpoints
  Expected open();

  // used by listener endpoints
  Expected openListener();

  Expected send(std::string_view);
  Expected setClosed();
  Expected close();
  bool isConnected() const { return _fd != 0; }

  auto getSocketHandle() const { return _fd; }

  auto &getClock() const { return _endPointPollManager.getClock(); }

  auto getSSLContext() { return _endPointPollManager.getSSLContext(); }

  Expected registerWithEpoll() {
    ++_fdRegistrations;
    return _endPointPollManager.registerEndpoint(_fd, _endPointDispatch);
  }

  Expected registerWithEpoll(int fd) {
    _fd = fd;
    ++_fdRegistrations;
    return _endPointPollManager.listenerRegisterEndpoint(_fd,
                                                         _endPointDispatch);
  }

  auto fdRegistrations() const { return _fdRegistrations; }

private:
  IIPEndpointPollManager &_endPointPollManager;
  IIPEndpointDispatch &_endPointDispatch;
  ConnectionType _connectionType;
  int _fd;
  int _fdRegistrations{0};
};

}; // namespace medici::sockets::live
