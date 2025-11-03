#pragma once

#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace medici::shm_endpoints {

enum class OpenMode { CREATE_OR_OPEN, OPEN_ONLY };

struct SharedMemBuilderBase {

  // Open existing segment only (no size requirements)
  SharedMemBuilderBase(const std::string &name) : _name(name) {
    _unlink_on_destroy = false;

    // Try opening existing shared memory
    _fd = shm_open(_name.c_str(), O_RDWR, 0);
    if (_fd == -1)
      throw std::system_error(errno, std::system_category(),
                              "shm_open (open_only) failed");

    // Verify size and map it
    struct stat st;
    if (fstat(_fd, &st) == -1)
      throw std::system_error(errno, std::system_category(),
                              "fstat failed (open_only)");

    _size = static_cast<std::size_t>(st.st_size);

    _addr = mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
    if (_addr == MAP_FAILED)
      throw std::system_error(errno, std::system_category(),
                              "mmap failed (open_only)");

    _created = false;
    return;
  }

  SharedMemBuilderBase(const std::string &name, std::size_t size)
      : _name(name), _size(size) {
    _unlink_on_destroy = true;
    // CREATE_OR_OPEN
    _fd = shm_open(_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (_fd != -1) {
      // Successfully created
      if (ftruncate(_fd, _size) == -1) {
        shm_unlink(_name.c_str());
        throw std::system_error(errno, std::system_category(),
                                "ftruncate failed");
      }

      _addr = mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
      if (_addr == MAP_FAILED) {
        shm_unlink(_name.c_str());
        throw std::system_error(errno, std::system_category(),
                                "mmap failed (create)");
      }

      _created = true;
      return;
    }

    // If exists, open existing
    if (errno == EEXIST) {
      _fd = shm_open(_name.c_str(), O_RDWR, 0);
      if (_fd == -1)
        throw std::system_error(errno, std::system_category(),
                                "shm_open (existing) failed");

      struct stat st;
      if (fstat(_fd, &st) == -1)
        throw std::system_error(errno, std::system_category(),
                                "fstat failed (existing)");

      std::size_t existing_size = static_cast<std::size_t>(st.st_size);

      // Handle size mismatch by resizing if possible
      if (existing_size != _size) {
        // Try to resize the existing segment
        if (ftruncate(_fd, _size) == -1) {
          throw std::system_error(
              errno, std::system_category(),
              "Failed to resize existing shared memory segment from " +
                  std::to_string(existing_size) + " to " +
                  std::to_string(_size) + " bytes");
        }
      }

      _addr = mmap(nullptr, _size, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
      if (_addr == MAP_FAILED)
        throw std::system_error(errno, std::system_category(),
                                "mmap failed (existing)");

      // Always clear existing memory when size is specified in constructor
      std::memset(_addr, 0, _size);

      _created = false;
      return;
    }

    throw std::system_error(errno, std::system_category(),
                            "shm_open failed (create_or_open)");
  }

  bool created() const noexcept { return _created; }

  void cleanup() noexcept {
    if (_addr) {
      munmap(_addr, _size);
      _addr = nullptr;
    }
    if (_fd != -1) {
      close(_fd);
      _fd = -1;
    }
    if (_created && _unlink_on_destroy) {
      shm_unlink(_name.c_str());
    }
  }

  ~SharedMemBuilderBase() { cleanup(); }

protected:
  std::string _name{};
  std::size_t _size{0};
  bool _unlink_on_destroy{false};
  int _fd{-1};
  void *_addr{nullptr};
  bool _created{false};
};

template <typename ObjectT>
class SharedMemoryObjectManager : public SharedMemBuilderBase {
public:
  template <typename... ConstructorArgs>
  SharedMemoryObjectManager(const std::string &name, std::size_t size,
                            ConstructorArgs &&...args)
      : SharedMemBuilderBase(name, size) {
    std::construct_at<ObjectT>(reinterpret_cast<ObjectT *>(this->_addr),
                               std::forward<ConstructorArgs>(args)...);
  }

  SharedMemoryObjectManager(const std::string &name)
      : SharedMemBuilderBase(name) {}

  SharedMemoryObjectManager(const SharedMemoryObjectManager &) = delete;
  SharedMemoryObjectManager &
  operator=(const SharedMemoryObjectManager &) = delete;

  ObjectT &operator*() noexcept { return *reinterpret_cast<ObjectT *>(_addr); }

  const ObjectT &operator*() const noexcept {
    return *reinterpret_cast<const ObjectT *>(_addr);
  }

  ObjectT *operator->() noexcept { return reinterpret_cast<ObjectT *>(_addr); }

  operator bool() const noexcept { return _addr != nullptr; }

  const ObjectT *operator->() const noexcept {
    return reinterpret_cast<const ObjectT *>(_addr);
  }

  std::size_t size() const noexcept { return _size; }
};

} // namespace medici::shm_endpoints