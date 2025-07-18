#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {
namespace v1 {
class Orders : public drogon::HttpController<Orders> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Orders::get_orders, "/api/v1/orders", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Orders::create_order, "/api/v1/orders", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  METHOD_LIST_END

  Task<> get_orders(HttpRequestPtr req,
                    std::function<void(const HttpResponsePtr&)> callback);
  Task<> create_order(HttpRequestPtr req,
                      std::function<void(const HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
