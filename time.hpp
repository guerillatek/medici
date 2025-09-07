
#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <type_traits>

namespace medici {
using SystemClock = std::chrono::system_clock;
using TimePoint = std::chrono::time_point<SystemClock>;

using SystemClock = std::chrono::system_clock;

struct SystemClockNow {
  TimePoint operator()() const { return SystemClock::now(); }
};

template <typename T>
concept ClockNowC = requires(T t) {
  { t() } -> std::same_as<TimePoint>;
};

using ClockNowT = std::function<TimePoint()>;

template <typename T>
concept DurationC = requires(T t) {
  typename std::chrono::duration<typename T::rep, typename T::period>;
};

inline TimePoint parseTimestamp(std::string_view timestamp) {
  std::tm tm = {};
  uint64_t nanoseconds = 0;

  // Parse date and time (YYYY-MM-DDTHH:MM:SS)
  if (strptime(timestamp.data(), "%Y-%m-%dT%H:%M:%S", &tm) == nullptr) {
    return TimePoint{};
  }

  // Move to fractional part if exists
  const char *frac_start = strchr(timestamp.data(), '.');
  if (frac_start) {
    frac_start++; // Skip the dot
    int frac_len = 0;

    // Parse up to 9 fractional digits
    while (frac_len < 9 && frac_start[frac_len] >= '0' &&
           frac_start[frac_len] <= '9') {
      nanoseconds = nanoseconds * 10 + (frac_start[frac_len] - '0');
      frac_len++;
    }

    // Pad with zeros if fewer than 9 digits
    while (frac_len++ < 9) {
      nanoseconds *= 10;
    }
  }

  // Parse timezone offset if exists
  int tz_offset = 0;
  const char *tz_start = frac_start ? frac_start + strcspn(frac_start, "+-Z")
                                    : timestamp.data() + timestamp.size();
  if (*tz_start == '+' || *tz_start == '-') {
    int sign = (*tz_start == '-') ? -1 : 1;
    int hours = (tz_start[1] - '0') * 10 + (tz_start[2] - '0');
    int minutes = (tz_start[4] - '0') * 10 + (tz_start[5] - '0');
    tz_offset = sign * (hours * 3600 + minutes * 60);
  }

  // Calculate epoch time
  uint64_t seconds_since_epoch = static_cast<uint64_t>(timegm(&tm)) - tz_offset;
  return TimePoint{
      std::chrono::nanoseconds{seconds_since_epoch * 1000000000 + nanoseconds}};
}

} // namespace medici
