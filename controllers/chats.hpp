#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {

namespace v1 {

class Chats : public drogon::HttpController<Chats> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Chats::get_conversations, "/api/v1/conversations", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Chats::create_conversation, "/api/v1/conversations", Post,
                Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Chats::get_messages,
                "/api/v1/conversations/{conversation_id}/messages", Get,
                Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Chats::send_message,
                "/api/v1/conversations/{conversation_id}/messages", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Chats::get_conversation_by_offer,
                "/api/v1/conversations/offer/{offer_id}", Get, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Chats::mark_messages_as_read,
                "/api/v1/conversations/{conversation_id}/read", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Chats::get_unread_count, "/api/v1/conversations/unread", Get,
                Options, "CorsMiddleware", "AuthMiddleware");

  METHOD_LIST_END

  Task<> get_conversations(
      HttpRequestPtr req,
      std::function<void(const HttpResponsePtr &)> callback);
  Task<> create_conversation(
      HttpRequestPtr req,
      std::function<void(const HttpResponsePtr &)> callback);
  Task<> get_messages(HttpRequestPtr req,
                      std::function<void(const HttpResponsePtr &)> callback,
                      std::string conversation_id);
  Task<> send_message(HttpRequestPtr req,
                      std::function<void(const HttpResponsePtr &)> callback,
                      std::string conversation_id);

  Task<> get_conversation_by_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback,
      std::string offer_id);

  // New chat functionality
  Task<> mark_messages_as_read(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback,
      std::string conversation_id);
  Task<> get_unread_count(
      HttpRequestPtr req,
      std::function<void(const HttpResponsePtr &)> callback);
};

}  // namespace v1
}  // namespace api
