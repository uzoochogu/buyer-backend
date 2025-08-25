#pragma once

#include <drogon/HttpController.h>

namespace api {
namespace v1 {

using drogon::Get;
using drogon::Options;
using drogon::Post;
using drogon::Put;

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

  static drogon::Task<> get_posts(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> get_post_by_id(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string id);
  static drogon::Task<> filter_posts(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> get_subscriptions(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> get_popular_tags(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);

  static drogon::Task<> create_post(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> subscribe_to_post(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string id);
  static drogon::Task<> unsubscribe_from_post(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> subscribe_to_entity(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string name);
  static drogon::Task<> unsubscribe_from_entity(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string name);

  static drogon::Task<> update_post(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string id);
};
}  // namespace v1
}  // namespace api
