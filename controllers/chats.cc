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

#include <format>

#include "../services/service_manager.hpp"
#include "../utilities/conversion.hpp"
#include "../utilities/json_manipulation.hpp"
#include "common_req_n_resp.hpp"
#include "scenario_specific_utils.hpp"

using drogon::app;
using drogon::CT_APPLICATION_JSON;
using drogon::HttpRequestPtr;
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::k200OK;
using drogon::k400BadRequest;
using drogon::k401Unauthorized;
using drogon::k403Forbidden;
using drogon::k404NotFound;
using drogon::k409Conflict;
using drogon::k500InternalServerError;
using drogon::Task;
using drogon::orm::DrogonDbException;

using api::v1::Chats;

struct Conversation {
  int id;
  std::string name;
  std::string other_username;
  std::string lastMessage;
  std::string modified_at;
};

struct CreateConversationRequest {
  std::string name;
  int user_id;
};

struct CreateConversationResponse {
  std::string status;
  int conversation_id;
  std::string message;
};

struct Message {
  int id;
  int sender_id;
  std::string sender_name;
  std::string content;
  std::string message_type;
  bool is_read;
  std::string created_at;
  std::string metadata;  // json content
  std::optional<std::vector<MediaQuickInfo>> media;
};

struct SendMessageRequest {
  std::optional<std::string> content;
  std::optional<std::vector<std::string>> media;
};

struct SendMessageResponse {
  std::string status;
  int message_id;
  std::string created_at;
  std::string message_type;
  std::optional<std::vector<MediaQuickInfo>> media;
};

struct GetConversationByOfferResponse {
  std::string status;
  int conversation_id;
  bool is_new;
};

struct MarkMessagesAsReadResponse {
  std::string status;
  int messages_marked;
};

struct UnreadCountResponse {
  int unread_count;
};

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
        convert::string_to_int(user_id).value());

    std::vector<Conversation> data;
    data.reserve(result.size());
    for (const auto& row : result) {
      data.emplace_back(Conversation{
          .id = row["id"].as<int>(),
          .name = row["name"].as<std::string>(),
          .other_username = row["other_username"].as<std::string>(),
          .lastMessage = row["last_message"].as<std::string>(),
          .modified_at = row["created_at"].as<std::string>()});
    }

    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(data).value_or(""));
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

