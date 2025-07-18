// #include "../utilities/uuid_generator.hpp"
#include "community.hpp"

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

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include "../services/service_manager.hpp"
#include "../utilities/conversion.hpp"
#include "../utilities/time_manipulation.hpp"
#include "scenario_specific_utils.hpp"

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

// Helper function to convert string array to JSON array
Json::Value string_array_to_json(const std::string& array_str) {
  Json::Value json_array(Json::arrayValue);

  if (array_str.empty() || array_str == "{}" || array_str == "NULL") {
    return json_array;
  }

  // Parse PostgreSQL array format: {tag1,tag2,tag3}
  std::string content =
      array_str.substr(1, array_str.length() - 2);  // Remove { }

  size_t pos = 0;
  std::string token;
  while ((pos = content.find(',')) != std::string::npos) {
    token = content.substr(0, pos);
    json_array.append(token);
    content.erase(0, pos + 1);
  }
  if (!content.empty()) {
    json_array.append(content);
  }

  return json_array;
}

// Helper function to convert JSON array to PostgreSQL array string
std::string json_array_to_string(const Json::Value& json_array) {
  if (!json_array.isArray()) {
    return "{}";
  }

  std::string result = "{";
  for (Json::ArrayIndex i = 0; i < json_array.size(); ++i) {
    if (i > 0) {
      result += ",";
    }
    result += json_array[i].asString();
  }
  result += "}";

  return result;
}

Task<> Community::get_posts(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  // Get page parameter, default to 1
  int page = 1;
  auto page_param = req->getParameter("page");
  if (!page_param.empty()) {
    try {
      page = std::stoi(page_param);
      if (page < 1) page = 1;
    } catch (...) {
      // Invalid page parameter, use default
      page = 1;
    }
  }

  // Set page size
  const int page_size = 10;
  const int offset = (page - 1) * page_size;

  // Get current user ID for subscription status
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT p.*, u.username, "
        "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
        "subscription_count, "
        "EXISTS(SELECT 1 FROM post_subscriptions WHERE post_id = p.id AND "
        "user_id = $3) AS is_subscribed "
        "FROM posts p "
        "JOIN users u ON p.user_id = u.id "
        "ORDER BY p.created_at DESC LIMIT $1 OFFSET $2",
        std::to_string(page_size), std::to_string(offset), current_user_id);

    Json::Value posts;
    for (const auto& row : result) {
      Json::Value post;
      int post_id = row["id"].as<int>();
      post["id"] = post_id;
      post["user_id"] = row["user_id"].as<int>();
      post["username"] = row["username"].as<std::string>();
      post["content"] = row["content"].as<std::string>();
      post["created_at"] = row["created_at"].as<std::string>();

      post["tags"] = string_array_to_json(row["tags"].as<std::string>());
      post["location"] =
          row["location"].isNull() ? "" : row["location"].as<std::string>();
      post["is_product_request"] = row["is_product_request"].as<bool>();
      post["request_status"] = row["request_status"].as<std::string>();
      post["price_range"] = row["price_range"].isNull()
                                ? ""
                                : row["price_range"].as<std::string>();
      post["subscription_count"] = row["subscription_count"].as<int>();
      post["is_subscribed"] = row["is_subscribed"].as<bool>();

      post["media"] = co_await get_media_attachments("post", post_id);
      posts.append(post);
    }

    auto resp = HttpResponse::newHttpJsonResponse(posts);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

