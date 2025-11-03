#pragma once

#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <format>

namespace medici::shm_endpoints {

inline const char *getBoostInterprocErrorText(
    const boost::interprocess::interprocess_exception &ex) {
  switch (ex.get_error_code()) {
  case boost::interprocess::no_error:
    return "No error";
  case boost::interprocess::system_error:
    return "System error";
  case boost::interprocess::other_error:
    return "Other error";
  case boost::interprocess::security_error:
    return "Security error";
  case boost::interprocess::read_only_error:
    return "Read only error";
  case boost::interprocess::io_error:
    return "IO error";
  case boost::interprocess::path_error:
    return "Path error";
  case boost::interprocess::not_found_error:
    return "Not found error";
  case boost::interprocess::busy_error:
    return "Resource busy error";
  case boost::interprocess::already_exists_error:
    return "Already exists error";
  case boost::interprocess::not_empty_error:
    return "Not empty error";
  case boost::interprocess::is_directory_error:
    return "Is directory error";
  case boost::interprocess::out_of_space_error:
    return "Out of space error";
  case boost::interprocess::out_of_memory_error:
    return "Out of memory error";
  case boost::interprocess::out_of_resource_error:
    return "Out of resource error";
  case boost::interprocess::lock_error:
    return "Lock error";
  case boost::interprocess::sem_error:
    return "Semaphore error";
  case boost::interprocess::mode_error:
    return "Mode error";
  case boost::interprocess::size_error:
    return "Size error";
  case boost::interprocess::corrupted_error:
    return "Corrupted error";
  case boost::interprocess::not_such_file_or_directory:
    return "No such file or directory";
  case boost::interprocess::invalid_argument:
    return "Invalid argument";
  case boost::interprocess::timeout_when_locking_error:
    return "Timeout when locking";
  case boost::interprocess::timeout_when_waiting_error:
    return "Timeout when waiting";
  default:
    return "Unknown interprocess error";
  }
}

} // namespace medici::shm_endpoints