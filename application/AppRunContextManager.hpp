#pragma once

#include "medici/application/AeronAppIOContext.hpp"
#include "medici/application/AppRunContextConfig.hpp"
#include "medici/application/IAppContext.hpp"
#include "medici/application/IPAppRunContext.hpp"

#include <Aeron.h>
#include <core/ts_log.h>
#include <format>
#include <map>
#include <memory>
#include <thread>
#include <vector>
namespace medici::application {

using AppRunContextConfigList = std::vector<AppRunContextConfig>;

template <sockets::SocketFactoryC SocketFactoryT, ClockNowC ClockNowT,
          EndpointEventPollMgrC IPEndpointEventPollMgrT,
          std::uint32_t ServiceRequestQueueSize = 256,
          std::uint32_t PublisherQueueSize = 1024>
class AppRunContextManager {

  using IAppContextPtr = std::shared_ptr<IAppContext>;
  using ThreadContextPair = std::pair<std::jthread, IAppContextPtr>;
  struct ContextWithRunParams {
    std::string name;
    IAppContextPtr context;
    std::optional<std::uint32_t> cpu;
    std::optional<std::uint32_t> schedPolicy;
    std::optional<std::uint32_t> schedPriority;
  };

public:
  using IPAppRunContextT =
      IPAppRunContext<SocketFactoryT, ClockNowT, IPEndpointEventPollMgrT,
                      ServiceRequestQueueSize>;

  AppRunContextManager(const AppRunContextConfigList &configList) {
    for (const AppRunContextConfig &config : configList) {
      if (config.runContextType() == "IPEndpoints") {
        _contextByIndex.emplace_back(ContextWithRunParams{
            config.name(), std::make_shared<IPAppRunContextT>(config, _clock),
            config.cpu(), config.schedPolicy(), config.schedPriority()});
        _contextLookup[config.name()] = _contextByIndex.back().context;
        continue;
      }
      throw std::runtime_error(
          std::format("Unknown run context type, {} specified in config",
                      config.runContextType()));
    }
  }

  void stopAllThreads() {
    for (auto &entry : _contextByIndex) {
      entry.context->stop();
    }
  }

  // Used by component manager to start run context
  // with designated index
  void startRunContext(std::uint32_t index) {
    if (index >= _contextByIndex.size()) {
      return;
    }

    auto result = _contextByIndex[index].context->start();
    if (!result) {
      stopAllThreads();
      throw std::runtime_error(result.error());
    }
  }
  // Used by component manager to provide access to particular
  // run context specified by a component in it's configuration
  auto &getAppRunContextWithRunParams(std::uint32_t index) {
    return _contextByIndex[index];
  }

  // Used by component manager to provide access to particular
  // run context specified by a component in it's configuration
  template <typename AppRunContextType>
  AppRunContextType &getAppRunContext(const std::string name) {
    auto entry = _contextLookup.find(name);
    if (entry == _contextLookup.end()) {
      throw std::runtime_error(
          std::format("No run context configured with name {}", name));
    }
    auto entryPtr = dynamic_cast<AppRunContextType *>(entry->second.get());
    if (entryPtr == nullptr) {
      throw std::runtime_error(std::format(
          "Run context configure with name {} is not of cast type", name));
    }

    return *entryPtr;
  }

  std::uint32_t getThreadCount() { return _contextByIndex.size(); }

private:
  std::vector<ContextWithRunParams> _contextByIndex;
  std::map<std::string, std::shared_ptr<IAppContext>> _contextLookup;
  ClockNowT _clock{};
};

} // namespace medici::application
