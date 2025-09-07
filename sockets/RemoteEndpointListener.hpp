#pragma once

#include "medici/application/concepts.hpp"
#include "medici/sockets/GroupEndpointCoordinator.hpp"
#include "medici/sockets/concepts.hpp"

namespace medici::sockets {

template <typename EndpointT, application::AppRunContextC ThreadRunContextT>
class RemoteEndpointListener {
public:
  RemoteEndpointListener(ThreadRunContextT &threadRunContext,
                         const IPEndpointConfig& listenerEndpointConfig,
                         CloseHandlerT listenerCloseHandler,
                         DisconnectedHandlerT listenerDisconnectHandler,
                         OnActiveHandlerT listenerOnActiveHandler,
                         auto &&remoteIncomingPayloadHandler,
                         auto &&remoteOutgoingPayloadHandler,
                         CloseHandlerT removeCloseHandler,
                         DisconnectedHandlerT remoteDisconnectedHandlerT,
                         OnActiveHandlerT remoteOnActiveHandler)
      : _listenEndpoint{threadRunContext.getSocketFactory()
                            .createTcpIpListenerEndpoint(
                                listenerEndpointConfig, listenerCloseHandler,
                                listenerDisconnectHandler,
                                listenerOnActiveHandler,
                                [this, &threadRunContext](
                                    const auto &remoteConfig, int clientFd) {
                                  return _remoteEndpointCoordinator
                                      .registerEndpoint(
                                          remoteConfig,
                                          threadRunContext
                                              .getEndpointPollManager(),
                                          clientFd);
                                })},
        _remoteEndpointCoordinator{
            std::forward<decltype(remoteIncomingPayloadHandler)>(
                remoteIncomingPayloadHandler),
            std::forward<decltype(remoteOutgoingPayloadHandler)>(
                remoteOutgoingPayloadHandler),
            removeCloseHandler, remoteDisconnectedHandlerT,
            remoteOnActiveHandler},
        _threadRunContext{threadRunContext} {}

  Expected start() { return _listenEndpoint->openEndpoint(); }

  Expected stop(const std::string &reason) {
    if (auto result = _listenEndpoint->closeEndpoint(reason); !result) {
      return result;
    }
    return _remoteEndpointCoordinator.closeRemoteEndpoints(reason);
  }

  auto &getEndpointCoordinator() { return _remoteEndpointCoordinator; }

private:
  using RemoteEndpointCoordinatorT = GroupEndpointCoordinator<EndpointT>;

  IIPEndpointPtr _listenEndpoint;
  RemoteEndpointCoordinatorT _remoteEndpointCoordinator;
  ThreadRunContextT &_threadRunContext;
};

} // namespace medici::sockets