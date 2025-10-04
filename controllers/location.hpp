#pragma once

#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

namespace api {
namespace v1 {

class LocationController : public drogon::HttpController<LocationController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(LocationController::add_location, "/api/v1/location",
                drogon::Post, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  ADD_METHOD_TO(LocationController::get_clusters, "/api/v1/location/clusters",
                drogon::Get, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  ADD_METHOD_TO(LocationController::find_nearby, "/api/v1/location/nearby",
                drogon::Get, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  ADD_METHOD_TO(LocationController::recluster, "/api/v1/location/recluster",
                drogon::Post, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  METHOD_LIST_END

  static drogon::Task<> add_location(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  // Paginated by offset (15 per page)
  static drogon::Task<> get_clusters(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  // Paginated by offset (15 per page)
  static drogon::Task<> find_nearby(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> recluster(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
