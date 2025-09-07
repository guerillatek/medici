#pragma once

#include "medici/sockets/live/LiveListenerEndpointBaseImpl.hpp"
#include "medici/sockets/live/WebSocketLiveEndpoint.hpp"

namespace medici::sockets::live {

template <template <class> class LiveSocketEndpointT>
class WebSocketLiveListenEndpoint : public LiveListenerEndpointBaseImpl {
public:
  WebSocketLiveListenEndpoint(
      const IPEndpointConfig &config,
      IIPEndpointPollManager &endpointPollManager, CloseHandlerT closeHandler,
      DisconnectedHandlerT disconnectedHandler,
      OnActiveHandlerT onActiveHandler,
      RemoteWebSocketEndpointHandlerC auto &&remoteWebSocketEndpointHandle)
      : LiveListenerEndpointBaseImpl{config,
                                     0 endpointPollManager,
                                     [this](
                                         int clientFd,
                                         const IPEndpointConfig &remoteConfig) {
                                       return _newEndpointHandler(
                                           clientFd, HttpEndpointConfig{
                                                         remoteConfig, ""});
                                     },
                                     closeHandler,
                                     disconnectedHandler,
                                     onActiveHandler},
        _newEndpointHandler{std::move(newEndpointHandler)} {}

private:
  RemoteWebSocketEndpointHandlerT _newEndpointHandler;
};

} // namespace medici::sockets::live
