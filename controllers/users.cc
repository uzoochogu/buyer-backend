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

#include "../utilities/conversion.hpp"
#include "../utilities/json_manipulation.hpp"
#include "common_req_n_resp.hpp"


using drogon::app;
using drogon::CT_APPLICATION_JSON;
using drogon::HttpResponse;

using api::v1::Users;

struct UserInfo {
  int id;
  std::string username;
  std::string email;
  std::string created_at;
};

drogon::Task<> Users::get_users(
    drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  std::size_t page = 1;
  std::size_t pageSize = 20;

  // Parse pagination parameters safely
  if (!req->getParameter("page").empty()) {
    page = std::max(
        1, convert::string_to_int(req->getParameter("page")).value_or(1));
  }
  if (!req->getParameter("pageSize").empty()) {
    pageSize = std::max(
        1, std::min(100, convert::string_to_int(req->getParameter("pageSize"))
                             .value_or(20)));
  }
  std::size_t offset = (page - 1) * pageSize;

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT id, username, email, created_at FROM users ORDER BY username "
        "LIMIT $1 OFFSET $2",
        pageSize, offset);

    std::vector<UserInfo> users_data;
    users_data.reserve(result.size());
    for (const auto& row : result) {
      users_data.emplace_back(
          UserInfo{.id = row["id"].as<int>(),
                   .username = row["username"].as<std::string>(),
                   .email = row["email"].as<std::string>(),
                   .created_at = row["created_at"].as<std::string>()});
    }

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(users_data).value_or(""));
    callback(resp);
  } catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(drogon::k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  }

  co_return;
}
