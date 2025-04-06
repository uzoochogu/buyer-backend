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

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

void Search::search(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto query = req->getParameter("query");

  LOG_DEBUG << "Search query received: '" << query << "'";

  // return empty results for empty query
  if (query.empty()) {
    Json::Value emptyResults = Json::Value(Json::arrayValue);
    auto resp = HttpResponse::newHttpJsonResponse(emptyResults);
    callback(resp);
    return;
  }

  auto db = app().getDbClient();

  // Search orders
  db->execSqlAsync(
      "SELECT * FROM orders WHERE CAST(id AS TEXT) ILIKE $1 OR status ILIKE $1",
      [callback, query](const Result& ordersResult) {
        Json::Value searchResults = Json::Value(Json::arrayValue);

        LOG_DEBUG << "Found " << ordersResult.size()
                  << " matching orders for query: '" << query << "'";

        for (const auto& row : ordersResult) {
          Json::Value order;
          order["id"] = row["id"].as<int>();
          order["type"] = "Order";
          order["details"] = "Order #" + row["id"].as<std::string>() + " - " +
                             row["status"].as<std::string>();
          searchResults.append(order);
        }

        // Also search posts
        auto db = app().getDbClient();
        db->execSqlAsync(
            "SELECT * FROM posts WHERE content ILIKE $1",
            [callback, searchResults, query](const Result& postsResult) {
              Json::Value finalResults = searchResults;

              LOG_DEBUG << "Found " << postsResult.size()
                        << " matching posts for query: '" << query << "'";

              for (const auto& row : postsResult) {
                Json::Value post;
                post["id"] = row["id"].as<int>();
                post["type"] = "Post";
                std::string content = row["content"].as<std::string>();
                post["details"] =
                    "Post: " + (content.length() > 50
                                    ? content.substr(0, 50) + "..."
                                    : content);
                finalResults.append(post);
              }

              // Prepare response
              auto resp = HttpResponse::newHttpJsonResponse(finalResults);
              callback(resp);
            },
            [callback, searchResults](const DrogonDbException& e) {
              LOG_ERROR << "Database error searching posts: "
                        << e.base().what();
              auto resp = HttpResponse::newHttpJsonResponse(searchResults);
              callback(resp);
            },
            "%" + query + "%");
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error searching orders: " << e.base().what();
        Json::Value emptyResults = Json::Value(Json::arrayValue);
        auto resp = HttpResponse::newHttpJsonResponse(emptyResults);
        callback(resp);
      },
      "%" + query + "%");
}
