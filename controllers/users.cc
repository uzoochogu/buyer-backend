#include "users.hpp"

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

void Users::get_users(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback) {
  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT id, username, email, created_at FROM users ORDER BY username",
      [callback](const Result& result) {
        Json::Value users;
        for (const auto& row : result) {
          Json::Value user;
          user["id"] = row["id"].as<int>();
          user["username"] = row["username"].as<std::string>();
          user["email"] = row["email"].as<std::string>();
          user["created_at"] = row["created_at"].as<std::string>();
          users.append(user);
        }

        auto resp = HttpResponse::newHttpJsonResponse(users);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      });
}
