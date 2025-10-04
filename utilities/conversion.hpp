#ifndef CONVERSION_HPP
#define CONVERSION_HPP

#include <charconv>
#include <concepts>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace convert {
/* constexpr */ inline std::optional<int> string_to_int(std::string_view sv) {
  int value;
  const auto ret = std::from_chars(sv.data(), sv.data() + sv.size(), value);
  if (ret.ec == std::errc{} && ret.ptr == sv.data() + sv.size()) return value;

  return std::nullopt;
};

template <class T>
concept Numeric = std::is_arithmetic_v<T>;

template <Numeric T>
inline std::optional<T> string_to_number(std::string_view sv) {
  std::optional<T> value{{}};  // default init T
  auto ret = std::from_chars(sv.data(), sv.data() + sv.size(), *value);
  if (ret.ec == std::errc{} && ret.ptr == sv.data() + sv.size()) return value;
  return std::nullopt;
}

// Array of strings to PostgreSQL array string
// if empty returns "{}".
inline std::string array_to_pgsql_array_string(
    std::span<const std::string> tags) {
  if (tags.empty()) {
    return "{}";
  }

  std::string result = "{";
  for (size_t i{0}; const auto& tag : tags) {
    if (i > 0) {
      result += ",";
    }
    result += tag;
    ++i;
  }
  result += "}";

  return result;
}

// PostgresSQL array string to std::vector<std::string>
inline std::vector<std::string> pgsql_array_string_to_vector(
    const std::string& array_str) {
  std::vector<std::string> result;

  if (array_str.size() < 2) {
    return result;
  }

  // Parse PostgreSQL array format: {tag1,tag2,tag3}, remove {}
  auto content = std::string_view(array_str.begin() + 1, array_str.end() - 1);

  size_t count = std::count(content.begin(), content.end(), ',') + 1;
  result.reserve(count);

  size_t start = 0;
  size_t end = content.find(',');
  while (end != std::string::npos) {
    result.emplace_back(content.substr(start, end - start));
    start = end + 1;
    end = content.find(',', start);
  }
  if (!content.substr(start).empty()) {
    result.emplace_back(content.substr(start));  // Add the last substring
  }

  return result;
}

}  // namespace convert

#endif
