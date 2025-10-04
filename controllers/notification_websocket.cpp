#include "notification_websocket.hpp"

#include "../services/service_manager.hpp"
#include "../utilities/json_manipulation.hpp"
#include "common_req_n_resp.hpp"

using drogon::app;
using drogon::HttpRequestPtr;
using drogon::WebSocketConnectionPtr;
using drogon::WebSocketMessageType;

struct WelcomeMessage {
  std::string type;
  std::string message;
};

void NotificationWebSocket::handleNewMessage(
    const WebSocketConnectionPtr& wsConnPtr, std::string&& message,
    const WebSocketMessageType& type) {
  auto connId = wsConnPtr->getContext<std::string>();

  switch (type) {
    case WebSocketMessageType::Text:
      if (message.empty()) {
        LOG_WARN << "Received empty message from user " << *connId;
        SimpleError error{.error = "Received empty message"};
        wsConnPtr->send(glz::write_json(error).value_or(""));
      }
      // else if (message_needs_parsing) {
      //   // For incoming messages, if they need to be parsed, define a struct
      //   // like `IncomingWebSocketMessage` and use
      //   `utilities::strict_read_json`
      //   // Example:
      //   // IncomingWebSocketMessage incoming_msg;
      //   // auto parse_error = utilities::strict_read_json(incoming_msg,
      //   message);
      //   // if (parse_error) {
      //   //   SimpleError error{.error = "Invalid message format"};
      //   //   wsConnPtr->send(glz::write_json(error).value_or(""));
      //   //   return;
      //   // }
      //   // Process incoming_msg...
      // }
      break;
    case WebSocketMessageType::Binary:
      break;
    case WebSocketMessageType::Ping:
      wsConnPtr->send("", drogon::WebSocketMessageType::Pong);
      break;
    case WebSocketMessageType::Pong:
      wsConnPtr->send("", drogon::WebSocketMessageType::Ping);
      break;
    case WebSocketMessageType::Close:
      wsConnPtr->forceClose();
      return;
    default:
      LOG_WARN << "Received unknown message type";
  }
}

void NotificationWebSocket::handleNewConnection(
    const HttpRequestPtr& req, const WebSocketConnectionPtr& wsConnPtr) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  if (current_user_id.empty()) {
    LOG_ERROR << "No authenticated/registered user for WebSocket connection";
    wsConnPtr->forceClose();
    return;
  }

  std::shared_ptr<std::string> ptr =
      std::make_shared<std::string>(current_user_id);
  wsConnPtr->setContext(std::static_pointer_cast<void>(ptr));

  // Register WebSocket connection with Connection manager
  ServiceManager::get_instance().get_connection_manager().add_connection(
      current_user_id, wsConnPtr);

  // Subscribe user to their existing tag subscriptions
  try {
    subscribe_user_to_existing_subs(current_user_id);
  } catch (const std::exception& e) {
    LOG_INFO << "\n\n\nException in subscribing user to existing tags: "
             << e.what();
  }

  LOG_INFO << "WebSocket connected for user: " << current_user_id;

  WelcomeMessage welcome{.type = "connected",
                         .message = "Connected to notification service"};
  wsConnPtr->send(glz::write_json(welcome).value_or(""));
}

void NotificationWebSocket::handleConnectionClosed(
    const WebSocketConnectionPtr& wsConnPtr) {
  auto connId = wsConnPtr->getContext<std::string>();
  ServiceManager::get_instance().get_connection_manager().remove_connection(
      *connId, wsConnPtr);
  ServiceManager::get_instance().get_connection_manager().unsubscribe(*connId);
  LOG_INFO << "WebSocket disconnected for user: " << *connId;
}

void NotificationWebSocket::subscribe_user_to_existing_subs(
    std::string user_id) {
  try {
    auto db = app().getDbClient();
    db->execSqlAsync(
        "SELECT subscription FROM user_subscriptions WHERE user_id = $1",
        [user_id](const drogon::orm::Result& result) {
          for (const auto& row : result) {
            std::string channel = row["subscription"].as<std::string>();
            try {
              ServiceManager::get_instance().get_subscriber().subscribe(
                  channel);
              ServiceManager::get_instance().get_connection_manager().subscribe(
                  channel, user_id);
            } catch (const std::exception& e) {
              LOG_ERROR << "Failed to subscribe user to existing tags: "
                        << e.what();
            }
          }
        },
        [=](const drogon::orm::DrogonDbException& e) {
          LOG_ERROR << "Failed to get user tag subscriptions: "
                    << e.base().what() << "Subscription failed";
        },
        user_id);
    LOG_INFO << "user_id " << user_id << " subscribed to existing tags: ";
  }

  catch (...) {
    LOG_ERROR << "User Notification subscriptions failed";
  }
}
