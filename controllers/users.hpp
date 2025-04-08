#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {
namespace v1 {

class Users : public drogon::HttpController<Users> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Users::get_users, "/api/v1/users", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  void get_users(const HttpRequestPtr& req,
                 std::function<void(const HttpResponsePtr&)>&& callback);
};

}  // namespace v1
}  // namespace api
