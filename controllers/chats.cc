#include "chats.hpp"

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

#include "../utilities/conversion.hpp"

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

Task<> Chats::get_conversations(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    // Get all conversations where the current user is a participant
    // Include the latest message and other participant info
    auto result = co_await db->execSqlCoro(
        "SELECT c.id, c.name, c.created_at, "
        "COALESCE(u.username, 'Unknown') as other_username, "
        "COALESCE(m.content, '') as last_message, "
        "COALESCE(m.created_at, c.created_at) as last_message_time "
        "FROM conversations c "
        "JOIN conversation_participants cp ON c.id = cp.conversation_id "
        "LEFT JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
        "AND cp2.user_id != $1 "
        "LEFT JOIN users u ON cp2.user_id = u.id "
        "LEFT JOIN ( "
        "  SELECT conversation_id, content, created_at, "
        "  ROW_NUMBER() OVER (PARTITION BY conversation_id ORDER BY created_at "
        "DESC) as rn "
        "  FROM messages "
        ") m ON m.conversation_id = c.id AND m.rn = 1 "
        "WHERE cp.user_id = $1 "
        "ORDER BY last_message_time DESC",
        std::stoi(user_id));

    Json::Value conversations;
    for (const auto& row : result) {
      Json::Value conversation;
      conversation["id"] = row["id"].as<int>();
      conversation["name"] = row["name"].as<std::string>();
      conversation["other_username"] = row["other_username"].as<std::string>();
      conversation["lastMessage"] = row["last_message"].as<std::string>();
      conversation["created_at"] = row["created_at"].as<std::string>();
      conversations.append(conversation);
    }

    auto resp = HttpResponse::newHttpJsonResponse(conversations);
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

Task<> Chats::create_conversation(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto json = req->getJsonObject();
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Ensure name and user_id are provided
  if (!json || !(*json).isMember("name") || !(*json).isMember("user_id") ||
      (*json)["name"].asString().empty() || (*json)["user_id"].asInt() < 0) {
    Json::Value error;
    error["error"] = "Valid user_id and name required";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  LOG_INFO << "Does user_id exist? : " << (*json).isMember("user_id");
  int other_user_id = (*json)["user_id"].asInt();
  std::string name = (*json)["name"].asString();

  auto db = app().getDbClient();

  try {
    // First check if a conversation already exists between these users
    auto result = co_await db->execSqlCoro(
        "SELECT c.id FROM conversations c "
        "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id AND "
        "cp1.user_id = $1 "
        "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id AND "
        "cp2.user_id = $2 "
        "LIMIT 1",
        std::stoi(user_id), other_user_id);

    if (result.size() > 0) {
      // Conversation already exists
      Json::Value ret;
      ret["status"] = "success";
      ret["conversation_id"] = result[0]["id"].as<int>();
      ret["message"] = "Conversation already exists";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } else {
      // Create new conversation
      auto insert_result = co_await db->execSqlCoro(
          "INSERT INTO conversations (name) VALUES ($1) RETURNING id", name);

      if (insert_result.size() > 0) {
        int conversation_id = insert_result[0]["id"].as<int>();

        // Add participants
        try {
          co_await db->execSqlCoro(
              "INSERT INTO conversation_participants (conversation_id, "
              "user_id) VALUES ($1, $2), ($1, $3)",
              conversation_id, std::stoi(user_id), other_user_id);

          Json::Value ret;
          ret["status"] = "success";
          ret["conversation_id"] = conversation_id;
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          callback(resp);
        } catch (const DrogonDbException& e) {
          LOG_ERROR << "Database error adding participants: "
                    << e.base().what();
          Json::Value error;
          error["error"] = "Database error";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
        }
      } else {
        Json::Value error;
        error["error"] = "Failed to create conversation";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      }
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error checking existing conversation: "
              << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

Task<> Chats::get_messages(HttpRequestPtr req,
                           std::function<void(const HttpResponsePtr&)> callback,
                           std::string conversation_id) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  // Validation: Malformed route
  if (!convert::string_to_int(conversation_id).has_value() ||
      convert::string_to_int(conversation_id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid conversation_id";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // First verify the user is a participant in this conversation
    auto result = co_await db->execSqlCoro(
        "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 "
        "AND "
        "user_id = $2",
        std::stoi(conversation_id), std::stoi(user_id));

    if (result.size() == 0) {
      // User is not a participant
      Json::Value error;
      error["error"] = "Unauthorized access to conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Get messages for the conversation
    auto messages_result = co_await db->execSqlCoro(
        "SELECT m.id, m.sender_id, u.username as sender_name, m.content, "
        "m.is_read, m.created_at, m.metadata "
        "FROM messages m "
        "JOIN users u ON m.sender_id = u.id "
        "WHERE m.conversation_id = $1 "
        "ORDER BY m.created_at ASC",
        std::stoi(conversation_id));

    Json::Value messages;
    for (const auto& row : messages_result) {
      Json::Value message;
      message["id"] = row["id"].as<int>();
      message["sender_id"] = row["sender_id"].as<int>();
      message["sender_name"] = row["sender_name"].as<std::string>();
      message["content"] = row["content"].as<std::string>();
      message["is_read"] = row["is_read"].as<bool>();
      message["created_at"] = row["created_at"].as<std::string>();
      message["metadata"] = row["metadata"].as<std::string>();
      messages.append(message);
    }

    auto resp = HttpResponse::newHttpJsonResponse(messages);
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

Task<> Chats::send_message(HttpRequestPtr req,
                           std::function<void(const HttpResponsePtr&)> callback,
                           std::string conversation_id) {
  auto json = req->getJsonObject();
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Malformed route
  if (!convert::string_to_int(conversation_id).has_value() ||
      convert::string_to_int(conversation_id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid conversation_id";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }
  // Validation: Missing message content
  if (!json || !(*json).isMember("content") ||
      (*json)["content"].asString().empty()) {
    Json::Value error;
    error["error"] = "Message content is required";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }
  std::string content = (*json)["content"].asString();

  auto db = app().getDbClient();

  try {
    // First verify the user is a participant in this conversation
    auto result = co_await db->execSqlCoro(
        "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 "
        "AND "
        "user_id = $2",
        std::stoi(conversation_id), std::stoi(user_id));

    if (result.size() == 0) {
      // User is not a participant
      Json::Value error;
      error["error"] = "Unauthorized access to conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Insert the message
    auto insert_result = co_await db->execSqlCoro(
        "INSERT INTO messages (conversation_id, sender_id, content) "
        "VALUES ($1, $2, $3) RETURNING id, created_at",
        std::stoi(conversation_id), std::stoi(user_id), content);

    if (insert_result.size() > 0) {
      Json::Value ret;
      ret["status"] = "success";
      ret["message_id"] = insert_result[0]["id"].as<int>();
      ret["created_at"] = insert_result[0]["created_at"].as<std::string>();
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } else {
      Json::Value error;
      error["error"] = "Failed to send message";
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

// Get conversation by offer ID
Task<> Chats::get_conversation_by_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string offer_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Validation: Malformed route
  if (!convert::string_to_int(offer_id).has_value() ||
      convert::string_to_int(offer_id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid offer ID";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  try {
    // First, check if the user is authorized to access this offer's
    // conversation
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(offer_id));

    if (result.size() == 0) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    // Only offer creator or post owner can access the conversation
    if (std::stoi(current_user_id) != offer_user_id &&
        std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Unauthorized";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Find conversation between these two users
    auto conv_result = co_await db->execSqlCoro(
        "SELECT c.id FROM conversations c "
        "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id "
        "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
        "WHERE cp1.user_id = $1 AND cp2.user_id = $2 "
        "LIMIT 1",
        std::stoi(current_user_id),
        (std::stoi(current_user_id) == offer_user_id ? post_user_id
                                                     : offer_user_id));

    if (conv_result.size() == 0) {
      // No conversation exists yet, create one
      auto new_conv_result = co_await db->execSqlCoro(
          "INSERT INTO conversations (name) VALUES ($1) RETURNING id",
          "Offer #" + offer_id + " Conversation");

      if (new_conv_result.size() == 0) {
        Json::Value error;
        error["error"] = "Failed to create conversation";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        co_return;
      }

      int conversation_id = new_conv_result[0]["id"].as<int>();

      // Add participants
      try {
        co_await db->execSqlCoro(
            "INSERT INTO conversation_participants "
            "(conversation_id, user_id) VALUES ($1, $2), ($1, $3)",
            conversation_id, std::stoi(current_user_id),
            (std::stoi(current_user_id) == offer_user_id ? post_user_id
                                                         : offer_user_id));

        Json::Value response;
        response["status"] = "success";
        response["conversation_id"] = conversation_id;
        response["is_new"] = true;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);
      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Database error adding participants: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      }
    } else {
      // Conversation exists
      Json::Value response;
      response["status"] = "success";
      response["conversation_id"] = conv_result[0]["id"].as<int>();
      response["is_new"] = false;

      auto resp = HttpResponse::newHttpJsonResponse(response);
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

Task<> Chats::mark_messages_as_read(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string conversation_id) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    // First verify the user is a participant in this conversation
    auto result = co_await db->execSqlCoro(
        "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 "
        "AND "
        "user_id = $2",
        std::stoi(conversation_id), std::stoi(user_id));

    if (result.size() == 0) {
      // User is not a participant
      Json::Value error;
      error["error"] = "Unauthorized access to conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Mark all messages not sent by the current user as read
    auto update_result = co_await db->execSqlCoro(
        "UPDATE messages SET is_read = true "
        "WHERE conversation_id = $1 AND sender_id != $2 AND is_read = false "
        "RETURNING id",
        std::stoi(conversation_id), std::stoi(user_id));

    Json::Value ret;
    ret["status"] = "success";
    ret["messages_marked"] = (int)update_result.size();
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

Task<> Chats::get_unread_count(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    // Get count of unread messages across all conversations
    auto result = co_await db->execSqlCoro(
        "SELECT COUNT(*) as unread_count "
        "FROM messages m "
        "JOIN conversation_participants cp ON m.conversation_id = "
        "cp.conversation_id "
        "WHERE cp.user_id = $1 AND m.sender_id != $1 AND m.is_read = false",
        std::stoi(user_id));

    Json::Value ret;
    ret["unread_count"] = result[0]["unread_count"].as<int>();
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
