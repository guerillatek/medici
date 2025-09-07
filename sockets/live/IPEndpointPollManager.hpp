#pragma once

#include "medici/sockets/interfaces.hpp"
#include "medici/time.hpp"
#include <boost/lockfree/spsc_queue.hpp>

#include <arpa/inet.h>
#include <atomic>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <sys/socket.h>
#include <unordered_set>

namespace medici::sockets::live {

struct PendingRemoteEndpoint {
  int fd;
  IIPEndpointDispatch &dispatch;
};

class IPEndpointPollManager : public IIPEndpointPollManager {
public:
  IPEndpointPollManager(const std::string &name, const ClockNowT &clock);
  Expected registerEndpoint(int fd, IIPEndpointDispatch &) override;

  Expected listenerRegisterEndpoint(int fd,
                                    IIPEndpointDispatch &dispatch) override;

  Expected removeEndpoint(int fd, IIPEndpointDispatch &) override;
  Expected initialize() override;
  ExpectedEventsCount pollAndDispatchEndpointsEvents();
  Expected shutdown() override;
  ~IPEndpointPollManager();
  const ClockNowT &getClock() const override { return _clock; }

  ExpectedContext getSSLContext() override;

  using SSLContextPtr =
      std::unique_ptr<SSL_CTX, std::function<void(SSL_CTX *)>>;
  static SSLContextPtr initSSL();

private:
  static SSLContextPtr _sslContext;
  std::unordered_map<int, IIPEndpointDispatch *> _registeredEndpoints{};
  ClockNowT _clock;
  int _epollHandle{0};
  std::string _name;

  using PendingRemoteEndpoints =
      boost::lockfree::spsc_queue<PendingRemoteEndpoint,
                                  boost::lockfree::capacity<1000>>;
                                  
  PendingRemoteEndpoints _pendingRemoteEndpoints;
};

}; // namespace medici::sockets::live
