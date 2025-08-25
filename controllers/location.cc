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

#include <format>

#include "../utilities/conversion.hpp"

using drogon::app;
using drogon::HttpRequestPtr;
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::k400BadRequest;
using drogon::k500InternalServerError;
using drogon::orm::DrogonDbException;

using api::v1::LocationController;

drogon::Task<> LocationController::add_location(
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
  } catch (const DrogonDbException& e) {
    Json::Value error;
    LOG_ERROR << "Failed to create error: " << e.base().what();
    error["error"] =
        std::format("Failed to save location:  {}", e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }
  co_return;
}

// Paginated by offset (15 per page)
drogon::Task<> LocationController::get_clusters(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  // Parse offset query param
  int64_t offset = 0;
  auto offset_str = req->getParameter("offset");
  if (!offset_str.empty()) {
    offset = convert::string_to_int(offset_str).value_or(0);
    if (offset < 0) offset = 0;
  }

  try {
    auto client = app().getDbClient();
    auto result = co_await client->execSqlCoro(
        "SELECT cluster_id, "
        "ST_AsGeoJSON(ST_Centroid(ST_Collect(geom::geometry))) as centroid, "
        "COUNT(*) as point_count FROM locations WHERE cluster_id IS NOT NULL "
        "GROUP BY cluster_id OFFSET $1 LIMIT 15",
        offset);

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
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error getting clusters: " << e.base().what();
    Json::Value error;
    error["error"] = e.base().what();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }
  co_return;
}

// Paginated by offset (15 per page)
drogon::Task<> LocationController::find_nearby(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  try {
    auto lat = req->getParameter("lat");
    auto lon = req->getParameter("lon");
    auto radius = req->getParameter("radius");

    if (radius.empty()) radius = "1000";  // Default 1km radius

    // Parse offset query param
    int64_t offset = 0;
    auto offset_str = req->getParameter("offset");
    if (!offset_str.empty()) {
      offset = convert::string_to_int(offset_str).value_or(0);
      if (offset < 0) offset = 0;
    }

    if (lat.empty() || lon.empty()) {
      Json::Value error;
      error["error"] = "Missing lat/lon parameters";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    auto client = app().getDbClient();
    auto result = co_await client->execSqlCoro(
        "SELECT user_id, latitude, longitude, accuracy, device_id, cluster_id, "
        "ST_Distance(geom, ST_SetSRID(ST_MakePoint($2, $1), 4326)::geography) "
        "as distance "
        "FROM locations "
        "WHERE ST_DWithin(geom, ST_SetSRID(ST_MakePoint($2, $1), "
        "4326)::geography, $3) "
        "ORDER BY distance OFFSET $4 LIMIT 15",
        std::stod(lat), std::stod(lon), std::stod(radius), offset);

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
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error finding nearby cluster : " << e.base().what();
    Json::Value error;
    error["error"] = e.base().what();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }
  co_return;
}

drogon::Task<> LocationController::recluster(
    const HttpRequestPtr req,
    std::function<void(const HttpResponsePtr&)> callback) {
  // Parse epsilon query param
  double epsilon = 1000.0;  // meters
  try {
    auto epsilon_str = req->getParameter("epsilon");
    if (!epsilon_str.empty()) {
      epsilon = std::stod(epsilon_str);
      if (epsilon < 0 || epsilon > 10'000'000) epsilon = 1000.0;
    }
  } catch (...) {
    epsilon = 1000.0;
  }

  try {
    // other Parameters for DBSCAN
    int min_points = 2;

    auto client = app().getDbClient();
    // Use a CTE to assign cluster numbers using PostGIS DBSCAN
    // and update the locations table with new cluster_ids
    auto result = co_await client->execSqlCoro(R"(
  WITH transformed AS (
    SELECT id, ST_Transform(geom::geometry, 3857) AS geom_3857
    FROM locations
  ),
  clustered AS (
    SELECT id,
           ST_ClusterDBSCAN(geom_3857, $1,$2) OVER () AS cluster_num
    FROM transformed
  )
  UPDATE locations loc
  SET cluster_id = CASE 
    WHEN c.cluster_num IS NULL THEN NULL
    ELSE 'cluster_' || c.cluster_num::text
  END
  FROM clustered c
  WHERE loc.id = c.id
  RETURNING loc.id, loc.cluster_id
)",
                                               epsilon, min_points);

    Json::Value ret;
    ret["status"] = "clustering completed";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k500InternalServerError);
    resp->setBody(e.base().what());
    callback(resp);
  }
  co_return;
}
