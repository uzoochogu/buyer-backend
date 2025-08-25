#include "search.hpp"

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
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::orm::DrogonDbException;
using drogon::orm::Transaction;

using api::v1::Search;

drogon::Task<> Search::search(
    drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto query = req->getParameter("query");

  LOG_DEBUG << "Search query received: '" << query << "'";

  // return empty results for empty query
  if (query.empty()) {
    Json::Value empty_results = Json::Value(Json::arrayValue);
    auto resp = HttpResponse::newHttpJsonResponse(empty_results);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();
  try {
    // Search orders
    auto orders_result = co_await db->execSqlCoro(
        "SELECT * FROM orders WHERE CAST(id AS TEXT) ILIKE $1 OR status ILIKE "
        "$1",
        "%" + query + "%");

    Json::Value search_results = Json::Value(Json::arrayValue);

    LOG_DEBUG << "Found " << orders_result.size()
              << " matching orders for query: '" << query << "'";

    for (const auto& row : orders_result) {
      Json::Value order;
      order["id"] = row["id"].as<int>();
      order["type"] = "Order";
      order["details"] = "Order #" + row["id"].as<std::string>() + " - " +
                         row["status"].as<std::string>();
      search_results.append(order);
    }

    try {
      // Also search posts
      auto posts_result = co_await db->execSqlCoro(
          "SELECT * FROM posts WHERE content ILIKE $1", "%" + query + "%");

      LOG_DEBUG << "Found " << posts_result.size()
                << " matching posts for query: '" << query << "'";

      for (const auto& row : posts_result) {
        Json::Value post;
        post["id"] = row["id"].as<int>();
        post["type"] = "Post";
        std::string content = row["content"].as<std::string>();
        post["details"] =
            "Post: " +
            (content.length() > 50 ? content.substr(0, 50) + "..." : content);
        search_results.append(post);
      }

      // Prepare response
      auto resp = HttpResponse::newHttpJsonResponse(search_results);
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error searching posts: " << e.base().what();
      // Still return the order results even if post search fails
      auto resp = HttpResponse::newHttpJsonResponse(search_results);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error searching orders: " << e.base().what();
    Json::Value empty_results = Json::Value(Json::arrayValue);
    auto resp = HttpResponse::newHttpJsonResponse(empty_results);
    callback(resp);
  }

  co_return;
}
