#pragma once
#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

namespace api {
namespace v1 {
class Search : public drogon::HttpController<Search> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Search::search, "/api/v1/search", drogon::Get, drogon::Options,
                "CorsMiddleware", "AuthMiddleware");

  METHOD_LIST_END

  static drogon::Task<> search(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
