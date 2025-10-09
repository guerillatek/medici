#pragma once

#include "medici/event_queue/concepts.hpp"
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

struct RegisteredEndpointEntry {
  IEndpointEventDispatch &dispatch;
  IPEndpointConfig config;
};

struct PendingRemoteEndpointEntry {
  int fd;
  IEndpointEventDispatch &dispatch;
  IPEndpointConfig config;
};

class IPEndpointPollManager : public IIPEndpointPollManager {
public:
  IPEndpointPollManager(const std::string &name, const ClockNowT &clock,
                        event_queue::IEventQueue &eventQueue,
                        const std::string &certFile = "",
                        const std::string &keyFile = "")
      : _clock{clock}, _name{name}, _eventQueue{eventQueue},
        _certFile{certFile}, _keyFile{keyFile} {
    if (!certFile.empty() && !keyFile.empty()) {
      auto &context = getSSLServerContextStatic(certFile, keyFile);
      if (!context) {
        throw std::runtime_error(
            std::format("Failed to create SSL server context for poll manager "
                        "name={}, reason='Invalid certificate or key file'",
                        name));
      }
    }
  };
  Expected registerEndpoint(int fd, IEndpointEventDispatch &,
                            const IPEndpointConfig &) override;

  Expected listenerRegisterEndpoint(int fd, IEndpointEventDispatch &dispatch,
                                    const IPEndpointConfig &) override;

  Expected removeEndpoint(int fd, IEndpointEventDispatch &,
                          const IPEndpointConfig &) override;
  Expected initialize() override;
  ExpectedEventsCount pollAndDispatchEndpointsEvents();
  Expected shutdown() override;
  ~IPEndpointPollManager();
  const ClockNowT &getClock() const override { return _clock; }

  ExpectedContext getSSLClientContext() override {
    return getSSLClientContextStatic().get();
  }

  ExpectedContext getSSLServerContext() override {
    return getSSLServerContextStatic().get();
  }

  using SSLContextPtr =
      std::unique_ptr<SSL_CTX, std::function<void(SSL_CTX *)>>;

  event_queue::IEventQueue &getEventQueue() override { return _eventQueue; }

private:
  static bool initSSL();
  static SSLContextPtr &getSSLClientContextStatic();
  static SSLContextPtr &
  getSSLServerContextStatic(const std::string &certFile = "",
                            const std::string &keyFile = "");
  static SSL_CTX *initSSLClientContext();
  static SSL_CTX *initSSLServerContext(const std::string &certFile,
                                       const std::string &keyFile);

  std::unordered_map<int, RegisteredEndpointEntry> _registeredEndpoints{};
  ClockNowT _clock;
  int _epollHandle{0};
  std::string _name;
  static bool _initialized;

  using PendingRemoteEndpoints = std::vector<PendingRemoteEndpointEntry>;

  PendingRemoteEndpoints _pendingRemoteEndpoints;
  event_queue::IEventQueue &_eventQueue;
  std::string _certFile;
  std::string _keyFile;
};

}; // namespace medici::sockets::live
