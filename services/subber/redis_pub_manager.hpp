#ifndef REDIS_PUB_MANAGER_HPP
#define REDIS_PUB_MANAGER_HPP

#include <sw/redis++/redis++.h>

#include <string>

class PubManager {
 public:
  PubManager() = delete;
  PubManager(const PubManager&) = delete;
  PubManager& operator=(const PubManager&) = delete;

  PubManager(const sw::redis::ConnectionOptions& conn_opts)
      : redis_(std::make_unique<sw::redis::Redis>(conn_opts)) {}
  PubManager(const std::string& uri)
      : redis_(std::make_unique<sw::redis::Redis>(uri)) {}

  std::unique_ptr<sw::redis::Subscriber> get_redis_subscriber() const {
    return std::make_unique<sw::redis::Subscriber>(redis_->subscriber());
  }

  void publish(const std::string& topic, const std::string& message) {
    redis_->publish(topic, message);
  }

 private:
  std::unique_ptr<sw::redis::Redis> redis_;
};

#endif  // REDIS_PUB_MANAGER_HPP
