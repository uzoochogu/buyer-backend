#include "community.hpp"

#include <drogon/HttpController.h>
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
#include <format>
#include <numeric>
#include <string>
#include <vector>

#include "../services/service_manager.hpp"
#include "../utilities/conversion.hpp"
#include "../utilities/json_manipulation.hpp"
#include "../utilities/time_manipulation.hpp"
#include "common_req_n_resp.hpp"
#include "scenario_specific_utils.hpp"

using drogon::app;
using drogon::CT_APPLICATION_JSON;
using drogon::HttpRequestPtr;
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::k200OK;
using drogon::k400BadRequest;
using drogon::k403Forbidden;
using drogon::k404NotFound;
using drogon::k500InternalServerError;
using drogon::Task;
using drogon::orm::DrogonDbException;

using api::v1::Community;

struct CommunityPost {
  int id;
  int user_id;
  std::string username;
  std::string content;
  std::string created_at;
  std::vector<std::string> tags;
  std::string location;
  bool is_product_request;
  std::string request_status;
  std::string price_range;
  int subscription_count;
  bool is_subscribed;
  std::optional<std::vector<MediaQuickInfo>> media;
};

struct CreatePostRequest {
  std::string content;
  std::optional<bool> is_product_request;
  std::optional<std::string> location;
  std::optional<std::string> price_range;
  std::optional<std::vector<std::string>> tags;
  std::optional<std::vector<std::string>> media;
};

struct CreatePostResponse {
  std::string status;
  int post_id;
  std::string created_at;
};

struct UpdatePostRequest {
  std::optional<std::string> content;
  std::optional<std::string> location;
  std::optional<std::string> price_range;
  std::optional<std::vector<std::string>> tags;
  std::optional<std::vector<std::string>> media;
};

struct FilterPostsRequest {
  std::optional<std::string> tags;
  std::optional<std::string> location;
  std::optional<std::string> status;
  std::optional<bool> is_product_request;
  std::optional<int> page;
};

struct Tag {
  std::string name;
  int count;
};

Task<> Community::get_posts(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto db = app().getDbClient();

  int page = 1;
  auto page_param = req->getParameter("page");
  if (!page_param.empty()) {
    auto page_optional = convert::string_to_int(page_param);
    if (page_optional && page_optional.value() > 0) {
      page = page_optional.value();
    }
  }

  const std::size_t page_size = 10;
  const std::size_t offset = (page - 1) * page_size;

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
        page_size, offset, convert::string_to_int(current_user_id).value());

    std::vector<CommunityPost> posts_list;
    for (const auto& row : result) {
      int post_id = row["id"].as<int>();
      auto media_attachments = co_await get_media_attachments("post", post_id);

      posts_list.push_back(
          {.id = post_id,
           .user_id = row["user_id"].as<int>(),
           .username = row["username"].as<std::string>(),
           .content = row["content"].as<std::string>(),
           .created_at = row["created_at"].as<std::string>(),
           .tags = convert::pgsql_array_string_to_vector(
               row["tags"].as<std::string>()),
           .location = row["location"].isNull()
                           ? ""
                           : row["location"].as<std::string>(),
           .is_product_request = row["is_product_request"].as<bool>(),
           .request_status = row["request_status"].as<std::string>(),
           .price_range = row["price_range"].isNull()
                              ? ""
                              : row["price_range"].as<std::string>(),
           .subscription_count = row["subscription_count"].as<int>(),
           .is_subscribed = row["is_subscribed"].as<bool>(),
           .media = media_attachments.value_or({})});
    }

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(posts_list).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}

