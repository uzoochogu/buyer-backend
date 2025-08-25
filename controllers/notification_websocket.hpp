#ifndef NOTIFICATION_WEBSOCKET_HPP
#define NOTIFICATION_WEBSOCKET_HPP

#include <drogon/WebSocketController.h>

class NotificationWebSocket
    : public drogon::WebSocketController<NotificationWebSocket> {
 public:
  void handleNewMessage(const drogon::WebSocketConnectionPtr& wsConnPtr,
                        std::string&& message,
                        const drogon::WebSocketMessageType& type) override;

  void handleNewConnection(
      const drogon::HttpRequestPtr& req,
      const drogon::WebSocketConnectionPtr& wsConnPtr) override;

  void handleConnectionClosed(
      const drogon::WebSocketConnectionPtr& wsConnPtr) override;

  WS_PATH_LIST_BEGIN
  WS_PATH_ADD("/ws/notifications", "WebSocketAuthMiddleware");
  WS_PATH_LIST_END

 private:
  static void subscribe_user_to_existing_subs(std::string user_id);
};

#endif  // NOTIFICATION_WEBSOCKET_HPP
