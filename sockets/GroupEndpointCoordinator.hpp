#pragma once
#include "medici/application/concepts.hpp"
#include "medici/sockets/EndpointCallbackContext.hpp"
#include "medici/sockets/concepts.hpp"

#include <type_traits>
#include <typeinfo>
#include <unordered_map>

namespace medici::sockets {

struct DefaultCallbackContext {};

template <typename EndpointT, typename CallbackContext = DefaultCallbackContext>
class GroupEndpointCoordinator {
public:
  using DefaultContextT =
      EndpointCallbackContext<EndpointT, GroupEndpointCoordinator<EndpointT>>;
  using CallbackContextT = std::conditional_t<
      std::is_same_v<DefaultCallbackContext, CallbackContext>, DefaultContextT,
      EndpointCallbackContext<EndpointT, CallbackContext>>;

  using CallbackContextPtr = std::unique_ptr<CallbackContextT>;
  using EndpointContextLookup = std::unordered_map<int, CallbackContextPtr>;
  using ContextEntry = EndpointContextLookup::iterator;
  using PayloadHandlerT = decltype(getEndpointHandlerType<EndpointT>());

  GroupEndpointCoordinator(auto &&incomingPayloadHandler,
                           auto &&outgoingPayloadHandler,
                           CloseHandlerT closeHandler,
                           DisconnectedHandlerT disconnectedHandlerT,
                           OnActiveHandlerT onActiveHandler)
      : _incomingPayloadHandler{std::move(incomingPayloadHandler)},
        _outgoingPayloadHandler{std::move(outgoingPayloadHandler)},
        _closeHandler{closeHandler},
        _disconnectedHandlerT{disconnectedHandlerT},
        _onActiveHandler{onActiveHandler} {}

  template <typename... ArgsT>
  Expected registerEndpoint(const auto &config,
                            IIPEndpointPollManager &endpointPollManager,
                            int fd) {
    auto context = std::make_unique<CallbackContextT>(
        *this, config, endpointPollManager, _incomingPayloadHandler,
        _outgoingPayloadHandler, _closeHandler, _disconnectedHandlerT,
        _onActiveHandler, fd);
    auto [it, inserted] = _contextLookup.emplace(fd, std::move(context));
    if (!inserted) {
      return std::unexpected(
          std::format("Endpoint with fd={} already registered", fd));
    }
    return {};
  }

  Expected removeEndpoint(int fd) {
    auto it = _contextLookup.find(fd);
    if (it == _contextLookup.end()) {
      return std::unexpected(std::format("Endpoint with fd={} not found", fd));
    }
    _contextLookup.erase(it);
    _activeContextEntry.reset();
    return {};
  }

  Expected setActiveEndpoint(int endpointId) {
    if (!_contextLookup.contains(endpointId)) {
      return std::unexpected(
          std::format("Endpoint with id={} not found", endpointId));
    }
    _activeContextEntry = _contextLookup.find(endpointId);
    return {};
  }

  auto &getActiveContext() {
    if (!_activeContextEntry) {
      throw std::runtime_error("No active endpoint set");
    }
    auto &activeContextEntry = _activeContextEntry.value();
    auto &activeContext = *(activeContextEntry->second.get());
    return activeContext;
  }

  Expected forEachEndpoint(auto &&callback) {
    for (auto &[fd, contextPtr] : _contextLookup) {
      auto &context = *contextPtr;
      if (auto result = callback(context); !result) {
        return result;
      }
    }
    return {};
  }

  Expected closeRemoteEndpoints(const std::string &reason) {
    forEachEndpoint([&reason](auto &context) {
      return context.getEndpoint().closeEndpoint(reason);
    });
    return {};
  }

private:
  PayloadHandlerT _incomingPayloadHandler;
  PayloadHandlerT _outgoingPayloadHandler;
  CloseHandlerT _closeHandler;
  DisconnectedHandlerT _disconnectedHandlerT;
  OnActiveHandlerT _onActiveHandler;

  std::unordered_map<int, CallbackContextPtr> _contextLookup;
  EndpointContextLookup _endpointContextLookup;
  std::optional<ContextEntry> _activeContextEntry;
};

} // namespace medici::sockets
