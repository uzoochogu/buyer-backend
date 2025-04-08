#pragma once
#include <drogon/drogon.h>

#include <string>

namespace config {
inline std::string get_jwt_secret() {
  static std::string secret;

  // Only fetch once
  if (secret.empty()) {
    const Json::Value& config = drogon::app().getCustomConfig();
    if (config.isMember("jwt_secret")) {
      secret = config["jwt_secret"].asString();
    } else {
      secret = "default_secret";  // Fallback
    }
  }

  return secret;
}
static inline const std::string JWT_SECRET = get_jwt_secret();
}  // namespace config