Task<> Community::create_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto json = req->getJsonObject();
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Post must have content
  if (!json || !(*json).isMember("content") ||
      (*json)["content"].asString().empty()) {
    Json::Value error;
    error["error"] = "Post has no content.";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  std::string content = (*json)["content"].asString();
  // Extract new fields
  bool is_product_request = json->isMember("is_product_request")
                                ? (*json)["is_product_request"].asBool()
                                : false;
  std::string request_status = is_product_request ? "open" : "";
  std::string location =
      json->isMember("location") ? (*json)["location"].asString() : "";
  std::string price_range =
      json->isMember("price_range") ? (*json)["price_range"].asString() : "";

  // Handle tags
  std::string tags_str = "{}";
  if (json->isMember("tags") && (*json)["tags"].isArray()) {
    tags_str = json_array_to_string((*json)["tags"]);
  }

  Json::Value media_array = json->isMember("media")
                                ? (*json)["media"]
                                : Json::Value(Json::arrayValue);

  if (media_array.size() > MAX_MEDIA_SIZE) {
    Json::Value error;
    error["error"] = "Maximum of 5 media items allowed per post";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();
  auto transaction = co_await db->newTransactionCoro();

  try {
    auto result = co_await transaction->execSqlCoro(
        "INSERT INTO posts (user_id, content, tags, location, "
        "is_product_request, request_status, price_range) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id, created_at",
        std::stoi(user_id), content, tags_str, location, is_product_request,
        request_status, price_range);

    if (result.size() < 1) {
      throw std::runtime_error("Initial post insert failed");
    }
    int post_id = result[0]["id"].as<int>();
    std::string created_at = result[0]["created_at"].as<std::string>();

    // insert media attachments
    std::size_t media_array_size = media_array.size();
    Json::Value processed_media =
        co_await process_media_attachments(std::move(media_array), transaction,
                                           std::stoi(user_id), "post", post_id);
    if (processed_media == Json::nullValue ||
        processed_media.size() < media_array_size) {
      LOG_ERROR << " Some Media info was not found";
      transaction->rollback();
      std::string error_string;
      for (const auto& media_item : processed_media) {
        error_string += media_item["file_name"].asString() + ", ";
      }
      Json::Value error;
      error["error"] = std::format(
          "Media info not found or processed, only the following media items "
          "were processed:\n{}",
          error_string);
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    // Notification
    // Auto-subscribe post owner to their post
    std::string post_id_str = std::to_string(post_id);
    std::string post_topic = create_topic("post", post_id_str);
    ServiceManager::get_instance().get_subscriber().subscribe(post_topic);
    ServiceManager::get_instance().get_connection_manager().subscribe(
        post_topic, user_id);
    store_user_subscription(user_id, post_topic);
    LOG_INFO << "User " << user_id
             << " subscribed to post topic: " << post_topic;

    // create post data
    Json::Value post_data_json;
    post_data_json["type"] = "post_created";
    post_data_json["id"] = post_id_str;
    post_data_json["message"] =
        is_product_request ? "New Product Request: " + content : content;
    post_data_json["modified_at"] = result[0]["created_at"].as<std::string>();

    Json::FastWriter writer;
    std::string post_data = writer.write(post_data_json);

    // Publish to tag subscribers
    if (json->isMember("tags") && (*json)["tags"].isArray()) {
      for (const auto& tag : (*json)["tags"]) {
        std::string tag_topic = tag.asString();
        ServiceManager::get_instance().get_publisher().publish(tag_topic,
                                                               post_data);
        LOG_INFO << "Published new post to tag channel: " << tag_topic;
      }
    }

    // Publish to location subscribers
    if (!location.empty()) {
      ServiceManager::get_instance().get_publisher().publish(location,
                                                             post_data);
      LOG_INFO << "Published new post to location channel: " << location;
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["post_id"] = post_id;
    ret["created_at"] = created_at;
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const std::exception& e) {
    transaction->rollback();
    LOG_ERROR << "Failed to create post" << e.what();
    Json::Value error;
    error["error"] = std::format("Failed to create post {}", e.what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  } catch (const DrogonDbException& e) {
    transaction->rollback();
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get a single post by ID
Task<> Community::get_post_by_id(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Malformed route
  if (!convert::string_to_int(id).has_value() ||
      convert::string_to_int(id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid ID";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  try {
    int post_id = std::stoi(id);
    auto result = co_await db->execSqlCoro(
        "SELECT p.*, u.username, "
        "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
        "subscription_count, "
        "EXISTS(SELECT 1 FROM post_subscriptions WHERE post_id = p.id AND "
        "user_id = $2) AS is_subscribed "
        "FROM posts p "
        "JOIN users u ON p.user_id = u.id "
        "WHERE p.id = $1",
        post_id, current_user_id);

    if (result.size() < 1) {
      Json::Value error;
      error["error"] = "Post not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
    }

    const auto& row = result[0];
    Json::Value post;
    post["id"] = post_id;
    post["user_id"] = row["user_id"].as<int>();
    post["username"] = row["username"].as<std::string>();
    post["content"] = row["content"].as<std::string>();
    post["created_at"] = row["created_at"].as<std::string>();

    post["tags"] = string_array_to_json(row["tags"].as<std::string>());
    post["location"] =
        row["location"].isNull() ? "" : row["location"].as<std::string>();
    post["is_product_request"] = row["is_product_request"].as<bool>();
    post["request_status"] = row["request_status"].as<std::string>();
    post["price_range"] =
        row["price_range"].isNull() ? "" : row["price_range"].as<std::string>();
    post["subscription_count"] = row["subscription_count"].as<int>();
    post["is_subscribed"] = row["is_subscribed"].as<bool>();

    post["media"] = co_await get_media_attachments("post", post_id);

    auto resp = HttpResponse::newHttpJsonResponse(post);
    callback(resp);

  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Reference: This update uses an initial SELECT query
// // Update a post
// Task<> Community::update_post(
//   HttpRequestPtr req,
//   std::function<void(const HttpResponsePtr&)> callback,
//   std::string id) {
// auto json = req->getJsonObject();
// std::string current_user_id =
//     req->getAttributes()->get<std::string>("current_user_id");

// auto db = app().getDbClient();

// try {
//   // First check if the user is the owner of the post
//   auto result = co_await db->execSqlCoro(
//       "SELECT user_id FROM posts WHERE id = $1", std::stoi(id));

//   if (result.size() == 0) {
//     Json::Value error;
//     error["error"] = "Post not found";
//     auto resp = HttpResponse::newHttpJsonResponse(error);
//     resp->setStatusCode(k404NotFound);
//     callback(resp);
//     co_return;
//   }

//   int post_user_id = result[0]["user_id"].as<int>();
//   if (post_user_id != std::stoi(current_user_id)) {
//     Json::Value error;
//     error["error"] = "You don't have permission to update this post";
//     auto resp = HttpResponse::newHttpJsonResponse(error);
//     resp->setStatusCode(k403Forbidden);
//     callback(resp);
//     co_return;
//   }

//   // First, get the current post data to use as defaults
//   auto currentPost = co_await db->execSqlCoro(
//       "SELECT content, request_status, location, price_range, tags FROM posts
//       WHERE id = $1", std::stoi(id));

//   if (currentPost.size() == 0) {
//     Json::Value error;
//     error["error"] = "Post not found";
//     auto resp = HttpResponse::newHttpJsonResponse(error);
//     resp->setStatusCode(k404NotFound);
//     callback(resp);
//     co_return;
//   }

//   // Extract current values to use as defaults
//   std::string currentContent = currentPost[0]["content"].as<std::string>();
//   std::string currentRequestStatus =
//   currentPost[0]["request_status"].as<std::string>(); std::string
//   currentLocation = currentPost[0]["location"].isNull() ?
//                                "" :
//                                currentPost[0]["location"].as<std::string>();
//   std::string currentPriceRange = currentPost[0]["price_range"].isNull() ?
//                                  "" :
//                                  currentPost[0]["price_range"].as<std::string>();
//   std::string currentTags = currentPost[0]["tags"].as<std::string>();

//   // Prepare update fields with values from JSON or current values as
//   defaults std::string content = json->isMember("content") ?
//                        (*json)["content"].asString() : currentContent;

//   std::string request_status = json->isMember("request_status") ?
//                               (*json)["request_status"].asString() :
//                               currentRequestStatus;

//   std::string location = json->isMember("location") ?
//                         (*json)["location"].asString() : currentLocation;

//   std::string price_range = json->isMember("price_range") ?
//                            (*json)["price_range"].asString() :
//                            currentPriceRange;

//   // Handle tags
//   std::string tags_str;
//   if (json->isMember("tags") && (*json)["tags"].isArray()) {
//     tags_str = json_array_to_string((*json)["tags"]);
//   } else {
//     tags_str = currentTags;
//   }

//   // Check if any fields were actually changed
//   bool hasChanges = false;
//   if (content != currentContent ||
//       request_status != currentRequestStatus ||
//       location != currentLocation ||
//       price_range != currentPriceRange ||
//       tags_str != currentTags) {
//     hasChanges = true;
//   }

//   if (!hasChanges) {
//     Json::Value ret;
//     ret["status"] = "success";
//     ret["message"] = "No fields to update";
//     auto resp = HttpResponse::newHttpJsonResponse(ret);
//     callback(resp);
//     co_return;
//   }

//   // Use a single update query with all fields
//   std::string update_query =
//       "UPDATE posts SET "
//       "content = $2, "
//       "request_status = $3, "
//       "location = $4, "
//       "price_range = $5, "
//       "tags = $6 "
//       "WHERE id = $1 "
//       "RETURNING id";

//   // Execute the query with all parameters
//   auto updateResult = co_await db->execSqlCoro(
//       update_query,
//       std::stoi(id),
//       content,
//       request_status,
//       location,
//       price_range,
//       tags_str);

//   if (updateResult.size() > 0) {
//     Json::Value ret;
//     ret["status"] = "success";
//     ret["message"] = "Post updated successfully";
//     auto resp = HttpResponse::newHttpJsonResponse(ret);
//     callback(resp);
//   } else {
//     Json::Value error;
//     error["error"] = "Failed to update post";
//     auto resp = HttpResponse::newHttpJsonResponse(error);
//     resp->setStatusCode(k500InternalServerError);
//     callback(resp);
//   }
// } catch (const DrogonDbException& e) {
//   LOG_ERROR << "Database error: " << e.base().what();
//   Json::Value error;
//   error["error"] = "Database error";
//   auto resp = HttpResponse::newHttpJsonResponse(error);
//   resp->setStatusCode(k500InternalServerError);
//   callback(resp);
// }

// co_return;
// }

// Update a post
// Current behaviour is to overwrite data with any newly provide data.
// Media files are overwritten with new files.
Task<> Community::update_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  auto json = req->getJsonObject();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Malformed route
  if (!convert::string_to_int(id).has_value() ||
      convert::string_to_int(id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid post id";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  // Validation: content type
  if (!json || !(*json).isMember("content") || !(*json)["content"].isString()) {
    Json::Value error;
    error["error"] = "conent must be a string if provided";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  Json::Value media_array = json->isMember("media")
                                ? (*json)["media"]
                                : Json::Value(Json::arrayValue);

  if (media_array.size() > MAX_MEDIA_SIZE) {
    Json::Value error;
    error["error"] = "Maximum of 5 media items allowed per post";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // First check if the user is the owner of the post
    auto check_result = co_await db->execSqlCoro(
        "SELECT user_id FROM posts WHERE id = $1", std::stoi(id));

    if (check_result.size() < 1) {
      Json::Value error;
      error["error"] = "Post not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int post_user_id = check_result[0]["user_id"].as<int>();
    if (post_user_id != std::stoi(current_user_id)) {
      Json::Value error;
      error["error"] = "You don't have permission to update this post";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Check if any fields are provided for update
    bool has_content = json->isMember("content");
    bool has_request_status = json->isMember("request_status");
    bool has_location = json->isMember("location");
    bool has_price_range = json->isMember("price_range");
    bool has_tags = json->isMember("tags") && (*json)["tags"].isArray();
    bool has_media = media_array.size() > 0;

    if (!has_content && !has_request_status && !has_location &&
        !has_price_range && !has_tags) {
      Json::Value ret;
      ret["status"] = "success";
      ret["message"] = "No fields to update";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
      co_return;
    }

    // Prepare values for the update
    std::string content = has_content ? (*json)["content"].asString() : "";

    // Disabled since we shouldn't update request status
    // std::string request_status =
    //     has_request_status ? (*json)["request_status"].asString() : "";

    // Disabled since we shouldn't update request status
    // // Validation: correct statuses include: open, cancelled, fulfilled,
    // // in_progress,
    // if (request_status != "open" && request_status != "in_progress" &&
    //     request_status != "cancelled" &&
    //     (request_status != "" && has_request_status) &&
    //     request_status != "fulfilled") {
    //   Json::Value error;
    //   error["error"] = "Invalid request status";
    //   auto resp = HttpResponse::newHttpJsonResponse(error);
    //   resp->setStatusCode(k400BadRequest);
    //   callback(resp);
    //   co_return;
    // }

    std::string location = has_location ? (*json)["location"].asString() : "";
    std::string price_range =
        has_price_range ? (*json)["price_range"].asString() : "";
    std::string tags_str =
        has_tags ? json_array_to_string((*json)["tags"]) : "{}";

    // Build the update query using CASE expressions
    std::string update_query =
        "UPDATE posts SET "
        "content = CASE WHEN $2 THEN $3 ELSE content END, "
        // "request_status = CASE WHEN $4 THEN $5 ELSE request_status END, "
        "location = CASE WHEN $4 THEN $5 ELSE location END, "
        "price_range = CASE WHEN $6 THEN $7 ELSE price_range END, "
        "tags = CASE WHEN $8 THEN $9 ELSE tags END "
        "WHERE id = $1 "
        "RETURNING id";

    auto transaction = co_await db->newTransactionCoro();
    try {
      int post_id = std::stoi(id);
      // Execute the query with all parameters
      auto update_result = co_await transaction->execSqlCoro(
          update_query, post_id,
          has_content,  // $2: has content flag
          content,      // $3: content value
          // has_request_status,  // $4: has request_status flag
          // request_status,      // $5: request_status value
          has_location,     // $4: has location flag
          location,         // $5: location value
          has_price_range,  // $6: has price_range flag
          price_range,      // $7: price_range value
          has_tags,         // $8: has tags flag
          tags_str          // $9: tags value
      );

      if (update_result.size() < 1) {
        throw std::runtime_error("Initial post update failed");
      }

      std::size_t media_array_size = media_array.size();
      Json::Value processed_media = co_await process_media_attachments(
          std::move(media_array), transaction, std::stoi(current_user_id),
          "post", post_id);
      if (processed_media == Json::nullValue ||
          processed_media.size() < media_array_size) {
        LOG_ERROR << " Some Media info was not found";
        transaction->rollback();
        std::string error_string;
        for (const auto& media_item : processed_media) {
          error_string += media_item["file_name"].asString() + ", ";
        }
        Json::Value error;
        error["error"] = std::format(
            "Media info not found or processed, only the following media items "
            "were processed:\n{}",
            error_string);
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        co_return;
      }

      // notification:
      std::string post_topic = create_topic("post", id);
      Json::Value post_data_json;
      post_data_json["type"] = "post_updated";
      post_data_json["id"] = id;
      post_data_json["message"] = "New update on post";
      post_data_json["modified_at"] = get_current_utc_timestamp();

      Json::FastWriter writer;
      std::string post_data = writer.write(post_data_json);

      // Publish to post (owner and post subscribers)
      ServiceManager::get_instance().get_publisher().publish(post_topic,
                                                             post_data);
      LOG_INFO << "Updated post: " << post_topic;

      Json::Value ret;
      ret["status"] = "success";
      ret["message"] = "Post updated successfully";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } catch (const std::exception& e) {
      // Rollback the transaction on error
      transaction->rollback();
      LOG_ERROR << "Error updating post: " << e.what();
      Json::Value error;
      error["error"] = std::format("Failed to update post: {}", e.what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Filter posts by tags, location, and status
Task<> Community::filter_posts(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Get filter parameters
  auto tags_param = req->getParameter("tags");
  auto location_param = req->getParameter("location");
  auto status_param = req->getParameter("status");
  auto is_product_request_param = req->getParameter("is_product_request");

  // Get page parameter, default to 1
  int page = 1;
  auto page_param = req->getParameter("page");
  if (!page_param.empty()) {
    try {
      page = std::stoi(page_param);
      if (page < 1) page = 1;
    } catch (...) {
      // Invalid page parameter, use default
      page = 1;
    }
  }

  // Set page size
  const int page_size = 10;
  const int offset = (page - 1) * page_size;

  // Parse tags if provided
  std::vector<std::string> tags;
  if (!tags_param.empty()) {
    std::string tags_str = tags_param;
    size_t pos = 0;
    std::string token;
    while ((pos = tags_str.find(',')) != std::string::npos) {
      token = tags_str.substr(0, pos);
      if (!token.empty()) {
        tags.push_back(token);
      }
      tags_str.erase(0, pos + 1);
    }
    if (!tags_str.empty()) {
      tags.push_back(tags_str);
    }
  }

  // Build a query with all possible parameters and use default values
  // For tags, we'll modify the query to handle them as a union (OR)
  std::string query =
      "SELECT p.*, u.username, "
      "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
      "subscription_count, "
      "EXISTS(SELECT 1 FROM post_subscriptions WHERE post_id = p.id AND "
      "user_id = $1) AS is_subscribed "
      "FROM posts p "
      "JOIN users u ON p.user_id = u.id "
      "WHERE 1=1 ";

  // Add tag filter as a union (OR) condition if tags are provided
  if (!tags.empty()) {
    query += "AND (";
    for (size_t i = 0; i < tags.size(); ++i) {
      if (i > 0) {
        query += " OR ";
      }
      // Escape single quotes in tag names to prevent SQL injection
      std::string escapedTag = tags[i];
      size_t pos = 0;
      while ((pos = escapedTag.find('\'', pos)) != std::string::npos) {
        escapedTag.replace(pos, 1, "''");
        pos += 2;
      }
      query += "'" + escapedTag + "' = ANY(p.tags)";
    }
    query += ") ";
  }

  // Add other filters with default values
  if (!location_param.empty()) {
    query += "AND p.location ILIKE '%" + location_param + "%' ";
  }

  if (!status_param.empty()) {
    query += "AND p.request_status = '" + status_param + "' ";
  }

  if (!is_product_request_param.empty()) {
    bool is_product_request =
        (is_product_request_param == "true" || is_product_request_param == "1");
    query += "AND p.is_product_request = " +
             std::string(is_product_request ? "TRUE" : "FALSE") + " ";
  }

  // Add pagination
  query += "ORDER BY p.created_at DESC LIMIT $2 OFFSET $3";

  LOG_DEBUG << "Filter query: " << query;
  LOG_DEBUG << "Tags: " << tags_param;

  try {
    // Execute the query with the fixed parameters
    auto result =
        co_await db->execSqlCoro(query,
                                 current_user_id,            // $1: user_id
                                 std::to_string(page_size),  // $2: limit
                                 std::to_string(offset)      // $3: offset
        );

    Json::Value posts{Json::arrayValue};
    for (const auto& row : result) {
      Json::Value post;
      int post_id = row["id"].as<int>();
      post["id"] = post_id;
      post["user_id"] = row["user_id"].as<int>();
      post["username"] = row["username"].as<std::string>();
      post["content"] = row["content"].as<std::string>();
      post["created_at"] = row["created_at"].as<std::string>();

      // New fields
      post["tags"] = string_array_to_json(row["tags"].as<std::string>());
      post["location"] =
          row["location"].isNull() ? "" : row["location"].as<std::string>();
      post["is_product_request"] = row["is_product_request"].as<bool>();
      post["request_status"] = row["request_status"].as<std::string>();
      post["price_range"] = row["price_range"].isNull()
                                ? ""
                                : row["price_range"].as<std::string>();
      post["subscription_count"] = row["subscription_count"].as<int>();
      post["is_subscribed"] = row["is_subscribed"].as<bool>();

      post["media"] = co_await get_media_attachments("post", post_id);
      posts.append(post);
    }
    auto resp = HttpResponse::newHttpJsonResponse(posts);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Subscribe to a post
Task<> Community::subscribe_to_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "INSERT INTO post_subscriptions (user_id, post_id) "
        "VALUES ($1, $2) "
        "ON CONFLICT (user_id, post_id) DO NOTHING "
        "RETURNING id",
        std::stoi(current_user_id), std::stoi(id));

    std::string post_id = id;
    std::string post_topic = create_topic("post", post_id);
    ServiceManager::get_instance().get_subscriber().subscribe(post_topic);
    ServiceManager::get_instance().get_connection_manager().subscribe(
        post_topic, current_user_id);
    store_user_subscription(current_user_id, post_topic);

    Json::Value ret;
    if (result.size() > 0) {
      ret["status"] = "success";
      ret["message"] = "Subscribed to post";
    } else {
      ret["status"] = "success";
      ret["message"] = "Already subscribed to post";
    }
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Unsubscribe from a post
Task<> Community::unsubscribe_from_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "DELETE FROM post_subscriptions "
        "WHERE user_id = $1 AND post_id = $2 "
        "RETURNING id",
        std::stoi(current_user_id), std::stoi(id));

    std::string post_topic = create_topic("post", id);
    ServiceManager::get_instance()
        .get_connection_manager()
        .unsubscribe_user_from_topic(current_user_id, post_topic);
    remove_user_subscription(current_user_id, post_topic);

    Json::Value ret;
    if (result.size() > 0) {
      ret["status"] = "success";
      ret["message"] = "Unsubscribed from post";
    } else {
      ret["status"] = "success";
      ret["message"] = "Not subscribed to post";
    }
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

Task<> Community::subscribe_to_entity(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string name) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  try {
    ServiceManager::get_instance().get_subscriber().subscribe(name);
    ServiceManager::get_instance().get_connection_manager().subscribe(
        name, current_user_id);
    store_user_subscription(current_user_id, name);

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Subscription successful";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}
Task<> Community::unsubscribe_from_entity(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string name) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  try {
    ServiceManager::get_instance()
        .get_connection_manager()
        .unsubscribe_user_from_topic(current_user_id, name);
    remove_user_subscription(current_user_id, name);

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Unsubscribed from entity";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get user's subscriptions
Task<> Community::get_subscriptions(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT p.*, u.username, "
        "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
        "subscription_count, "
        "TRUE AS is_subscribed "
        "FROM posts p "
        "JOIN users u ON p.user_id = u.id "
        "JOIN post_subscriptions ps ON p.id = ps.post_id "
        "WHERE ps.user_id = $1 "
        "ORDER BY p.created_at DESC",
        std::stoi(current_user_id));

    Json::Value posts;
    for (const auto& row : result) {
      Json::Value post;
      post["id"] = row["id"].as<int>();
      post["user_id"] = row["user_id"].as<int>();
      post["username"] = row["username"].as<std::string>();
      post["content"] = row["content"].as<std::string>();
      post["created_at"] = row["created_at"].as<std::string>();

      // New fields
      post["tags"] = string_array_to_json(row["tags"].as<std::string>());
      post["location"] =
          row["location"].isNull() ? "" : row["location"].as<std::string>();
      post["is_product_request"] = row["is_product_request"].as<bool>();
      post["request_status"] = row["request_status"].as<std::string>();
      post["price_range"] = row["price_range"].isNull()
                                ? ""
                                : row["price_range"].as<std::string>();
      post["subscription_count"] = row["subscription_count"].as<int>();
      post["is_subscribed"] = row["is_subscribed"].as<bool>();

      posts.append(post);
    }

    auto resp = HttpResponse::newHttpJsonResponse(posts);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get popular tags
Task<> Community::get_popular_tags(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT DISTINCT unnest(tags) as tag, "
        "COUNT(*) as count "
        "FROM posts "
        "GROUP BY tag "
        "ORDER BY count DESC "
        "LIMIT 20");

    Json::Value tags(Json::arrayValue);
    for (const auto& row : result) {
      Json::Value tag;
      tag["name"] = row["tag"].as<std::string>();
      tag["count"] = row["count"].as<int>();
      tags.append(tag);
    }

    auto resp = HttpResponse::newHttpJsonResponse(tags);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}
