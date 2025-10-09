#pragma once

#include "medici/sockets/live/LiveListenerEndpointBaseImpl.hpp"

namespace medici::sockets::live {

class TcpIpLiveListenEndpoint : public LiveListenerEndpointBaseImpl {
public:
  TcpIpLiveListenEndpoint(const IPEndpointConfig &config,
                          IIPEndpointPollManager &endpointPollManager,
                          CloseHandlerT closeHandler,
                          DisconnectedHandlerT disconnectedHandler,
                          OnActiveHandlerT onActiveHandler,
                          RemoteTcpEndpointHandlerC auto &&newEndpointHandler)
      : LiveListenerEndpointBaseImpl{
            config,
            endpointPollManager,
            [this, endpointHandler = std::move(newEndpointHandler)](
                int clientFd, const IPEndpointConfig &remoteConfig) {
              return endpointHandler(remoteConfig, clientFd);
            },
            closeHandler,
            disconnectedHandler,
            onActiveHandler} {}
};

} // namespace medici::sockets::live
