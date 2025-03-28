#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {
namespace v1 {
class Orders : public drogon::HttpController<Orders> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Orders::get_orders, "/api/v1/orders", Get, Options,
                "CorsMiddleware");
  ADD_METHOD_TO(Orders::create_order, "/api/v1/orders", Post, Options,
                "CorsMiddleware");

  METHOD_LIST_END

  void get_orders(const HttpRequestPtr& req,
                  std::function<void(const HttpResponsePtr&)>&& callback);
  void create_order(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback);
};
}  // namespace v1
}  // namespace api
