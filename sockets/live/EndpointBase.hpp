#pragma once

#include "medici/sockets/IPEndpointHandlerCapture.hpp"
#include "medici/sockets/live/IPEndpointConnectionManager.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"

namespace medici::sockets::live {

template <SocketPayloadHandlerC IncomingPayloadHandlerT>
class EndpointBase : public IEndpointEventDispatch {

public:
  using SocketEventHandlers = IPEndpointHandlerCapture<IncomingPayloadHandlerT>;

  EndpointBase(ConnectionType connectionType, const IPEndpointConfig &config,
               IIPEndpointPollManager &endpointPollManager,
               IncomingPayloadHandlerT &&incomingPayloadHandler,
               SocketPayloadHandlerC auto &&outgoingPayloadHandler,
               CloseHandlerT closeHandler,
               DisconnectedHandlerT DisconnectedHandlerT,
               OnActiveHandlerT onActiveHandler)
      : _config{config}, _eventHandlers{config,
                                        std::forward<IncomingPayloadHandlerT>(
                                            incomingPayloadHandler),
                                        outgoingPayloadHandler,
                                        closeHandler,
                                        DisconnectedHandlerT,
                                        onActiveHandler},
        _connectionManager{config, endpointPollManager, *this, connectionType} {

  }

  EndpointBase(int fd, ConnectionType connectionType,
               const IPEndpointConfig &config,
               IIPEndpointPollManager &endpointPollManager,
               IncomingPayloadHandlerT &&incomingPayloadHandler,
               SocketPayloadHandlerC auto &&outgoingPayloadHandler,
               CloseHandlerT closeHandler,
               DisconnectedHandlerT DisconnectedHandlerT,
               OnActiveHandlerT onActiveHandler)
      : _config{config}, _eventHandlers{config,
                                        std::forward<IncomingPayloadHandlerT>(
                                            incomingPayloadHandler),
                                        outgoingPayloadHandler,
                                        closeHandler,
                                        DisconnectedHandlerT,
                                        onActiveHandler},
        _connectionManager{config, fd, endpointPollManager, *this,
                           connectionType} {

    if (connectionType == ConnectionType::TCP) {
      if (auto result = _connectionManager.registerWithEpoll(); !result) {
        throw std::runtime_error(result.error());
      }
      endpointPollManager.getEventQueue().postAction(
          [this]() { return this->onActive(); });
    }
  }

  auto &getConfig() const { return _config; }

protected:
  void resizeInboundBuffer(size_t newSize) { _inboundBuffer.resize(newSize); }
  auto &getConnectionManager() { return _connectionManager; }

  auto &getConnectionManager() const { return _connectionManager; }

  auto &getInboundBuffer() const { return _inboundBuffer; }
  auto &getOutboundBuffer() const { return _outboundBuffer; }
  auto &getOutboundBuffer() { return _outboundBuffer; }
  auto getInboundBufferAvailableSize() const {
    return _inboundBuffer.size() - _inboundWriteOffset;
  }
  void clearOutboundBuffer() { _outboundBuffer.clear(); }
  auto &getEventHandlers() { return _eventHandlers; }

  auto getInboundBufferWritePos() const {
    return const_cast<char *>(_inboundBuffer.data() + _inboundWriteOffset);
  }

  Expected registerTimer(const timers::IEndPointTimerPtr &timer) override {
    _eventHandlers.registerTimer(timer);
    return {};
  }

  Expected onActive() override { return _eventHandlers.onActive(); }

  // Incomplete content from previous read that needs to be prepended to
  //  next read
  Expected prependPartialContent(const char *content, size_t contentSize) {
    _inboundWriteOffset = contentSize;
    if (_inboundWriteOffset > _inboundBuffer.size()) {
      return std::unexpected(
          std::format("Endpoint name={} prependPartialRead buffer overrun",
                      this->getConfig().name()));
    }
    std::memcpy(_inboundBuffer.data(), content, contentSize);
    return {};
  }

  auto getAndClearPrependSize() {
    auto size = _inboundWriteOffset;
    _inboundWriteOffset = 0;
    return size;
  }

  IPEndpointConfig _config;
  std::vector<char> _inboundBuffer{};
  std::vector<char> _outboundBuffer{};
  size_t _inboundWriteOffset{0};
  SocketEventHandlers _eventHandlers;
  IPEndpointConnectionManager _connectionManager;
};

} // namespace medici::sockets::live