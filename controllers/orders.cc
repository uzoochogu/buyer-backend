#include "orders.hpp"

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

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

Task<> Orders::get_orders(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  try {
    auto result =
        co_await db->execSqlCoro("SELECT * FROM orders ORDER BY id DESC");

    Json::Value orders{Json::arrayValue};
    for (const auto& row : result) {
      Json::Value order;
      order["id"] = row["id"].as<int>();
      order["user_id"] = row["user_id"].as<int>();
      order["status"] = row["status"].as<std::string>();
      order["created_at"] = row["created_at"].as<std::string>();
      orders.append(order);
    }

    auto resp = HttpResponse::newHttpJsonResponse(orders);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

Task<> Orders::create_order(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto json = req->getJsonObject();
  int user_id = (*json)["user_id"].asInt();
  std::string status = (*json)["status"].asString();

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "INSERT INTO orders (user_id, status) VALUES ($1, $2) RETURNING id",
        user_id, status);

    Json::Value ret;
    ret["status"] = "success";
    if (result.size() > 0) {
      ret["order_id"] = result[0]["id"].as<int>();
    }

    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value ret;
    ret["status"] = "error";
    ret["message"] = e.base().what();
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}
