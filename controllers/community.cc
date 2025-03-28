#include "community.hpp"

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/SqlBinder.h>


// #include <drogon/orm/Transaction.h>
#include <drogon/orm/ResultIterator.h>
// #include <drogon/orm/ResultStream.h>
// #include <drogon/orm/ResultStreamIterator.h>
// #include <drogon/orm/ResultStreamRow.h>
// #include <drogon/orm/ResultStreamField.h>
// #include <drogon/orm/ResultStreamException.h>

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

void Community::get_posts(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT * FROM posts ORDER BY created_at DESC",
      [callback](const Result& result) {
        Json::Value posts;
        for (const auto& row : result) {
          Json::Value post;
          post["user"] = row["user_id"].as<std::string>();
          post["content"] = row["content"].as<std::string>();
          post["created_at"] = row["created_at"].as<std::string>();
          posts.append(post);
        }

        auto resp = HttpResponse::newHttpJsonResponse(posts);
        callback(resp);
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      });
}

void Community::create_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  int userId = (*json)["user_id"].asInt();
  std::string content = (*json)["content"].asString();

  auto db = app().getDbClient();

  db->execSqlAsync(
      "INSERT INTO posts (user_id, content) VALUES ($1, $2)",
      [callback](const Result& result) {
        Json::Value ret;
        ret["status"] = "success";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      },
      userId, content);
}
