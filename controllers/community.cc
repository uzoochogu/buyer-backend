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

void Community::get_posts(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
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

  db->execSqlAsync(
      "SELECT p.*, u.username, "
      "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
      "subscription_count, "
      "EXISTS(SELECT 1 FROM post_subscriptions WHERE post_id = p.id AND "
      "user_id = $3) AS is_subscribed "
      "FROM posts p "
      "JOIN users u ON p.user_id = u.id "
      "ORDER BY p.created_at DESC LIMIT $1 OFFSET $2",
      [callback](const Result& result) {
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::to_string(page_size), std::to_string(offset), current_user_id);
}

void Community::create_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");
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

  auto db = app().getDbClient();

  db->execSqlAsync(
      "INSERT INTO posts (user_id, content, tags, location, "
      "is_product_request, request_status, price_range) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7) RETURNING id, created_at",
      [callback](const Result& result) {
        if (result.size() > 0) {
          Json::Value ret;
          ret["status"] = "success";
          ret["post_id"] = result[0]["id"].as<int>();
          ret["created_at"] = result[0]["created_at"].as<std::string>();
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          callback(resp);
        } else {
          Json::Value error;
          error["error"] = "Failed to create post";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
        }
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(user_id), content, tags_str, location, is_product_request,
      request_status, price_range);
}

// Get a single post by ID
void Community::get_post_by_id(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  db->execSqlAsync(
      "SELECT p.*, u.username, "
      "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
      "subscription_count, "
      "EXISTS(SELECT 1 FROM post_subscriptions WHERE post_id = p.id AND "
      "user_id = $2) AS is_subscribed "
      "FROM posts p "
      "JOIN users u ON p.user_id = u.id "
      "WHERE p.id = $1",
      [callback](const Result& result) {
        if (result.size() > 0) {
          const auto& row = result[0];
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

          auto resp = HttpResponse::newHttpJsonResponse(post);
          callback(resp);
        } else {
          Json::Value error;
          error["error"] = "Post not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
        }
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id), current_user_id);
}

// Update a post
void Community::update_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  auto json = req->getJsonObject();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  // First check if the user is the owner of the post
  *db << "SELECT user_id FROM posts WHERE id = $1" << std::stoi(id) >>
      [callback, db, json, id, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Post not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int post_user_id = result[0]["user_id"].as<int>();
        if (post_user_id != std::stoi(current_user_id)) {
          Json::Value error;
          error["error"] = "You don't have permission to update this post";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }

        // Prepare update fields
        std::string content =
            json->isMember("content") ? (*json)["content"].asString() : "";
        std::string request_status = json->isMember("request_status")
                                         ? (*json)["request_status"].asString()
                                         : "";
        std::string location =
            json->isMember("location") ? (*json)["location"].asString() : "";
        std::string price_range = json->isMember("price_range")
                                      ? (*json)["price_range"].asString()
                                      : "";

        // Handle tags
        std::string tags_str = "{}";
        if (json->isMember("tags") && (*json)["tags"].isArray()) {
          tags_str = json_array_to_string((*json)["tags"]);
        }

        // Build the update query dynamically based on provided fields
        std::string update_query = "UPDATE posts SET ";
        std::vector<std::string> update_fields;

        if (!content.empty()) {
          update_fields.push_back("content = $2");
        }

        if (!request_status.empty()) {
          update_fields.push_back("request_status = $3");
        }

        if (!location.empty()) {
          update_fields.push_back("location = $4");
        }

        if (!price_range.empty()) {
          update_fields.push_back("price_range = $5");
        }

        if (tags_str != "{}") {
          update_fields.push_back("tags = $6");
        }

        if (update_fields.empty()) {
          Json::Value ret;
          ret["status"] = "success";
          ret["message"] = "No fields to update";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          callback(resp);
          return;
        }

        update_query += std::accumulate(
            std::next(update_fields.begin()), update_fields.end(),
            update_fields[0], [](const std::string& a, const std::string& b) {
              return a + ", " + b;
            });

        update_query += " WHERE id = $1 RETURNING id";

        auto query = *db << update_query;
        query << std::stoi(id);

        if (!content.empty()) {
          query << content;
        }

        if (!request_status.empty()) {
          query << request_status;
        }

        if (!location.empty()) {
          query << location;
        }

        if (!price_range.empty()) {
          query << price_range;
        }

        if (tags_str != "{}") {
          query << tags_str;
        }

        // Set success callback
        query >> [callback](const Result& updateResult) {
          if (updateResult.size() > 0) {
            Json::Value ret;
            ret["status"] = "success";
            ret["message"] = "Post updated successfully";
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
          } else {
            Json::Value error;
            error["error"] = "Failed to update post";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
          }
        };

        // Set error callback
        query >> [callback](const DrogonDbException& e) {
          LOG_ERROR << "Database error: " << e.base().what();
          Json::Value error;
          error["error"] = "Database error";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
        };
      } >>
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      };
}

// Filter posts by tags, location, and status - use defaults
void Community::filter_posts(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
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

  // Execute the query with the fixed parameters
  db->execSqlAsync(
      query,
      [callback](const Result& result) {
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      current_user_id,            // $1: user_id
      std::to_string(page_size),  // $2: limit
      std::to_string(offset)      // $3: offset
  );
}

// Subscribe to a post
void Community::subscribe_to_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  db->execSqlAsync(
      "INSERT INTO post_subscriptions (user_id, post_id) "
      "VALUES ($1, $2) "
      "ON CONFLICT (user_id, post_id) DO NOTHING "
      "RETURNING id",
      [callback](const Result& result) {
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(current_user_id), std::stoi(id));
}

// Unsubscribe from a post
void Community::unsubscribe_from_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  db->execSqlAsync(
      "DELETE FROM post_subscriptions "
      "WHERE user_id = $1 AND post_id = $2 "
      "RETURNING id",
      [callback](const Result& result) {
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(current_user_id), std::stoi(id));
}

// Get user's subscriptions
void Community::get_subscriptions(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT p.*, u.username, "
      "(SELECT COUNT(*) FROM post_subscriptions WHERE post_id = p.id) AS "
      "subscription_count, "
      "TRUE AS is_subscribed "
      "FROM posts p "
      "JOIN users u ON p.user_id = u.id "
      "JOIN post_subscriptions ps ON p.id = ps.post_id "
      "WHERE ps.user_id = $1 "
      "ORDER BY p.created_at DESC",
      [callback](const Result& result) {
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(current_user_id));
}

// Get popular tags
void Community::get_popular_tags(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT DISTINCT unnest(tags) as tag, "
      "COUNT(*) as count "
      "FROM posts "
      "GROUP BY tag "
      "ORDER BY count DESC "
      "LIMIT 20",
      [callback](const Result& result) {
        Json::Value tags(Json::arrayValue);
        for (const auto& row : result) {
          Json::Value tag;
          tag["name"] = row["tag"].as<std::string>();
          tag["count"] = row["count"].as<int>();
          tags.append(tag);
        }

        auto resp = HttpResponse::newHttpJsonResponse(tags);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      });
}
