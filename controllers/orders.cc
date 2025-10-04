#include "orders.hpp"

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
using drogon::HttpRequestPtr;
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::k500InternalServerError;
using drogon::Task;
using drogon::orm::DrogonDbException;

using api::v1::Orders;

struct OrderInfo {
  int id;
  int user_id;
  std::string status;
  std::string created_at;
};

struct CreateOrderRequest {
  int user_id;
  std::string status;
};

struct CreateOrderResponse {
  std::string status;
  int order_id;
};

Task<> Orders::get_orders(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  // Pagination parameters
  std::size_t page = 1;
  std::size_t pageSize = 20;

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
        "SELECT * FROM orders ORDER BY id DESC LIMIT $1 OFFSET $2", pageSize,
        offset);

    std::vector<OrderInfo> orders_data;
    orders_data.reserve(result.size());
    for (const auto& row : result) {
      orders_data.emplace_back(
          OrderInfo{.id = row["id"].as<int>(),
                    .user_id = row["user_id"].as<int>(),
                    .status = row["status"].as<std::string>(),
                    .created_at = row["created_at"].as<std::string>()});
    }

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(orders_data).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  }

  co_return;
}

Task<> Orders::create_order(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  CreateOrderRequest create_req;
  auto parse_error = utilities::strict_read_json(create_req, req->getBody());

  if (parse_error || create_req.user_id <= 0 || create_req.status.empty()) {
    SimpleError error{.error = "Invalid JSON or missing fields"};
    auto resp = HttpResponse::newHttpResponse(drogon::k400BadRequest,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  try {
    auto result = co_await db->execSqlCoro(
        "INSERT INTO orders (user_id, status) VALUES ($1, $2) RETURNING id",
        create_req.user_id, create_req.status);

    if (result.empty()) {
      SimpleStatus ret{.status = "failed"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }
    CreateOrderResponse response{.status = "success",
                                 .order_id = result[0]["id"].as<int>()};

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(response).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = e.base().what()};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  }

  co_return;
}
