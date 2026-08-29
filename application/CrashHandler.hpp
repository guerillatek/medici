#pragma once

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <iostream>
#include <mutex>
#include <ostream>
#include <pthread.h>
#include <stacktrace>
#include <string>

namespace medici::application {

// Reusable, process-wide crash diagnostics support. Once installed, a stack
// trace (and, where available, the offending thread's name and any active
// exception) is written to the supplied output stream on a fatal signal, an
// unhandled exception, or a direct call to std::terminate().
//
// Signals such as SIGSEGV/SIGABRT are delivered to the thread that caused
// them, and an uncaught exception on a std::jthread invokes std::terminate()
// on that same thread, so a single install() from main() is enough to cover
// crashes on every thread managed by AppRunContextManager as well as the
// thread that launched it.
//
// Note: this is a best-effort diagnostic aid, not an async-signal-safe
// handler - std::stacktrace/std::format/iostream may allocate - but that
// tradeoff is acceptable since the process is about to terminate regardless.
class CrashHandler {
public:
  static CrashHandler &instance() {
    static CrashHandler handler;
    return handler;
  }

  CrashHandler(const CrashHandler &) = delete;
  CrashHandler &operator=(const CrashHandler &) = delete;

  // Directs future crash dumps to `out` and registers the signal/terminate
  // handlers. Safe to call repeatedly, e.g. to retarget the output stream.
  static void install(std::ostream &out = std::cerr,
                      std::initializer_list<int> signals = {
                          SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
    auto &self = instance();
    {
      std::lock_guard lock{self._mutex};
      self._output = &out;
    }
    std::set_terminate(&CrashHandler::onTerminate);
    for (int signalNum : signals) {
      std::signal(signalNum, &CrashHandler::onFatalSignal);
    }
  }

private:
  CrashHandler() = default;

  static void onFatalSignal(int signalNum) {
    instance().dumpStackTrace(
        std::format("fatal signal {} ({})", signalNum, strsignal(signalNum)));
    std::signal(signalNum, SIG_DFL);
    std::raise(signalNum);
  }

  static void onTerminate() {
    if (auto activeException = std::current_exception()) {
      try {
        std::rethrow_exception(activeException);
      } catch (const std::exception &e) {
        instance().dumpStackTrace(
            std::format("unhandled exception: {}", e.what()));
      } catch (...) {
        instance().dumpStackTrace("unhandled exception of unknown type");
      }
    } else {
      instance().dumpStackTrace("std::terminate() called");
    }
    // Restore the default SIGABRT disposition first so the abort() below
    // doesn't re-enter onFatalSignal() and produce a duplicate dump.
    std::signal(SIGABRT, SIG_DFL);
    std::abort();
  }

  static std::string currentThreadName() {
    char name[16] = {};
    if (pthread_getname_np(pthread_self(), name, sizeof(name)) == 0 &&
        name[0] != '\0') {
      return std::string{name};
    }
    return "<unnamed>";
  }

  void dumpStackTrace(const std::string &reason) {
    std::lock_guard lock{_mutex};
    auto &out = _output ? *_output : std::cerr;
    out << std::format("=== CRASH on thread '{}': {} ===\n",
                       currentThreadName(), reason)
        << std::stacktrace::current() << std::endl;
  }

  std::ostream *_output{&std::cerr};
  std::mutex _mutex;
};

} // namespace medici::application
