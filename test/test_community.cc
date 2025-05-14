#include <drogon/HttpClient.h>
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>
#include <drogon/utils/Utilities.h>

#include <string>

DROGON_TEST(CommunityTest) {
  // Setup test database connection
  auto db_client = drogon::app().getDbClient();

  // Clean up any test data from previous runs
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM post_subscriptions WHERE user_id IN (SELECT id FROM users "
      "WHERE "
      "username = 'testcomm1' OR username = 'testcomm2')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM posts WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testcomm1' OR username = 'testcomm2')"));
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testcomm1' OR username = 'testcomm2')"));
  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testcomm1' "
                             "OR username = 'testcomm2'"));

  // Create test users for community testing
  auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:5555");

  // Register first user
  Json::Value register_json1;
  register_json1["username"] = "testcomm1";
  register_json1["email"] = "testcomm1@example.com";
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
  register_json2["username"] = "testcomm2";
  register_json2["email"] = "testcomm2@example.com";
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
      "SELECT id FROM users WHERE username = 'testcomm1'");
  auto user2_result = db_client->execSqlSync(
      "SELECT id FROM users WHERE username = 'testcomm2'");

  CHECK(user1_result.size() > 0);
  CHECK(user2_result.size() > 0);

  int user1_id = user1_result[0]["id"].as<int>();
  int user2_id = user2_result[0]["id"].as<int>();

  // Test 1: Create a regular post
  Json::Value create_post_json;
  create_post_json["content"] = "This is a test post from testcomm1";
  create_post_json["tags"] = Json::Value(Json::arrayValue);
  create_post_json["tags"].append("test");
  create_post_json["tags"].append("integration");
  create_post_json["location"] = "Test Location";
  create_post_json["is_product_request"] = false;

  auto create_post_req =
      drogon::HttpRequest::newHttpJsonRequest(create_post_json);
  create_post_req->setMethod(drogon::Post);
  create_post_req->setPath("/api/v1/posts");
  create_post_req->addHeader("Authorization", "Bearer " + token1);

  auto create_post_resp = client->sendRequest(create_post_req);
  CHECK(create_post_resp.second->getStatusCode() == drogon::k200OK);

  auto create_post_resp_json = create_post_resp.second->getJsonObject();
  CHECK((*create_post_resp_json)["status"].asString() == "success");
  CHECK((*create_post_resp_json)["post_id"].asInt() > 0);

  int regular_post_id = (*create_post_resp_json)["post_id"].asInt();

  // Test 2: Create a product request post
  Json::Value create_product_req_json;
  create_product_req_json["content"] = "Looking for a test product";
  create_product_req_json["tags"] = Json::Value(Json::arrayValue);
  create_product_req_json["tags"].append("product");
  create_product_req_json["tags"].append("request");
  create_product_req_json["location"] = "Product Location";
  create_product_req_json["is_product_request"] = true;
  create_product_req_json["price_range"] = "$10-$50";

  auto create_product_req_req =
      drogon::HttpRequest::newHttpJsonRequest(create_product_req_json);
  create_product_req_req->setMethod(drogon::Post);
  create_product_req_req->setPath("/api/v1/posts");
  create_product_req_req->addHeader("Authorization", "Bearer " + token2);

  auto create_product_req_resp = client->sendRequest(create_product_req_req);
  CHECK(create_product_req_resp.second->getStatusCode() == drogon::k200OK);

  auto create_product_req_resp_json =
      create_product_req_resp.second->getJsonObject();
  CHECK((*create_product_req_resp_json)["status"].asString() == "success");
  CHECK((*create_product_req_resp_json)["post_id"].asInt() > 0);

  int product_post_id = (*create_product_req_resp_json)["post_id"].asInt();

  // Test 3: Get all posts
  auto get_posts_req = drogon::HttpRequest::newHttpRequest();
  get_posts_req->setMethod(drogon::Get);
  get_posts_req->setPath("/api/v1/posts");
  get_posts_req->addHeader("Authorization", "Bearer " + token1);

  auto get_posts_resp = client->sendRequest(get_posts_req);
  CHECK(get_posts_resp.second->getStatusCode() == drogon::k200OK);

  auto get_posts_resp_json = get_posts_resp.second->getJsonObject();
  CHECK(get_posts_resp_json->isArray());

  bool found_regular_post = false;
  bool found_product_post = false;

  for (const auto& post : *get_posts_resp_json) {
    if (post["id"].asInt() == regular_post_id) {
      found_regular_post = true;
      CHECK(post["content"].asString() == "This is a test post from testcomm1");
      CHECK(post["user_id"].asInt() == user1_id);
      CHECK(post["is_product_request"].asBool() == false);
      CHECK(post["tags"].isArray());
      CHECK(post["tags"].size() == 2);
      CHECK(post["location"].asString() == "Test Location");
    }
    if (post["id"].asInt() == product_post_id) {
      found_product_post = true;
      CHECK(post["content"].asString() == "Looking for a test product");
      CHECK(post["user_id"].asInt() == user2_id);
      CHECK(post["is_product_request"].asBool() == true);
      CHECK(post["request_status"].asString() == "open");
      CHECK(post["price_range"].asString() == "$10-$50");
    }
  }

  CHECK(found_regular_post);
  CHECK(found_product_post);

  // Test 4: Get a specific post by ID
  auto get_post_req = drogon::HttpRequest::newHttpRequest();
  get_post_req->setMethod(drogon::Get);
  get_post_req->setPath("/api/v1/posts/" + std::to_string(regular_post_id));
  get_post_req->addHeader("Authorization", "Bearer " + token1);

  auto get_post_resp = client->sendRequest(get_post_req);
  CHECK(get_post_resp.second->getStatusCode() == drogon::k200OK);

  auto get_post_resp_json = get_post_resp.second->getJsonObject();
  CHECK(get_post_resp_json->isObject());
  CHECK((*get_post_resp_json)["id"].asInt() == regular_post_id);
  CHECK((*get_post_resp_json)["content"].asString() ==
        "This is a test post from testcomm1");
  CHECK((*get_post_resp_json)["user_id"].asInt() == user1_id);
  CHECK((*get_post_resp_json)["is_product_request"].asBool() == false);

  // Test 5: Update a post
  Json::Value update_post_json;
  update_post_json["content"] = "Updated test post content";
  update_post_json["tags"] = Json::Value(Json::arrayValue);
  update_post_json["tags"].append("test");
  update_post_json["tags"].append("updated");

  auto update_post_req =
      drogon::HttpRequest::newHttpJsonRequest(update_post_json);
  update_post_req->setMethod(drogon::Put);
  update_post_req->setPath("/api/v1/posts/" + std::to_string(regular_post_id));
  update_post_req->addHeader("Authorization", "Bearer " + token1);

  auto update_post_resp = client->sendRequest(update_post_req);
  CHECK(update_post_resp.second->getStatusCode() == drogon::k200OK);

  auto update_post_resp_json = update_post_resp.second->getJsonObject();
  CHECK((*update_post_resp_json)["status"].asString() == "success");

  // Verify the post was updated
  auto get_updated_post_req = drogon::HttpRequest::newHttpRequest();
  get_updated_post_req->setMethod(drogon::Get);
  get_updated_post_req->setPath("/api/v1/posts/" +
                                std::to_string(regular_post_id));
  get_updated_post_req->addHeader("Authorization", "Bearer " + token1);

  auto get_updated_post_resp = client->sendRequest(get_updated_post_req);
  CHECK(get_updated_post_resp.second->getStatusCode() == drogon::k200OK);

  auto get_updated_post_json = get_updated_post_resp.second->getJsonObject();
  CHECK((*get_updated_post_json)["content"].asString() ==
        "Updated test post content");
  CHECK((*get_updated_post_json)["tags"].isArray());
  CHECK((*get_updated_post_json)["tags"].size() == 2);

  bool has_updated_tag = false;
  for (const auto& tag : (*get_updated_post_json)["tags"]) {
    if (tag.asString() == "updated") {
      has_updated_tag = true;
      break;
    }
  }
  CHECK(has_updated_tag);

  // Test 6: Try to update someone else's post (should fail)
  Json::Value unauthorized_update_json;
  unauthorized_update_json["content"] = "This update should fail";

  auto unauthorized_update_req =
      drogon::HttpRequest::newHttpJsonRequest(unauthorized_update_json);
  unauthorized_update_req->setMethod(drogon::Put);
  unauthorized_update_req->setPath("/api/v1/posts/" +
                                   std::to_string(product_post_id));
  unauthorized_update_req->addHeader("Authorization", "Bearer " + token1);

  auto unauthorized_update_resp = client->sendRequest(unauthorized_update_req);
  CHECK(unauthorized_update_resp.second->getStatusCode() ==
        drogon::k403Forbidden);

  // Test 7: Subscribe to a post
  auto subscribe_req = drogon::HttpRequest::newHttpRequest();
  subscribe_req->setMethod(drogon::Post);
  subscribe_req->setPath("/api/v1/posts/" + std::to_string(product_post_id) +
                         "/subscribe");
  subscribe_req->addHeader("Authorization", "Bearer " + token1);

  auto subscribe_resp = client->sendRequest(subscribe_req);
  CHECK(subscribe_resp.second->getStatusCode() == drogon::k200OK);

  auto subscribe_resp_json = subscribe_resp.second->getJsonObject();
  CHECK((*subscribe_resp_json)["status"].asString() == "success");

  // Test 8: Get subscriptions
  auto get_subs_req = drogon::HttpRequest::newHttpRequest();
  get_subs_req->setMethod(drogon::Get);
  get_subs_req->setPath("/api/v1/posts/subscriptions");
  get_subs_req->addHeader("Authorization", "Bearer " + token1);

  auto get_subs_resp = client->sendRequest(get_subs_req);
  CHECK(get_subs_resp.second->getStatusCode() == drogon::k200OK);

  auto get_subs_resp_json = get_subs_resp.second->getJsonObject();
  CHECK(get_subs_resp_json->isArray());

  bool found_subscription = false;
  for (const auto& post : *get_subs_resp_json) {
    if (post["id"].asInt() == product_post_id) {
      found_subscription = true;
      CHECK(post["is_subscribed"].asBool() == true);
      break;
    }
  }
  CHECK(found_subscription);
  // Test 9: Unsubscribe from a post
  auto unsubscribe_req = drogon::HttpRequest::newHttpRequest();
  unsubscribe_req->setMethod(drogon::Post);
  unsubscribe_req->setPath("/api/v1/posts/" + std::to_string(product_post_id) +
                           "/unsubscribe");
  unsubscribe_req->addHeader("Authorization", "Bearer " + token1);

  auto unsubscribe_resp = client->sendRequest(unsubscribe_req);
  CHECK(unsubscribe_resp.second->getStatusCode() == drogon::k200OK);

  auto unsubscribe_resp_json = unsubscribe_resp.second->getJsonObject();
  CHECK((*unsubscribe_resp_json)["status"].asString() == "success");

  // Verify unsubscription worked
  auto verify_unsub_req = drogon::HttpRequest::newHttpRequest();
  verify_unsub_req->setMethod(drogon::Get);
  verify_unsub_req->setPath("/api/v1/posts/" + std::to_string(product_post_id));
  verify_unsub_req->addHeader("Authorization", "Bearer " + token1);

  auto verify_unsub_resp = client->sendRequest(verify_unsub_req);
  CHECK(verify_unsub_resp.second->getStatusCode() == drogon::k200OK);

  auto verify_unsub_json = verify_unsub_resp.second->getJsonObject();
  CHECK((*verify_unsub_json)["is_subscribed"].asBool() == false);

  // Test 10: Get popular tags
  auto get_tags_req = drogon::HttpRequest::newHttpRequest();
  get_tags_req->setMethod(drogon::Get);
  get_tags_req->setPath("/api/v1/posts/tags");
  get_tags_req->addHeader("Authorization", "Bearer " + token1);

  auto get_tags_resp = client->sendRequest(get_tags_req);
  CHECK(get_tags_resp.second->getStatusCode() == drogon::k200OK);

  auto get_tags_resp_json = get_tags_resp.second->getJsonObject();
  CHECK(get_tags_resp_json->isArray());

  // We should find at least some of our test tags
  bool found_test_tag = false;
  bool found_product_tag = false;

  for (const auto& tag : *get_tags_resp_json) {
    if (tag["name"].asString() == "test") {
      found_test_tag = true;
    }
    if (tag["name"].asString() == "product") {
      found_product_tag = true;
    }
  }

  // At least one of our tags should be in the popular tags
  // CHECK(found_test_tag || found_product_tag);

  // Test 11: Filter posts by tag
  auto filter_by_tag_req = drogon::HttpRequest::newHttpRequest();
  filter_by_tag_req->setMethod(drogon::Get);
  filter_by_tag_req->setPath("/api/v1/posts/filter?tags=test");
  filter_by_tag_req->addHeader("Authorization", "Bearer " + token1);

  auto filter_by_tag_resp = client->sendRequest(filter_by_tag_req);
  CHECK(filter_by_tag_resp.second->getStatusCode() == drogon::k200OK);

  auto filter_by_tag_json = filter_by_tag_resp.second->getJsonObject();
  CHECK(filter_by_tag_json->isArray());

  bool found_test_post_in_filter = false;
  for (const auto& post : *filter_by_tag_json) {
    if (post["id"].asInt() == regular_post_id) {
      found_test_post_in_filter = true;
      break;
    }
  }
  CHECK(found_test_post_in_filter);

  // Test 12: Filter posts by product request
  auto filter_by_product_req = drogon::HttpRequest::newHttpRequest();
  filter_by_product_req->setMethod(drogon::Get);
  filter_by_product_req->setPath(
      "/api/v1/posts/filter?is_product_request=true");
  filter_by_product_req->addHeader("Authorization", "Bearer " + token1);

  auto filter_by_product_resp = client->sendRequest(filter_by_product_req);
  CHECK(filter_by_product_resp.second->getStatusCode() == drogon::k200OK);

  auto filter_by_product_json = filter_by_product_resp.second->getJsonObject();
  CHECK(filter_by_product_json->isArray());

  bool found_product_post_in_filter = false;
  for (const auto& post : *filter_by_product_json) {
    if (post["id"].asInt() == product_post_id) {
      found_product_post_in_filter = true;
      CHECK(post["is_product_request"].asBool() == true);
      break;
    }
  }
  CHECK(found_product_post_in_filter);

  // Test 13: Filter posts by location
  auto filter_by_location_req = drogon::HttpRequest::newHttpRequest();
  filter_by_location_req->setMethod(drogon::Get);
  filter_by_location_req->setPath("/api/v1/posts/filter?location=Test");
  filter_by_location_req->addHeader("Authorization", "Bearer " + token1);

  auto filter_by_location_resp = client->sendRequest(filter_by_location_req);
  CHECK(filter_by_location_resp.second->getStatusCode() == drogon::k200OK);

  auto filter_by_location_json =
      filter_by_location_resp.second->getJsonObject();
  CHECK(filter_by_location_json->isArray());

  bool found_location_post_in_filter = false;
  for (const auto& post : *filter_by_location_json) {
    if (post["id"].asInt() == regular_post_id) {
      found_location_post_in_filter = true;
      CHECK(post["location"].asString() == "Test Location");
      break;
    }
  }
  CHECK(found_location_post_in_filter);

  // Test 14: Filter posts by status
  auto filter_by_status_req = drogon::HttpRequest::newHttpRequest();
  filter_by_status_req->setMethod(drogon::Get);
  filter_by_status_req->setPath("/api/v1/posts/filter?status=open");
  filter_by_status_req->addHeader("Authorization", "Bearer " + token1);

  auto filter_by_status_resp = client->sendRequest(filter_by_status_req);
  CHECK(filter_by_status_resp.second->getStatusCode() == drogon::k200OK);

  auto filter_by_status_json = filter_by_status_resp.second->getJsonObject();
  CHECK(filter_by_status_json->isArray());

  bool found_open_status_post = false;
  for (const auto& post : *filter_by_status_json) {
    if (post["id"].asInt() == product_post_id) {
      found_open_status_post = true;
      CHECK(post["request_status"].asString() == "open");
      break;
    }
  }
  CHECK(found_open_status_post);

  // Test 15: Update product request status
  Json::Value update_status_json;
  update_status_json["request_status"] = "in_progress";

  auto update_status_req =
      drogon::HttpRequest::newHttpJsonRequest(update_status_json);
  update_status_req->setMethod(drogon::Put);
  update_status_req->setPath("/api/v1/posts/" +
                             std::to_string(product_post_id));
  update_status_req->addHeader("Authorization", "Bearer " + token2);

  auto update_status_resp = client->sendRequest(update_status_req);
  CHECK(update_status_resp.second->getStatusCode() == drogon::k200OK);

  auto update_status_resp_json = update_status_resp.second->getJsonObject();
  CHECK((*update_status_resp_json)["status"].asString() == "success");

  // Verify status was updated
  auto verify_status_req = drogon::HttpRequest::newHttpRequest();
  verify_status_req->setMethod(drogon::Get);
  verify_status_req->setPath("/api/v1/posts/" +
                             std::to_string(product_post_id));
  verify_status_req->addHeader("Authorization", "Bearer " + token2);

  auto verify_status_resp = client->sendRequest(verify_status_req);
  CHECK(verify_status_resp.second->getStatusCode() == drogon::k200OK);

  auto verify_status_json = verify_status_resp.second->getJsonObject();
  CHECK((*verify_status_json)["request_status"].asString() == "in_progress");

  // Test 16: Test pagination
  // Create multiple posts to ensure we have enough for pagination
  std::vector<int> pagination_post_ids;
  for (int i = 0; i < 15; i++) {
    Json::Value pagination_post_json;
    pagination_post_json["content"] =
        "Pagination test post " + std::to_string(i);
    pagination_post_json["tags"] = Json::Value(Json::arrayValue);
    pagination_post_json["tags"].append("pagination");

    auto pagination_post_req =
        drogon::HttpRequest::newHttpJsonRequest(pagination_post_json);
    pagination_post_req->setMethod(drogon::Post);
    pagination_post_req->setPath("/api/v1/posts");
    pagination_post_req->addHeader("Authorization", "Bearer " + token1);

    auto pagination_post_resp = client->sendRequest(pagination_post_req);
    CHECK(pagination_post_resp.second->getStatusCode() == drogon::k200OK);

    auto pagination_post_resp_json =
        pagination_post_resp.second->getJsonObject();
    pagination_post_ids.push_back(
        (*pagination_post_resp_json)["post_id"].asInt());
  }

  // Get first page of posts (default page size should be 10)
  auto page1_req = drogon::HttpRequest::newHttpRequest();
  page1_req->setMethod(drogon::Get);
  page1_req->setPath("/api/v1/posts?page=1");
  page1_req->addHeader("Authorization", "Bearer " + token1);

  auto page1_resp = client->sendRequest(page1_req);
  CHECK(page1_resp.second->getStatusCode() == drogon::k200OK);

  auto page1_json = page1_resp.second->getJsonObject();
  CHECK(page1_json->isArray());

  // Should have at most 10 posts on first page
  CHECK(page1_json->size() <= 10);

  // Get second page of posts
  auto page2_req = drogon::HttpRequest::newHttpRequest();
  page2_req->setMethod(drogon::Get);
  page2_req->setPath("/api/v1/posts?page=2");
  page2_req->addHeader("Authorization", "Bearer " + token1);

  auto page2_resp = client->sendRequest(page2_req);
  CHECK(page2_resp.second->getStatusCode() == drogon::k200OK);

  auto page2_json = page2_resp.second->getJsonObject();
  CHECK(page2_json->isArray());

  // Verify we have different posts on different pages
  bool pages_are_different = true;
  if (page1_json->size() > 0 && page2_json->size() > 0) {
    // Compare first post ID from each page
    int first_post_id_page1 = (*page1_json)[0]["id"].asInt();
    int first_post_id_page2 = (*page2_json)[0]["id"].asInt();
    pages_are_different = (first_post_id_page1 != first_post_id_page2);
  }
  CHECK(pages_are_different);

  // Test 17: Test invalid post ID
  auto invalid_post_req = drogon::HttpRequest::newHttpRequest();
  invalid_post_req->setMethod(drogon::Get);
  invalid_post_req->setPath("/api/v1/posts/invalid");
  invalid_post_req->addHeader("Authorization", "Bearer " + token1);

  auto invalid_post_resp = client->sendRequest(invalid_post_req);
  CHECK(invalid_post_resp.second->getStatusCode() == drogon::k400BadRequest);

  // Test 18: Test non-existent post ID
  auto nonexistent_post_req = drogon::HttpRequest::newHttpRequest();
  nonexistent_post_req->setMethod(drogon::Get);
  nonexistent_post_req->setPath("/api/v1/posts/999999");
  nonexistent_post_req->addHeader("Authorization", "Bearer " + token1);

  auto nonexistent_post_resp = client->sendRequest(nonexistent_post_req);
  CHECK(nonexistent_post_resp.second->getStatusCode() == drogon::k404NotFound);

  // Test 19: Test creating post with empty content
  Json::Value empty_post_json;
  empty_post_json["content"] = "";

  auto empty_post_req =
      drogon::HttpRequest::newHttpJsonRequest(empty_post_json);
  empty_post_req->setMethod(drogon::Post);
  empty_post_req->setPath("/api/v1/posts");
  empty_post_req->addHeader("Authorization", "Bearer " + token1);

  auto empty_post_resp = client->sendRequest(empty_post_req);
  CHECK(empty_post_resp.second->getStatusCode() != drogon::k200OK);

  // Test 20: Test unauthorized access (no token)
  auto no_auth_req = drogon::HttpRequest::newHttpRequest();
  no_auth_req->setMethod(drogon::Get);
  no_auth_req->setPath("/api/v1/posts");

  auto no_auth_resp = client->sendRequest(no_auth_req);
  CHECK(no_auth_resp.second->getStatusCode() == drogon::k401Unauthorized);

  // Test 21: Test with invalid token
  auto invalid_token_req = drogon::HttpRequest::newHttpRequest();
  invalid_token_req->setMethod(drogon::Get);
  invalid_token_req->setPath("/api/v1/posts");
  invalid_token_req->addHeader("Authorization", "Bearer invalid_token_here");

  auto invalid_token_resp = client->sendRequest(invalid_token_req);
  CHECK(invalid_token_resp.second->getStatusCode() == drogon::k401Unauthorized);

  // Test 22: Test combined filters
  auto combined_filter_req = drogon::HttpRequest::newHttpRequest();
  combined_filter_req->setMethod(drogon::Get);
  combined_filter_req->setPath(
      "/api/v1/posts/"
      "filter?tags=product&is_product_request=true&status=in_progress");
  combined_filter_req->addHeader("Authorization", "Bearer " + token1);

  auto combined_filter_resp = client->sendRequest(combined_filter_req);
  CHECK(combined_filter_resp.second->getStatusCode() == drogon::k200OK);

  auto combined_filter_json = combined_filter_resp.second->getJsonObject();
  CHECK(combined_filter_json->isArray());

  bool found_combined_filter_post = false;
  for (const auto& post : *combined_filter_json) {
    if (post["id"].asInt() == product_post_id) {
      found_combined_filter_post = true;
      CHECK(post["is_product_request"].asBool() == true);
      CHECK(post["request_status"].asString() == "in_progress");

      bool has_product_tag = false;
      for (const auto& tag : post["tags"]) {
        if (tag.asString() == "product") {
          has_product_tag = true;
          break;
        }
      }
      CHECK(has_product_tag);
      break;
    }
  }
  CHECK(found_combined_filter_post);

  // Test 23: Test multiple tag filter
  auto multi_tag_filter_req = drogon::HttpRequest::newHttpRequest();
  multi_tag_filter_req->setMethod(drogon::Get);
  multi_tag_filter_req->setPath("/api/v1/posts/filter?tags=product,request");
  multi_tag_filter_req->addHeader("Authorization", "Bearer " + token1);

  auto multi_tag_filter_resp = client->sendRequest(multi_tag_filter_req);
  CHECK(multi_tag_filter_resp.second->getStatusCode() == drogon::k200OK);

  auto multi_tag_filter_json = multi_tag_filter_resp.second->getJsonObject();
  CHECK(multi_tag_filter_json->isArray());

  bool found_multi_tag_post = false;
  for (const auto& post : *multi_tag_filter_json) {
    if (post["id"].asInt() == product_post_id) {
      found_multi_tag_post = true;

      int tag_match_count = 0;
      for (const auto& tag : post["tags"]) {
        if (tag.asString() == "product" || tag.asString() == "request") {
          tag_match_count++;
        }
      }
      // Should match at least one of the tags
      CHECK(tag_match_count > 0);
      break;
    }
  }
  CHECK(found_multi_tag_post);

  // Test 24: Test updating a post with invalid status
  Json::Value invalid_status_json;
  invalid_status_json["request_status"] = "invalid_status";

  auto invalid_status_req =
      drogon::HttpRequest::newHttpJsonRequest(invalid_status_json);
  invalid_status_req->setMethod(drogon::Put);
  invalid_status_req->setPath("/api/v1/posts/" +
                              std::to_string(product_post_id));
  invalid_status_req->addHeader("Authorization", "Bearer " + token2);

  auto invalid_status_resp = client->sendRequest(invalid_status_req);
  // This might return 400 Bad Request or might still succeed but normalize the
  // status Either way, we'll check that the status wasn't set to the invalid
  // value

  auto verify_invalid_status_req = drogon::HttpRequest::newHttpRequest();
  verify_invalid_status_req->setMethod(drogon::Get);
  verify_invalid_status_req->setPath("/api/v1/posts/" +
                                     std::to_string(product_post_id));
  verify_invalid_status_req->addHeader("Authorization", "Bearer " + token2);

  auto verify_invalid_status_resp =
      client->sendRequest(verify_invalid_status_req);
  CHECK(verify_invalid_status_resp.second->getStatusCode() == drogon::k200OK);

  auto verify_invalid_status_json =
      verify_invalid_status_resp.second->getJsonObject();
  CHECK((*verify_invalid_status_json)["request_status"].asString() !=
        "invalid_status");

  // Test 25: Test subscription count
  // First, subscribe to the post again
  auto resubscribe_req = drogon::HttpRequest::newHttpRequest();
  resubscribe_req->setMethod(drogon::Post);
  resubscribe_req->setPath("/api/v1/posts/" + std::to_string(product_post_id) +
                           "/subscribe");
  resubscribe_req->addHeader("Authorization", "Bearer " + token1);

  auto resubscribe_resp = client->sendRequest(resubscribe_req);
  CHECK(resubscribe_resp.second->getStatusCode() == drogon::k200OK);

  // Now check the subscription count
  auto check_sub_count_req = drogon::HttpRequest::newHttpRequest();
  check_sub_count_req->setMethod(drogon::Get);
  check_sub_count_req->setPath("/api/v1/posts/" +
                               std::to_string(product_post_id));
  check_sub_count_req->addHeader("Authorization", "Bearer " + token1);

  auto check_sub_count_resp = client->sendRequest(check_sub_count_req);
  CHECK(check_sub_count_resp.second->getStatusCode() == drogon::k200OK);

  auto check_sub_count_json = check_sub_count_resp.second->getJsonObject();
  CHECK((*check_sub_count_json)["subscription_count"].asInt() >= 1);
  CHECK((*check_sub_count_json)["is_subscribed"].asBool() == true);

  // Test 26: Test filter with no results
  auto no_results_filter_req = drogon::HttpRequest::newHttpRequest();
  no_results_filter_req->setMethod(drogon::Get);
  no_results_filter_req->setPath(
      "/api/v1/posts/filter?tags=nonexistenttag123456789");
  no_results_filter_req->addHeader("Authorization", "Bearer " + token1);

  auto no_results_filter_resp = client->sendRequest(no_results_filter_req);
  CHECK(no_results_filter_resp.second->getStatusCode() == drogon::k200OK);

  auto no_results_filter_json = no_results_filter_resp.second->getJsonObject();
  CHECK(no_results_filter_json->isArray());
  CHECK(no_results_filter_json->size() == 0);

  // Test 27: Test creating a post with very long content
  std::string long_content(5000, 'a');  // 5000 character string
  Json::Value long_post_json;
  long_post_json["content"] = long_content;

  auto long_post_req = drogon::HttpRequest::newHttpJsonRequest(long_post_json);
  long_post_req->setMethod(drogon::Post);
  long_post_req->setPath("/api/v1/posts");
  long_post_req->addHeader("Authorization", "Bearer " + token1);

  auto long_post_resp = client->sendRequest(long_post_req);
  // This might succeed or fail depending on database column limits
  // If it succeeds, verify the content was stored correctly
  if (long_post_resp.second->getStatusCode() == drogon::k200OK) {
    auto long_post_resp_json = long_post_resp.second->getJsonObject();
    int long_post_id = (*long_post_resp_json)["post_id"].asInt();

    auto get_long_post_req = drogon::HttpRequest::newHttpRequest();
    get_long_post_req->setMethod(drogon::Get);
    get_long_post_req->setPath("/api/v1/posts/" + std::to_string(long_post_id));
    get_long_post_req->addHeader("Authorization", "Bearer " + token1);

    auto get_long_post_resp = client->sendRequest(get_long_post_req);
    CHECK(get_long_post_resp.second->getStatusCode() == drogon::k200OK);

    auto get_long_post_json = get_long_post_resp.second->getJsonObject();
    CHECK((*get_long_post_json)["content"].asString().length() ==
          long_content.length());

    // Add to cleanup list
    pagination_post_ids.push_back(long_post_id);
  }

  // Test 28: Test creating a post with special characters in content
  Json::Value special_chars_json;
  special_chars_json["content"] =
      "Special characters: !@#$%^&*()_+{}|:<>?~`-=[]\\;',./";

  auto special_chars_req =
      drogon::HttpRequest::newHttpJsonRequest(special_chars_json);
  special_chars_req->setMethod(drogon::Post);
  special_chars_req->setPath("/api/v1/posts");
  special_chars_req->addHeader("Authorization", "Bearer " + token1);

  auto special_chars_resp = client->sendRequest(special_chars_req);
  CHECK(special_chars_resp.second->getStatusCode() == drogon::k200OK);

  auto special_chars_resp_json = special_chars_resp.second->getJsonObject();
  int special_chars_post_id = (*special_chars_resp_json)["post_id"].asInt();

  auto get_special_chars_req = drogon::HttpRequest::newHttpRequest();
  get_special_chars_req->setMethod(drogon::Get);
  get_special_chars_req->setPath("/api/v1/posts/" +
                                 std::to_string(special_chars_post_id));
  get_special_chars_req->addHeader("Authorization", "Bearer " + token1);

  auto get_special_chars_resp = client->sendRequest(get_special_chars_req);
  CHECK(get_special_chars_resp.second->getStatusCode() == drogon::k200OK);

  auto get_special_chars_json = get_special_chars_resp.second->getJsonObject();
  CHECK((*get_special_chars_json)["content"].asString() ==
        "Special characters: !@#$%^&*()_+{}|:<>?~`-=[]\\;',./");

  // Add to cleanup list
  pagination_post_ids.push_back(special_chars_post_id);

  // Clean up test data in the correct order to avoid foreign key violations
  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM post_subscriptions WHERE user_id IN (SELECT id FROM users "
      "WHERE "
      "username = 'testcomm1' OR username = 'testcomm2')"));

  // Clean up all created posts
  for (int post_id : pagination_post_ids) {
    REQUIRE_NOTHROW(
        db_client->execSqlSync("DELETE FROM posts WHERE id = $1", post_id));
  }

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM posts WHERE id IN ($1, $2)",
                             regular_post_id, product_post_id));

  REQUIRE_NOTHROW(db_client->execSqlSync(
      "DELETE FROM user_sessions WHERE user_id IN (SELECT id FROM users WHERE "
      "username = 'testcomm1' OR username = 'testcomm2')"));

  REQUIRE_NOTHROW(
      db_client->execSqlSync("DELETE FROM users WHERE username = 'testcomm1' "
                             "OR username = 'testcomm2'"));
}
