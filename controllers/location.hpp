#pragma once

#include <drogon/HttpController.h>
#include <drogon/orm/DbClient.h>

using namespace drogon;

struct LocationPoint {
  double latitude;
  double longitude;
  double accuracy;
  std::string user_id;
  std::string device_id;
  std::string cluster_id;
  Json::Value to_json() const {
    Json::Value json;
    json["latitude"] = latitude;
    json["longitude"] = longitude;
    json["accuracy"] = accuracy;
    json["user_id"] = user_id;
    json["device_id"] = device_id;
    json["cluster_id"] = cluster_id;
    return json;
  }
};

namespace api {
namespace v1 {
class LocationController : public HttpController<LocationController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(LocationController::add_location, "/api/v1/location", Post,
                Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(LocationController::get_clusters, "/api/v1/location/clusters",
                Get, Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(LocationController::find_nearby, "/api/v1/location/nearby", Get,
                Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(LocationController::recluster, "/api/v1/location/recluster",
                Post, Options, "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  Task<> add_location(const HttpRequestPtr req,
                      std::function<void(const HttpResponsePtr&)> callback);
  Task<> get_clusters(const HttpRequestPtr req,
                      std::function<void(const HttpResponsePtr&)> callback);
  Task<> find_nearby(const HttpRequestPtr req,
                     std::function<void(const HttpResponsePtr&)> callback);
  Task<> recluster(const HttpRequestPtr req,
                   std::function<void(const HttpResponsePtr&)> callback);

 private:
  Task<std::vector<LocationPoint>> get_all_locations_from_db();
  Task<void> update_cluster_assignments(
      const std::vector<std::string>& cluster_ids,
      const std::vector<LocationPoint>& points);
  Task<void> perform_clustering();
};
}  // namespace v1
}  // namespace api
