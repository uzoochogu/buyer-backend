#include "orders.hpp"

// Add definition of your processing function here
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/orm/ResultIterator.h>

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

void Orders::get_orders(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    auto db = app().getDbClient();

    db->execSqlAsync(
        "SELECT * FROM orders ORDER BY id DESC",
        [callback](const Result& result) {
            Json::Value orders;
            for (const auto& row : result) {
                Json::Value order;
                order["id"] = row["id"].as<int>();
                order["user_id"] = row["user_id"].as<int>();
                order["status"] = row["status"].as<std::string>();
                order["created_at"] = row["created_at"].as<std::string>();
                orders.append(order);
            }

            auto resp = HttpResponse::newHttpJsonResponse(orders);
            // resp->addHeader("Access-Control-Allow-Origin", "*");
            // resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            // resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
            callback(resp);
        },
        [](const DrogonDbException& e) {
            LOG_ERROR << "Database error: " << e.base().what();
        }
    );
}

void Orders::create_order(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
    auto json = req->getJsonObject();
    int userId = (*json)["user_id"].asInt();
    std::string status = (*json)["status"].asString();

    auto db = app().getDbClient();

    db->execSqlAsync(
        "INSERT INTO orders (user_id, status) VALUES ($1, $2) RETURNING id",
        [callback](const Result& result) {
            Json::Value ret;
            ret["status"] = "success";
            if (result.size() > 0) {
                ret["order_id"] = result[0]["id"].as<int>();
            }
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            // resp->addHeader("Access-Control-Allow-Origin", "*");
            // resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            // resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
            callback(resp);
        },
        [callback](const DrogonDbException& e) {
            Json::Value ret;
            ret["status"] = "error";
            ret["message"] = e.base().what();
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            // resp->addHeader("Access-Control-Allow-Origin", "*");
            // resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            // resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
            callback(resp);
        },
        userId, status
    );
}
