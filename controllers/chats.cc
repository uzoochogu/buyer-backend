#include "chats.hpp"

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/SqlBinder.h>
#include <drogon/orm/ResultIterator.h>

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

void Chats::get_conversations(const HttpRequestPtr& req,
                             std::function<void(const HttpResponsePtr&)>&& callback) {
  std::string userId = req->getAttributes()->get<std::string>("current_user_id");
  
  auto db = app().getDbClient();
  
  // Get all conversations where the current user is a participant
  // Include the latest message and other participant info
  db->execSqlAsync(
      "SELECT c.id, c.name, c.created_at, "
      "COALESCE(u.username, 'Unknown') as other_username, "
      "COALESCE(m.content, '') as last_message, "
      "COALESCE(m.created_at, c.created_at) as last_message_time "
      "FROM conversations c "
      "JOIN conversation_participants cp ON c.id = cp.conversation_id "
      "LEFT JOIN conversation_participants cp2 ON c.id = cp2.conversation_id AND cp2.user_id != $1 "
      "LEFT JOIN users u ON cp2.user_id = u.id "
      "LEFT JOIN ( "
      "  SELECT conversation_id, content, created_at, "
      "  ROW_NUMBER() OVER (PARTITION BY conversation_id ORDER BY created_at DESC) as rn "
      "  FROM messages "
      ") m ON m.conversation_id = c.id AND m.rn = 1 "
      "WHERE cp.user_id = $1 "
      "ORDER BY last_message_time DESC",
      [callback](const Result& result) {
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(userId));
}

void Chats::create_conversation(const HttpRequestPtr& req,
                               std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string userId = req->getAttributes()->get<std::string>("current_user_id");
  int otherUserId = (*json)["user_id"].asInt();
  std::string name = (*json)["name"].asString();
  
  auto db = app().getDbClient();
  
  // First check if a conversation already exists between these users
  db->execSqlAsync(
      "SELECT c.id FROM conversations c "
      "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id AND cp1.user_id = $1 "
      "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id AND cp2.user_id = $2 "
      "LIMIT 1",
      [callback, db, userId, otherUserId, name](const Result& result) {
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
          db->execSqlAsync(
              "INSERT INTO conversations (name) VALUES ($1) RETURNING id",
              [callback, db, userId, otherUserId](const Result& insertResult) {
                if (insertResult.size() > 0) {
                  int conversationId = insertResult[0]["id"].as<int>();
                  
                  // Add participants
                  db->execSqlAsync(
                      "INSERT INTO conversation_participants (conversation_id, user_id) VALUES ($1, $2), ($1, $3)",
                      [callback, conversationId](const Result& participantResult) {
                        Json::Value ret;
                        ret["status"] = "success";
                        ret["conversation_id"] = conversationId;
                        auto resp = HttpResponse::newHttpJsonResponse(ret);
                        callback(resp);
                      },
                      [callback](const DrogonDbException& e) {
                        LOG_ERROR << "Database error adding participants: " << e.base().what();
                        Json::Value error;
                        error["error"] = "Database error";
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k500InternalServerError);
                        callback(resp);
                      },
                      conversationId, std::stoi(userId), otherUserId);
                } else {
                  Json::Value error;
                  error["error"] = "Failed to create conversation";
                  auto resp = HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(k500InternalServerError);
                  callback(resp);
                }
              },
              [callback](const DrogonDbException& e) {
                LOG_ERROR << "Database error creating conversation: " << e.base().what();
                Json::Value error;
                error["error"] = "Database error";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              name);
        }
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error checking existing conversation: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(userId), otherUserId);
}

void Chats::get_messages(const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& callback,
                        const std::string& conversation_id) {
  std::string userId = req->getAttributes()->get<std::string>("current_user_id");
  
  auto db = app().getDbClient();
  
  // First verify the user is a participant in this conversation
  db->execSqlAsync(
      "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 AND user_id = $2",
      [callback, db, conversation_id, userId](const Result& result) {
        if (result.size() == 0) {
          // User is not a participant
          Json::Value error;
          error["error"] = "Unauthorized access to conversation";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }
        
        // Get messages for the conversation
        db->execSqlAsync(
            "SELECT m.id, m.sender_id, u.username as sender_name, m.content, m.is_read, m.created_at "
            "FROM messages m "
            "JOIN users u ON m.sender_id = u.id "
            "WHERE m.conversation_id = $1 "
            "ORDER BY m.created_at ASC",
            [callback](const Result& messagesResult) {
              Json::Value messages;
              for (const auto& row : messagesResult) {
                Json::Value message;
                message["id"] = row["id"].as<int>();
                message["sender_id"] = row["sender_id"].as<int>();
                message["sender_name"] = row["sender_name"].as<std::string>();
                message["content"] = row["content"].as<std::string>();
                message["is_read"] = row["is_read"].as<bool>();
                message["created_at"] = row["created_at"].as<std::string>();
                messages.append(message);
              }

              auto resp = HttpResponse::newHttpJsonResponse(messages);
              callback(resp);
            },
            [callback](const DrogonDbException& e) {
              LOG_ERROR << "Database error getting messages: " << e.base().what();
              Json::Value error;
              error["error"] = "Database error";
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(conversation_id));
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error verifying participant: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(conversation_id), std::stoi(userId));
}

void Chats::send_message(const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& callback,
                        const std::string& conversation_id) {
  auto json = req->getJsonObject();
  std::string userId = req->getAttributes()->get<std::string>("current_user_id");
  std::string content = (*json)["content"].asString();
  
  auto db = app().getDbClient();
  
  // First verify the user is a participant in this conversation
  db->execSqlAsync(
      "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 AND user_id = $2",
      [callback, db, conversation_id, userId, content](const Result& result) {
        if (result.size() == 0) {
          // User is not a participant
          Json::Value error;
          error["error"] = "Unauthorized access to conversation";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }
        
        // Insert the message
        db->execSqlAsync(
            "INSERT INTO messages (conversation_id, sender_id, content) "
            "VALUES ($1, $2, $3) RETURNING id, created_at",
            [callback](const Result& insertResult) {
              if (insertResult.size() > 0) {
                Json::Value ret;
                ret["status"] = "success";
                ret["message_id"] = insertResult[0]["id"].as<int>();
                ret["created_at"] = insertResult[0]["created_at"].as<std::string>();
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                callback(resp);
              } else {
                Json::Value error;
                error["error"] = "Failed to send message";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              }
            },
            [callback](const DrogonDbException& e) {
              LOG_ERROR << "Database error sending message: " << e.base().what();
              Json::Value error;
              error["error"] = "Database error";
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(conversation_id), std::stoi(userId), content);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error verifying participant: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(conversation_id), std::stoi(userId));
}
