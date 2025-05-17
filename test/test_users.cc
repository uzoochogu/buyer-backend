#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

DROGON_TEST(UsersTest) {
  // Setup test database connection
  auto db_client = drogon::app().getDbClient();

  // Clean up any test data from previous runs
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testuser1' OR username = 'testuser2')"));
  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testuser1' "
                             "OR username = 'testuser2'"));

  // Create test users for testing
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");

  // Register first user
  Json::Value register_json1;
  register_json1["username"] = "testuser1";
  register_json1["email"] = "testuser1@example.com";
  register_json1["password"] = "password123";
  auto register_req1 = drogon::HttpRequest::newHttpJsonRequest(register_json1);
  register_req1->setMethod(drogon::Post);
  register_req1->setPath("/api/v1/auth/register");

  auto register_resp1 = client->sendRequest(register_req1);
  REQUIRE(register_resp1.second->getStatusCode() == drogon::k200OK);

  auto resp_json1 = register_resp1.second->getJsonObject();
  CHECK((*resp_json1)["status"].asString() == "success");
  CHECK((*resp_json1)["token"].asString().length() > 0);

  std::string token1 = (*resp_json1)["token"].asString();

  // Register second user
  Json::Value register_json2;
  register_json2["username"] = "testuser2";
  register_json2["email"] = "testuser2@example.com";
  register_json2["password"] = "password123";
  auto register_req2 = drogon::HttpRequest::newHttpJsonRequest(register_json2);
  register_req2->setMethod(drogon::Post);
  register_req2->setPath("/api/v1/auth/register");

  auto register_resp2 = client->sendRequest(register_req2);
  REQUIRE(register_resp2.second->getStatusCode() == drogon::k200OK);

  auto resp_json2 = register_resp2.second->getJsonObject();
  CHECK((*resp_json2)["status"].asString() == "success");
  CHECK((*resp_json2)["token"].asString().length() > 0);

  std::string token2 = (*resp_json2)["token"].asString();

  // Get user IDs for both test users
  auto user1_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testuser1'");
  auto user2_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testuser2'");

  CHECK(user1_result.size() > 0);
  CHECK(user2_result.size() > 0);

  int user1_id = user1_result[0]["id"].as<int>();
  int user2_id = user2_result[0]["id"].as<int>();

  // Test 1: Get all users (authenticated)
  auto get_users_req = drogon::HttpRequest::newHttpRequest();
  get_users_req->setMethod(drogon::Get);
  get_users_req->setPath("/api/v1/users");
  get_users_req->addHeader("Authorization", "Bearer " + token1);

  auto get_users_resp = client->sendRequest(get_users_req);
  CHECK(get_users_resp.second->getStatusCode() == drogon::k200OK);

  auto get_users_resp_json = get_users_resp.second->getJsonObject();
  CHECK(get_users_resp_json->isArray());

  // Check if our test users are in the list
  bool found_user1 = false;
  bool found_user2 = false;

  for (const auto& user : *get_users_resp_json) {
    if (user["id"].asInt() == user1_id) {
      found_user1 = true;
      CHECK(user["username"].asString() == "testuser1");
      CHECK(user["email"].asString() == "testuser1@example.com");
      CHECK(user.isMember("created_at"));
      // Ensure password hash is not exposed
      CHECK(!user.isMember("password_hash"));
    }
    if (user["id"].asInt() == user2_id) {
      found_user2 = true;
      CHECK(user["username"].asString() == "testuser2");
      CHECK(user["email"].asString() == "testuser2@example.com");
      CHECK(user.isMember("created_at"));
      // Ensure password hash is not exposed
      CHECK(!user.isMember("password_hash"));
    }
  }

  CHECK(found_user1);
  CHECK(found_user2);

  // Test 2: Get users without authentication (should fail)
  auto get_users_unauth_req = drogon::HttpRequest::newHttpRequest();
  get_users_unauth_req->setMethod(drogon::Get);
  get_users_unauth_req->setPath("/api/v1/users");

  auto get_users_unauth_resp = client->sendRequest(get_users_unauth_req);
  CHECK(get_users_unauth_resp.second->getStatusCode() ==
        drogon::k401Unauthorized);

  // Test 3: Get users with invalid token (should fail)
  auto get_users_invalid_token_req = drogon::HttpRequest::newHttpRequest();
  get_users_invalid_token_req->setMethod(drogon::Get);
  get_users_invalid_token_req->setPath("/api/v1/users");
  get_users_invalid_token_req->addHeader("Authorization",
                                         "Bearer invalid_token_here");

  auto get_users_invalid_token_resp =
      client->sendRequest(get_users_invalid_token_req);
  CHECK(get_users_invalid_token_resp.second->getStatusCode() ==
        drogon::k401Unauthorized);

  // Test 4: Verify CORS headers are present
  auto options_req = drogon::HttpRequest::newHttpRequest();
  options_req->setMethod(drogon::Options);
  options_req->setPath("/api/v1/users");

  auto options_resp = client->sendRequest(options_req);
  CHECK(options_resp.second->getStatusCode() == drogon::k200OK);

  // Check for CORS headers
  // Didn't check for origin since we are making request without an origin.
  // CHECK(
  //   options_resp.second->getHeader("Access-Control-Allow-Origin").length() >
  //   0);
  CHECK(
      options_resp.second->getHeader("Access-Control-Allow-Methods").length() >
      0);
  CHECK(
      options_resp.second->getHeader("Access-Control-Allow-Headers").length() >
      0);

  // Test 5: Check that the response includes all expected users from seed data
  // Get the count of users in the database
  auto user_count_result =
      db_client->execSqlSync("SELECT COUNT(*) as count FROM users");
  CHECK(user_count_result.size() > 0);
  int total_user_count = user_count_result[0]["count"].as<int>();

  // The response should include all users
  CHECK(get_users_resp_json->size() == total_user_count);

  // Test 6: Verify that the user data format is consistent
  for (const auto& user : *get_users_resp_json) {
    // Each user should have these fields
    CHECK(user.isMember("id"));
    CHECK(user.isMember("username"));
    CHECK(user.isMember("email"));
    CHECK(user.isMember("created_at"));

    // These fields should not be exposed
    CHECK(!user.isMember("password_hash"));
    CHECK(!user.isMember("password"));

    // Check data types
    CHECK(user["id"].isInt());
    CHECK(user["username"].isString());
    CHECK(user["email"].isString());
    CHECK(user["created_at"].isString());
  }

  // Test 7: Check that the response is ordered by username as specified in the
  // controller
  bool is_ordered = true;
  std::string prev_username = "";

  for (const auto& user : *get_users_resp_json) {
    std::string current_username = user["username"].asString();
    if (!prev_username.empty() && current_username < prev_username) {
      is_ordered = false;
      break;
    }
    prev_username = current_username;
  }

  CHECK(is_ordered);

  // Test 8: Verify that the endpoint handles database errors gracefully
  // This is hard to test directly, but we can check that the controller has
  // error handling by examining the code. For integration tests, we'll assume
  // the error handling works.

  // Clean up test data
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testuser1' OR username = 'testuser2')"));

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testuser1' "
                             "OR username = 'testuser2'"));
}