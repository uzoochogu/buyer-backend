#pragma once
#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

namespace api {

namespace v1 {
class Authentication : public drogon::HttpController<Authentication> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Authentication::login, "/api/v1/auth/login", drogon::Post,
                drogon::Options, "CorsMiddleware");
  ADD_METHOD_TO(Authentication::logout, "/api/v1/auth/logout", drogon::Post,
                drogon::Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Authentication::refresh, "/api/v1/auth/refresh", drogon::Post,
                drogon::Options, "CorsMiddleware");
  ADD_METHOD_TO(Authentication::register_user, "/api/v1/auth/register",
                drogon::Post, drogon::Options, "CorsMiddleware");
  METHOD_LIST_END

  // drogon::Task<void>
  static drogon::Task<> login(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> logout(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> refresh(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> register_user(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
