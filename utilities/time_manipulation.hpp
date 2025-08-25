#ifndef TIME_MANIPULATION_HPP
#define TIME_MANIPULATION_HPP

#include <chrono>
#include <format>
#include <string>

/**
 * Gets SQL compatible UTC timestamp with microseconds precision
 * Format: YYYY-MM-DD HH:MM:SS.ssssss
 */
inline std::string get_precise_sql_utc_timestamp() {
  auto now = std::chrono::utc_clock::now();
  auto seconds = std::chrono::floor<std::chrono::seconds>(now);
  auto us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - seconds);
  return std::format("{}.{:06}", std::format("{:%F %T}", seconds), us.count());
}

#endif  // TIME_MANIPULATION_HPP
