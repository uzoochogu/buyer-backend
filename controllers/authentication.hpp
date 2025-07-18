#pragma once
#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

using namespace drogon;

namespace api {

namespace v1 {
class Authentication : public drogon::HttpController<Authentication> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Authentication::login, "/api/v1/auth/login", Post, Options,
                "CorsMiddleware");
  ADD_METHOD_TO(Authentication::logout, "/api/v1/auth/logout", Post, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Authentication::refresh, "/api/v1/auth/refresh", Post, Options,
                "CorsMiddleware");
  ADD_METHOD_TO(Authentication::register_user, "/api/v1/auth/register", Post,
                Options, "CorsMiddleware");
  METHOD_LIST_END

  // drogon::Task<void>
  Task<> login(HttpRequestPtr req,
               std::function<void(const HttpResponsePtr&)> callback);
  Task<> logout(HttpRequestPtr req,
                std::function<void(const HttpResponsePtr&)> callback);
  Task<> refresh(HttpRequestPtr req,
                 std::function<void(const HttpResponsePtr&)> callback);
  Task<> register_user(HttpRequestPtr req,
                       std::function<void(const HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
