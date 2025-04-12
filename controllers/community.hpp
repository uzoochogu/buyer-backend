#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {
namespace v1 {
class Community : public drogon::HttpController<Community> {
 public:
  METHOD_LIST_BEGIN
  // Gets
  ADD_METHOD_TO(Community::get_posts, "/api/v1/posts", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::get_post_by_id, "/api/v1/posts/{id}", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::filter_posts, "/api/v1/posts/filter", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::get_subscriptions, "/api/v1/posts/subscriptions",
                Get, Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::get_popular_tags, "/api/v1/posts/tags", Get, Options,
                "CorsMiddleware", "AuthMiddleware");

  // Posts
  ADD_METHOD_TO(Community::create_post, "/api/v1/posts", Post, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::subscribe_to_post, "/api/v1/posts/{id}/subscribe",
                Post, Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::unsubscribe_from_post,
                "/api/v1/posts/{id}/unsubscribe", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  // Puts
  ADD_METHOD_TO(Community::update_post, "/api/v1/posts/{id}", Put, Options,
                "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  void get_posts(const HttpRequestPtr& req,
                 std::function<void(const HttpResponsePtr&)>&& callback);
  void get_post_by_id(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback,
                      const std::string& id);
  void filter_posts(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback);
  void get_subscriptions(
      const HttpRequestPtr& req,
      std::function<void(const HttpResponsePtr&)>&& callback);
  void get_popular_tags(const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& callback);

  void create_post(const HttpRequestPtr& req,
                   std::function<void(const HttpResponsePtr&)>&& callback);
  void subscribe_to_post(const HttpRequestPtr& req,
                         std::function<void(const HttpResponsePtr&)>&& callback,
                         const std::string& id);
  void unsubscribe_from_post(
      const HttpRequestPtr& req,
      std::function<void(const HttpResponsePtr&)>&& callback,
      const std::string& id);

  void update_post(const HttpRequestPtr& req,
                   std::function<void(const HttpResponsePtr&)>&& callback,
                   const std::string& id);
};
}  // namespace v1
}  // namespace api
