#include <drogon/HttpClient.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

#include "helpers.hpp"

DROGON_TEST(ProofsAndEscrowTest) {
  auto db_client = drogon::app().getDbClient();

  helpers::cleanup_db();

  // Create test users for escrow testing
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");

  // Register first user (buyer)
  Json::Value register_json1;
  register_json1["username"] = "testescrow1";
  register_json1["email"] = "testescrow1@example.com";
  register_json1["password"] = "password123";
  auto register_req1 = drogon::HttpRequest::newHttpJsonRequest(register_json1);
  register_req1->setMethod(drogon::Post);
  register_req1->setPath("/api/v1/auth/register");

  auto register_resp1 = client->sendRequest(register_req1);
  REQUIRE(register_resp1.second->getStatusCode() == drogon::k200OK);

  auto resp_json1 = register_resp1.second->getJsonObject();
  std::string token1 = (*resp_json1)["token"].asString();

  // Register second user (seller)
  Json::Value register_json2;
  register_json2["username"] = "testescrow2";
  register_json2["email"] = "testescrow2@example.com";
  register_json2["password"] = "password123";
  auto register_req2 = drogon::HttpRequest::newHttpJsonRequest(register_json2);
  register_req2->setMethod(drogon::Post);
  register_req2->setPath("/api/v1/auth/register");

  auto register_resp2 = client->sendRequest(register_req2);
  REQUIRE(register_resp2.second->getStatusCode() == drogon::k200OK);

  auto resp_json2 = register_resp2.second->getJsonObject();
  std::string token2 = (*resp_json2)["token"].asString();

  // Get user IDs for both test users
  auto user1_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testescrow1'");
  auto user2_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testescrow2'");

  REQUIRE(user1_result.size() > 0);
  REQUIRE(user2_result.size() > 0);

  int user1_id = user1_result[0]["id"].as<int>();
  int user2_id = user2_result[0]["id"].as<int>();

  // Create a post and offer for testing
  Json::Value create_post_json;
  create_post_json["content"] =
      "Test post for escrow testing - Looking for a laptop";
  create_post_json["tags"] = Json::Value(Json::arrayValue);
  create_post_json["tags"].append("electronics");
  create_post_json["tags"].append("laptop");
  create_post_json["is_product_request"] = true;

  auto create_post_req =
      drogon::HttpRequest::newHttpJsonRequest(create_post_json);
  create_post_req->setMethod(drogon::Post);
  create_post_req->setPath("/api/v1/posts");
  create_post_req->addHeader("Authorization", "Bearer " + token1);

  auto create_post_resp = client->sendRequest(create_post_req);
  REQUIRE(create_post_resp.second->getStatusCode() == drogon::k200OK);

  auto create_post_resp_json = create_post_resp.second->getJsonObject();
  int post_id = (*create_post_resp_json)["post_id"].asInt();

  // Create an offer
  Json::Value create_offer_json;
  create_offer_json["title"] = "Test Escrow Offer - MacBook Pro";
  create_offer_json["description"] = "MacBook Pro 16-inch with M1 Pro chip";
  create_offer_json["price"] = 1999.99;
  create_offer_json["is_public"] = true;

  auto create_offer_req =
      drogon::HttpRequest::newHttpJsonRequest(create_offer_json);
  create_offer_req->setMethod(drogon::Post);
  create_offer_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                            "/offers");
  create_offer_req->addHeader("Authorization", "Bearer " + token2);

  auto create_offer_resp = client->sendRequest(create_offer_req);
  REQUIRE(create_offer_resp.second->getStatusCode() == drogon::k200OK);

  auto create_offer_resp_json = create_offer_resp.second->getJsonObject();
  int offer_id = (*create_offer_resp_json)["offer_id"].asInt();

  // Accept the offer to move to escrow stage
  auto accept_offer_req = drogon::HttpRequest::newHttpRequest();
  accept_offer_req->setMethod(drogon::Post);
  accept_offer_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                            "/accept");
  accept_offer_req->addHeader("Authorization", "Bearer " + token1);

  auto accept_offer_resp = client->sendRequest(accept_offer_req);
  REQUIRE(accept_offer_resp.second->getStatusCode() == drogon::k200OK);

  // TODO:

  // Test request_proof

  // Test submit_proof

  // Test get_proofs

  //   Test approve_proof

  // Test reject_proof

  //   Test get_escrow

  //   Test create_escrow

  helpers::cleanup_db();
}
