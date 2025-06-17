#ifndef NOTIFICATION_WEBSOCKET_HPP
#define NOTIFICATION_WEBSOCKET_HPP

// #include "../utilities/uuid_generator.hpp"
#include "notification_websocket.hpp"

#include "../services/service_manager.hpp"

void NotificationWebSocket::handleNewMessage(
    const WebSocketConnectionPtr& wsConnPtr, std::string&& message,
    const WebSocketMessageType& type) {
  auto connId = wsConnPtr->getContext<std::string>();

  // do nothing for now
  // Sample code:
  //   if (message.starts_with("subscribe:")) {
  //     auto topic = message.substr(10);
  //     ServiceManager::get_instance().get_connection_manager().subscribe(topic,
  //                                                                       *connId);
  //     wsConnPtr->send("Subscribed to " + topic);
  //   } else if (message.starts_with("publish:")) {
  //     auto payload = message.substr(8);
  //     // Assume format: topic:message
  //     auto delim = payload.find(':');
  //     if (delim != std::string::npos) {
  //       auto topic = payload.substr(0, delim);
  //       auto content = payload.substr(delim + 1);
  //       // publisher->publish(topic, content);
  //       ServiceManager::get_instance().get_publisher().publish(topic,
  //       content);
  //     }
  //   }
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

  // Send welcome message
  Json::Value welcome;
  welcome["type"] = "connected";
  welcome["message"] = "Connected to notification service";
  Json::StreamWriterBuilder builder;
  wsConnPtr->send(Json::writeString(builder, welcome));
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

#endif  // NOTIFICATION_WEBSOCKET_HPP
