#include <drogon/HttpMiddleware.h>
#ifndef JWT_DISABLE_PICOJSON
#define JWT_DISABLE_PICOJSON
#endif
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include <string>

#include "../config/config.hpp"

using drogon::HttpResponse;
using drogon::k401Unauthorized;

class AuthMiddleware : public drogon::HttpCoroMiddleware<AuthMiddleware> {
 public:
  AuthMiddleware() = default;

  drogon::Task<drogon::HttpResponsePtr> invoke(
      const drogon::HttpRequestPtr &req,
      drogon::MiddlewareNextAwaiter &&next) override {
    // Skip OPTIONS requests (which is used for CORS)
    if (req->getMethod() == drogon::HttpMethod::Options) {
      auto resp = co_await next;
      co_return resp;
    }
    try {
      // Authorization header with Bearer prefix
      const std::string &auth_header = req->getHeader("Authorization");

      if (auth_header.empty() || auth_header.substr(0, 7) != "Bearer ") {
        auto resp = HttpResponse::newHttpJsonResponse(
            {{"error", "Unauthorized: No valid token provided"}});
        resp->setStatusCode(k401Unauthorized);
        co_return resp;
      }

      // Extract the token
      std::string token = auth_header.substr(7);

      using traits = jwt::traits::open_source_parsers_jsoncpp;
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
      auto resp = co_await next;
      co_return resp;
    } catch (const std::exception &e) {
      LOG_ERROR << "Auth error: " << e.what();
      auto resp = HttpResponse::newHttpJsonResponse(
          {{"error", "Unauthorized: Invalid token"}});
      resp->setStatusCode(k401Unauthorized);
      co_return (resp);
    }
  }
};