Task<> Chats::create_conversation(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  auto body = req->getBody();
  CreateConversationRequest create_conv_req;
  auto parse_error = utilities::strict_read_json(create_conv_req, body);

  if (parse_error || create_conv_req.name.empty() ||
      create_conv_req.user_id < 0) {
    LOG_INFO << "Validation failed for create_conversation";
    SimpleError ret{.error = "Valid user_id and name required"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  int other_user_id = create_conv_req.user_id;
  std::string name = create_conv_req.name;
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT c.id FROM conversations c "
        "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id AND "
        "cp1.user_id = $1 "
        "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id AND "
        "cp2.user_id = $2 "
        "LIMIT 1",
        convert::string_to_int(user_id).value(), other_user_id);

    if (!result.empty()) {
      CreateConversationResponse ret{
          .status = "success",
          .conversation_id = result[0]["id"].as<int>(),
          .message = "Conversation already exists"};
      auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    } else {
      auto insert_result = co_await db->execSqlCoro(
          "INSERT INTO conversations (name) VALUES ($1) RETURNING id, "
          "created_at",
          name);

      if (insert_result.empty()) {
        SimpleError ret{.error = "Failed to create conversation"};
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
        co_return;
      }
      int conversation_id = insert_result[0]["id"].as<int>();
      std::string conversation_id_str =
          insert_result[0]["id"].as<std::string>();

      // Add participants
      try {
        co_await db->execSqlCoro(
            "INSERT INTO conversation_participants (conversation_id, "
            "user_id) VALUES ($1, $2), ($1, $3)",
            conversation_id, convert::string_to_int(user_id).value(),
            other_user_id);

        // notification
        std::string conversation_topic =
            create_topic("chat", conversation_id_str);
        std::string other_user_id_str = std::to_string(other_user_id);
        ServiceManager::get_instance().get_subscriber().subscribe(
            conversation_topic);
        ServiceManager::get_instance().get_connection_manager().subscribe(
            conversation_topic, user_id);
        ServiceManager::get_instance().get_connection_manager().subscribe(
            conversation_topic, other_user_id_str);
        store_user_subscription(user_id, conversation_topic);
        store_user_subscription(other_user_id_str, conversation_topic);
        LOG_INFO << "User " << user_id << " and " << other_user_id_str
                 << " subscribed to conversation topic: " << conversation_topic;

        NotificationMessage msg{
            .type = "chat_created",
            .id = conversation_id_str,
            .message = "New Conversation created",
            .modified_at = insert_result[0]["created_at"].as<std::string>()};

        ServiceManager::get_instance().get_publisher().publish(
            conversation_topic, glz::write_json(msg).value_or(""));

        CreateConversationResponse ret{.status = "success",
                                       .conversation_id = conversation_id,
                                       .message = {}};
        auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Database error adding participants: " << e.base().what();
        SimpleError ret{.error = "Database error"};
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
      }
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error checking existing conversation: "
              << e.base().what();
    SimpleError ret{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}

Task<> Chats::get_messages(HttpRequestPtr req,
                           std::function<void(const HttpResponsePtr&)> callback,
                           std::string conversation_id) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto conv_id_optional = convert::string_to_int(conversation_id);
  if (!conv_id_optional || conv_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid conversation_id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int conv_id = conv_id_optional.value();
  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 "
        "AND "
        "user_id = $2",
        conv_id, convert::string_to_int(user_id).value());

    if (result.empty()) {
      SimpleError ret{.error = "Unauthorized access to conversation"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    auto messages_result = co_await db->execSqlCoro(
        "SELECT m.id, m.sender_id, u.username as sender_name, m.content, "
        "m.message_type, m.is_read, m.created_at, m.metadata "
        "FROM messages m "
        "JOIN users u ON m.sender_id = u.id "
        "WHERE m.conversation_id = $1 "
        "ORDER BY m.created_at ASC",
        conv_id);

    std::vector<Message> messages_list;
    messages_list.reserve(messages_result.size());
    for (const auto& row : messages_result) {
      int message_id = row["id"].as<int>();
      auto media_attachments =
          row["message_type"].as<std::string>() != "text"
              ? co_await get_media_attachments("message", message_id)
              : std::unexpected<std::string>("failed");
      messages_list.emplace_back(
          Message{.id = message_id,
                  .sender_id = row["sender_id"].as<int>(),
                  .sender_name = row["sender_name"].as<std::string>(),
                  .content = row["content"].as<std::string>(),
                  .message_type = row["message_type"].as<std::string>(),
                  .is_read = row["is_read"].as<bool>(),
                  .created_at = row["created_at"].as<std::string>(),
                  .metadata = row["metadata"].as<std::string>(),
                  .media = media_attachments.value_or({})});
    }
    auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(messages_list).value_or(""));
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

Task<> Chats::send_message(HttpRequestPtr req,
                           std::function<void(const HttpResponsePtr&)> callback,
                           std::string conversation_id) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto conv_id_optional = convert::string_to_int(conversation_id);
  if (!conv_id_optional || conv_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid conversation_id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int conv_id = conv_id_optional.value();

  SendMessageRequest send_msg_req;
  auto parse_error = utilities::strict_read_json(send_msg_req, req->getBody());

  // Validation: content and media can't be both missing. If content is provided
  // (and media isn't), it can't be empty. If media is provided it should be
  // either null or an array.
  if (parse_error || (!send_msg_req.content && !send_msg_req.media) ||
      (send_msg_req.content && !send_msg_req.media &&
       send_msg_req.content->empty()) ||
      (send_msg_req.media && !send_msg_req.media->empty() &&
       send_msg_req.media->size() > service::MAX_MEDIA_SIZE)) {
    LOG_INFO << "Message content or media is required, content a string, media "
                "an array of strings. Max media size is "
             << service::MAX_MEDIA_SIZE;
    SimpleError ret{.error =
                        "Message content or media is required, content a "
                        "string, media an array of strings"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  std::string content = send_msg_req.content.value_or("");

  std::string message_type = "text";
  if (send_msg_req.media.has_value()) {
    message_type = content.empty() ? "media" : "mixed";
  }

  auto db = app().getDbClient();

  try {
    int current_user_id = convert::string_to_int(user_id).value();
    auto result = co_await db->execSqlCoro(
        "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 "
        "AND "
        "user_id = $2",
        conv_id, current_user_id);

    if (result.empty()) {
      SimpleError ret{.error = "Unauthorized access to conversation"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    auto transaction = co_await db->newTransactionCoro();
    try {
      auto insert_result = co_await transaction->execSqlCoro(
          "INSERT INTO messages (conversation_id, sender_id, content, "
          "message_type) "
          "VALUES ($1, $2, $3, $4) RETURNING id, created_at",
          conv_id, current_user_id, content, message_type);

      if (insert_result.empty()) {
        throw std::runtime_error("Initial message insert failed");
      }

      int message_id = insert_result[0]["id"].as<int>();
      std::string created_at = insert_result[0]["created_at"].as<std::string>();

      std::expected<std::vector<MediaQuickInfo>, std::string> processed_media;
      if (send_msg_req.media.has_value() && !send_msg_req.media->empty()) {
        std::size_t media_array_size = send_msg_req.media->size();
        processed_media = co_await process_media_attachments(
            std::move(*send_msg_req.media), transaction, current_user_id,
            "message", message_id);
        if (!processed_media.has_value() ||
            processed_media->size() < media_array_size) {
          LOG_ERROR << " Some Media info was not found";
          transaction->rollback();
          std::string error_string;
          for (const auto& media_item : *processed_media) {
            error_string += media_item.filename + ", ";
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

      std::string chat_topic = create_topic("chat", conversation_id);

      NotificationMessage msg{
          .type = "message_sent",
          .id = conversation_id,
          .message = message_type != "text"
                         ? std::format("Media shared: {}", content)
                         : content,
          .modified_at = created_at};

      ServiceManager::get_instance().get_publisher().publish(
          chat_topic, glz::write_json(msg).value_or(""));
      SendMessageResponse ret{
          .status = "success",
          .message_id = message_id,
          .created_at = created_at,
          .message_type = message_type,
          .media = processed_media->empty()
                       ? std::nullopt
                       : std::make_optional(*processed_media)};
      auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);

      co_return;
    } catch (const std::exception& e) {
      LOG_ERROR << "Failed to send message: " << e.what();
      transaction->rollback();
      SimpleError ret{.error =
                          std::format("Failed to send message: {}", e.what())};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
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

// Get conversation by offer ID
Task<> Chats::get_conversation_by_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string offer_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  auto offer_id_optional = convert::string_to_int(offer_id);
  if (!offer_id_optional || offer_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid offer ID"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int offer_id_int = offer_id_optional.value();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        offer_id_int);

    if (result.empty()) {
      SimpleError ret{.error = "Offer not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    if (convert::string_to_int(current_user_id).value() != offer_user_id &&
        convert::string_to_int(current_user_id).value() != post_user_id) {
      SimpleError ret{.error = "Unauthorized"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    auto conv_result = co_await db->execSqlCoro(
        "SELECT c.id FROM conversations c "
        "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id "
        "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
        "WHERE cp1.user_id = $1 AND cp2.user_id = $2 "
        "LIMIT 1",
        convert::string_to_int(current_user_id).value(),
        (convert::string_to_int(current_user_id).value() == offer_user_id
             ? post_user_id
             : offer_user_id));

    if (conv_result.empty()) {
      auto new_conv_result = co_await db->execSqlCoro(
          "INSERT INTO conversations (name) VALUES ($1) RETURNING id",
          "Offer #" + offer_id + " Conversation");

      if (new_conv_result.empty()) {
        SimpleError ret{.error = "Failed to create conversation"};
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
        co_return;
      }

      int conversation_id = new_conv_result[0]["id"].as<int>();

      try {
        co_await db->execSqlCoro(
            "INSERT INTO conversation_participants "
            "(conversation_id, user_id) VALUES ($1, $2), ($1, $3)",
            conversation_id, convert::string_to_int(current_user_id).value(),
            (convert::string_to_int(current_user_id).value() == offer_user_id
                 ? post_user_id
                 : offer_user_id));

        GetConversationByOfferResponse response{
            .status = "success",
            .conversation_id = conversation_id,
            .is_new = true};

        auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(response).value_or(""));
        callback(resp);
      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Database error adding participants: " << e.base().what();
        SimpleError ret{.error = "Database error"};
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
      }
    } else {
      GetConversationByOfferResponse response{
          .status = "success",
          .conversation_id = conv_result[0]["id"].as<int>(),
          .is_new = false};

      auto resp = HttpResponse::newHttpResponse(k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(response).value_or(""));
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

Task<> Chats::mark_messages_as_read(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string conversation_id) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto conv_id_optional = convert::string_to_int(conversation_id);
  if (!conv_id_optional || conv_id_optional.value() < 0) {
    SimpleError ret{.error = "Invalid conversation_id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }
  int conv_id = conv_id_optional.value();

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT 1 FROM conversation_participants WHERE conversation_id = $1 "
        "AND "
        "user_id = $2",
        conv_id, convert::string_to_int(user_id).value());

    if (result.empty()) {
      SimpleError ret{.error = "Unauthorized access to conversation"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    auto update_result = co_await db->execSqlCoro(
        "UPDATE messages SET is_read = true "
        "WHERE conversation_id = $1 AND sender_id != $2 AND is_read = false "
        "RETURNING id",
        conv_id, convert::string_to_int(user_id).value());

    MarkMessagesAsReadResponse ret{
        .status = "success", .messages_marked = (int)update_result.size()};
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

Task<> Chats::get_unread_count(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT COUNT(*) as unread_count "
        "FROM messages m "
        "JOIN conversation_participants cp ON m.conversation_id = "
        "cp.conversation_id "
        "WHERE cp.user_id = $1 AND m.sender_id != $1 AND m.is_read = false",
        convert::string_to_int(user_id).value());

    UnreadCountResponse ret{.unread_count =
                                result[0]["unread_count"].as<int>()};
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
