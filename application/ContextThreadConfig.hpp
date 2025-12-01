
#pragma once
#include "medici/time.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace medici::application {

class ContextThreadConfig {
public:
  auto name() const { return _name; }
  auto runContextType() const { return _runContextType; }
  // Event Queue parameters

  // Thread scheduling parameters
  auto cpu() const { return _cpu; }
  auto schedPolicy() const { return _schedPolicy; }
  auto schedPriority() const { return _schedPriority; }

  std::string _name;
  std::string _runContextType;

  std::optional<std::uint32_t> _cpu{};
  std::optional<std::uint32_t> _schedPolicy{};
  std::optional<std::uint32_t> _schedPriority{};
  virtual ~ContextThreadConfig() = default;
};

using ContextThreadConfigPtr = std::shared_ptr<ContextThreadConfig>;

} // namespace medici::application