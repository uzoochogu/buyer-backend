#ifndef TIME_MANIPULATION_HPP
#define TIME_MANIPULATION_HPP

#include <chrono>
#include <format>
#include <string>

inline std::string get_current_utc_timestamp() {
  auto now = std::chrono::utc_clock::now();
  std::string timestamp = std::format("{:%F %T}", now);

  // Get microseconds
  auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()) %
            1000000;
  return std::format(".{:06}", us.count());
}

#endif  // TIME_MANIPULATION_HPP
