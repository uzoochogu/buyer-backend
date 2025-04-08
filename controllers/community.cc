#include "community.hpp"

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

void Community::get_posts(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto db = app().getDbClient();

  // Get page parameter, default to 1
  int page = 1;
  auto pageParam = req->getParameter("page");
  if (!pageParam.empty()) {
    try {
      page = std::stoi(pageParam);
      if (page < 1) page = 1;
    } catch (...) {
      // Invalid page parameter, use default
      page = 1;
    }
  }

  // Set page size
  const int page_size = 10;
  std::string val = "10";
  const int offset = (page - 1) * page_size;
  std::string val2 = std::to_string(offset);

  db->execSqlAsync(
      "SELECT * FROM posts ORDER BY created_at DESC LIMIT $1 OFFSET $2",
      [callback](const Result& result) {
        Json::Value posts;
        for (const auto& row : result) {
          Json::Value post;
          post["id"] = row["id"].as<int>();
          post["user"] = row["user_id"].as<std::string>();
          post["content"] = row["content"].as<std::string>();
          post["created_at"] = row["created_at"].as<std::string>();
          posts.append(post);
        }

        auto resp = HttpResponse::newHttpJsonResponse(posts);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::to_string(page_size), std::to_string(offset));
}

void Community::create_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string user_id = (*json)["user_id"].asString();
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
      std::stoi(user_id), content);
}
