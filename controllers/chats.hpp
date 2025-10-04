#pragma once

#include <drogon/HttpController.h>

#include <string>

namespace api {

namespace v1 {
using drogon::Get;
using drogon::Options;
using drogon::Post;

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

  static drogon::Task<> get_conversations(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> create_conversation(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
  static drogon::Task<> get_messages(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string conversation_id);
  static drogon::Task<> send_message(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string conversation_id);
  static drogon::Task<> get_conversation_by_offer(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string offer_id);
  static drogon::Task<> mark_messages_as_read(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback,
      std::string conversation_id);
  static drogon::Task<> get_unread_count(
      drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr&)> callback);
};
}  // namespace v1
}  // namespace api
