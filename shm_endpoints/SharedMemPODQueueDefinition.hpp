#pragma once

#include "medici/event_queue/concepts.hpp"
#include "medici/shm_endpoints/interfaces.hpp"

#include <algorithm>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <cstdint>
#include <cstring>
#include <optional>
#include <pthread.h>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

// This is a multichannel interprocess queue for POD
// types. It uses the SharedMemoryObjectManager template for shared memory
// management and c++ atomic operations for synchronization. The queue supports
// a fixed number of channels, each with its own SPSC queue (Ring Buffer) with
// Each message is tagged with a type index to allow for multiple POD message
// types to be sent through the same queue. Support for more sophisticated
// message serialization can be trivially be added by defining POD types that
// can contain the serialized data.

namespace medici::shm_endpoints {

using Expected = medici::event_queue::Expected;
using AsyncExpected = medici::event_queue::AsyncExpected;

template <typename T>
concept IsPodType =
    std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

template <typename T, typename... MessageTypes>
static constexpr bool IsPodTypeGroup() {
  if constexpr (sizeof...(MessageTypes) == 0) {
    return IsPodType<T>;
  } else {
    return IsPodType<T> && IsPodTypeGroup<MessageTypes...>();
  }
}

template <typename TargetT, typename H, typename... MessageTypes>
static constexpr std::int32_t GetTypeIndex(std::tuple<H, MessageTypes...> &&,
                                           std::int32_t currentIndex = 0) {
  if constexpr (sizeof...(MessageTypes) == 0) {
    if constexpr (std::is_same_v<TargetT, H>) {
      return currentIndex;
    } else {
      return -1; // Not found
    }
  } else {
    if constexpr (std::is_same_v<TargetT, H>) {
      return currentIndex;
    }
    return GetTypeIndex<TargetT, MessageTypes...>(std::tuple<MessageTypes...>{},
                                                  currentIndex + 1);
  }
}

template <typename T> class FixedConstructLenSPSC {

public:
  char name[64]{0};

private:
  std::atomic<size_t> _head{0};
  std::atomic<size_t> _tail{0};
  const std::uint32_t _queueSize;
  T _storage[];

public:
  FixedConstructLenSPSC(std::uint32_t queueSize) : _queueSize(queueSize + 1) {}

  bool enqueue(const T &value) {
    auto targetTail = (_tail.load(std::memory_order_relaxed) + 1) % _queueSize;
    if (targetTail == _head.load(std::memory_order_acquire)) {
      return false;
    }
    _storage[_tail.load(std::memory_order_relaxed)] = value;
    _tail.store(targetTail, std::memory_order_release);
    return true;
  }

  // Direct consumption with check for updates
  std::expected<bool, std::string> consume(auto &&handler) {
    auto currentHead = _head.load(std::memory_order_relaxed);
    if (currentHead == _tail.load(std::memory_order_acquire)) {
      return false;
    }

    auto result = handler(_storage[currentHead]);
    auto nextHead = (currentHead + 1) % _queueSize;
    _head.store(nextHead, std::memory_order_release);
    if (!result) {
      return std::unexpected(result.error());
    }
    return true;
  }

  // Polling interface to check for updates and consume separately
  bool updatesAvailable() {
    auto currentHead = _head.load(std::memory_order_relaxed);
    if (currentHead == _tail.load(std::memory_order_acquire)) {
      return false;
    }
    return true;
  }

  Expected consumeAvailable(auto &&handler) {
    auto currentHead = _head.load(std::memory_order_relaxed);
    auto result = handler(_storage[currentHead]);
    auto nextHead = (currentHead + 1) % _queueSize;
    _head.store(nextHead, std::memory_order_release);
    return result;
  }

  FixedConstructLenSPSC *next() {
    return reinterpret_cast<FixedConstructLenSPSC *>(
        reinterpret_cast<std::byte *>(this) + sizeof(FixedConstructLenSPSC<T>) +
        sizeof(T) * _queueSize);
  }

  static constexpr size_t getQueueSize(std::uint32_t storageEntries) {
    return sizeof(FixedConstructLenSPSC<T>) + sizeof(T) * (storageEntries + 1);
  }
};

enum class ShmQueueType { SPMCQueue, MPSCQueue, FullDuplexQueue };

template <typename... MessageTypes> struct SharedMemPODQueueDefinition {
  static_assert(IsPodTypeGroup<MessageTypes...>(),
                "All MessageTypes must be POD types");

  using byte_storage_array = alignas(std::max({alignof(MessageTypes)...}))
      std::byte[std::max({sizeof(MessageTypes)...})];

  using TypeTuple = std::tuple<MessageTypes...>;

  struct MessageQueueEntry {
    std::uint32_t _typeIndex;
    byte_storage_array messageData;
    // Same accessor methods...
  };

  template <std::uint32_t I = 0>
  static Expected dispatchToHandler(const MessageQueueEntry &entry,
                                    auto &&handler) {
    if constexpr (I < sizeof...(MessageTypes)) {
      using CurrentType = std::tuple_element_t<I, TypeTuple>;
      if (entry._typeIndex == I) {
        return handler(
            *reinterpret_cast<const CurrentType *>(entry.messageData));
      } else {
        return dispatchToHandler<I + 1>(
            entry, std::forward<decltype(handler)>(handler));
      }
    } else {
      return std::unexpected("Unknown message type index");
    }
  }

  using QueueChannelT = FixedConstructLenSPSC<MessageQueueEntry>;

  struct MutexGuard {
    pthread_mutex_t *mutex;
    ~MutexGuard() { pthread_mutex_unlock(mutex); }
  };

  struct SharedMemoryLayout {
    // Metadata stored in shared memory for consumers to discover
    const ShmQueueType _queueType;
    const std::uint32_t _allowedChannels;
    const std::uint32_t _consumerQueueSize;
    const std::uint64_t _messageTypesHash; // Hash of MessageTypes... tuple
    std::uint32_t _activeIncomingChannel{0};
    std::uint32_t _activeOutgoingChannel{0};
    std::atomic<std::uint32_t> _activeChannels{0};

    // Multiprocess mutex for channel allocation synchronization
    pthread_mutex_t _allocationMutex;

    QueueChannelT _queueChannels[];

    SharedMemoryLayout(ShmQueueType queueType, std::uint32_t consumerCount,
                       std::uint32_t consumerQueueSize)
        : _queueType{queueType}, _allowedChannels{consumerCount},
          _consumerQueueSize{consumerQueueSize},
          _messageTypesHash{typeid(std::tuple<MessageTypes...>).hash_code()},
          _activeChannels{0} {

      // Initialize multiprocess mutex
      pthread_mutexattr_t attr;
      pthread_mutexattr_init(&attr);
      pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);

      int result = pthread_mutex_init(&_allocationMutex, &attr);
      pthread_mutexattr_destroy(&attr);
      if (result != 0) {
        throw std::runtime_error("Failed to initialize multiprocess mutex: " +
                                 std::string(strerror(result)));
      }

      // Initialize queue channels
      QueueChannelT *targetChannel = &_queueChannels[0];
      for (std::uint32_t i = 0; i < consumerCount; ++i) {
        std::construct_at(targetChannel, consumerQueueSize);
        targetChannel = targetChannel->next();
      }
    }

    ~SharedMemoryLayout() { pthread_mutex_destroy(&_allocationMutex); }

    auto &getQueueChannel(std::uint32_t consumerIndex) {
      QueueChannelT *zeroOffset = &_queueChannels[0];
      auto consumerQueue =
          reinterpret_cast<std::byte *>(zeroOffset) +
          QueueChannelT::getQueueSize(_consumerQueueSize) * (consumerIndex);
      return *reinterpret_cast<QueueChannelT *>(consumerQueue);
    }

    auto &getActiveOutgoingChannel() {
      return getQueueChannel(_activeOutgoingChannel);
    }

    auto &getActiveIncomingChannel() {
      return getQueueChannel(_activeIncomingChannel);
    }

    auto &AllocQueueChannel(const std::string &consumerName) {
      // Lock the mutex for exclusive access
      int lockResult = pthread_mutex_lock(&_allocationMutex);
      if (lockResult != 0) {
        throw std::runtime_error("Failed to acquire allocation mutex: " +
                                 std::string(strerror(lockResult)));
      }

      // Ensure we unlock even if an exception is thrown
      MutexGuard guard{&_allocationMutex};

      // Get current active channel count
      auto currentChannels = _activeChannels.load(std::memory_order_acquire);

      // Check if name is already allocated by examining existing channel names
      for (std::uint32_t i = 0; i < currentChannels; ++i) {
        auto &existingChannel = getQueueChannel(i);
        if (existingChannel.name[0] != '\0' &&
            std::strncmp(existingChannel.name, consumerName.c_str(),
                         sizeof(existingChannel.name) - 1) == 0) {
          throw std::runtime_error("Consumer name '" + consumerName +
                                   "' is already allocated");
        }
      }

      if (++currentChannels > _allowedChannels) {
        throw std::runtime_error(std::format(
            "No more consumer channels available, {} in use, attempting {}",
            currentChannels, _allowedChannels));
      }
      // Get the queue channel and set its name
      auto &channel = getQueueChannel(currentChannels - 1);
      std::strncpy(channel.name, consumerName.c_str(),
                   sizeof(channel.name) - 1);
      channel.name[sizeof(channel.name) - 1] = '\0';
      // Update active channels count
      _activeChannels.store(currentChannels, std::memory_order_release);
      return channel;
    }

    Expected enqueueMessageToAllChannels(const MessageQueueEntry &entry,
                                         auto &&backOffHandler) {
      for (std::uint32_t i = 0;
           i < _activeChannels.load(std::memory_order_relaxed); ++i) {
        QueueChannelT &channel = getQueueChannel(i);
        _activeOutgoingChannel = i;
        if (!channel.enqueue(entry)) {
          auto result = dispatchToHandler(
              entry, std::forward<decltype(backOffHandler)>(backOffHandler));
          if (!result) {
            return std::unexpected(result.error());
          }
        }
      }
      return {};
    }

    ExpectedEventsCount consumeMessageFromAllChannels(auto &&handler) {
      std::uint32_t messagesHandled = 0;
      for (std::uint32_t i = 0; i < _activeChannels; ++i) {
        QueueChannelT &channel = getQueueChannel(i);
        _activeIncomingChannel = i;
        while (channel.updatesAvailable()) {
          auto result = channel.consumeAvailable(
              std::forward<decltype(handler)>(handler));
          if (!result) {
            return std::unexpected(result.error());
          }
          ++messagesHandled;
        }
      }
      return messagesHandled;
    }
  };
};

} // namespace medici::shm_endpoints