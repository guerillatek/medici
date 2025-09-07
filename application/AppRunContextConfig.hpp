
#pragma once
#include "medici/time.hpp"

#include <chrono>
#include <string>
#include <string_view>

namespace medici::application {

class AppRunContextConfig {
public:
  auto name() const { return _name; }
  auto runContextType() const { return _runContextType; }
  auto producers() const { return _producers; }
  auto inactivityMicros() const {
    return std::chrono::microseconds{_inactivityMicros};
  }
  auto cpu() const { return _cpu; }
  auto schedPolicy() const { return _schedPolicy; }
  auto schedPriority() const { return _schedPriority; }

  std::string _name;
  std::string _runContextType;
  std::uint32_t _producers;
  std::uint32_t _inactivityMicros;
  std::optional<std::uint32_t> _cpu;
  std::optional<std::uint32_t> _schedPolicy;
  std::optional<std::uint32_t> _schedPriority;
};
} // namespace medici::application