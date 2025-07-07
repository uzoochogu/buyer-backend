#pragma once
#include <drogon/drogon.h>

#include <string>

namespace config {
inline std::string get_config_value(const std::string &key,
                                    const std::string &default_value) {
  const Json::Value &config = drogon::app().getCustomConfig();
  if (config.isMember(key)) {
    return config[key].asString();
  }
  return default_value;
}

// Only fetch once
static inline const std::string JWT_SECRET =
    get_config_value("jwt_secret", "default_secret");

}  // namespace config
