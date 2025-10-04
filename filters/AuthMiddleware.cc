#include <drogon/HttpMiddleware.h>
#ifndef JWT_DISABLE_PICOJSON
#define JWT_DISABLE_PICOJSON
#endif
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include <glaze/glaze.hpp>
#include <string>

#include "../config/config.hpp"
#include "../controllers/common_req_n_resp.hpp"

using drogon::HttpResponse;

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
        auto resp = HttpResponse::newHttpResponse(drogon::k401Unauthorized,
                                                  drogon::CT_APPLICATION_JSON);
        SimpleError err{.error = "Unauthorized: No valid token provided"};
        resp->setBody(glz::write_json(err).value_or(""));
        co_return resp;
      }
      std::string token = auth_header.substr(7);

      using traits = jwt::traits::open_source_parsers_jsoncpp;
      auto decoded = jwt::decode<traits>(token);

      // Verify signature and expiration
      auto verifier =
          jwt::verify<traits>()
              .allow_algorithm(jwt::algorithm::hs256{config::JWT_SECRET})
              .with_issuer("buyer-app");

      // Extract user ID from token and add to request attributes for later use
      verifier.verify(decoded);
      auto claim = decoded.get_payload_claim("user_id");
      auto payload_type = claim.get_type();
      std::string user_id;
      if (payload_type == jwt::json::type::integer ||
          payload_type == jwt::json::type::number)
        user_id = std::to_string(static_cast<int>(claim.as_number()));
      else if (payload_type == jwt::json::type::string)
        user_id = claim.as_string();
      else
        throw std::exception("invalid type");

      // Pass user_id through request attributes
      req->getAttributes()->insert("current_user_id", user_id);

      // Token is valid, proceed to the next middleware/controller
      auto resp = co_await next;
      co_return resp;
    } catch (const std::exception &e) {
      LOG_ERROR << "Auth error: " << e.what();
      auto resp = HttpResponse::newHttpResponse(drogon::k401Unauthorized,
                                                drogon::CT_APPLICATION_JSON);
      SimpleError err{.error = "Unauthorized: Invalid token"};
      resp->setBody(glz::write_json(err).value_or(""));
      co_return resp;
    }
  }
};
