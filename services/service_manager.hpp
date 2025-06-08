#ifndef SERVICE_MANAGER_HPP
#define SERVICE_MANAGER_HPP

#include <memory>
#include <zmq.hpp>

#include "./subber/connection_manager.hpp"
#include "./subber/pub_manager.hpp"
#include "./subber/sub_manager.hpp"


class ServiceManager {
 public:
  static ServiceManager& get_instance() {
    static ServiceManager instance;
    return instance;
  }

  // non-copyable, move-only
  ServiceManager(const ServiceManager&) = delete;
  ServiceManager& operator=(const ServiceManager&) = delete;

  PubManager& get_publisher() { return *publisher_; }
  SubManager& get_subscriber() { return *subscriber_; }
  ConnectionManager& get_connection_manager() { return *conn_mgr_; }

  void initialize() {
    context_ = std::make_unique<zmq::context_t>(1);
    conn_mgr_ = std::make_unique<ConnectionManager>();
    publisher_ = std::make_unique<PubManager>(*context_);
    subscriber_ = std::make_unique<SubManager>(*context_, *conn_mgr_);

    // Start subscriber
    subscriber_->run();
  }

  void shutdown() {
    if (subscriber_) {
      subscriber_->stop();  // Graceful shutdown
    }
  }

 private:
  ServiceManager() = default;

  std::unique_ptr<zmq::context_t> context_;
  std::unique_ptr<ConnectionManager> conn_mgr_;
  std::unique_ptr<PubManager> publisher_;
  std::unique_ptr<SubManager> subscriber_;
};

#endif  // SERVICE_MANAGER_HPP
