#include "chats.hpp"

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

void Chats::get_chats(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback) {
  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT * FROM chats ORDER BY created_at DESC",
      [callback](const Result& result) {
        Json::Value chats;
        for (const auto& row : result) {
          Json::Value chat;
          chat["user"] = row["user_id"].as<std::string>();
          chat["message"] = row["message"].as<std::string>();
          chat["created_at"] = row["created_at"].as<std::string>();
          chats.append(chat);
        }

        auto resp = HttpResponse::newHttpJsonResponse(chats);
        callback(resp);
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      });
}

void Chats::send_chat(const HttpRequestPtr& req,
                      std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string userId = (*json)["user_id"].asString();
  std::string message = (*json)["message"].asString();

  auto db = app().getDbClient();

  db->execSqlAsync(
      "INSERT INTO chats (user_id, message) VALUES ($1, $2)",
      [callback](const Result& result) {
        Json::Value ret;
        ret["status"] = "success";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      },
      std::stoi(userId), message);
}
