#pragma once

#include <drogon/WebSocketController.h>

using namespace drogon;

class NotificationWebSocket
    : public drogon::WebSocketController<NotificationWebSocket> {
 public:
  void handleNewMessage(const WebSocketConnectionPtr& wsConnPtr,
                        std::string&& message,
                        const WebSocketMessageType& type) override;

  void handleNewConnection(const HttpRequestPtr& req,
                           const WebSocketConnectionPtr& wsConnPtr) override;

  void handleConnectionClosed(const WebSocketConnectionPtr& wsConnPtr) override;

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws/notifications", "WebSocketAuthMiddleware");
  WS_PATH_LIST_END

 private:
  void subscribe_user_to_existing_subs(std::string user_id);
};
