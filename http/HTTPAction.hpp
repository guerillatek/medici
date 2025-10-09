#pragma once

#include <format>
#include <ostream>
#include <sstream>
#include <string>
#include <expected>

namespace medici::http {

enum class HTTPAction {
  GET,
  POST,
  PUT,
  DELETE,
  PATCH,
  HEAD,
  OPTIONS,
  TRACE,
  CONNECT
};

inline std::ostream &operator<<(std::ostream &os, HTTPAction action) {
  switch (action) {
  case HTTPAction::GET:
    return os << "GET";
  case HTTPAction::POST:
    return os << "POST";
  case HTTPAction::PUT:
    return os << "PUT";
  case HTTPAction::DELETE:
    return os << "DELETE";
  case HTTPAction::PATCH:
    return os << "PATCH";
  case HTTPAction::HEAD:
    return os << "HEAD";
  case HTTPAction::OPTIONS:
    return os << "OPTIONS";
  case HTTPAction::TRACE:
    return os << "TRACE";
  case HTTPAction::CONNECT:
    return os << "CONNECT";
  };
  return os;
}

inline std::expected<HTTPAction, std::string>
to_HTTPAction(const std::string &method) {
  if (method == "GET") {
    return HTTPAction::GET;
  } else if (method == "POST") {
    return HTTPAction::POST;
  } else if (method == "PUT") {
    return HTTPAction::PUT;
  } else if (method == "DELETE") {
    return HTTPAction::DELETE;
  } else if (method == "PATCH") {
    return HTTPAction::PATCH;
  } else if (method == "HEAD") {
    return HTTPAction::HEAD;
  } else if (method == "OPTIONS") {
    return HTTPAction::OPTIONS;
  } else if (method == "TRACE") {
    return HTTPAction::TRACE;
  } else if (method == "CONNECT") {
    return HTTPAction::CONNECT;
  }
  return std::unexpected("Invalid HTTP action method: " + method);
}

} // namespace medici::http

template <> struct std::formatter<medici::http::HTTPAction> {

  constexpr auto parse(auto &ctx) { return ctx.begin(); }

  auto format(const medici::http::HTTPAction &action, auto &ctx) const {
    std::ostringstream writer;
    writer << action;
    return std::format_to(ctx.out(), "{}", writer.str());
  }
};
