#ifndef CONNECTION_MANAGER_HPP
#define CONNECTION_MANAGER_HPP

#include <drogon/WebSocketConnection.h>
#include <drogon/drogon.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

// enum class notification_type : uint8_t {}

// notification types
// post_created
// post_updated

// offer_created
// offer_updated
// offer_negotiated
// offer_accepted
// offer_rejected

// chat_created
// message_sent

inline std::string create_topic(const std::string &topic_type,
                                const std::string &topic_id) {
  return topic_type + ":" + topic_id;
}

// inline drogon::Task<> remove_user_subscription(std::string user_id, const
// std::string &topic) {
inline void remove_user_subscription(std::string user_id,
                                     const std::string &topic) {
  try {
    auto db = drogon::app().getDbClient();

    /*     co_await db->execSqlCoro(
            "DELETE FROM user_subscriptions "
            "WHERE user_id = $1 AND subscription = $2 "
            "RETURNING id",
            std::stoi(user_id), topic); */

    db->execSqlAsync(
        "DELETE FROM user_subscriptions "
        "WHERE user_id = $1 AND subscription = $2 "
        "RETURNING id",
        [](const drogon::orm::Result &) {
          LOG_INFO << "Subscription removed from database";
        },
        [](const drogon::orm::DrogonDbException &e) {
          LOG_ERROR << "Failed to remove subscription: " << e.base().what();
        },
        user_id, topic);
  } catch (...) {
    LOG_ERROR << "Failed to remove subscription\n";
  }
}

inline void store_user_subscription(const std::string &user_id,
                                    const std::string &topic) {
  try {
    auto db = drogon::app().getDbClient();

    db->execSqlAsync(
        "INSERT INTO user_subscriptions (user_id, subscription) "
        "VALUES ($1, $2) "
        "ON CONFLICT (user_id, subscription) DO NOTHING "
        "RETURNING id",
        [](const drogon::orm::Result &) {
          LOG_INFO << "Subscription stored in database";
        },
        [](const drogon::orm::DrogonDbException &e) {
          LOG_ERROR << "Failed to store subscription: " << e.base().what();
        },
        user_id, topic);
  } catch (...) {
    LOG_ERROR << "Failed to store subscription\n";
  }
}

inline void store_notification_in_db(std::string user_id,
                                     const std::string &message) {
  // Message should be a JSON object following the schema at minimum:
  // {
  //     "type": notification_type, see notification_type enum
  //     "id": Relevant id for this message, notifications related to a post -
  //     post_id,
  //           offers- offer_id, messages - message_id etc
  //     "message": message_content,
  //     "modified_at": timestamp (may not be used)
  // }

  // parse  message into json
  Json::Value notification;

  Json::CharReaderBuilder builder;
  std::string errors;
  std::istringstream data_stream(message);
  if (!Json::parseFromStream(builder, data_stream, &notification, &errors)) {
    LOG_ERROR << "Failed to parse message as JSON: " << errors;
    return;
  }

  try {
    auto db = drogon::app().getDbClient();
    if (notification.isMember("type") && notification.isMember("message") &&
        notification["type"].isString() && notification["message"].isString()) {
      db->execSqlAsync(
          "INSERT INTO notifications (user_id, type, message) VALUES ($1, $2, "
          "$3)",
          [](const drogon::orm::Result &) {
            LOG_INFO << "Notification stored in database";
          },
          [](const drogon::orm::DrogonDbException &e) {
            LOG_ERROR << "Failed to store notification: " << e.base().what();
          },
          user_id, notification["type"].asString(),
          notification["message"].asString());
    } else {
      LOG_ERROR << "Invalid notification format, saving failed";
    }
  } catch (...) {
    LOG_ERROR << "Failed to store notification\n";
  }
}

/**
 * @brief Manages WebSocket connections, subscriptions, and message broadcasting
 *
 * This class provides thread-safe methods for:
 * - Adding and removing WebSocket connections
 * - Subscribing and unsubscribing connections to topics
 * - Broadcasting messages to subscribed connections
 *
 * Uses thread-safe mechanisms like mutex locks to prevent race conditions
 * when manipulating connection and subscriber data structures.
 *
 * Current Design: Multiple connections per user
 * Improvements: Create limit for number of concurrent connection or switch to
 * Alternative design
 *
 * Alternative Design:
 * One connection per user, relegation older connections:
 * To support this in the current code, always broadcast from the front.
 * Then on removal of the main connection,
 * the second newest connection automatically becomes main connection.
 */
class ConnectionManager {
 public:
  void add_connection(const std::string conn_id,
                      const drogon::WebSocketConnectionPtr &conn) {
    std::lock_guard<std::mutex> lock(mutex_);

    // multiple connections per user
    connections_[conn_id].emplace_front(conn);  // make it the main connection
  }
  void remove_connection(const std::string conn_id,
                         const drogon::WebSocketConnectionPtr &conn) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = connections_.find(conn_id);
    if (it != connections_.end() && !it->second.empty()) {
      connections_[conn_id].remove(conn);
    }
  }
  void subscribe(const std::string &topic, const std::string conn_id) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      subscribers_[topic].insert(conn_id);
    } catch (const std::system_error &e) {
      LOG_ERROR << "Lock acquisition failed, topic subscription failed: "
                << e.what();
    } catch (const std::bad_alloc &e) {
      LOG_ERROR << "Out of Memory, no further subscriptions: " << e.what();
    } catch (const std::exception &e) {
      LOG_ERROR << "Topic Subscription failed: " << e.what();
    }

    // // search before insertion to prevent crashes.
    // // For debugging - can be removed in prod
    // LOG_INFO << "Content of topic " << topic << " , Connection ID: " <<
    // conn_id
    //          << "\n";
    // auto it = connections_.find(conn_id);
    // if (it != connections_.end()) {
    //   // send to only the main (newest) connection
    //   it->second.front()->send("Subscribed to topic: " + topic);
    // }
  }
  void unsubscribe(const std::string &conn_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[topic, ids] : subscribers_) {
      if (connections_[conn_id].empty()) {
        ids.erase(conn_id);
        connections_.erase(conn_id);
      }
    }
  }

  void unsubscribe_user_from_topic(const std::string &conn_id,
                                   std::string &topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[topic].erase(conn_id);
  }

  void broadcast(const std::string &topic, const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &conn_id : subscribers_[topic]) {
      auto it = connections_.find(conn_id);
      if (it != connections_.end()) {
        // store notification in DB
        store_notification_in_db(conn_id, message);
        // send notification
        for (auto &conn : it->second) {
          conn->send(message);
        }
      }
    }
  }

 private:
  std::unordered_map<std::string, std::list<drogon::WebSocketConnectionPtr>>
      connections_;
  std::unordered_map<std::string, std::unordered_set<std::string>> subscribers_;
  std::mutex mutex_;
};

#endif  // CONNECTION_MANAGER_HPP
