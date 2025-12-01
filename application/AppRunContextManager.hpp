#pragma once

#include "medici/application/ContextThreadConfig.hpp"
#include "medici/application/IAppContext.hpp"
#include "medici/application/IPAppRunContext.hpp"
#include "medici/event_queue/concepts.hpp"
#include "medici/shm_endpoints/SharedMemEndpointFactory.hpp"
#include "medici/sockets/live/IPEndpointPollManager.hpp"
#include "medici/sockets/live/LiveSocketFactory.hpp"

#include <Aeron.h>
#include <format>
#include <map>
#include <memory>
#include <thread>
#include <vector>

namespace medici::application {

class IPEndpointPollManagerWithShmSupport
    : public medici::sockets::live::IPEndpointPollManager {
public:
  using SharedMemEndpointFactoryT =
      shm_endpoints::SharedMemEndpointFactory<event_queue::IEventQueue>;

  SharedMemEndpointFactoryT _shmEndpointFactory;

  IPEndpointPollManagerWithShmSupport(const std::string &name,
                                      const ClockNowT &clock,
                                      event_queue::IEventQueue &eventQueue,
                                      const std::string &certFile = "",
                                      const std::string &keyFile = "")
      : medici::sockets::live::IPEndpointPollManager(name, clock, eventQueue,
                                                     certFile, keyFile),
        _shmEndpointFactory{eventQueue} {}

  SharedMemEndpointFactoryT &getSharedMemEndpointFactory() {
    return _shmEndpointFactory;
  }

  ExpectedEventsCount pollAndDispatchEndpointsEvents() {
    auto result = medici::sockets::live::IPEndpointPollManager::
        pollAndDispatchEndpointsEvents();
    if (!result) {
      return result;
    }

    auto shmResult = _shmEndpointFactory.pollAndDispatchEndpointsEvents();
    if (!shmResult) {
      return shmResult;
    }

    return *result + *shmResult;
  }
};

using ContextThreadConfigList = std::vector<ContextThreadConfigPtr>;

template <std::uint32_t ServiceRequestQueueSize = 256,
          std::uint32_t PublisherQueueSize = 1024>
class AppRunContextManager {

  using IAppContextPtr = std::shared_ptr<IAppContext>;
  using ExpectedContextPtr = std::expected<IAppContextPtr, std::string>;
  using ContextFactoryFunction =
      std::function<ExpectedContextPtr(const ContextThreadConfig &)>;
  using ContextFactoryFunctionRegistry =
      std::map<std::string, ContextFactoryFunction>;

  using ThreadContextPair = std::pair<std::jthread, IAppContextPtr>;

  struct ContextWithRunParams {
    std::string name;
    IAppContextPtr context;
    std::optional<std::uint32_t> cpu;
    std::optional<std::uint32_t> schedPolicy;
    std::optional<std::uint32_t> schedPriority;
  };

public:
  using LiveIPEndpointContextT = IPAppRunContext<
      medici::sockets::live::LiveSocketFactory, medici::SystemClockNow,
      medici::sockets::live::IPEndpointPollManager, ServiceRequestQueueSize>;

  using LiveIPShmEndpointContextT = IPAppRunContext<
      medici::sockets::live::LiveSocketFactory, medici::SystemClockNow,
      IPEndpointPollManagerWithShmSupport, ServiceRequestQueueSize>;

  AppRunContextManager() {
    _contextFactoryRegistry["LiveIPEndpointContext"] =
        [this](const ContextThreadConfig &config) -> ExpectedContextPtr {
      auto ipConfig = dynamic_cast<const IPContextThreadConfig *>(&config);
      if (ipConfig == nullptr) {
        return std::unexpected{
            "AppRunContextManager: Invalid config type for IP context"};
      }
      return std::make_shared<LiveIPEndpointContextT>(*ipConfig, _clock);
    };

    _contextFactoryRegistry["LiveIPShmEndpointContext"] =
        [this](const ContextThreadConfig &config) -> ExpectedContextPtr {
      auto ipConfig = dynamic_cast<const IPContextThreadConfig *>(&config);
      if (ipConfig == nullptr) {
        return std::unexpected{
            "AppRunContextManager: Invalid config type for IP context"};
      }
      return std::make_shared<LiveIPShmEndpointContextT>(*ipConfig,
                                                                    _clock);
    };
  }

