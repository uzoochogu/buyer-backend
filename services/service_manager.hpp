#ifndef SERVICE_MANAGER_HPP
#define SERVICE_MANAGER_HPP

#include <memory>
#include <zmq.hpp>

#include "./media_server/s3_service.hpp"
#include "./subber/connection_manager.hpp"
#include "./subber/pub_manager.hpp"
#include "./subber/sub_manager.hpp"

// Redis PubSub option: noticed instability with large number of subscriptions
// #include "./subber/redis_pub_manager.hpp"
// #include "./subber/redis_sub_manager.hpp"
namespace service {
inline constexpr std::string_view BUCKET_NAME = "media";
inline constexpr std::uint32_t MAX_MEDIA_SIZE = 5U;
}  // namespace service
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
  S3Service& get_s3_service() { return *s3_service_; }

  void initialize() {
    context_ = std::make_unique<zmq::context_t>(1);
    conn_mgr_ = std::make_unique<ConnectionManager>();
    publisher_ = std::make_unique<PubManager>(*context_);
    subscriber_ = std::make_unique<SubManager>(*context_, *conn_mgr_);

    // AWS SDK
    Aws::SDKOptions options;
    Aws::InitAPI(options);
    s3_service_ = std::make_unique<S3Service>();

    // // Redis PubSub option:
    // conn_mgr_ = std::make_unique<ConnectionManager>();
    // publisher_ = std::make_unique<PubManager>("tcp://127.0.0.1:6379");
    // // publisher_ = std::make_unique<PubManager>("tcp://127.0.0.1:6400"); //
    // // valkey option
    // subscriber_ = std::make_unique<SubManager>(
    //     publisher_->get_redis_subscriber(), *conn_mgr_);

    // Start subscriber
    subscriber_->run();
  }

  void shutdown() {
    if (subscriber_) {
      subscriber_->stop();  // Graceful shutdown
    }

    Aws::SDKOptions options;
    Aws::ShutdownAPI(options);
  }

 private:
  ServiceManager() = default;

  std::unique_ptr<zmq::context_t> context_;
  std::unique_ptr<ConnectionManager> conn_mgr_;
  std::unique_ptr<PubManager> publisher_;
  std::unique_ptr<SubManager> subscriber_;
  std::unique_ptr<S3Service> s3_service_;
};

#endif  // SERVICE_MANAGER_HPP
