#pragma once
#include "medici/sockets/RemoteEndpointListener.hpp"
#include "medici/sockets/live/HTTPLiveServerEndpoint.hpp"

#include <filesystem>
#include <type_traits>
#include <typeinfo>
#include <unordered_map>

namespace medici::sockets {

template <template <class> class SocketEndpointT,
          application::IPAppRunContextC ThreadRunContextT,
          typename ExtendedContextData = NoExtendedContextData>
class HttpServerHandler {
public:
  using HttpServerEndpointT = live::HTTPLiveServerEndpoint<SocketEndpointT>;
  using RemoteEndpointListenerT =
      RemoteEndpointListener<HttpServerEndpointT, ThreadRunContextT,
                             ExtendedContextData>;
  using RemoteEndpointContextT =
      typename RemoteEndpointListenerT::RemoteEndpointContextT;

  HttpServerHandler(ThreadRunContextT &serverThreadContext,
                    const std::string &baseFilePath,
                    const HttpEndpointConfig &listenEndpoint)
      : _serverEndpointListener{
            serverThreadContext,
            listenEndpoint,
            [this](const std::string &reason,
                   const sockets::IPEndpointConfig &) {
              return handleClosedListener(reason, _listenEndpointConfig);
            },
            [this](const std::string &reason,
                   const sockets::IPEndpointConfig &) {
              return handleDisconnectedListener(reason, _listenEndpointConfig);
            },
            [this]() { return handleListenerActive(); },
            [this](http::HTTPAction action, const std::string &requestURI,
                   const http::HttpFields &fields,
                   const sockets::HttpServerPayloadT &payload, TimePoint tp) {
              return demuxRemoteHttpClientRequests(
                  action, requestURI, fields, payload, tp,
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            },
            [this](std::string_view payload, medici::TimePoint tp) {
              return handleOutgoingHttpResponse(
                  payload, tp,
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            },
            [this](const std::string &reason,
                   const sockets::IPEndpointConfig &endpointConfig) {
              return handleClosedRemoteListener(
                  reason, endpointConfig,
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            },
            [this](const std::string &reason,
                   const sockets::IPEndpointConfig &endpointConfig) {
              return handleDisconnectedRemoteListener(
                  reason, endpointConfig,
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            },
            [this]() {
              return handleRemoteListenerActive(
                  _serverEndpointListener.getEndpointCoordinator()
                      .getActiveContext());
            }},
        _baseFilePath{baseFilePath}, _listenEndpointConfig{listenEndpoint} {}

  Expected start() { return _serverEndpointListener.start(); }

protected:
  http::SupportedCompression
  getAcceptedCompression(const http::HttpFields &requestHeaders) {
    if (requestHeaders.HasField("Accept-Encoding")) {
      auto acceptEncoding = requestHeaders.getField("Accept-Encoding").value();
      if (acceptEncoding.find("gzip") != std::string::npos) {
        return http::SupportedCompression::GZip;
      } else if (acceptEncoding.find("deflate") != std::string::npos) {
        return http::SupportedCompression::HttpDeflate;
      } else if (acceptEncoding.find("br") != std::string::npos) {
        return http::SupportedCompression::Brotli;
      }
    }
    return http::SupportedCompression::None;
  }

  Expected sendDirectoryListing(const std::string &directoryPath,
                                const http::HttpFields &requestHeaders,
                                TimePoint tp,
                                RemoteEndpointContextT &remoteEndpointContext) {
    std::string listingHtml = "<html><body><h1>Directory Listing</h1><ul>";

    for (const auto &entry :
         std::filesystem::directory_iterator(directoryPath)) {
      listingHtml += "<li><a href=\"" + entry.path().filename().string() +
                     "\">" + entry.path().filename().string() + "</a></li>";
    }

    listingHtml += "</ul></body></html>";

    return remoteEndpointContext.getEndpoint().sendHttpResponse(
        http::HttpFields{}, 200, listingHtml, "OK", http::ContentType::TextHtml,
        getAcceptedCompression(requestHeaders));
  }

private:
  virtual Expected handleListenerActive() { return {}; }

  virtual Expected handleClosedListener(const std::string &reason,
                                        const IPEndpointConfig &endpoint) {
    return {};
  }

  virtual Expected
  handleDisconnectedListener(const std::string &reason,
                             const IPEndpointConfig &endpoint) {
    return {};
  }

  virtual Expected
  handleFileRequest(const std::string &filePath,
                    const http::HttpFields &requestHeaders, TimePoint tp,
                    RemoteEndpointContextT &remoteEndpointContext) {

    if (std::filesystem::is_directory(filePath)) {
      return sendDirectoryListing(filePath, requestHeaders, tp,
                                  remoteEndpointContext);
    }

    auto sendResult = remoteEndpointContext.getEndpoint().sendFileResponse(
        requestHeaders, 200, "OK", filePath,
        http::getContentTypeFromFilePath(filePath),
        getAcceptedCompression(requestHeaders));

    if (sendResult) {
      return {};
    }
    
    auto responseHtml = std::format(
        "<html><body><h1>Error sending file '{}'</h1><p>{}</p></body></html>",
        filePath, sendResult.error());

    return remoteEndpointContext.getEndpoint().sendHttpResponse(
        http::HttpFields{}, 404, responseHtml, sendResult.error(),
        http::ContentType::TextHtml, http::SupportedCompression::None);
  }

  virtual Expected demuxRemoteHttpClientRequests(
      http::HTTPAction action, const std::string &uriPath,
      const http::HttpFields &requestHeaders,
      const sockets::HttpServerPayloadT &payload, TimePoint tp,
      RemoteEndpointContextT &remoteEndpointContext) {

    if ((action == http::HTTPAction::GET) &&
        (uriPath.find(_listenEndpointConfig.uriPath()) == 0)) {
      // Handle file requests
      std::string filePath =
          _baseFilePath +
          uriPath.substr(_listenEndpointConfig.uriPath().length());
      return handleFileRequest(filePath, requestHeaders, tp,
                               remoteEndpointContext);
    } else {
      return handleRemoteHttpRequest(action, uriPath, requestHeaders, payload,
                                     tp, remoteEndpointContext);
    };
  }

  virtual Expected
  handleRemoteHttpRequest(http::HTTPAction action, const std::string &uriPath,
                          const http::HttpFields &requestHeaders,
                          const sockets::HttpServerPayloadT &payload,
                          TimePoint tp,
                          RemoteEndpointContextT &remoteEndpointContext) {
    return remoteEndpointContext.getEndpoint().sendHttpResponse(
        http::HttpFields{}, 404,
        "<html><body><h1>404 Not Found</h1><p>The requested resource was not "
        "found on this server.</p></body></html>",
        "Not Found", http::ContentType::TextHtml,
        getAcceptedCompression(requestHeaders));
  }

  virtual Expected
  handleOutgoingHttpResponse(std::string_view payload, medici::TimePoint,
                             RemoteEndpointContextT &remoteEndpoint) {
    return {};
  }

  virtual Expected
  handleClosedRemoteListener(const std::string &reason,
                             const IPEndpointConfig &endpoint,
                             RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  virtual Expected handleDisconnectedRemoteListener(
      const std::string &reason, const IPEndpointConfig &endpoint,
      RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  virtual Expected
  handleRemoteListenerActive(RemoteEndpointContextT &remoteEndpointContext) {
    return {};
  }

  RemoteEndpointListenerT _serverEndpointListener;
  std::string _baseFilePath;
  HttpEndpointConfig _listenEndpointConfig;
};

} // namespace medici::sockets
