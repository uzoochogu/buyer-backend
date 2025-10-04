#ifndef USERS_HPP
#define USERS_HPP

#include <drogon/HttpController.h>

namespace api {
namespace v1 {
class Users : public drogon::HttpController<Users> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Users::get_users, "/api/v1/users", drogon::Get, drogon::Options,
                "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  static drogon::Task<> get_users(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
};

}  // namespace v1
}  // namespace api

#endif  // USERS_HPPs