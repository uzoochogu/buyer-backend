#pragma once

#include <drogon/HttpController.h>

namespace api {
namespace v1 {
class Orders : public drogon::HttpController<Orders> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Orders::get_orders, "/api/v1/orders", drogon::Get,
                drogon::Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Orders::create_order, "/api/v1/orders", drogon::Post,
                drogon::Options, "CorsMiddleware", "AuthMiddleware");

  METHOD_LIST_END

  static drogon::Task<> get_orders(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> create_order(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
