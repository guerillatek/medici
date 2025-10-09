#pragma once

#include "medici/sockets/concepts.hpp"
#include "medici/sockets/interfaces.hpp"
#include "medici/timers/interfaces.hpp"
#include <format>

namespace medici::sockets {

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class IPEndpointHandlerCapture {
public:
  IPEndpointHandlerCapture(const IPEndpointConfig &endpointConfig,
                           IncomingPayloadHandlerT &&incomingPayloadHandler,
                           SocketPayloadHandlerC auto &&outgoingPayloadHandler,
                           CloseHandlerT closeHandler,
                           DisconnectedHandlerT DisconnectedHandlerT,
                           OnActiveHandlerT onActiveHandler)
      : _endpointConfig{endpointConfig},
        _incomingPayloadHandler{std::move(incomingPayloadHandler)},
        _outgoingPayloadHandler{std::move(outgoingPayloadHandler)},
        _closeHandler{closeHandler},
        _DisconnectedHandlerT{DisconnectedHandlerT},
        _onActiveHandler{onActiveHandler} {}

  // Constructor for Multicast Endpoint capture
  IPEndpointHandlerCapture(const IPEndpointConfig &endpointConfig,
                           IncomingPayloadHandlerT &&incomingPayloadHandler,
                           CloseHandlerT closeHandler,
                           DisconnectedHandlerT DisconnectedHandlerT,
                           OnActiveHandlerT onActiveHandler)
      : _endpointConfig{endpointConfig},
        _incomingPayloadHandler{std::move(incomingPayloadHandler)},
        _closeHandler{closeHandler},
        _DisconnectedHandlerT{DisconnectedHandlerT},
        _onActiveHandler{onActiveHandler} {}

  Expected onActive() {
    for (auto timerEntry : _registeredTimers) {
      timerEntry->start();
    }
    if (!_onActiveHandler) {
      return {};
    }
    return _onActiveHandler();
  }

  Expected onPayloadRead(std::string_view payload, TimePoint deliveryTime) {
    return _incomingPayloadHandler(payload, deliveryTime);
  }

  Expected onPayloadSend(std::string_view payload, TimePoint deliveryTime) {
    if (!_outgoingPayloadHandler) {
      return {};
    }
    return _outgoingPayloadHandler(payload, deliveryTime);
  }

  Expected onCloseEndpoint(const std::string &reason,
                           const IPEndpointConfig &endpointConfig) {
    for (auto timerEntry : _registeredTimers) {
      timerEntry->stop();
    }
    if (!_closeHandler) {
      return {};
    }
    return _closeHandler(reason, endpointConfig);
  }

  Expected
  onDisconnected(const std::string &reason,
                 const medici::sockets::IPEndpointConfig &endpointConfig) {
    for (auto timerEntry : _registeredTimers) {
      timerEntry->stop();
    }

    if (!_DisconnectedHandlerT) {
      return std::unexpected(
          std::format("Endpoint name={} DISCONNECTED errno={}",
                      _endpointConfig.name(), reason));
    }
    return _DisconnectedHandlerT(reason, endpointConfig);
  }

  Expected registerTimer(const timers::IEndPointTimerPtr &timer) {
    _registeredTimers.push_back(timer);
    return {};
  }

private:
  const IPEndpointConfig &_endpointConfig;
  IncomingPayloadHandlerT _incomingPayloadHandler;
  SocketPayloadHandlerT _outgoingPayloadHandler{};
  CloseHandlerT _closeHandler;
  DisconnectedHandlerT _DisconnectedHandlerT;
  OnActiveHandlerT _onActiveHandler;
  std::vector<timers::IEndPointTimerPtr> _registeredTimers;
};

} // namespace medici::sockets
