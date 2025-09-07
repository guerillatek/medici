#pragma once

#include "medici/sockets/concepts.hpp"

namespace medici::sockets {

template <typename EndpointT, typename CoordinatorT>
class EndpointCallbackBaseMembers {

public:
  EndpointCallbackBaseMembers(CoordinatorT &endPointCoordinator,
                              const IPEndpointConfig &config)
      : _fd{0}, _config{config}, _endPointCoordinator{endPointCoordinator} {}

  auto getOSFileDescriptor() const { return _fd; }
  auto getConfig() const { return _config; }

private:
  int _fd;
  IPEndpointConfig _config;

protected:
  CoordinatorT &_endPointCoordinator;
};

template <typename EndpointT, typename CoordinatorT>
class CallbackBaseSelect {};

template <IsTcpEndpoint EndpointT, typename CoordinatorT>
class CallbackBaseSelect<EndpointT, CoordinatorT>
    : public EndpointCallbackBaseMembers<EndpointT, CoordinatorT> {
public:
  template <typename... ArgsT>
  CallbackBaseSelect(CoordinatorT &endPointCoordinator,
                     const IPEndpointConfig &config,
                     IIPEndpointPollManager &endpointPollManager,
                     SocketPayloadHandlerC auto &incomingPayloadHandler,
                     SocketPayloadHandlerC auto &outgoingPayloadHandler,
                     CloseHandlerT &closeHandler,
                     DisconnectedHandlerT &disconnectedHandler,
                     OnActiveHandlerT &onActiveHandler, ArgsT &&...args)
      : EndpointCallbackBaseMembers<EndpointT,
                                    CoordinatorT>{this->_endPointCoordinator,
                                                  config},
        _endpoint{std::forward<ArgsT>(args)...,
                  config,
                  endpointPollManager,
                  [this, &incomingPayloadHandler](auto payload, auto tp) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return incomingPayloadHandler(payload, tp);
                  },
                  [this, &outgoingPayloadHandler](auto payload, auto tp) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return outgoingPayloadHandler(payload, tp);
                  },
                  [this, &closeHandler](const std::string &reason) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return closeHandler(reason);
                  },
                  [this, &disconnectedHandler](const auto &reason) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return disconnectedHandler(reason);
                  },
                  [this, &onActiveHandler]() {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return onActiveHandler();
                  }} {}

  auto &getEndpoint() { return _endpoint; }

private:
  EndpointT _endpoint;
};

template <IsHttpEndpoint EndpointT, typename CoordinatorT>
class CallbackBaseSelect<EndpointT, CoordinatorT>
    : public EndpointCallbackBaseMembers<EndpointT, CoordinatorT> {
public:
  template <typename... ArgsT>
  CallbackBaseSelect(CoordinatorT &endPointCoordinator,
                     const IPEndpointConfig &config,
                     IIPEndpointPollManager &endpointPollManager,
                     HttpPayloadHandlerC auto &incomingPayloadHandler,
                     HttpPayloadHandlerC auto &outgoingPayloadHandler,
                     CloseHandlerT &closeHandler,
                     DisconnectedHandlerT &disconnectedHandler,
                     OnActiveHandlerT &onActiveHandler, ArgsT &&...args)
      : EndpointCallbackBaseMembers<EndpointT,
                                    CoordinatorT>{std::forward<ArgsT>(args)...,
                                                  endPointCoordinator, config},
        _endpoint{
            std::forward<ArgsT>(args)...,
            config,
            endpointPollManager,
            [this, &incomingPayloadHandler](http::HttpFields &&httpFields,
                                            auto payload, auto tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  this->getOSFileDescriptor());
              return incomingPayloadHandler(
                  std::forward<http::HttpFields>(httpFields), payload, tp);
            },
            [this, &outgoingPayloadHandler](http::HttpFields &&httpFields,
                                            auto payload, auto tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  this->getOSFileDescriptor());
              return outgoingPayloadHandler(
                  std::forward<http::HttpFields>(httpFields), payload, tp);
            },
            [this, &closeHandler](const std::string &reason) {
              this->_endPointCoordinator.setActiveEndpoint(
                  this->getOSFileDescriptor());
              return closeHandler(reason);
            },
            [this, &disconnectedHandler](const auto &reason) {
              this->_endPointCoordinator.setActiveEndpoint(
                  this->getOSFileDescriptor());
              return disconnectedHandler(reason);
            },
            [this, &onActiveHandler]() {
              this->_endPointCoordinator.setActiveEndpoint(
                  this->getOSFileDescriptor());
              return onActiveHandler();
            }} {}

  auto &getEndpoint() const { return _endpoint; }

private:
  EndpointT _endpoint;
};

template <IsWebSocketEndpoint EndpointT, typename CoordinatorT>
class CallbackBaseSelect<EndpointT, CoordinatorT>
    : public EndpointCallbackBaseMembers<EndpointT, CoordinatorT> {
public:
  template <typename... ArgsT>
  CallbackBaseSelect(CoordinatorT &endPointCoordinator,
                     const IPEndpointConfig &config,
                     IIPEndpointPollManager &endpointPollManager,
                     WebSocketPayloadHandlerC auto &incomingPayloadHandler,
                     WebSocketPayloadHandlerC auto &outgoingPayloadHandler,
                     CloseHandlerT &closeHandler,
                     DisconnectedHandlerT &disconnectedHandler,
                     OnActiveHandlerT &onActiveHandler, ArgsT &&...args)
      : EndpointCallbackBaseMembers<EndpointT,
                                    CoordinatorT>{std::forward<ArgsT>(args)...,
                                                  endPointCoordinator, config},
        _endpoint{std::forward<ArgsT>(args)...,
                  config,
                  endpointPollManager,
                  [this, &incomingPayloadHandler](auto payload, WSOpCode opCode,
                                                  auto tp) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return incomingPayloadHandler(payload, opCode, tp);
                  },
                  [this, &outgoingPayloadHandler](auto payload, WSOpCode opCode,
                                                  auto tp) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return outgoingPayloadHandler(payload, opCode, tp);
                  },
                  [this, &closeHandler](const std::string &reason) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return closeHandler(reason);
                  },
                  [this, &disconnectedHandler](const auto &reason) {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return disconnectedHandler(reason);
                  },
                  [this, &onActiveHandler]() {
                    this->_endPointCoordinator.setActiveEndpoint(
                        this->getOSFileDescriptor());
                    return onActiveHandler();
                  }} {}

  auto &getEndpoint() const { return _endpoint; }

private:
  EndpointT _endpoint;
};

template <typename EndpointT, typename CoordinatorT>
class EndpointCallbackContext
    : public CallbackBaseSelect<EndpointT, CoordinatorT> {
public:
  using SelectedBase = CallbackBaseSelect<EndpointT, CoordinatorT>;
  template <typename... ArgsT>
  EndpointCallbackContext(CoordinatorT &endPointCoordinator,
                          const IPEndpointConfig &config,
                          IIPEndpointPollManager &endpointPollManager,
                          auto &incomingPayloadHandler,
                          auto &outgoingPayloadHandler,
                          CloseHandlerT &closeHandler,
                          DisconnectedHandlerT &disconnectedHandler,
                          OnActiveHandlerT &onActiveHandler, ArgsT &&...args)
      : SelectedBase{endPointCoordinator,         config,
                     endpointPollManager,         incomingPayloadHandler,
                     outgoingPayloadHandler,      closeHandler,
                     disconnectedHandler,         onActiveHandler,
                     std::forward<ArgsT>(args)...} {}
};

} // namespace medici::sockets