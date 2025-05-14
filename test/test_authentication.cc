#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

DROGON_TEST(AuthenticationTest) {
  // Setup test database connection
  auto db_client = drogon::app().getDbClient();

  // Clean up any test data from previous runs
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testuser')"));
  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testuser'"));

  // Test registration
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");
  Json::Value request_json;
  request_json["username"] = "testuser";
  request_json["email"] = "testuser@example.com";
  request_json["password"] = "password123";
  auto req = drogon::HttpRequest::newHttpJsonRequest(request_json);
  req->setMethod(drogon::Post);
  req->setPath("/api/v1/auth/register");

  auto resp = client->sendRequest(req);
  REQUIRE(resp.second->getStatusCode() == drogon::k200OK);

  auto json = resp.second->getJsonObject();
  REQUIRE((*json)["status"].asString() == "success");
  REQUIRE((*json)["token"].asString().length() > 0);
  REQUIRE((*json)["refresh_token"].asString().length() > 0);

  std::string token = (*json)["token"].asString();
  std::string refresh_token = (*json)["refresh_token"].asString();

  // logout first since registering logs you in
  req = drogon::HttpRequest::newHttpRequest();
  req->setMethod(drogon::Post);
  req->setPath("/api/v1/auth/logout");
  req->addHeader("Authorization", "Bearer " + token);

  resp = client->sendRequest(req);
  REQUIRE(resp.second->getStatusCode() == drogon::k200OK);
  json = resp.second->getJsonObject();
  REQUIRE((*json)["status"].asString() == "success");

  // Test login
  Json::Value login_json;
  login_json["username"] = "testuser";
  login_json["password"] = "password123";
  req = drogon::HttpRequest::newHttpJsonRequest(login_json);
  req->setMethod(drogon::Post);
  req->setPath("/api/v1/auth/login");

  resp = client->sendRequest(req);
  REQUIRE(resp.second->getStatusCode() == drogon::k200OK);
  json = resp.second->getJsonObject();
  REQUIRE((*json)["status"].asString() == "success");
  REQUIRE((*json)["token"].asString().length() > 0);
  REQUIRE((*json)["refresh_token"].asString().length() > 0);

  token = (*json)["token"].asString();
  refresh_token = (*json)["refresh_token"].asString();

  // Test refresh token
  Json::Value refresh_json;
  refresh_json["refresh_token"] = refresh_token;
  req = drogon::HttpRequest::newHttpJsonRequest(refresh_json);
  req->setMethod(drogon::Post);
  req->setPath("/api/v1/auth/refresh");
  req->addHeader("Authorization", "Bearer " + token);

  resp = client->sendRequest(req);
  REQUIRE(resp.second->getStatusCode() == drogon::k200OK);
  json = resp.second->getJsonObject();
  REQUIRE((*json)["status"].asString() == "success");
  REQUIRE((*json)["token"].asString().length() > 0);

  // Test logout
  req = drogon::HttpRequest::newHttpRequest();
  req->setMethod(drogon::Post);
  req->setPath("/api/v1/auth/logout");
  req->addHeader("Authorization", "Bearer " + token);

  resp = client->sendRequest(req);
  REQUIRE(resp.second->getStatusCode() == drogon::k200OK);
  json = resp.second->getJsonObject();
  REQUIRE((*json)["status"].asString() == "success");

  // Clean up test data
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testuser')"));
  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testuser'"));
}
