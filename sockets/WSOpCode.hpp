#pragma once

#include <cstdint>
#include <format>

namespace medici::sockets {

// https://en.wikipedia.org/wiki/WebSocket#cite_note-41
constexpr static char WS_FIN = 0b1000'0000;
[[maybe_unused]] constexpr static char WS_RSV1 = 0b0100'0000;
[[maybe_unused]] constexpr static char WS_RSV2 = 0b0010'0000;
[[maybe_unused]] constexpr static char WS_RSV3 = 0b0001'0000;
constexpr static char WS_OPCODE = 0b0000'1111;
constexpr static char WS_MASK = 0b1000'0000;
constexpr static char WS_LENGTH = 0b0111'1111;

enum class WSOpCode : std::uint8_t {
  Continuation = 0x0,
  Text = 0x1,
  Binary = 0x2,
  ClosedConnection = 0x8,
  Ping = 0x9,
  Pong = 0xA
};

} // namespace medici::sockets

template <> struct std::formatter<medici::sockets::WSOpCode> {

  constexpr auto parse(auto &ctx) { return ctx.begin(); }

  auto format(const medici::sockets::WSOpCode &opCode, auto &ctx) const {
    using namespace medici::sockets;
    switch (opCode) {
    case WSOpCode::Continuation:
      return std::format_to(ctx.out(), "Continuation");
    case WSOpCode::Text:
      return std::format_to(ctx.out(), "Text");
    case WSOpCode::Binary:
      return std::format_to(ctx.out(), "Binary");
    case WSOpCode::ClosedConnection:
      return std::format_to(ctx.out(), "ClosedConnection");
    case WSOpCode::Ping:
      return std::format_to(ctx.out(), "Ping");
    case WSOpCode::Pong:
      return std::format_to(ctx.out(), "Pong");
    };
    return std::format_to(ctx.out(), "Unknown");
  }
};