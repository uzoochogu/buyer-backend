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

  ADD_METHOD_TO(Community::subscribe_to_entity,
                "/api/v1/entity/{name}/subscribe", Post, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Community::unsubscribe_from_entity,
                "/api/v1/entity/{name}/unsubscribe", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  // Puts
  ADD_METHOD_TO(Community::update_post, "/api/v1/posts/{id}", Put, Options,
                "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  Task<> get_posts(const HttpRequestPtr req,
                   std::function<void(const HttpResponsePtr&)> callback);
  Task<> get_post_by_id(HttpRequestPtr req,
                        std::function<void(const HttpResponsePtr&)> callback,
                        std::string id);
  Task<> filter_posts(HttpRequestPtr req,
                      std::function<void(const HttpResponsePtr&)> callback);
  Task<> get_subscriptions(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback);
  Task<> get_popular_tags(HttpRequestPtr req,
                          std::function<void(const HttpResponsePtr&)> callback);

  Task<> create_post(HttpRequestPtr req,
                     std::function<void(const HttpResponsePtr&)> callback);
  Task<> subscribe_to_post(HttpRequestPtr req,
                           std::function<void(const HttpResponsePtr&)> callback,
                           std::string id);
  Task<> unsubscribe_from_post(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  Task<> subscribe_to_entity(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string name);
  Task<> unsubscribe_from_entity(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string name);

  Task<> update_post(HttpRequestPtr req,
                     std::function<void(const HttpResponsePtr&)> callback,
                     std::string id);
};
}  // namespace v1
}  // namespace api
