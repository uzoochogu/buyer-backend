#include <drogon/HttpClient.h>
#include <drogon/drogon_test.h>

#include <string>

#include "helpers.hpp"

DROGON_TEST(OffersWorkflowTest) {
  auto db_client = drogon::app().getDbClient();

  helpers::cleanup_db();

  // Create test users for offer testing
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");

  // Register first user (post owner)
  Json::Value register_json1;
  register_json1["username"] = "testoffer1";
  register_json1["email"] = "testoffer1@example.com";
  register_json1["password"] = "password123";
  auto register_req1 = drogon::HttpRequest::newHttpJsonRequest(register_json1);
  register_req1->setMethod(drogon::Post);
  register_req1->setPath("/api/v1/auth/register");

  auto register_resp1 = client->sendRequest(register_req1);
  REQUIRE(register_resp1.second->getStatusCode() == drogon::k200OK);

  auto resp_json1 = register_resp1.second->getJsonObject();
  REQUIRE((*resp_json1)["status"].asString() == "success");
  REQUIRE((*resp_json1)["token"].asString().length() > 0);

  std::string token1 = (*resp_json1)["token"].asString();

  // Register second user (offer maker)
  Json::Value register_json2;
  register_json2["username"] = "testoffer2";
  register_json2["email"] = "testoffer2@example.com";
  register_json2["password"] = "password123";
  auto register_req2 = drogon::HttpRequest::newHttpJsonRequest(register_json2);
  register_req2->setMethod(drogon::Post);
  register_req2->setPath("/api/v1/auth/register");

  auto register_resp2 = client->sendRequest(register_req2);
  REQUIRE(register_resp2.second->getStatusCode() == drogon::k200OK);

  auto resp_json2 = register_resp2.second->getJsonObject();
  REQUIRE((*resp_json2)["status"].asString() == "success");
  REQUIRE((*resp_json2)["token"].asString().length() > 0);

  std::string token2 = (*resp_json2)["token"].asString();

  // Register third user (offer maker2)
  Json::Value register_json3;
  register_json3["username"] = "testoffer3";
  register_json3["email"] = "testoffer3@example.com";
  register_json3["password"] = "password123";
  auto register_req3 = drogon::HttpRequest::newHttpJsonRequest(register_json3);
  register_req3->setMethod(drogon::Post);
  register_req3->setPath("/api/v1/auth/register");

  auto register_resp3 = client->sendRequest(register_req3);
  REQUIRE(register_resp3.second->getStatusCode() == drogon::k200OK);

  auto resp_json3 = register_resp3.second->getJsonObject();
  REQUIRE((*resp_json3)["status"].asString() == "success");
  REQUIRE((*resp_json3)["token"].asString().length() > 0);

  std::string token3 = (*resp_json3)["token"].asString();

  // Get user IDs for  test users
  drogon::orm::Result user1_result, user2_result, user3_result;
  REQUIRE_NOTHROW(user1_result = db_client->execSqlSync(
                      "SELECT id FROM users WHERE username = 'testoffer1'"));
  REQUIRE_NOTHROW(user2_result = db_client->execSqlSync(
                      "SELECT id FROM users WHERE username = 'testoffer2'"));
  REQUIRE_NOTHROW(user3_result = db_client->execSqlSync(
                      "SELECT id FROM users WHERE username = 'testoffer3'"));

  REQUIRE(user1_result.size() > 0);
  REQUIRE(user2_result.size() > 0);
  REQUIRE(user3_result.size() > 0);

  int user1_id = user1_result[0]["id"].as<int>();
  int user2_id = user2_result[0]["id"].as<int>();
  int user3_id = user3_result[0]["id"].as<int>();

  // Test 1: Create a product request post by user1
  Json::Value create_post_json;
  create_post_json["content"] =
      "Test post for offer testing - Looking for a smartphone";
  create_post_json["tags"] = Json::Value(Json::arrayValue);
  create_post_json["tags"].append("electronics");
  create_post_json["tags"].append("smartphone");
  create_post_json["location"] = "Test Location";
  create_post_json["is_product_request"] = true;
  create_post_json["price_range"] = "$500-$800";

  auto create_post_req =
      drogon::HttpRequest::newHttpJsonRequest(create_post_json);
  create_post_req->setMethod(drogon::Post);
  create_post_req->setPath("/api/v1/posts");
  create_post_req->addHeader("Authorization", "Bearer " + token1);

  auto create_post_resp = client->sendRequest(create_post_req);
  REQUIRE(create_post_resp.second->getStatusCode() == drogon::k200OK);

  auto create_post_resp_json = create_post_resp.second->getJsonObject();
  REQUIRE((*create_post_resp_json)["status"].asString() == "success");
  REQUIRE((*create_post_resp_json)["post_id"].asInt() > 0);

  int post_id = (*create_post_resp_json)["post_id"].asInt();

  // Test 2: Create an offer for the post by user2
  Json::Value create_offer_json;
  create_offer_json["title"] = "Test Offer - Samsung Galaxy S21";
  create_offer_json["description"] =
      "Brand new Samsung Galaxy S21 with 128GB storage";
  create_offer_json["price"] = 699.99;
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
  REQUIRE((*create_offer_resp_json)["status"].asString() == "success");
  REQUIRE((*create_offer_resp_json)["offer_id"].asInt() > 0);

  int offer_id = (*create_offer_resp_json)["offer_id"].asInt();

  // Test 3: Get offers for the post
  auto get_offers_req = drogon::HttpRequest::newHttpRequest();
  get_offers_req->setMethod(drogon::Get);
  get_offers_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                          "/offers");
  get_offers_req->addHeader("Authorization", "Bearer " + token1);

  auto get_offers_resp = client->sendRequest(get_offers_req);
  REQUIRE(get_offers_resp.second->getStatusCode() == drogon::k200OK);

  auto get_offers_resp_json = get_offers_resp.second->getJsonObject();
  REQUIRE(get_offers_resp_json->isArray());

  bool found_offer = false;
  for (const auto& offer : *get_offers_resp_json) {
    if (offer["id"].asInt() == offer_id) {
      found_offer = true;
      CHECK(offer["title"].asString() == "Test Offer - Samsung Galaxy S21");
      CHECK(offer["price"].asDouble() == 699.99);
      CHECK(offer["status"].asString() == "pending");
      break;
    }
  }
  CHECK(found_offer);

  // Test 4: Get notifications for user1 (post owner)
  auto get_notifications_req = drogon::HttpRequest::newHttpRequest();
  get_notifications_req->setMethod(drogon::Get);
  get_notifications_req->setPath("/api/v1/offers/notifications");
  get_notifications_req->addHeader("Authorization", "Bearer " + token1);

  auto get_notifications_resp = client->sendRequest(get_notifications_req);
  REQUIRE(get_notifications_resp.second->getStatusCode() == drogon::k200OK);

  auto get_notifications_resp_json =
      get_notifications_resp.second->getJsonObject();
  REQUIRE(get_notifications_resp_json->isArray());

  bool found_notification = false;
  int notification_id = 0;
  for (const auto& notification : *get_notifications_resp_json) {
    if (notification["offer_id"].asInt() == offer_id) {
      found_notification = true;
      notification_id = notification["id"].asInt();
      CHECK(notification["is_read"].asBool() == false);
      CHECK(notification["offer_title"].asString() ==
            "Test Offer - Samsung Galaxy S21");
      break;
    }
  }
  CHECK(found_notification);

  // Test 5: Mark notification as read
  auto mark_read_req = drogon::HttpRequest::newHttpRequest();
  mark_read_req->setMethod(drogon::Post);
  mark_read_req->setPath("/api/v1/offers/notifications/" +
                         std::to_string(notification_id) + "/read");
  mark_read_req->addHeader("Authorization", "Bearer " + token1);

  auto mark_read_resp = client->sendRequest(mark_read_req);
  REQUIRE(mark_read_resp.second->getStatusCode() == drogon::k200OK);

  auto mark_read_resp_json = mark_read_resp.second->getJsonObject();
  CHECK((*mark_read_resp_json)["status"].asString() == "success");

  // Test 6: Negotiate on the offer (user1 counter-offers)
  Json::Value negotiate_json;
  negotiate_json["price"] = 650.00;
  negotiate_json["message"] = "Would you consider $650 for the phone?";

  auto negotiate_req = drogon::HttpRequest::newHttpJsonRequest(negotiate_json);
  negotiate_req->setMethod(drogon::Post);
  negotiate_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                         "/negotiate");
  negotiate_req->addHeader("Authorization", "Bearer " + token1);

  auto negotiate_resp = client->sendRequest(negotiate_req);

  REQUIRE(negotiate_resp.second->getStatusCode() == drogon::k200OK);

  auto negotiate_resp_json = negotiate_resp.second->getJsonObject();
  CHECK((*negotiate_resp_json)["status"].asString() == "success");
  REQUIRE((*negotiate_resp_json)["negotiation_id"].asInt() > 0);

  int negotiation_id = (*negotiate_resp_json)["negotiation_id"].asInt();

  // Test 7: Get negotiations for the offer
  auto get_negotiations_req = drogon::HttpRequest::newHttpRequest();
  get_negotiations_req->setMethod(drogon::Get);
  get_negotiations_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                                "/negotiations");
  get_negotiations_req->addHeader("Authorization", "Bearer " + token2);

  auto get_negotiations_resp = client->sendRequest(get_negotiations_req);
  REQUIRE(get_negotiations_resp.second->getStatusCode() == drogon::k200OK);

  auto get_negotiations_resp_json =
      get_negotiations_resp.second->getJsonObject();
  REQUIRE((*get_negotiations_resp_json)["negotiations"].isArray());

  bool found_negotiation = false;
  for (const auto& negotiation :
       (*get_negotiations_resp_json)["negotiations"]) {
    if (negotiation["id"].asInt() == negotiation_id) {
      found_negotiation = true;
      CHECK(negotiation["proposed_price"].asDouble() == 650.00);
      CHECK(negotiation["message"].asString() ==
            "Would you consider $650 for the phone?");
      CHECK(negotiation["status"].asString() == "pending");
      break;
    }
  }
  CHECK(found_negotiation);

  // Test 8: Check if a conversation was created for the offer (user 1 and user
  // 2)
  auto get_conversation_req = drogon::HttpRequest::newHttpRequest();
  get_conversation_req->setMethod(drogon::Get);
  get_conversation_req->setPath("/api/v1/conversations/offer/" +
                                std::to_string(offer_id));
  get_conversation_req->addHeader("Authorization", "Bearer " + token1);

  auto get_conversation_resp = client->sendRequest(get_conversation_req);
  REQUIRE(get_conversation_resp.second->getStatusCode() == drogon::k200OK);

  auto get_conversation_resp_json =
      get_conversation_resp.second->getJsonObject();

  int conversation_id =
      (*get_conversation_resp_json)["conversation_id"].asInt();

  REQUIRE(conversation_id > 0);

  // Test 9: Check if negotiation message was added to the conversation
  auto get_messages_req = drogon::HttpRequest::newHttpRequest();
  get_messages_req->setMethod(drogon::Get);
  get_messages_req->setPath("/api/v1/conversations/" +
                            std::to_string(conversation_id) + "/messages");
  get_messages_req->addHeader("Authorization", "Bearer " + token1);

  auto get_messages_resp = client->sendRequest(get_messages_req);
  REQUIRE(get_messages_resp.second->getStatusCode() == drogon::k200OK);

  auto get_messages_resp_json = get_messages_resp.second->getJsonObject();
  REQUIRE(get_messages_resp_json->isArray());

  bool found_negotiation_message = false;
  for (const auto& message : *get_messages_resp_json) {
    if (message["content"].asString().find("$650") != std::string::npos) {
      found_negotiation_message = true;
      break;
    }
  }
  CHECK(found_negotiation_message);

  // Test 10: Accept the negotiation (user2 (offer creator) accepts the
  // counter-offer)
  auto accept_negotiation_req = drogon::HttpRequest::newHttpRequest();
  accept_negotiation_req->setMethod(drogon::Post);
  accept_negotiation_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                                  "/accept-counter");
  accept_negotiation_req->addHeader("Authorization", "Bearer " + token2);

  auto accept_negotiation_resp = client->sendRequest(accept_negotiation_req);
  REQUIRE(accept_negotiation_resp.second->getStatusCode() == drogon::k200OK);

  auto accept_negotiation_resp_json =
      accept_negotiation_resp.second->getJsonObject();
  CHECK((*accept_negotiation_resp_json)["status"].asString() == "success");

  // Test 11: Verify the offer price was updated
  auto get_updated_offer_req = drogon::HttpRequest::newHttpRequest();
  get_updated_offer_req->setMethod(drogon::Get);
  get_updated_offer_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                                 "/offers");
  get_updated_offer_req->addHeader("Authorization", "Bearer " + token1);

  auto get_updated_offer_resp = client->sendRequest(get_updated_offer_req);
  REQUIRE(get_updated_offer_resp.second->getStatusCode() == drogon::k200OK);

  auto get_updated_offer_resp_json =
      get_updated_offer_resp.second->getJsonObject();
  REQUIRE(get_updated_offer_resp_json->isArray());

  bool found_updated_offer = false;
  for (const auto& offer : *get_updated_offer_resp_json) {
    if (offer["id"].asInt() == offer_id) {
      found_updated_offer = true;
      CHECK(offer["price"].asDouble() == 650.00);
      break;
    }
  }
  CHECK(found_updated_offer);

  // -------------------------
  // New Post with More Offers and Negotiations
  // -------------------------

  // Test 13: Create a product request post by user2 this time
  create_post_json["content"] =
      "Test Post 2 - Mercedes Benz G53 for sale - Sleek new car";
  create_post_json["tags"] = Json::Value(Json::arrayValue);
  create_post_json["tags"].append("vehicles");
  create_post_json["location"] = "Test Location 2";
  create_post_json["is_product_request"] = true;
  create_post_json["price_range"] = "$90000-$120000";

  create_post_req = drogon::HttpRequest::newHttpJsonRequest(create_post_json);
  create_post_req->setMethod(drogon::Post);
  create_post_req->setPath("/api/v1/posts");
  create_post_req->addHeader("Authorization", "Bearer " + token2);

  create_post_resp = client->sendRequest(create_post_req);
  REQUIRE(create_post_resp.second->getStatusCode() == drogon::k200OK);

  create_post_resp_json = create_post_resp.second->getJsonObject();
  CHECK((*create_post_resp_json)["status"].asString() == "success");
  CHECK((*create_post_resp_json)["post_id"].asInt() > 0);

  post_id = (*create_post_resp_json)["post_id"].asInt();

  // Test 14: Create an offer for the post by user1
  create_offer_json = Json::Value{};
  create_offer_json["title"] = "Test Offer 2 - Want to buy Mercedes Benz G53";
  create_offer_json["description"] = "I am serious about purchasing car";
  create_offer_json["price"] = 50000;
  create_offer_json["is_public"] = true;

  create_offer_req = drogon::HttpRequest::newHttpJsonRequest(create_offer_json);
  create_offer_req->setMethod(drogon::Post);
  create_offer_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                            "/offers");
  create_offer_req->addHeader("Authorization", "Bearer " + token1);

  create_offer_resp = client->sendRequest(create_offer_req);
  REQUIRE(create_offer_resp.second->getStatusCode() == drogon::k200OK);

  create_offer_resp_json = create_offer_resp.second->getJsonObject();
  CHECK((*create_offer_resp_json)["status"].asString() == "success");
  CHECK((*create_offer_resp_json)["offer_id"].asInt() > 0);

  offer_id = (*create_offer_resp_json)["offer_id"].asInt();

  // Test 15: Create an offer for the post by user3, this offer will be ignored
  // mostly
  create_offer_json = Json::Value{};
  create_offer_json["title"] =
      "Test Offer 3 - I am ready to buy Mercedes Benz G53";
  create_offer_json["description"] =
      "It look like a used car, how about this price?";
  create_offer_json["price"] = 20000;
  create_offer_json["is_public"] = false;

  create_offer_req = drogon::HttpRequest::newHttpJsonRequest(create_offer_json);
  create_offer_req->setMethod(drogon::Post);
  create_offer_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                            "/offers");
  create_offer_req->addHeader("Authorization", "Bearer " + token3);

  create_offer_resp = client->sendRequest(create_offer_req);
  REQUIRE(create_offer_resp.second->getStatusCode() == drogon::k200OK);

  create_offer_resp_json = create_offer_resp.second->getJsonObject();
  CHECK((*create_offer_resp_json)["status"].asString() == "success");
  CHECK((*create_offer_resp_json)["offer_id"].asInt() > 0);

  int ignored_offer_id = (*create_offer_resp_json)["offer_id"].asInt();

  // Test 16: Check offers for the post
  get_offers_req = drogon::HttpRequest::newHttpRequest();
  get_offers_req->setMethod(drogon::Get);
  get_offers_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                          "/offers");
  get_offers_req->addHeader("Authorization", "Bearer " + token2);

  get_offers_resp = client->sendRequest(get_offers_req);
  REQUIRE(get_offers_resp.second->getStatusCode() == drogon::k200OK);

  get_offers_resp_json = get_offers_resp.second->getJsonObject();
  CHECK(get_offers_resp_json->isArray());

  int post_offers = 0;
  for (const auto& offer : *get_offers_resp_json) {
    if (offer["id"].asInt() == offer_id) {
      ++post_offers;
      CHECK(offer["title"].asString() ==
            "Test Offer 2 - Want to buy Mercedes Benz G53");
      CHECK(offer["price"].asDouble() == 50000.0);
      CHECK(offer["status"].asString() == "pending");
    } else if (offer["id"].asInt() == ignored_offer_id) {
      ++post_offers;
      CHECK(offer["title"].asString() ==
            "Test Offer 3 - I am ready to buy Mercedes Benz G53");
      CHECK(offer["price"].asDouble() == 20000.0);
      CHECK(offer["status"].asString() == "pending");
    }
  }
  CHECK(post_offers == 2);
  CHECK(get_offers_resp_json->size() == 2);

  // Test 17: Negotiation 1, User 2 (Post creator) negotiates (counter-offers)
  // with user 1
  negotiate_json = Json::Value{};
  negotiate_json["price"] = 80000.00;
  negotiate_json["message"] = "I can only release it for this price";

  negotiate_req = drogon::HttpRequest::newHttpJsonRequest(negotiate_json);
  negotiate_req->setMethod(drogon::Post);
  negotiate_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                         "/negotiate");
  negotiate_req->addHeader("Authorization", "Bearer " + token2);

  negotiate_resp = client->sendRequest(negotiate_req);

  REQUIRE(negotiate_resp.second->getStatusCode() == drogon::k200OK);

  negotiate_resp_json = negotiate_resp.second->getJsonObject();
  CHECK((*negotiate_resp_json)["status"].asString() == "success");
  REQUIRE((*negotiate_resp_json)["negotiation_id"].asInt() > 0);

  // Test 17: Negotiation 1, User 2 (Post creator) negotiates (counter-offers)
  // with user 3
  negotiate_json = Json::Value{};
  negotiate_json["price"] = 80000.00;
  negotiate_json["message"] = "I can only release it for this price";

  negotiate_req = drogon::HttpRequest::newHttpJsonRequest(negotiate_json);
  negotiate_req->setMethod(drogon::Post);
  negotiate_req->setPath("/api/v1/offers/" + std::to_string(ignored_offer_id) +
                         "/negotiate");
  negotiate_req->addHeader("Authorization", "Bearer " + token2);

  negotiate_resp = client->sendRequest(negotiate_req);

  REQUIRE(negotiate_resp.second->getStatusCode() == drogon::k200OK);

  negotiate_resp_json = negotiate_resp.second->getJsonObject();
  CHECK((*negotiate_resp_json)["status"].asString() == "success");
  CHECK((*negotiate_resp_json)["negotiation_id"].asInt() > 0);

  // Test 18: Negotiation 2, User 1 (offer creator) negotiates (counter-offers)
  // lower
  negotiate_json = Json::Value{};
  negotiate_json["price"] = 70000.00;
  negotiate_json["message"] = "What about this price?";

  negotiate_req = drogon::HttpRequest::newHttpJsonRequest(negotiate_json);
  negotiate_req->setMethod(drogon::Post);
  negotiate_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                         "/negotiate");
  negotiate_req->addHeader("Authorization", "Bearer " + token1);

  negotiate_resp = client->sendRequest(negotiate_req);

  REQUIRE(negotiate_resp.second->getStatusCode() == drogon::k200OK);

  negotiate_resp_json = negotiate_resp.second->getJsonObject();
  CHECK((*negotiate_resp_json)["status"].asString() == "success");
  CHECK((*negotiate_resp_json)["negotiation_id"].asInt() > 0);

  // Test 19: Negotiation 3, User 2 (Post creator) negotiates (counter-offers)
  // again
  negotiate_json = Json::Value{};
  negotiate_json["price"] = 80000.00;
  negotiate_json["message"] = "Make it higher than that";

  negotiate_req = drogon::HttpRequest::newHttpJsonRequest(negotiate_json);
  negotiate_req->setMethod(drogon::Post);
  negotiate_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                         "/negotiate");
  negotiate_req->addHeader("Authorization", "Bearer " + token2);

  negotiate_resp = client->sendRequest(negotiate_req);

  REQUIRE(negotiate_resp.second->getStatusCode() == drogon::k200OK);

  negotiate_resp_json = negotiate_resp.second->getJsonObject();
  CHECK((*negotiate_resp_json)["status"].asString() == "success");
  CHECK((*negotiate_resp_json)["negotiation_id"].asInt() > 0);

  // Test 20: Negotiation 4, User 1 (Offer creator)  does final negotiation
  // (counter-offers) again
  negotiate_json = Json::Value{};
  negotiate_json["price"] = 75000.00;
  negotiate_json["message"] = "Highest I can do";

  negotiate_req = drogon::HttpRequest::newHttpJsonRequest(negotiate_json);
  negotiate_req->setMethod(drogon::Post);
  negotiate_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                         "/negotiate");
  negotiate_req->addHeader("Authorization", "Bearer " + token1);

  negotiate_resp = client->sendRequest(negotiate_req);

  REQUIRE(negotiate_resp.second->getStatusCode() == drogon::k200OK);

  negotiate_resp_json = negotiate_resp.second->getJsonObject();
  CHECK((*negotiate_resp_json)["status"].asString() == "success");
  CHECK((*negotiate_resp_json)["negotiation_id"].asInt() > 0);

  // Test 21: Accept the offer (user2 (post creator) accepts the offer)
  auto accept_offer_req = drogon::HttpRequest::newHttpRequest();
  accept_offer_req->setMethod(drogon::Post);
  accept_offer_req->setPath("/api/v1/offers/" + std::to_string(offer_id) +
                            "/accept");
  accept_offer_req->addHeader("Authorization", "Bearer " + token2);

  auto accept_offer_resp = client->sendRequest(accept_offer_req);
  REQUIRE(accept_offer_resp.second->getStatusCode() == drogon::k200OK);

  auto accept_offer_resp_json = accept_offer_resp.second->getJsonObject();
  CHECK((*accept_offer_resp_json)["status"].asString() == "success");

  // Test 22: Verify the offer status was updated
  auto get_final_offer_req = drogon::HttpRequest::newHttpRequest();
  get_final_offer_req->setMethod(drogon::Get);
  get_final_offer_req->setPath("/api/v1/posts/" + std::to_string(post_id) +
                               "/offers");
  get_final_offer_req->addHeader("Authorization", "Bearer " + token2);

  auto get_final_offer_resp = client->sendRequest(get_final_offer_req);
  REQUIRE(get_final_offer_resp.second->getStatusCode() == drogon::k200OK);

  auto get_final_offer_resp_json = get_final_offer_resp.second->getJsonObject();
  REQUIRE(get_final_offer_resp_json->isArray());

  bool found_final_offer = false;
  for (const auto& offer : *get_final_offer_resp_json) {
    if (offer["id"].asInt() == offer_id) {
      found_final_offer = true;
      CHECK(offer["status"].asString() == "accepted");
      break;
    }
  }
  CHECK(found_final_offer);

  // Test 23: Offer accepted: Check if final negotiation message has accepted in
  // metadata Test 24: Also check if all other messages have rejected in
  // metadata.
  auto get_offer_messages_req = drogon::HttpRequest::newHttpRequest();
  get_offer_messages_req->setMethod(drogon::Get);
  get_offer_messages_req->setPath(
      "/api/v1/conversations/" + std::to_string(conversation_id) + "/messages");
  get_offer_messages_req->addHeader("Authorization", "Bearer " + token2);

  auto get_offer_messages_resp = client->sendRequest(get_offer_messages_req);
  REQUIRE(get_offer_messages_resp.second->getStatusCode() == drogon::k200OK);

  auto get_offer_messages_resp_json =
      get_offer_messages_resp.second->getJsonObject();
  REQUIRE(get_offer_messages_resp_json->isArray());

  bool found_acceptance_message = false;
  bool all_others_rejected = true;
  int accepted_message = 0;

  for (const auto& message : *get_offer_messages_resp_json) {
    Json::Reader reader;
    Json::Value metadata;
    std::string metadata_str =
        message["metadata"].asString();  // it is stringified
    if (!reader.parse(metadata_str, metadata)) {
      LOG_ERROR << "Error parsing metadata JSON: "
                << reader.getFormattedErrorMessages();
    }

    if (!metadata.isNull() && metadata.isObject() &&
        metadata["offer_id"].asString() == std::to_string(offer_id)) {
      if (metadata["offer_status"].asString() == "accepted" &&
          accepted_message ==
              0) {  // ensure we only have 1 accepted_message per offer_id
        found_acceptance_message = true;
        accepted_message = message["id"].asInt();
      } else if (message["id"].asInt() != accepted_message &&
                 metadata["offer_status"].asString() != "rejected") {
        all_others_rejected = false;
      }
    }
  }

  CHECK(found_acceptance_message);
  CHECK(all_others_rejected);

  // Test 25: Verify that the other offers were rejected
  // Get updated offers information
  get_offers_resp = client->sendRequest(get_offers_req);
  REQUIRE(get_offers_resp.second->getStatusCode() == drogon::k200OK);

  get_offers_resp_json = get_offers_resp.second->getJsonObject();
  CHECK(get_offers_resp_json->isArray());
  for (const auto& offer : *get_offers_resp_json) {
    if (offer["id"].asInt() != offer_id) {
      CHECK(offer["status"].asString() == "rejected");
    }
  }

  // Test 26: Verify the conversation created between test user 2 (post owner)
  // and user 3 (second offer creator).
  get_conversation_req = drogon::HttpRequest::newHttpRequest();
  get_conversation_req->setMethod(drogon::Get);
  get_conversation_req->setPath("/api/v1/conversations/offer/" +
                                std::to_string(ignored_offer_id));
  get_conversation_req->addHeader("Authorization", "Bearer " + token2);

  get_conversation_resp = client->sendRequest(get_conversation_req);
  REQUIRE(get_conversation_resp.second->getStatusCode() == drogon::k200OK);

  get_conversation_resp_json = get_conversation_resp.second->getJsonObject();
  conversation_id = (*get_conversation_resp_json)["conversation_id"].asInt();
  REQUIRE(conversation_id > 0);

  // Test 27: Verify that other offers messages have their metadata properly
  // updated to rejected.
  get_offer_messages_req = drogon::HttpRequest::newHttpRequest();
  get_offer_messages_req->setMethod(drogon::Get);
  get_offer_messages_req->setPath(
      "/api/v1/conversations/" + std::to_string(conversation_id) + "/messages");
  get_offer_messages_req->addHeader("Authorization", "Bearer " + token2);

  get_offer_messages_resp = client->sendRequest(get_offer_messages_req);
  REQUIRE(get_offer_messages_resp.second->getStatusCode() == drogon::k200OK);

  get_offer_messages_resp_json =
      get_offer_messages_resp.second->getJsonObject();
  REQUIRE(get_offer_messages_resp_json->isArray());

  found_acceptance_message = false;
  all_others_rejected = true;
  accepted_message = 0;

  for (const auto& message : *get_offer_messages_resp_json) {
    Json::Reader reader;
    Json::Value metadata;
    std::string metadata_str = message["metadata"].asString();
    if (!reader.parse(metadata_str, metadata)) {
      LOG_ERROR << "Error parsing metadata JSON: "
                << reader.getFormattedErrorMessages();
    }

    if (!metadata.isNull() && metadata.isObject() &&
        metadata["offer_id"].asString() == std::to_string(offer_id)) {
      if (metadata["offer_status"].asString() == "accepted" &&
          accepted_message == 0) {
        found_acceptance_message = true;
        accepted_message = message["id"].asInt();
      } else if (message["id"].asInt() != accepted_message &&
                 metadata["offer_status"].asString() != "rejected") {
        all_others_rejected = false;
      }
    }
  }

  CHECK(!found_acceptance_message);
  CHECK(all_others_rejected);

  // Test 28: Verify that negotiation_status in offer table is updated to
  // completed for accepted an rejected offers

  get_offers_req = drogon::HttpRequest::newHttpRequest();

  helpers::cleanup_db();
}
