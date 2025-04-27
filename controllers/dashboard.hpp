#pragma once

#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

using namespace drogon;

namespace api {
namespace v1 {
class Dashboard : public drogon::HttpController<Dashboard> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Dashboard::get_dashboard_data, "/api/v1/dashboard", Get,
                Options, "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  Task<> get_dashboard_data(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
