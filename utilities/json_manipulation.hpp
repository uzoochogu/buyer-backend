#ifndef JSON_MANIPULATION_HPP
#define JSON_MANIPULATION_HPP

#include <glaze/glaze.hpp>
#include <optional>
#include <string>

namespace utilities {

/**
 * @brief Strict read JSON that requires all keys to be present and does not
 * allow unknown keys.
 * @tparam T The type to read into.
 * @param value The value to read into. Of type T.
 * @param buffer The input buffer containing the JSON data.
 * @return glz::error_ctx if there is an error, otherwise returns the parsed
 * value
 */
template <glz::read_supported<glz::JSON> T, glz::is_buffer Buffer>
[[nodiscard]] inline glz::error_ctx strict_read_json(T &value,
                                                     Buffer &&buffer) {
  glz::context ctx{};
  // .error_on_unknown_keys = true,
  return read<glz::opts{.error_on_missing_keys = true}>(
      value, std::forward<Buffer>(buffer), ctx);
}

/**
 * @brief Strict read JSON that requires all keys to be present and does not
 * allow unknown keys.
 * @tparam T The type to read into.
 * @param buffer The input buffer containing the JSON data.
 * @return glz::expected containing the parsed value of type T or an error
 * context
 */
template <glz::read_supported<glz::JSON> T, glz::is_buffer Buffer>
[[nodiscard]] inline glz::expected<T, glz::error_ctx> strict_read_json(
    Buffer &&buffer) {
  T value{};
  glz::context ctx{};
  const glz::error_ctx ec = read<glz::opts{.error_on_missing_keys = true}>(
      value, std::forward<Buffer>(buffer), ctx);
  if (ec) {
    return glz::unexpected<glz::error_ctx>(ec);
  }
  return value;
}

/**
 * @brief Relaxed read JSON that allows unknown keys and missing keys.
 * @tparam T The type to read into.
 * @param value The value to read into. Of type T.
 * @param buffer The input buffer containing the JSON data.
 * @return glz::error_ctx if there is an error, otherwise returns the parsed
 * value
 */
template <glz::read_supported<glz::JSON> T, glz::is_buffer Buffer>
[[nodiscard]] inline glz::error_ctx relaxed_read_json(T &value,
                                                      Buffer &&buffer) {
  glz::context ctx{};
  // .error_on_missing_keys = false (default)
  return read<glz::opts{.error_on_unknown_keys = false}>(
      value, std::forward<Buffer>(buffer), ctx);
}

/**
 * @brief Relaxed read JSON that allows unknown keys and missing keys.
 * @tparam T The type to read into.
 * @param buffer The input buffer containing the JSON data.
 * @return glz::expected containing the parsed value of type T or an error
 * context
 */
template <glz::read_supported<glz::JSON> T, glz::is_buffer Buffer>
[[nodiscard]] inline glz::expected<T, glz::error_ctx> relaxed_read_json(
    Buffer &&buffer) {
  T value{};
  glz::context ctx{};
  const glz::error_ctx ec = read<glz::opts{.error_on_unknown_keys = false}>(
      value, std::forward<Buffer>(buffer), ctx);
  if (ec) {
    return glz::unexpected<glz::error_ctx>(ec);
  }
  return value;
}

}  // namespace utilities

#endif  // JSON_MANIPULATION_HPP
