#pragma once

#include "medici/application/concepts.hpp"

#include <expected>

namespace medici::application {

using Expected = std::expected<void, std::string>;

class IAppContext {
public:
  virtual Expected start() = 0;
  virtual Expected stop() = 0;
};
} // namespace medici::application