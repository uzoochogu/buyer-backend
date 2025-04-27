#pragma once
#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

using namespace drogon;

namespace api {
namespace v1 {
class Search : public drogon::HttpController<Search> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Search::search, "/api/v1/search", Get, Options,
                "CorsMiddleware", "AuthMiddleware");

  METHOD_LIST_END

  Task<> search(HttpRequestPtr req,
                std::function<void(const HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