Task<> Community::create_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto body = req->getBody();
  CreatePostRequest create_post_req;
  auto parse_error = utilities::strict_read_json(create_post_req, body);

  if (parse_error || create_post_req.content.empty()) {
    SimpleError ret{.error = "Post has no content."};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  std::string content = create_post_req.content;
  bool is_product_request = create_post_req.is_product_request.value_or(false);
  std::string request_status = is_product_request ? "open" : "";
  std::string location = create_post_req.location.value_or("");
  std::string price_range = create_post_req.price_range.value_or("");

  std::string tags_str = "{}";
  if (create_post_req.tags && !create_post_req.tags->empty()) {
    tags_str =
        convert::array_to_pgsql_array_string(create_post_req.tags.value());
  }
  if (create_post_req.media.has_value() &&
      create_post_req.media->size() > service::MAX_MEDIA_SIZE) {
    SimpleError ret{.error = "Maximum of 5 media items allowed per post"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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
        convert::string_to_int(user_id).value(), content, tags_str, location,
        is_product_request, request_status, price_range);

    if (result.empty()) {
      throw std::runtime_error("Initial post insert failed");
    }
    int post_id = result[0]["id"].as<int>();
    std::string created_at = result[0]["created_at"].as<std::string>();

    if (create_post_req.media.has_value() && !create_post_req.media->empty()) {
      std::size_t media_array_size = create_post_req.media->size();
      auto processed_media = co_await process_media_attachments(
          std::move(*create_post_req.media), transaction,
          convert::string_to_int(user_id).value(), "post", post_id);
      if (processed_media->empty() ||
          processed_media->size() < media_array_size) {
        LOG_ERROR << " Some Media info was not found";
        transaction->rollback();
        std::string error_string;
        for (const auto& medi_item : *processed_media) {
          error_string += medi_item.filename + ", ";
        }
        SimpleError ret{.error =
                            std::format("Media info not found or processed, "
                                        "only the following media items "
                                        "were processed:\n{}",
                                        error_string)};
        auto resp =
            HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
        co_return;
      }
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

    NotificationMessage msg{
        .type = "post_created",
        .id = post_id_str,
        .message =
            is_product_request ? "New Product Request: " + content : content,
        .modified_at = result[0]["created_at"].as<std::string>()};

    std::string post_data = glz::write_json(msg).value_or("");

    // Publish to tag subscribers
    if (create_post_req.tags && !create_post_req.tags->empty()) {
      for (const auto& tag : create_post_req.tags.value()) {
        std::string tag_topic = tag;
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

    CreatePostResponse ret{
        .status = "success", .post_id = post_id, .created_at = created_at};
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  } catch (const std::exception& e) {
    transaction->rollback();
    LOG_ERROR << "Failed to create post" << e.what();
    SimpleError ret{.error = std::format("Failed to create post {}", e.what())};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    transaction->rollback();
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

  auto post_id_optional = convert::string_to_int(id);
  if (!post_id_optional || post_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid ID"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int post_id = post_id_optional.value();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT p.*, u.username, "
        "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
        "subscription_count, "
        "EXISTS(SELECT 1 FROM post_subscriptions WHERE post_id = p.id AND "
        "user_id = $2) AS is_subscribed "
        "FROM posts p "
        "JOIN users u ON p.user_id = u.id "
        "WHERE p.id = $1",
        post_id, convert::string_to_int(current_user_id).value());

    if (result.empty()) {
      SimpleError ret{.error = "Post not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    const auto& row = result[0];
    auto media_attachments = co_await get_media_attachments("post", post_id);
    CommunityPost post_obj{
        .id = post_id,
        .user_id = row["user_id"].as<int>(),
        .username = row["username"].as<std::string>(),
        .content = row["content"].as<std::string>(),
        .created_at = row["created_at"].as<std::string>(),
        .tags = convert::pgsql_array_string_to_vector(
            row["tags"].as<std::string>()),
        .location =
            row["location"].isNull() ? "" : row["location"].as<std::string>(),
        .is_product_request = row["is_product_request"].as<bool>(),
        .request_status = row["request_status"].as<std::string>(),
        .price_range = row["price_range"].isNull()
                           ? ""
                           : row["price_range"].as<std::string>(),
        .subscription_count = row["subscription_count"].as<int>(),
        .is_subscribed = row["is_subscribed"].as<bool>(),
        .media = media_attachments.value_or({})};

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(post_obj).value_or(""));
    callback(resp);

  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}

// Update a post
// Current behaviour is to overwrite data with any newly provide data.
// Media files are overwritten with new files.
Task<> Community::update_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  auto body = req->getBody();
  UpdatePostRequest update_post_req;
  auto parse_error = utilities::strict_read_json(update_post_req, body);

  auto post_id_optional = convert::string_to_int(id);
  if (!post_id_optional || post_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid post id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int post_id_int = post_id_optional.value();

  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  if (parse_error ||
      (update_post_req.content && update_post_req.content->empty())) {
    SimpleError ret{.error = "Content must be a string if provided"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  if (update_post_req.media.has_value() &&
      update_post_req.media->size() > service::MAX_MEDIA_SIZE) {
    SimpleError ret{.error = "Maximum of 5 media items allowed per post"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    auto check_result = co_await db->execSqlCoro(
        "SELECT user_id FROM posts WHERE id = $1", post_id_int);

    if (check_result.empty()) {
      SimpleError ret{.error = "Post not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    int post_user_id = check_result[0]["user_id"].as<int>();
    if (post_user_id != convert::string_to_int(current_user_id).value()) {
      SimpleError ret{.error = "You don't have permission to update this post"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    bool has_content = update_post_req.content.has_value();
    bool has_location = update_post_req.location.has_value();
    bool has_price_range = update_post_req.price_range.has_value();
    bool has_tags =
        update_post_req.tags.has_value() && !update_post_req.tags->empty();

    if (!has_content && !has_location && !has_price_range && !has_tags &&
        !update_post_req.media.has_value()) {
      StatusResponse ret{.status = "success", .message = "No fields to update"};
      auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    std::string content = update_post_req.content.value_or("");
    std::string location = update_post_req.location.value_or("");
    std::string price_range = update_post_req.price_range.value_or("");
    std::string tags_str = "{}";
    if (update_post_req.tags) {
      tags_str =
          convert::array_to_pgsql_array_string(update_post_req.tags.value());
    }

    // Build the update query using CASE expressions
    std::string update_query =
        "UPDATE posts SET "
        "content = CASE WHEN $2 THEN $3 ELSE content END, "
        "location = CASE WHEN $4 THEN $5 ELSE location END, "
        "price_range = CASE WHEN $6 THEN $7 ELSE price_range END, "
        "tags = CASE WHEN $8 THEN $9 ELSE tags END "
        "WHERE id = $1 "
        "RETURNING id";

    auto transaction = co_await db->newTransactionCoro();
    try {
      auto update_result = co_await transaction->execSqlCoro(
          update_query, post_id_int, has_content, content, has_location,
          location, has_price_range, price_range, has_tags, tags_str);

      if (update_result.empty()) {
        throw std::runtime_error("Initial post update failed");
      }

      if (update_post_req.media.has_value() &&
          !update_post_req.media->empty()) {
        std::size_t media_array_size = update_post_req.media->size();
        auto processed_media = co_await process_media_attachments(
            std::move(*update_post_req.media), transaction,
            convert::string_to_int(current_user_id).value(), "post",
            post_id_int);
        if (!processed_media.has_value() ||
            processed_media->size() < media_array_size) {
          LOG_ERROR << " Some Media info was not found";
          transaction->rollback();
          std::string error_string;
          for (const auto& medi_item : *processed_media) {
            error_string += medi_item.filename + ", ";
          }
          SimpleError ret{.error =
                              std::format("Media info not found or processed, "
                                          "only the following media items "
                                          "were processed:\n{}",
                                          error_string)};
          auto resp = HttpResponse::newHttpResponse(k400BadRequest,
                                                    CT_APPLICATION_JSON);
          resp->setBody(glz::write_json(ret).value_or(""));
          callback(resp);
          co_return;
        }
      }

      // notification:
      std::string post_topic = create_topic("post", id);

      NotificationMessage msg{.type = "post_updated",
                              .id = id,
                              .message = "New update on post",
                              .modified_at = get_precise_sql_utc_timestamp()};

      // Publish to post (owner and post subscribers)
      ServiceManager::get_instance().get_publisher().publish(
          post_topic, glz::write_json(msg).value_or(""));
      LOG_INFO << "Updated post: " << post_topic;

      StatusResponse ret{.status = "success",
                         .message = "Post updated successfully"};
      auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
    } catch (const std::exception& e) {
      transaction->rollback();
      LOG_ERROR << "Error updating post: " << e.what();
      SimpleError ret{.error =
                          std::format("Failed to update post: {}", e.what())};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

  FilterPostsRequest filter_req;
  auto parse_error = utilities::strict_read_json(filter_req, req->getBody());

  filter_req.tags = req->getParameter("tags");
  filter_req.location = req->getParameter("location");
  filter_req.status = req->getParameter("status");
  auto is_product_request_param = req->getParameter("is_product_request");
  if (!is_product_request_param.empty()) {
    filter_req.is_product_request =
        (is_product_request_param == "true" || is_product_request_param == "1");
  }
  auto page_param = req->getParameter("page");
  if (!page_param.empty()) {
    auto page_optional = convert::string_to_int(page_param);
    if (page_optional && page_optional.value() > 0) {
      filter_req.page = page_optional.value();
    }
  }

  std::size_t page = filter_req.page.value_or(1);
  const std::size_t page_size = 10;
  const std::size_t offset = (page - 1) * page_size;

  std::vector<std::string> tags;
  if (filter_req.tags) {
    std::string tags_str = filter_req.tags.value();
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
  if (filter_req.location && !filter_req.location->empty()) {
    query += "AND p.location ILIKE '%" + filter_req.location.value() + "%' ";
  }

  if (filter_req.status && !filter_req.status->empty()) {
    query += "AND p.request_status = '" + filter_req.status.value() + "' ";
  }

  if (filter_req.is_product_request) {
    query +=
        "AND p.is_product_request = " +
        std::string(filter_req.is_product_request.value() ? "TRUE" : "FALSE") +
        " ";
  }

  // Add pagination
  query += "ORDER BY p.created_at DESC LIMIT $2 OFFSET $3";

  LOG_DEBUG << "Filter query: " << query;

  try {
    auto result = co_await db->execSqlCoro(
        query, convert::string_to_int(current_user_id).value(), page_size,
        offset);

    std::vector<CommunityPost> posts_list;
    for (const auto& row : result) {
      int post_id = row["id"].as<int>();
      auto media_attachments = co_await get_media_attachments("post", post_id);
      posts_list.emplace_back(CommunityPost{
          .id = post_id,
          .user_id = row["user_id"].as<int>(),
          .username = row["username"].as<std::string>(),
          .content = row["content"].as<std::string>(),
          .created_at = row["created_at"].as<std::string>(),
          .tags = convert::pgsql_array_string_to_vector(
              row["tags"].as<std::string>()),
          .location =
              row["location"].isNull() ? "" : row["location"].as<std::string>(),
          .is_product_request = row["is_product_request"].as<bool>(),
          .request_status = row["request_status"].as<std::string>(),
          .price_range = row["price_range"].isNull()
                             ? ""
                             : row["price_range"].as<std::string>(),
          .subscription_count = row["subscription_count"].as<int>(),
          .is_subscribed = row["is_subscribed"].as<bool>(),
          .media = media_attachments.value_or({})});
    }
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(posts_list).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

  auto post_id_optional = convert::string_to_int(id);
  if (!post_id_optional || post_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid post ID"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int post_id_int = post_id_optional.value();

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "INSERT INTO post_subscriptions (user_id, post_id) "
        "VALUES ($1, $2) "
        "ON CONFLICT (user_id, post_id) DO NOTHING "
        "RETURNING id",
        convert::string_to_int(current_user_id).value(), post_id_int);

    std::string post_topic = create_topic("post", id);
    ServiceManager::get_instance().get_subscriber().subscribe(post_topic);
    ServiceManager::get_instance().get_connection_manager().subscribe(
        post_topic, current_user_id);
    store_user_subscription(current_user_id, post_topic);

    StatusResponse ret{.status = "success",
                       .message = result.empty() ? "Already subscribed to post"
                                                 : "Subscribed to post"};
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

  auto post_id_optional = convert::string_to_int(id);
  if (!post_id_optional || post_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid post ID"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int post_id_int = post_id_optional.value();

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "DELETE FROM post_subscriptions "
        "WHERE user_id = $1 AND post_id = $2 "
        "RETURNING id",
        convert::string_to_int(current_user_id).value(), post_id_int);

    std::string post_topic = create_topic("post", id);
    ServiceManager::get_instance()
        .get_connection_manager()
        .unsubscribe_user_from_topic(current_user_id, post_topic);
    remove_user_subscription(current_user_id, post_topic);

    StatusResponse ret{.status = "success",
                       .message = result.empty() ? "Not subscribed to post"
                                                 : "Unsubscribed from post"};

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

    StatusResponse ret{.status = "success",
                       .message = "Subscription successful"};
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

    StatusResponse ret{.status = "success",
                       .message = "Unsubscribed from entity"};
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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
        convert::string_to_int(current_user_id).value());

    std::vector<CommunityPost> posts_list;
    posts_list.reserve(result.size());
    for (const auto& row : result) {
      std::optional<std::vector<MediaQuickInfo>> media_attachments =
          std::nullopt;
      posts_list.emplace_back(CommunityPost{
          .id = row["id"].as<int>(),
          .user_id = row["user_id"].as<int>(),
          .username = row["username"].as<std::string>(),
          .content = row["content"].as<std::string>(),
          .created_at = row["created_at"].as<std::string>(),
          .tags = convert::pgsql_array_string_to_vector(
              row["tags"].as<std::string>()),
          .location =
              row["location"].isNull() ? "" : row["location"].as<std::string>(),
          .is_product_request = row["is_product_request"].as<bool>(),
          .request_status = row["request_status"].as<std::string>(),
          .price_range = row["price_range"].isNull()
                             ? ""
                             : row["price_range"].as<std::string>(),
          .subscription_count = row["subscription_count"].as<int>(),
          .is_subscribed = row["is_subscribed"].as<bool>(),
          .media = media_attachments});
    }

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(posts_list).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
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

    std::vector<Tag> tags_response;
    tags_response.reserve(result.size());
    for (const auto& row : result) {
      tags_response.push_back({.name = row["tag"].as<std::string>(),
                               .count = row["count"].as<int>()});
    }

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(tags_response).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}
