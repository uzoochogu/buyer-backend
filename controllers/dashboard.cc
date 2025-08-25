#include "dashboard.hpp"

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

using drogon::app;
using drogon::orm::DrogonDbException;

using namespace api::v1;

drogon::Task<> Dashboard::get_dashboard_data(
    drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  try {
    // Fetch orders data
    auto result = co_await db->execSqlCoro(
        "SELECT status, COUNT(*) FROM orders GROUP BY status");

    Json::Value orders_data;
    for (const auto& row : result) {
      orders_data[row["status"].as<std::string>()] = row["count"].as<int>();
    }

    // Prepare response
    Json::Value ret;
    ret["orders"] = orders_data;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }

  co_return;
}
