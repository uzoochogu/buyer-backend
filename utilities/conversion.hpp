#ifndef CONVERSION_HPP
#define CONVERSION_HPP

#include <charconv>
#include <optional>
#include <string_view>

namespace convert {
/* constexpr */ inline std::optional<int> string_to_int(std::string_view sv) {
  std::optional<int> value{};
  const auto ret = std::from_chars(sv.data(), sv.data() + sv.size(), *value);
  if (ret.ec == std::errc{}) return value;

  return std::nullopt;
};
}  // namespace convert

#endif
