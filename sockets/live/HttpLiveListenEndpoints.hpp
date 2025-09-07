#pragma once

#include "medici/sockets/live/HTTPLiveEndpoint.hpp"
#include "medici/sockets/live/LiveListenerEndpointBaseImpl.hpp"

namespace medici::sockets::live {

class HttpLiveListenEndpoint : public LiveListenerEndpointBaseImpl {
public:
  HttpLiveListenEndpoint(const IPEndpointConfig &config,
                         IIPEndpointPollManager &endpointPollManager,
                         CloseHandlerT closeHandler,
                         DisconnectedHandlerT disconnectedHandler,
                         OnActiveHandlerT onActiveHandler,
                         RemoteHttpEndpointHandlerC auto &&newEndpointHandler)
      : LiveListenerEndpointBaseImpl{config,
                                     endpointPollManager,
                                     [this](
                                         int clientFd,
                                         const IPEndpointConfig &remoteConfig) {
                                       return _newEndpointHandler(
                                           HttpEndpointConfig{remoteConfig, ""},
                                           clientFd);
                                     },
                                     closeHandler,
                                     disconnectedHandler,
                                     onActiveHandler},
        _newEndpointHandler{std::move(newEndpointHandler)} {}

private:
  RemoteHttpEndpointHandlerT _newEndpointHandler;
};

} // namespace medici::sockets::live
