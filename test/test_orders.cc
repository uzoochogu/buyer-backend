#include <drogon/HttpClient.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

#include "helpers.hpp"

DROGON_TEST(OrdersTest) {
  auto db_client = drogon::app().getDbClient();

  helpers::cleanup_db();

  // Create test user for testing
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");

  // Register user
  Json::Value register_json;
  register_json["username"] = "testorder";
  register_json["email"] = "testorder@example.com";
  register_json["password"] = "password123";
  auto register_req = drogon::HttpRequest::newHttpJsonRequest(register_json);
  register_req->setMethod(drogon::Post);
  register_req->setPath("/api/v1/auth/register");

  auto register_resp = client->sendRequest(register_req);
  REQUIRE(register_resp.second->getStatusCode() == drogon::k200OK);

  auto resp_json = register_resp.second->getJsonObject();
  CHECK((*resp_json)["status"].asString() == "success");
  CHECK((*resp_json)["token"].asString().length() > 0);

  std::string token = (*resp_json)["token"].asString();

  // Get user ID for test user
  auto user_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testorder'");
  CHECK(user_result.size() > 0);
  int user_id = user_result[0]["id"].as<int>();

  // Test 1: Create a new order
  Json::Value create_order_json;
  create_order_json["user_id"] = user_id;
  create_order_json["status"] = "pending";

  auto create_order_req =
      drogon::HttpRequest::newHttpJsonRequest(create_order_json);
  create_order_req->setMethod(drogon::Post);
  create_order_req->setPath("/api/v1/orders");
  create_order_req->addHeader("Authorization", "Bearer " + token);

  auto create_order_resp = client->sendRequest(create_order_req);
  CHECK(create_order_resp.second->getStatusCode() == drogon::k200OK);

  auto create_order_resp_json = create_order_resp.second->getJsonObject();
  CHECK((*create_order_resp_json)["status"].asString() == "success");
  CHECK((*create_order_resp_json)["order_id"].asInt() > 0);

  int order_id = (*create_order_resp_json)["order_id"].asInt();

  // Test 2: Get all orders
  auto get_orders_req = drogon::HttpRequest::newHttpRequest();
  get_orders_req->setMethod(drogon::Get);
  get_orders_req->setPath("/api/v1/orders");
  get_orders_req->addHeader("Authorization", "Bearer " + token);

  auto get_orders_resp = client->sendRequest(get_orders_req);
  CHECK(get_orders_resp.second->getStatusCode() == drogon::k200OK);

  auto get_orders_resp_json = get_orders_resp.second->getJsonObject();
  CHECK(get_orders_resp_json->isArray());
  CHECK(!get_orders_resp_json->empty());

  // Verify order structure for first order
  if (get_orders_resp_json->size() > 0) {
    const auto& first_order = (*get_orders_resp_json)[0];
    CHECK(first_order.isMember("id"));
    CHECK(first_order.isMember("user_id"));
    CHECK(first_order.isMember("status"));
    CHECK(first_order.isMember("created_at"));
  }

  // Test 3: Verify the newly created order is in the list
  auto verify_orders_req = drogon::HttpRequest::newHttpRequest();
  verify_orders_req->setMethod(drogon::Get);
  verify_orders_req->setPath("/api/v1/orders");
  verify_orders_req->addHeader("Authorization", "Bearer " + token);

  auto verify_orders_resp = client->sendRequest(verify_orders_req);
  CHECK(verify_orders_resp.second->getStatusCode() == drogon::k200OK);

  auto verify_orders_resp_json = verify_orders_resp.second->getJsonObject();
  CHECK(verify_orders_resp_json->isArray());

  bool found_order = false;
  for (const auto& order : *verify_orders_resp_json) {
    if (order["id"].asInt() == order_id) {
      found_order = true;
      CHECK(order["user_id"].asInt() == user_id);
      CHECK(order["status"].asString() == "pending");
      break;
    }
  }
  CHECK(found_order);

  // Test 4: Create another order with different status
  Json::Value create_order_json2;
  create_order_json2["user_id"] = user_id;
  create_order_json2["status"] = "in_progress";

  auto create_order_req2 =
      drogon::HttpRequest::newHttpJsonRequest(create_order_json2);
  create_order_req2->setMethod(drogon::Post);
  create_order_req2->setPath("/api/v1/orders");
  create_order_req2->addHeader("Authorization", "Bearer " + token);

  auto create_order_resp2 = client->sendRequest(create_order_req2);
  CHECK(create_order_resp2.second->getStatusCode() == drogon::k200OK);

  auto create_order_resp_json2 = create_order_resp2.second->getJsonObject();
  int order_id2 = (*create_order_resp_json2)["order_id"].asInt();

  helpers::cleanup_db();
}
