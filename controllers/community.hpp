#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {
namespace v1 {
class Community : public drogon::HttpController<Community> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Community::get_posts, "/api/v1/posts", Get, Options,
                "CorsMiddleware");
  ADD_METHOD_TO(Community::create_post, "/api/v1/posts", Post, Options,
                "CorsMiddleware");
  METHOD_LIST_END

  void get_posts(const HttpRequestPtr& req,
                 std::function<void(const HttpResponsePtr&)>&& callback);
  void create_post(const HttpRequestPtr& req,
                   std::function<void(const HttpResponsePtr&)>&& callback);
};
}  // namespace v1
}  // namespace api
