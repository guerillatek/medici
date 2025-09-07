#pragma once

#include <format>
#include <ostream>
#include <sstream>
#include <string>
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
} // namespace medici::http

template <> struct std::formatter<medici::http::HTTPAction> {

  constexpr auto parse(auto &ctx) { return ctx.begin(); }

  auto format(const medici::http::HTTPAction &action, auto &ctx) const {
    std::ostringstream writer;
    writer << action;
    return std::format_to(ctx.out(), "{}", writer.str());
  }
};