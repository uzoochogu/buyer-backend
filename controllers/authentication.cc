#include "authentication.hpp"

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

void Authentication::login(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string username = (*json)["username"].asString();
  std::string password = (*json)["password"].asString();

  auto db = app().getDbClient();
  db->execSqlAsync(
      "SELECT * FROM users WHERE username = $1 AND password_hash = $2",
      [callback](const Result& result) {
        if (result.size() > 0) {
          Json::Value ret;
          ret["status"] = "success";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          // // Add CORS headers
          // resp->addHeader("Access-Control-Allow-Origin", "*");
          // resp->addHeader("Access-Control-Allow-Methods", "GET, POST,
          // OPTIONS, PUT, DELETE");
          // resp->addHeader("Access-Control-Allow-Headers", "Content-Type,
          // Authorization");
          callback(resp);
        } else {
          Json::Value ret;
          ret["status"] = "failure";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          // // Add CORS headers
          // resp->addHeader("Access-Control-Allow-Origin", "*");
          // resp->addHeader("Access-Control-Allow-Methods", "GET, POST,
          // OPTIONS, PUT, DELETE");
          // resp->addHeader("Access-Control-Allow-Headers", "Content-Type,
          // Authorization");
          callback(resp);
        }
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      },
      username, password);
}

void Authentication::logout(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  Json::Value ret;
  ret["status"] = "success";
  auto resp = HttpResponse::newHttpJsonResponse(ret);
  callback(resp);
}
