#define NOMINMAX  // to avoid conflict with min/max macros in Windows
// https://github.com/boostorg/winapi/issues/65,
// https://github.com/stevenlovegrove/Pangolin/issues/352
#include "location.hpp"

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/ResultIterator.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/SqlBinder.h>

#define MLPACK_PRINT_INFO
#define MLPACK_PRINT_WARN
#include <mlpack.hpp>
#include <mlpack/methods/dbscan/dbscan.hpp>

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

Task<> LocationController::add_location(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  try {
    auto json = req->getJsonObject();
    if (!json || json->empty() || !(*json).isMember("latitude") ||
        !(*json).isMember("longitude") || !(*json)["latitude"].isDouble() ||
        !(*json)["longitude"].isDouble()) {
      LOG_ERROR << "Invalid longitude or latitude, note that they are required";
      Json::Value error;
      error["error"] =
          "Invalid longitude or latitude, note that they are required";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    if (!(*json).isMember("gps_accuracy") ||
        !(*json)["gps_accuracy"].isDouble()) {
      (*json)["gps_accuracy"] = 100.0;
      LOG_INFO << "gps_accuracy not provided, using default value of 100.0";
    }

    std::string user_id =
        req->getAttributes()->get<std::string>("current_user_id");

    std::string device_id =
        user_id + "_device";  // single device location stored per user_id

    auto client = app().getDbClient();
    co_await client->execSqlCoro(
        "INSERT INTO locations (user_id, latitude, longitude, accuracy, "
        "device_id, geom) "
        "VALUES ($1, $2, $3, $4, $5, ST_SetSRID(ST_MakePoint($3, $2), 4326)) "
        "ON CONFLICT (user_id) DO UPDATE "
        "SET latitude = EXCLUDED.latitude, longitude = EXCLUDED.longitude, "
        "accuracy = EXCLUDED.accuracy, geom = "
        "ST_SetSRID(ST_MakePoint(EXCLUDED.longitude, EXCLUDED.latitude), "
        "4326), device_id = EXCLUDED.device_id",
        std::stoi(user_id), (*json)["latitude"].asDouble(),
        (*json)["longitude"].asDouble(), (*json)["gps_accuracy"].asDouble(),
        device_id
        //    (*json)["device_id"].asString()  // not currently used
    );

    Json::Value ret;
    ret["status"] = "success";
    ret["device_id"] = device_id;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const std::exception& e) {
    Json::Value error;
    LOG_ERROR << "Failed to create error: " << e.what();
    error["error"] = "Failed to save location";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }
  co_return;
}

Task<> LocationController::get_clusters(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  try {
    auto client = app().getDbClient();
    auto result = co_await client->execSqlCoro(
        "SELECT cluster_id, "
        "ST_AsGeoJSON(ST_Centroid(ST_Collect(geom::geometry))) as centroid, "
        "COUNT(*) as point_count FROM locations WHERE cluster_id IS NOT NULL "
        "GROUP BY cluster_id");

    Json::Value clusters(Json::arrayValue);
    for (const auto& row : result) {
      Json::Value cluster;
      cluster["id"] = row["cluster_id"].as<std::string>();
      cluster["point_count"] = row["point_count"].as<int>();

      Json::Value centroid;
      Json::Reader().parse(row["centroid"].as<std::string>(), centroid);
      cluster["centroid"] = centroid;

      clusters.append(cluster);
    }

    auto resp = HttpResponse::newHttpJsonResponse(clusters);
    callback(resp);
  } catch (const std::exception& e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody(e.what());
    callback(resp);
  }
  co_return;
}

Task<> LocationController::find_nearby(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  try {
    auto lat = req->getParameter("lat");
    auto lon = req->getParameter("lon");
    auto radius = req->getParameter("radius");

    if (radius.empty()) radius = "1000";  // Default 1km radius

    if (lat.empty() || lon.empty()) {
      throw std::runtime_error("Missing lat/lon parameters");
    }

    auto client = app().getDbClient();
    auto result = co_await client->execSqlCoro(
        "SELECT latitude, longitude, accuracy, device_id, cluster_id, "
        "ST_Distance(geom, ST_SetSRID(ST_MakePoint($2, $1), 4326)::geography) "
        "as distance "
        "FROM locations "
        "WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint($2, $1), "
        "4326)::geography, $3) "
        "ORDER BY distance LIMIT 100",
        std::stod(lat), std::stod(lon), std::stod(radius));

    Json::Value nearby_points(Json::arrayValue);
    for (const auto& row : result) {
      Json::Value point;
      point["latitude"] = row["latitude"].as<double>();
      point["longitude"] = row["longitude"].as<double>();
      point["accuracy"] = row["accuracy"].as<double>();
      point["user_id"] = row["user_id"].as<std::string>();
      point["device_id"] = row["device_id"].as<std::string>();
      point["cluster_id"] = row["cluster_id"].as<std::string>();
      point["distance"] = row["distance"].as<double>();
      nearby_points.append(point);
    }

    auto resp = HttpResponse::newHttpJsonResponse(nearby_points);
    callback(resp);
  } catch (const std::exception& e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody(e.what());
    callback(resp);
  }
  co_return;
}
Task<> LocationController::recluster(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  try {
    co_await perform_clustering();

    Json::Value ret;
    ret["status"] = "clustering completed";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const std::exception& e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody(e.what());
    callback(resp);
  }
  co_return;
}

Task<std::vector<LocationPoint>>
LocationController::get_all_locations_from_db() {
  auto client = app().getDbClient();
  auto result = co_await client->execSqlCoro(
      "SELECT latitude, longitude, accuracy, user_id, device_id, cluster_id "
      "FROM "
      "locations");

  std::vector<LocationPoint> points;
  for (const auto& row : result) {
    LocationPoint point;
    point.latitude = row["latitude"].as<double>();
    point.longitude = row["longitude"].as<double>();
    point.accuracy = row["accuracy"].as<double>();
    point.user_id = row["user_id"].as<std::string>();
    point.device_id = row["device_id"].as<std::string>();
    point.cluster_id = row["cluster_id"].as<std::string>();
    points.push_back(point);
  }

  co_return points;
}

Task<void> LocationController::update_cluster_assignments(
    const std::vector<std::string>& cluster_ids,
    const std::vector<LocationPoint>& points) {
  LOG_INFO << "Updating cluster assignments";

  auto client = app().getDbClient();
  auto trans = co_await client->newTransactionCoro();

  // clear all cluster assignments
  co_await trans->execSqlCoro("UPDATE locations SET cluster_id = NULL");

  LOG_INFO << "Total number of points: " << points.size();
  int count{0};
  for (size_t i = 0; i < points.size(); ++i) {
    if (!cluster_ids[i].empty()) {
      count++;
      // currently using user_id as the identifier
      co_await trans->execSqlCoro(
          "UPDATE locations SET cluster_id = $1 WHERE user_id = $2",
          cluster_ids[i], points[i].user_id
          // points[i].device_id
          //  points[i].latitude,
          //  points[i].longitude  // adding it might cause mismatch due to
          //  precision issues
      );
    }
  }
  LOG_INFO << "Non empties: " << count << " rows";
  co_return;
}

Task<void> LocationController::perform_clustering() {
  auto points = co_await get_all_locations_from_db();

  // Data for DBSCAN
  arma::mat data(2, points.size());
  for (size_t i = 0; i < points.size(); ++i) {
    data(0, i) = points[i].latitude;
    data(1, i) = points[i].longitude;
  }

  // DBSCAN clustering parameters
  // Adjust these parameters based on your needs
  double epsilon = 0.01;  // ~1.1km in degrees
  size_t minPoints = 2;
  mlpack::DBSCAN<> dbscan(epsilon, minPoints);

  arma::Row<size_t> assignments;
  dbscan.Cluster(data, assignments);

  int count{0};
  // Convert assignments to cluster IDs
  std::vector<std::string> cluster_ids;
  for (size_t i = 0; i < assignments.n_elem; ++i) {
    if (assignments[i] == std::numeric_limits<size_t>::max()) {
      cluster_ids.push_back("");  // Noise point
      count++;
    } else {
      cluster_ids.push_back("cluster_" + std::to_string(assignments[i]));
    }
  }

  co_await update_cluster_assignments(cluster_ids, points);
  LOG_INFO << "Empties count:  " << count;
  co_return;
}
