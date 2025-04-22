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
  METHOD_LIST_END

  void get_conversations(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void create_conversation(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback);
  void get_messages(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback,
                    const std::string &conversation_id);
  void send_message(const HttpRequestPtr &req,
                    std::function<void(const HttpResponsePtr &)> &&callback,
                    const std::string &conversation_id);

  void get_conversation_by_offer(
      const HttpRequestPtr &req,
      std::function<void(const HttpResponsePtr &)> &&callback,
      const std::string &offer_id);
};

}  // namespace v1
}  // namespace api
