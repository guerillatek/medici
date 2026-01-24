
#pragma once
#include "medici/application/ContextThreadConfig.hpp"

namespace medici::application {

class IPContextThreadConfig : public ContextThreadConfig {
public:
  auto producers() const { return _producers; }
  auto inactivityMicros() const {
    return std::chrono::microseconds{_inactivityMicros};
  }
  auto certFile() const { return _certFile; }
  auto keyFile() const { return _keyFile; }
  auto keyPassword() const { return _keyPassword; }
  IPContextThreadConfig() = default;
  IPContextThreadConfig(const std::string name, std::uint32_t producers = 1,
                        std::uint32_t inactivityMicros = 100,
                        const std::string &certFile = "",
                        const std::string &keyFile = "",
                        const std::string &keyPassword = "",
                        std::optional<std::uint32_t> cpu = {},
                        std::optional<std::uint32_t> schedPolicy = {},
                        std::optional<std::uint32_t> schedPriority = {})

      : ContextThreadConfig{}, _producers{producers},
        _inactivityMicros{inactivityMicros}, _certFile{certFile},
        _keyFile{keyFile}, _keyPassword{keyPassword} {
    _runContextType = "LiveIPEndpointContext";
    _name = name;
    _cpu = cpu;
    _schedPolicy = schedPolicy;
    _schedPriority = schedPriority;
  }

  std::uint32_t _producers{1};
  std::uint32_t _inactivityMicros{100};
  std::string _certFile{};
  std::string _keyFile{};
  std::string _keyPassword{};
};

} // namespace medici::application