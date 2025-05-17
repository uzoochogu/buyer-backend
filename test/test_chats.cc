#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

DROGON_TEST(ChatsTest) {
  // Setup test database connection
  auto db_client = drogon::app().getDbClient();

  // Clean up any test data from previous runs
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM messages WHERE sender_id IN (SELECT id FROM users WHERE "
      "username = 'testchat1' OR username = 'testchat2')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM conversation_participants WHERE user_id IN (SELECT id FROM "
      "users WHERE "
      "username = 'testchat1' OR username = 'testchat2')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM conversations WHERE name LIKE 'Test Conversation%'"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testchat1' OR username = 'testchat2')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM offers WHERE title = 'Test Offer for Chat'"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM posts WHERE content = 'Test post for chat testing'"));
  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testchat1' "
                             "OR username = 'testchat2'"));

  // Create test users for chat testing
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");

  // Register first user
  Json::Value register_json1;
  register_json1["username"] = "testchat1";
  register_json1["email"] = "testchat1@example.com";
  register_json1["password"] = "password123";
  auto register_req1 = drogon::HttpRequest::newHttpJsonRequest(register_json1);
  register_req1->setMethod(drogon::Post);
  register_req1->setPath("/api/v1/auth/register");

  auto register_resp1 = client->sendRequest(register_req1);
  CHECK(register_resp1.second->getStatusCode() == drogon::k200OK);

  auto resp_json1 = register_resp1.second->getJsonObject();
  CHECK((*resp_json1)["status"].asString() == "success");
  CHECK((*resp_json1)["token"].asString().length() > 0);

  std::string token1 = (*resp_json1)["token"].asString();

  // Register second user
  Json::Value register_json2;
  register_json2["username"] = "testchat2";
  register_json2["email"] = "testchat2@example.com";
  register_json2["password"] = "password123";
  auto register_req2 = drogon::HttpRequest::newHttpJsonRequest(register_json2);
  register_req2->setMethod(drogon::Post);
  register_req2->setPath("/api/v1/auth/register");

  auto register_resp2 = client->sendRequest(register_req2);
  CHECK(register_resp2.second->getStatusCode() == drogon::k200OK);

  auto resp_json2 = register_resp2.second->getJsonObject();
  CHECK((*resp_json2)["status"].asString() == "success");
  CHECK((*resp_json2)["token"].asString().length() > 0);

  std::string token2 = (*resp_json2)["token"].asString();

  // Get user IDs for both test users
  auto user1_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testchat1'");
  auto user2_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testchat2'");

  CHECK(user1_result.size() > 0);
  CHECK(user2_result.size() > 0);

  int user1_id = user1_result[0]["id"].as<int>();
  int user2_id = user2_result[0]["id"].as<int>();

  // Test 1: Create a new conversation
  Json::Value create_conv_json;
  create_conv_json["user_id"] = user2_id;
  create_conv_json["name"] = "Test Conversation 1";

  auto create_conv_req =
      drogon::HttpRequest::newHttpJsonRequest(create_conv_json);
  create_conv_req->setMethod(drogon::Post);
  create_conv_req->setPath("/api/v1/conversations");
  create_conv_req->addHeader("Authorization", "Bearer " + token1);

  auto create_conv_resp = client->sendRequest(create_conv_req);
  CHECK(create_conv_resp.second->getStatusCode() == drogon::k200OK);

  auto create_conv_resp_json = create_conv_resp.second->getJsonObject();
  CHECK((*create_conv_resp_json)["status"].asString() == "success");
  CHECK((*create_conv_resp_json)["conversation_id"].asInt() > 0);

  int conversation_id = (*create_conv_resp_json)["conversation_id"].asInt();

  // Test 2: Get conversations list for user1
  auto get_convs_req = drogon::HttpRequest::newHttpRequest();
  get_convs_req->setMethod(drogon::Get);
  get_convs_req->setPath("/api/v1/conversations");
  get_convs_req->addHeader("Authorization", "Bearer " + token1);

  auto get_convs_resp = client->sendRequest(get_convs_req);
  CHECK(get_convs_resp.second->getStatusCode() == drogon::k200OK);

  auto get_convs_resp_json = get_convs_resp.second->getJsonObject();
  CHECK(get_convs_resp_json->isArray());

  bool found_conversation = false;
  for (const auto& conv : *get_convs_resp_json) {
    if (conv["id"].asInt() == conversation_id &&
        conv["name"].asString() == "Test Conversation 1") {
      found_conversation = true;
      break;
    }
  }
  CHECK(found_conversation);

  // Test 3: Send a message from user1 to the conversation
  Json::Value send_msg_json;
  send_msg_json["content"] = "Hello from user1!";

  auto send_msg_req = drogon::HttpRequest::newHttpJsonRequest(send_msg_json);
  send_msg_req->setMethod(drogon::Post);
  send_msg_req->setPath("/api/v1/conversations/" +
                        std::to_string(conversation_id) + "/messages");
  send_msg_req->addHeader("Authorization", "Bearer " + token1);

  auto send_msg_resp = client->sendRequest(send_msg_req);
  CHECK(send_msg_resp.second->getStatusCode() == drogon::k200OK);

  auto send_msg_resp_json = send_msg_resp.second->getJsonObject();
  CHECK((*send_msg_resp_json)["status"].asString() == "success");
  CHECK((*send_msg_resp_json)["message_id"].asInt() > 0);

  int message1_id = (*send_msg_resp_json)["message_id"].asInt();

  // Test 4: Get messages in the conversation as user2
  auto get_msgs_req = drogon::HttpRequest::newHttpRequest();
  get_msgs_req->setMethod(drogon::Get);
  get_msgs_req->setPath("/api/v1/conversations/" +
                        std::to_string(conversation_id) + "/messages");
  get_msgs_req->addHeader("Authorization", "Bearer " + token2);

  auto get_msgs_resp = client->sendRequest(get_msgs_req);
  CHECK(get_msgs_resp.second->getStatusCode() == drogon::k200OK);

  auto get_msgs_resp_json = get_msgs_resp.second->getJsonObject();
  CHECK(get_msgs_resp_json->isArray());
  CHECK(get_msgs_resp_json->size() > 0);

  bool found_message = false;
  for (const auto& msg : *get_msgs_resp_json) {
    if (msg["content"].asString() == "Hello from user1!" &&
        msg["sender_id"].asInt() == user1_id) {
      found_message = true;
      // Check if message is initially unread
      CHECK(msg["is_read"].asBool() == false);
      break;
    }
  }
  CHECK(found_message);

  // Test 5: Send a reply from user2
  Json::Value reply_json;
  reply_json["content"] = "Hello back from user2!";

  auto reply_req = drogon::HttpRequest::newHttpJsonRequest(reply_json);
  reply_req->setMethod(drogon::Post);
  reply_req->setPath("/api/v1/conversations/" +
                     std::to_string(conversation_id) + "/messages");
  reply_req->addHeader("Authorization", "Bearer " + token2);

  auto reply_resp = client->sendRequest(reply_req);
  CHECK(reply_resp.second->getStatusCode() == drogon::k200OK);

  auto reply_resp_json = reply_resp.second->getJsonObject();
  CHECK((*reply_resp_json)["status"].asString() == "success");
  CHECK((*reply_resp_json)["message_id"].asInt() > 0);

  int message2_id = (*reply_resp_json)["message_id"].asInt();

  // Test 6: Mark messages as read for user1
  auto mark_read_req = drogon::HttpRequest::newHttpRequest();
  mark_read_req->setMethod(drogon::Post);
  mark_read_req->setPath("/api/v1/conversations/" +
                         std::to_string(conversation_id) + "/read");
  mark_read_req->addHeader("Authorization", "Bearer " + token1);

  auto mark_read_resp = client->sendRequest(mark_read_req);
  CHECK(mark_read_resp.second->getStatusCode() == drogon::k200OK);

  auto mark_read_resp_json = mark_read_resp.second->getJsonObject();
  CHECK((*mark_read_resp_json)["status"].asString() == "success");
  CHECK((*mark_read_resp_json)["messages_marked"].asInt() > 0);

  // Test 7: Verify messages are marked as read
  auto get_msgs_after_read_req = drogon::HttpRequest::newHttpRequest();
  get_msgs_after_read_req->setMethod(drogon::Get);
  get_msgs_after_read_req->setPath(
      "/api/v1/conversations/" + std::to_string(conversation_id) + "/messages");
  get_msgs_after_read_req->addHeader("Authorization", "Bearer " + token1);

  auto get_msgs_after_read_resp = client->sendRequest(get_msgs_after_read_req);
  CHECK(get_msgs_after_read_resp.second->getStatusCode() == drogon::k200OK);

  auto get_msgs_after_read_json =
      get_msgs_after_read_resp.second->getJsonObject();

  bool user2_message_read = false;
  for (const auto& msg : *get_msgs_after_read_json) {
    if (msg["id"].asInt() == message2_id &&
        msg["sender_id"].asInt() == user2_id) {
      user2_message_read = msg["is_read"].asBool();
      break;
    }
  }
  CHECK(user2_message_read);

  // Test 8: Get unread count for user2
  auto unread_req = drogon::HttpRequest::newHttpRequest();
  unread_req->setMethod(drogon::Get);
  unread_req->setPath("/api/v1/conversations/unread");
  unread_req->addHeader("Authorization", "Bearer " + token2);

  auto unread_resp = client->sendRequest(unread_req);
  CHECK(unread_resp.second->getStatusCode() == drogon::k200OK);

  auto unread_resp_json = unread_resp.second->getJsonObject();
  CHECK((*unread_resp_json).isMember("unread_count"));
  int unread_count = (*unread_resp_json)["unread_count"].asInt();

  // User2 should have at least one unread message (from user1)
  CHECK(unread_count >= 1);

  // Test 9: Try to access a conversation without being a participant
  // Create a new conversation between user2 and a system user (not user1)
  // For simplicity, we'll just try to access a non-existent conversation ID
  int non_existent_conversation_id = conversation_id + 10000;

  auto unauthorized_get_msgs_req = drogon::HttpRequest::newHttpRequest();
  unauthorized_get_msgs_req->setMethod(drogon::Get);
  unauthorized_get_msgs_req->setPath(
      "/api/v1/conversations/" + std::to_string(non_existent_conversation_id) +
      "/messages");
  unauthorized_get_msgs_req->addHeader("Authorization", "Bearer " + token1);

  auto unauthorized_get_msgs_resp =
      client->sendRequest(unauthorized_get_msgs_req);
  // Should either be 403 Forbidden or 404 Not Found
  CHECK(unauthorized_get_msgs_resp.second->getStatusCode() != drogon::k200OK);

  // Test 10: Test conversation by offer ID
  // First create a test post and offer
  REQUIRE_NOTHROW(
      db_client->execSqlSync("INSERT INTO posts (user_id, content, created_at) "
                             "VALUES ($1, 'Test post for chat testing', NOW())",
                             user1_id));

  auto post_result = db_client->execSqlSync(
      "SELECT id FROM posts WHERE content = 'Test post for chat testing'");
  CHECK(post_result.size() > 0);
  int post_id = post_result[0]["id"].as<int>();

  REQUIRE_NOTHROW(db_client->execSqlSync(
      "INSERT INTO offers (post_id, user_id, title, description, price, "
      "original_price, is_public, status) "
      "VALUES ($1, $2, 'Test Offer for Chat', 'Test offer description', "
      "100.00, 100.00, true, 'pending')",
      post_id, user2_id));

  auto offer_result = db_client->execSqlSync(
      "SELECT id FROM offers WHERE title = 'Test Offer for Chat'");
  CHECK(offer_result.size() > 0);
  int offer_id = offer_result[0]["id"].as<int>();

  // Test 10: Get or create conversation by offer ID
  auto get_conv_by_offer_req = drogon::HttpRequest::newHttpRequest();
  get_conv_by_offer_req->setMethod(drogon::Get);
  get_conv_by_offer_req->setPath("/api/v1/conversations/offer/" +
                                 std::to_string(offer_id));
  get_conv_by_offer_req->addHeader("Authorization", "Bearer " + token1);

  auto get_conv_by_offer_resp = client->sendRequest(get_conv_by_offer_req);
  CHECK(get_conv_by_offer_resp.second->getStatusCode() == drogon::k200OK);

  auto get_conv_by_offer_json = get_conv_by_offer_resp.second->getJsonObject();
  CHECK((*get_conv_by_offer_json)["status"].asString() == "success");
  CHECK((*get_conv_by_offer_json).isMember("conversation_id"));

  int offer_conversation_id =
      (*get_conv_by_offer_json)["conversation_id"].asInt();

  // Test 11: Send a message in the offer conversation
  Json::Value offer_msg_json;
  offer_msg_json["content"] = "I'm interested in your offer!";

  auto offer_msg_req = drogon::HttpRequest::newHttpJsonRequest(offer_msg_json);
  offer_msg_req->setMethod(drogon::Post);
  offer_msg_req->setPath("/api/v1/conversations/" +
                         std::to_string(offer_conversation_id) + "/messages");
  offer_msg_req->addHeader("Authorization", "Bearer " + token1);

  auto offer_msg_resp = client->sendRequest(offer_msg_req);
  CHECK(offer_msg_resp.second->getStatusCode() == drogon::k200OK);

  auto offer_msg_resp_json = offer_msg_resp.second->getJsonObject();
  CHECK((*offer_msg_resp_json)["status"].asString() == "success");
  CHECK((*offer_msg_resp_json)["message_id"].asInt() > 0);

  // Test 12: User2 should be able to access the offer conversation
  auto user2_get_offer_msgs_req = drogon::HttpRequest::newHttpRequest();
  user2_get_offer_msgs_req->setMethod(drogon::Get);
  user2_get_offer_msgs_req->setPath("/api/v1/conversations/" +
                                    std::to_string(offer_conversation_id) +
                                    "/messages");
  user2_get_offer_msgs_req->addHeader("Authorization", "Bearer " + token2);

  auto user2_get_offer_msgs_resp =
      client->sendRequest(user2_get_offer_msgs_req);
  CHECK(user2_get_offer_msgs_resp.second->getStatusCode() == drogon::k200OK);

  auto user2_get_offer_msgs_json =
      user2_get_offer_msgs_resp.second->getJsonObject();
  CHECK(user2_get_offer_msgs_json->isArray());

  bool found_offer_message = false;
  for (const auto& msg : *user2_get_offer_msgs_json) {
    if (msg["content"].asString() == "I'm interested in your offer!" &&
        msg["sender_id"].asInt() == user1_id) {
      found_offer_message = true;
      break;
    }
  }
  CHECK(found_offer_message);

  // NO PAGINATION SUPPORT YET
  // // Test 13: Test pagination of messages
  // // Send multiple messages to ensure we have enough for pagination
  // for (int i = 0; i < 15; i++) {
  //   Json::Value pagination_msg_json;
  //   pagination_msg_json["content"] =
  //       "Pagination test message " + std::to_string(i);

  //   auto pagination_msg_req =
  //       drogon::HttpRequest::newHttpJsonRequest(pagination_msg_json);
  //   pagination_msg_req->setMethod(drogon::Post);
  //   pagination_msg_req->setPath("/api/v1/conversations/" +
  //                               std::to_string(conversation_id) +
  //                               "/messages");
  //   pagination_msg_req->addHeader("Authorization", "Bearer " + token1);

  //   auto pagination_msg_resp = client->sendRequest(pagination_msg_req);
  //   CHECK(pagination_msg_resp.second->getStatusCode() == drogon::k200OK);
  // }

  // // Get first page of messages (default page size should be 10)
  // auto page1_req = drogon::HttpRequest::newHttpRequest();
  // page1_req->setMethod(drogon::Get);
  // page1_req->setPath("/api/v1/conversations/" +
  //                    std::to_string(conversation_id) + "/messages?page=1");
  // page1_req->addHeader("Authorization", "Bearer " + token1);

  // auto page1_resp = client->sendRequest(page1_req);
  // CHECK(page1_resp.second->getStatusCode() == drogon::k200OK);

  // auto page1_json = page1_resp.second->getJsonObject();
  // CHECK(page1_json->isArray());

  // // Should have at most 10 messages on first page
  // CHECK(page1_json->size() <= 10);

  // // Get second page of messages
  // auto page2_req = drogon::HttpRequest::newHttpRequest();
  // page2_req->setMethod(drogon::Get);
  // page2_req->setPath("/api/v1/conversations/" +
  //                    std::to_string(conversation_id) + "/messages?page=2");
  // page2_req->addHeader("Authorization", "Bearer " + token1);

  // auto page2_resp = client->sendRequest(page2_req);
  // CHECK(page2_resp.second->getStatusCode() == drogon::k200OK);

  // auto page2_json = page2_resp.second->getJsonObject();
  // CHECK(page2_json->isArray());

  // // Verify we have different messages on different pages
  // bool pages_are_different = true;
  // if (page1_json->size() > 0 && page2_json->size() > 0) {
  //   // Compare first message ID from each page
  //   int first_msg_id_page1 = (*page1_json)[0]["id"].asInt();
  //   int first_msg_id_page2 = (*page2_json)[0]["id"].asInt();
  //   pages_are_different = (first_msg_id_page1 != first_msg_id_page2);
  // }
  // CHECK(pages_are_different);

  // Test 14: Test invalid conversation ID
  auto invalid_conv_req = drogon::HttpRequest::newHttpRequest();
  invalid_conv_req->setMethod(drogon::Get);
  invalid_conv_req->setPath("/api/v1/conversations/invalid/messages");
  invalid_conv_req->addHeader("Authorization", "Bearer " + token1);

  auto invalid_conv_resp = client->sendRequest(invalid_conv_req);
  CHECK(invalid_conv_resp.second->getStatusCode() == drogon::k400BadRequest);

  // Test 15: Test sending empty message
  Json::Value empty_msg_json;
  empty_msg_json["content"] = "";

  auto empty_msg_req = drogon::HttpRequest::newHttpJsonRequest(empty_msg_json);
  empty_msg_req->setMethod(drogon::Post);
  empty_msg_req->setPath("/api/v1/conversations/" +
                         std::to_string(conversation_id) + "/messages");
  empty_msg_req->addHeader("Authorization", "Bearer " + token1);

  auto empty_msg_resp = client->sendRequest(empty_msg_req);
  CHECK(empty_msg_resp.second->getStatusCode() == drogon::k400BadRequest);

  // Test 16: Test sending message without content field
  Json::Value no_content_json;
  no_content_json["wrong_field"] = "This should fail";

  auto no_content_req =
      drogon::HttpRequest::newHttpJsonRequest(no_content_json);
  no_content_req->setMethod(drogon::Post);
  no_content_req->setPath("/api/v1/conversations/" +
                          std::to_string(conversation_id) + "/messages");
  no_content_req->addHeader("Authorization", "Bearer " + token1);

  auto no_content_resp = client->sendRequest(no_content_req);
  CHECK(no_content_resp.second->getStatusCode() == drogon::k400BadRequest);

  // Test 17: Test creating conversation with invalid user ID
  Json::Value invalid_user_json;
  invalid_user_json["user_id"] = -1;
  invalid_user_json["name"] = "Invalid User Conversation";

  auto invalid_user_req =
      drogon::HttpRequest::newHttpJsonRequest(invalid_user_json);
  invalid_user_req->setMethod(drogon::Post);
  invalid_user_req->setPath("/api/v1/conversations");
  invalid_user_req->addHeader("Authorization", "Bearer " + token1);

  auto invalid_user_resp = client->sendRequest(invalid_user_req);
  CHECK(invalid_user_resp.second->getStatusCode() != drogon::k200OK);

  // Test 18: Test creating conversation with missing fields
  Json::Value missing_fields_json;
  // Missing user_id field
  missing_fields_json["name"] = "Missing Fields Conversation";

  auto missing_fields_req =
      drogon::HttpRequest::newHttpJsonRequest(missing_fields_json);
  missing_fields_req->setMethod(drogon::Post);
  missing_fields_req->setPath("/api/v1/conversations");
  missing_fields_req->addHeader("Authorization", "Bearer " + token1);

  auto missing_fields_resp = client->sendRequest(missing_fields_req);
  CHECK(missing_fields_resp.second->getStatusCode() == drogon::k400BadRequest);

  // Test 19: Test unauthorized access (no token)
  auto no_auth_req = drogon::HttpRequest::newHttpRequest();
  no_auth_req->setMethod(drogon::Get);
  no_auth_req->setPath("/api/v1/conversations");

  auto no_auth_resp = client->sendRequest(no_auth_req);
  CHECK(no_auth_resp.second->getStatusCode() == drogon::k401Unauthorized);

  // Test 20: Test with invalid token
  auto invalid_token_req = drogon::HttpRequest::newHttpRequest();
  invalid_token_req->setMethod(drogon::Get);
  invalid_token_req->setPath("/api/v1/conversations");
  invalid_token_req->addHeader("Authorization", "Bearer invalid_token_here");

  auto invalid_token_resp = client->sendRequest(invalid_token_req);
  CHECK(invalid_token_resp.second->getStatusCode() == drogon::k401Unauthorized);

  // Clean up test data in the correct order to avoid foreign key violations
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM messages WHERE conversation_id IN ($1, $2)", conversation_id,
      offer_conversation_id));

  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM conversation_participants WHERE conversation_id IN ($1, $2)",
      conversation_id, offer_conversation_id));

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM conversations WHERE id IN ($1, $2)",
                             conversation_id, offer_conversation_id));

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM offers WHERE id = $1", offer_id));

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM posts WHERE id = $1", post_id));

  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testchat1' OR username = 'testchat2')"));

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testchat1' "
                             "OR username = 'testchat2'"));
}
