#pragma once

#include "medici/sockets/concepts.hpp"

namespace medici::sockets {

template <typename EndpointT, typename CoordinatorT>
class EndpointCallbackBaseMembers {

public:
  EndpointCallbackBaseMembers(CoordinatorT &endPointCoordinator,
                              const IPEndpointConfig &config)
      : _config{config}, _endPointCoordinator{endPointCoordinator} {}

  auto getConfig() const { return _config; }

private:
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
                                    CoordinatorT>{endPointCoordinator, config},
        _endpoint{
            std::forward<ArgsT>(args)...,
            config,
            endpointPollManager,
            [this, &incomingPayloadHandler](auto payload, auto tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return incomingPayloadHandler(payload, tp);
            },
            [this, &outgoingPayloadHandler](auto payload, auto tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return outgoingPayloadHandler(payload, tp);
            },
            [this, &closeHandler](const std::string &reason,
                                  const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return closeHandler(reason, endpointConfig);
            },
            [this, &disconnectedHandler](
                const auto &reason, const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return disconnectedHandler(reason, endpointConfig);
            },
            [this, &onActiveHandler]() {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return onActiveHandler();
            }} {}

  auto &getEndpoint() const { return _endpoint; }
  auto &getEndpoint() { return _endpoint; }

private:
  EndpointT _endpoint;
};

template <IsHttpClientEndpoint EndpointT, typename CoordinatorT>
class CallbackBaseSelect<EndpointT, CoordinatorT>
    : public EndpointCallbackBaseMembers<EndpointT, CoordinatorT> {
public:
  template <typename... ArgsT>
  CallbackBaseSelect(CoordinatorT &endPointCoordinator,
                     const IPEndpointConfig &config,
                     IIPEndpointPollManager &endpointPollManager,
                     HttpClientPayloadHandlerC auto &incomingPayloadHandler,
                     SocketPayloadHandlerC auto &outgoingPayloadHandler,
                     CloseHandlerT &closeHandler,
                     DisconnectedHandlerT &disconnectedHandler,
                     OnActiveHandlerT &onActiveHandler, ArgsT &&...args)
      : EndpointCallbackBaseMembers<EndpointT,
                                    CoordinatorT>{endPointCoordinator, config},
        _endpoint{
            std::forward<ArgsT>(args)...,
            config,
            endpointPollManager,
            [this, &incomingPayloadHandler](const http::HttpFields &httpFields,
                                            auto payload, int ec, auto tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return incomingPayloadHandler(httpFields, payload, ec, tp);
            },
            [this, &outgoingPayloadHandler](auto payload, auto tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return outgoingPayloadHandler(payload, tp);
            },
            [this, &closeHandler](const std::string &reason,
                                  const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return closeHandler(reason, endpointConfig);
            },
            [this, &disconnectedHandler](
                const auto &reason, const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return disconnectedHandler(reason, endpointConfig);
            },
            [this, &onActiveHandler]() {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return onActiveHandler();
            }} {}

  auto &getEndpoint() const { return _endpoint; }
  auto &getEndpoint() { return _endpoint; }

private:
  EndpointT _endpoint;
};

template <IsHttpServerEndpoint EndpointT, typename CoordinatorT>
class CallbackBaseSelect<EndpointT, CoordinatorT>
    : public EndpointCallbackBaseMembers<EndpointT, CoordinatorT> {
public:
  template <typename... ArgsT>
  CallbackBaseSelect(CoordinatorT &endPointCoordinator,
                     const IPEndpointConfig &config,
                     IIPEndpointPollManager &endpointPollManager,
                     HttpServerPayloadHandlerC auto &incomingPayloadHandler,
                     SocketPayloadHandlerC auto &outgoingPayloadHandler,
                     CloseHandlerT &closeHandler,
                     DisconnectedHandlerT &disconnectedHandler,
                     OnActiveHandlerT &onActiveHandler, ArgsT &&...args)
      : EndpointCallbackBaseMembers<EndpointT,
                                    CoordinatorT>{endPointCoordinator, config},
        _endpoint{
            std::forward<ArgsT>(args)...,
            config,
            endpointPollManager,
            [this, &incomingPayloadHandler](
                http::HTTPAction action, const std::string &requestURI,
                const http::HttpFields &httpFields,
                const HttpServerPayloadT &payload, TimePoint tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return incomingPayloadHandler(action, requestURI, httpFields,
                                            payload, tp);
            },
            [this, &outgoingPayloadHandler](std::string_view payload,
                                            TimePoint tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return outgoingPayloadHandler(payload, tp);
            },
            [this, &closeHandler](const std::string &reason,
                                  const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return closeHandler(reason, endpointConfig);
            },
            [this, &disconnectedHandler](
                const auto &reason, const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return disconnectedHandler(reason, endpointConfig);
            },
            [this, &onActiveHandler]() {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return onActiveHandler();
            }} {}

  auto &getEndpoint() const { return _endpoint; }
  auto &getEndpoint() { return _endpoint; }

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
                                    CoordinatorT>{endPointCoordinator, config},
        _endpoint{
            std::forward<ArgsT>(args)...,
            config,
            endpointPollManager,
            [this, &incomingPayloadHandler](std::string_view payload,
                                            WSOpCode opCode,
                                            medici::TimePoint tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return incomingPayloadHandler(payload, opCode, tp);
            },
            [this, &outgoingPayloadHandler](std::string_view payload,
                                            WSOpCode opCode,
                                            medici::TimePoint tp) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return outgoingPayloadHandler(payload, opCode, tp);
            },
            [this, &closeHandler](const std::string &reason,
                                  const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return closeHandler(reason, endpointConfig);
            },
            [this, &disconnectedHandler](
                const auto &reason, const IPEndpointConfig &endpointConfig) {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return disconnectedHandler(reason, endpointConfig);
            },
            [this, &onActiveHandler]() {
              this->_endPointCoordinator.setActiveEndpoint(
                  _endpoint.getEndpointUniqueId());
              return onActiveHandler();
            }} {}

  auto &getEndpoint() const { return _endpoint; }
  auto &getEndpoint() { return _endpoint; }

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