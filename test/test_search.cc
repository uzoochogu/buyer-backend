#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

DROGON_TEST(SearchTest) {
  // Setup test database connection
  auto db_client = drogon::app().getDbClient();

  // Clean up any test data from previous runs
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testsearch')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM orders WHERE status = 'test_search_status'"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM posts WHERE content LIKE '%test_search_content%'"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM users WHERE username = 'testsearch'"));

  // Create a test user for authentication
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");
  Json::Value register_json;
  register_json["username"] = "testsearch";
  register_json["email"] = "testsearch@example.com";
  register_json["password"] = "password123";
  auto registerReq = drogon::HttpRequest::newHttpJsonRequest(register_json);
  registerReq->setMethod(drogon::Post);
  registerReq->setPath("/api/v1/auth/register");

  auto register_resp = client->sendRequest(registerReq);
  REQUIRE(register_resp.second->getStatusCode() == drogon::k200OK);

  auto response_json = register_resp.second->getJsonObject();
  CHECK((*response_json)["status"].asString() == "success");
  CHECK((*response_json)["token"].asString().length() > 0);

  std::string token = (*response_json)["token"].asString();

  // Create test data for search
  // 1. Create a test order
  int user_id = std::stoi(
      db_client
          ->execSqlSync(
              "SELECT id FROM users WHERE username = 'testsearch'")[0][0]
          .as<std::string>());

  REQUIRE_NOTHROW(
      db_client->execSqlSync("INSERT INTO orders (user_id, status, created_at) "
                             "VALUES ($1, 'test_search_status', NOW())",
                             user_id));

  // 2. Create a test post
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "INSERT INTO posts (user_id, content, created_at) VALUES ($1, 'This is a "
      "test_search_content post', NOW())",
      user_id));

  // Wait a moment for data to be committed
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Test 1: Search for order by status
  auto search_order_req = drogon::HttpRequest::newHttpRequest();
  search_order_req->setMethod(drogon::Get);
  search_order_req->setPath("/api/v1/search?query=test_search_status");
  search_order_req->addHeader("Authorization", "Bearer " + token);

  auto search_order_resp = client->sendRequest(search_order_req);
  CHECK(search_order_resp.second->getStatusCode() == drogon::k200OK);

  auto search_order_json = search_order_resp.second->getJsonObject();
  CHECK(search_order_json->isArray());

  bool found_order = false;
  for (const auto& result : *search_order_json) {
    if (result["type"].asString() == "Order" &&
        result["details"].asString().find("test_search_status") !=
            std::string::npos) {
      found_order = true;
      break;
    }
  }
  CHECK(found_order);

  // Test 2: Search for post by content
  auto search_post_req = drogon::HttpRequest::newHttpRequest();
  search_post_req->setMethod(drogon::Get);
  search_post_req->setPath("/api/v1/search?query=test_search_content");
  search_post_req->addHeader("Authorization", "Bearer " + token);

  auto search_post_resp = client->sendRequest(search_post_req);
  CHECK(search_post_resp.second->getStatusCode() == drogon::k200OK);

  auto searchPostJson = search_post_resp.second->getJsonObject();
  CHECK(searchPostJson->isArray());

  bool found_post = false;
  for (const auto& result : *searchPostJson) {
    if (result["type"].asString() == "Post" &&
        result["details"].asString().find("test_search_content") !=
            std::string::npos) {
      found_post = true;
      break;
    }
  }
  CHECK(found_post);

  // Test 3: Search with empty query should return empty results
  auto empty_search_req = drogon::HttpRequest::newHttpRequest();
  empty_search_req->setMethod(drogon::Get);
  empty_search_req->setPath("/api/v1/search?query=");
  empty_search_req->addHeader("Authorization", "Bearer " + token);

  auto empty_search_resp = client->sendRequest(empty_search_req);
  CHECK(empty_search_resp.second->getStatusCode() == drogon::k200OK);

  auto empty_search_json = empty_search_resp.second->getJsonObject();
  CHECK(empty_search_json->isArray());
  CHECK(empty_search_json->size() == 0);

  // Test 4: Search with non-existent term should return empty results
  auto non_existent_search_req = drogon::HttpRequest::newHttpRequest();
  non_existent_search_req->setMethod(drogon::Get);
  non_existent_search_req->setPath("/api/v1/search?query=nonexistentterm12345");
  non_existent_search_req->addHeader("Authorization", "Bearer " + token);

  auto non_existent_search_resp = client->sendRequest(non_existent_search_req);
  CHECK(non_existent_search_resp.second->getStatusCode() == drogon::k200OK);

  auto non_existent_search_json =
      non_existent_search_resp.second->getJsonObject();
  CHECK(non_existent_search_json->isArray());
  CHECK(non_existent_search_json->size() == 0);

  // Test 5: Search without authentication should fail
  auto unauth_search_req = drogon::HttpRequest::newHttpRequest();
  unauth_search_req->setMethod(drogon::Get);
  unauth_search_req->setPath("/api/v1/search?query=test");

  auto unauth_search_resp = client->sendRequest(unauth_search_req);
  CHECK(unauth_search_resp.second->getStatusCode() == drogon::k401Unauthorized);

  // Clean up test data
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testsearch')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM orders WHERE status = 'test_search_status'"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM posts WHERE content LIKE '%test_search_content%'"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM users WHERE username = 'testsearch'"));
}
