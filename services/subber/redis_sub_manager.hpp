#ifndef REDIS_SUB_MANAGER_HPP
#define REDIS_SUB_MANAGER_HPP

#include <sw/redis++/redis++.h>

#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "connection_manager.hpp"

class SubManager {
 public:
  SubManager() = delete;
  SubManager(std::unique_ptr<sw::redis::Subscriber> subscriber,
             ConnectionManager &manager)
      : conn_mgr_(manager), subscriber_(std::move(subscriber)) {
    subscriber_->on_message(
        [this](const std::string &channel, const std::string &message) {
          // LOG_INFO << "\n\n\n\nMessage Broadcast\n\n\n\n";
          conn_mgr_.broadcast(channel, message);
        });
  }
  SubManager(const SubManager &) = delete;
  SubManager &operator=(const SubManager &) = delete;

  void unsubscribe(const std::string &channel) {
    if (channel.empty()) {
      subscriber_->unsubscribe();
    } else
      subscriber_->unsubscribe(channel);
  }

  void subscribe(const std::string &topic) {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      subscriber_->subscribe(topic);
    } catch (const std::exception &e) {
      LOG_ERROR << "Redis topic subscription failed: " << e.what();
      return;
    }
  }
  void run();

  void stop() {
    if (subscriber_) {
      // subscriber_->unsubscribe();
    }
    if (sub_thread_.joinable()) {
      sub_thread_.request_stop();
      sub_thread_.join();
    }
  }

  ~SubManager() { stop(); }

 private:
  std::unique_ptr<sw::redis::Subscriber>
      subscriber_;  // To consider AsyncSubscriber
  ConnectionManager &conn_mgr_;
  std::jthread sub_thread_;
  std::mutex mutex_;
};

#endif  // REDIS_SUB_MANAGER_HPP
