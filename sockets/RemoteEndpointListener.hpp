#pragma once

#include "medici/application/concepts.hpp"
#include "medici/sockets/GroupEndpointCoordinator.hpp"
#include "medici/sockets/concepts.hpp"

namespace medici::sockets {

template <typename EndpointT, application::IPAppRunContextC ThreadRunContextT,
          typename ExtendedContextData = NoExtendedContextData>
class RemoteEndpointListener {
public:
  RemoteEndpointListener(ThreadRunContextT &threadRunContext,
                         const auto &listenerEndpointConfig,
                         CloseHandlerT listenerCloseHandler,
                         DisconnectedHandlerT listenerDisconnectHandler,
                         OnActiveHandlerT listenerOnActiveHandler,
                         auto &&remoteIncomingPayloadHandler,
                         auto &&remoteOutgoingPayloadHandler,
                         CloseHandlerT remoteCloseHandler,
                         DisconnectedHandlerT remoteDisconnectedHandlerT,
                         OnActiveHandlerT remoteOnActiveHandler)
      : _threadRunContext{threadRunContext},
        _listenEndpoint{makeListenerEndpoint<EndpointT>(
            listenerEndpointConfig, listenerCloseHandler,
            listenerDisconnectHandler, listenerOnActiveHandler,
            [this, &threadRunContext](const auto &remoteConfig, int clientFd) {
              return _remoteEndpointCoordinator.registerEndpoint(
                  remoteConfig, threadRunContext.getEndpointPollManager(),
                  clientFd);
            })},
        _remoteEndpointCoordinator{
            std::forward<decltype(remoteIncomingPayloadHandler)>(
                remoteIncomingPayloadHandler),
            std::forward<decltype(remoteOutgoingPayloadHandler)>(
                remoteOutgoingPayloadHandler),
            remoteCloseHandler, remoteDisconnectedHandlerT,
            remoteOnActiveHandler} {}

  template <typename SocketT>
  auto makeListenerEndpoint(const auto &listenerEndpointConfig,
                            CloseHandlerT listenerCloseHandler,
                            DisconnectedHandlerT listenerDisconnectHandler,
                            OnActiveHandlerT listenerOnActiveHandler,
                            auto &&listenerRegisterHandler) {
    if constexpr (IsHttpServerEndpoint<SocketT> ||
                  IsWebSocketEndpoint<SocketT>) {
      return _threadRunContext.getSocketFactory().createHttpListenerEndpoint(
          listenerEndpointConfig, listenerCloseHandler,
          listenerDisconnectHandler, listenerOnActiveHandler,
          std::forward<decltype(listenerRegisterHandler)>(
              listenerRegisterHandler));
    } else {
      static_assert(IsTcpEndpoint<SocketT>);
      return _threadRunContext.getSocketFactory().createTcpIpListenerEndpoint(
          listenerEndpointConfig, listenerCloseHandler,
          listenerDisconnectHandler, listenerOnActiveHandler,
          std::forward<decltype(listenerRegisterHandler)>(
              listenerRegisterHandler));
    }
  }

  Expected start() { return _listenEndpoint->openEndpoint(); }

  Expected stop(const std::string &reason) {
    if (auto result = _listenEndpoint->closeEndpoint(reason); !result) {
      return result;
    }
    return _remoteEndpointCoordinator.closeRemoteEndpoints(reason);
  }

  auto &getEndpointCoordinator() { return _remoteEndpointCoordinator; }

  using RemoteEndpointCoordinatorT =
      GroupEndpointCoordinator<EndpointT, ExtendedContextData>;

  using RemoteEndpointContextT =
      typename RemoteEndpointCoordinatorT::CallbackContextT;

private:
  ThreadRunContextT &_threadRunContext;
  IIPEndpointPtr _listenEndpoint;
  RemoteEndpointCoordinatorT _remoteEndpointCoordinator;
};

} // namespace medici::sockets