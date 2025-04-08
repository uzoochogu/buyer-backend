#pragma once

#include <drogon/HttpController.h>

using namespace drogon;

namespace api {

namespace v1 {

class Chats : public drogon::HttpController<Chats> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(Chats::get_chats, "/api/v1/chats", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(Chats::send_chat, "/api/v1/chats", Post, Options,
                "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  void get_chats(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);
  void send_chat(const HttpRequestPtr &req,
                 std::function<void(const HttpResponsePtr &)> &&callback);
};

}  // namespace v1
}  // namespace api
