#include <drogon/HttpMiddleware.h>
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

#include <string>

#include "../config/config.hpp"

using traits = jwt::traits::nlohmann_json;
using namespace drogon;

class WebSocketAuthMiddleware : public HttpMiddleware<WebSocketAuthMiddleware> {
 public:
  WebSocketAuthMiddleware() {};

  void invoke(const HttpRequestPtr &req, MiddlewareNextCallback &&nextCb,
              MiddlewareCallback &&mcb) override {
    // Skip OPTIONS requests (for CORS)
    if (req->getMethod() == HttpMethod::Options) {
      nextCb(std::move(mcb));
      return;
    }

    try {
      std::string token;

      // Try to get token from query parameter first
      auto token_param = req->getParameter("token");
      if (!token_param.empty()) {
        token = token_param;
      } else {
        // Fallback to Authorization header
        const std::string &auth_header = req->getHeader("Authorization");
        if (!auth_header.empty() && auth_header.substr(0, 7) == "Bearer ") {
          token = auth_header.substr(7);
        }
      }

      if (token.empty()) {
        LOG_ERROR << "WebSocket connection rejected: No token provided";
        auto resp = HttpResponse::newHttpJsonResponse(
            {{"error", "Unauthorized: No token provided"}});
        resp->setStatusCode(k401Unauthorized);
        mcb(resp);
        return;
      }

      // Verify the token
      auto decoded = jwt::decode<traits>(token);

      // Verify signature and expiration
      auto verifier =
          jwt::verify<traits>()
              .allow_algorithm(jwt::algorithm::hs256{config::JWT_SECRET})
              .with_issuer("buyer-app");

      verifier.verify(decoded);

      // Extract user ID from token and add to request attributes for later use
      // The user_id is stored as a string in the JWT, so extract it as a string
      std::string user_id;
      try {
        // First try as string
        user_id = decoded.get_payload_claim("user_id").as_string();
      } catch (...) {
        try {
          // If that fails, try as integer and convert to string
          user_id =
              std::to_string(decoded.get_payload_claim("user_id").as_integer());
        } catch (const std::exception &e) {
          LOG_ERROR << "Failed to extract user_id from token: " << e.what();
          throw;
        }
      }

      // Pass user_id through request attributes
      req->getAttributes()->insert("current_user_id", user_id);

      // Token is valid, proceed to the next middleware/controller
      nextCb(std::move(mcb));
    } catch (const std::exception &e) {
      LOG_ERROR << "Websocket auth error: " << e.what();
      auto resp = HttpResponse::newHttpJsonResponse(
          {{"error", "Unauthorized: Invalid token"}});
      resp->setStatusCode(k401Unauthorized);
      mcb(resp);
    }
  }
};
