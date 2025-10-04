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

class WebSocketAuthMiddleware
    : public drogon::HttpMiddleware<WebSocketAuthMiddleware> {
 public:
  WebSocketAuthMiddleware() {};

  void invoke(const drogon::HttpRequestPtr &req,
              drogon::MiddlewareNextCallback &&nextCb,
              drogon::MiddlewareCallback &&mcb) override {
    // CORS
    if (req->getMethod() == drogon::HttpMethod::Options) {
      nextCb(std::move(mcb));
      return;
    }

    try {
      // Try to get token from query parameter first
      auto token = req->getParameter("token");
      if (token.empty()) {
        // Fallback to Authorization header
        const std::string &auth_header = req->getHeader("Authorization");
        if (!auth_header.empty() && auth_header.substr(0, 7) == "Bearer ") {
          token = auth_header.substr(7);
        }
      }

      if (token.empty()) {
        LOG_ERROR << "WebSocket connection rejected: No token provided";
        auto resp = HttpResponse::newHttpResponse(drogon::k401Unauthorized,
                                                  drogon::CT_APPLICATION_JSON);
        SimpleError err{.error = "Unauthorized: No valid token provided"};
        resp->setBody(glz::write_json(err).value_or(""));
        mcb(resp);
        return;
      }

      using traits = jwt::traits::open_source_parsers_jsoncpp;
      auto decoded = jwt::decode<traits>(token);

      auto verifier =
          jwt::verify<traits>()
              .allow_algorithm(jwt::algorithm::hs256{config::JWT_SECRET})
              .with_issuer("buyer-app");

      verifier.verify(decoded);

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

      req->getAttributes()->insert("current_user_id", user_id);

      nextCb(std::move(mcb));
    } /* catch (const jwt::error::token_verification_exception& e) {
      if(e.code() == jwt::error::token_verification_error::token_expired) {
      auto resp = HttpResponse::newHttpJsonResponse(
          {{"error", "token expired"}});
      resp->setStatusCode(k401Unauthorized);
      mcb(resp);
      } else {
        throw e;
      }
    }  */
    catch (const std::exception &e) {
      LOG_ERROR << "Websocket auth error: " << e.what();
      auto resp = HttpResponse::newHttpResponse(drogon::k401Unauthorized,
                                                drogon::CT_APPLICATION_JSON);
      SimpleError err{.error = "Unauthorized: Invalid token"};
      resp->setBody(glz::write_json(err).value_or(""));
      mcb(resp);
    }
  }
};