  Expected configureContexts(const ContextThreadConfigList &configs) {
    for (const auto &config : configs) {
      auto it = _contextFactoryRegistry.find(config->runContextType());
      if (it == _contextFactoryRegistry.end()) {
        return std::unexpected(std::string{std::format(
            "No factory for run context type {}", config->runContextType())});
      }
      auto contextOrError = it->second(*config);
      if (!contextOrError) {
        return std::unexpected(
            std::string{std::format("Failed to create run context {}: {}",
                                    config->name(), contextOrError.error())});
      }
      auto result = _contextLookup.emplace(
          config->name(),
          ContextWithRunParams{.name = config->name(),
                               .context = *contextOrError,
                               .cpu = config->_cpu,
                               .schedPolicy = config->_schedPolicy,
                               .schedPriority = config->_schedPriority});
      if (!result.second) {
        return std::unexpected(std::string{
            std::format("Duplicate run context name {}", config->name())});
      }
    }
    return {};
  }

  Expected startAllThreads() {
    for (auto &entry : _contextLookup) {
      auto threadName = entry.first;
      auto &context = entry.second.context;
      _threadsByName.emplace(threadName, std::jthread{[&context, threadName]() {
                               if (auto result = context->start(); !result) {
                                 return;
                               }
                             }});
      if (entry.second.cpu) {
        if (auto result = set_thread_cpu_affinity(
                _threadsByName[threadName].native_handle(), *entry.second.cpu);
            !result) {
          return std::unexpected(std::string{
              std::format("Failed to set CPU affinity for thread {}: {}",
                          threadName, result.error())});
        }
      }
      if (entry.second.schedPolicy && entry.second.schedPriority) {
        if (auto result = set_thread_sched_policy(
                _threadsByName[threadName].native_handle(),
                *entry.second.schedPolicy, *entry.second.schedPriority);
            !result) {
          return std::unexpected(std::string{
              std::format("Failed to set scheduling policy for thread {}: {}",
                          threadName, result.error())});
        }
      }
    }
    for (auto &threadEntry : _threadsByName) {
      if (threadEntry.second.joinable()) {
        threadEntry.second.join();
      }
    }
    return {};
  }

  void stopAllThreads() {
    for (auto &entry : _contextLookup) {
      entry.second.context->stop();
    }
  }

  template <typename AppRunContextType>
  AppRunContextType &getAppRunContext(const std::string name) {
    auto it = _contextLookup.find(name);
    if (it == _contextLookup.end()) {
      throw std::runtime_error(
          std::format("No run context with name {}", name));
    }
    auto context = dynamic_cast<AppRunContextType *>(it->second.context.get());
    if (context == nullptr) {
      throw std::runtime_error(
          std::format("Run context {} is not of requested type", name));
    }
    return *context;
  }

  std::uint32_t getThreadCount() { return _contextLookup.size(); }

private:
  Expected set_thread_cpu_affinity(std::jthread::native_handle_type handle,
                                   std::uint32_t cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);

    int result = pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
      return std::unexpected(
          std::format("pthread_setaffinity_np failed: {}", strerror(result)));
    }
    return {};
  }

  Expected set_thread_sched_policy(std::jthread::native_handle_type handle,
                                   std::uint32_t policy,
                                   std::uint32_t priority) {
    struct sched_param param;
    param.sched_priority = priority;

    int result = pthread_setschedparam(handle, policy, &param);
    if (result != 0) {
      return std::unexpected(
          std::format("pthread_setschedparam failed: {}", strerror(result)));
    }
    return {};
  }

  std::map<std::string, ContextWithRunParams> _contextLookup;
  std::map<std::string, std::jthread> _threadsByName;
  medici::SystemClockNow _clock{};
  ContextFactoryFunctionRegistry _contextFactoryRegistry;
};

} // namespace medici::application
