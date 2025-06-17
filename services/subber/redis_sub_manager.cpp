// #include "redis_sub_manager.hpp"

// #include "../service_manager.hpp"

// void SubManager::run() {
//   sub_thread_ = std::jthread([this](std::stop_token stoken) {
//     subscriber_->unsubscribe();
//     subscriber_->subscribe("cc");
//     while (!stoken.stop_requested()) {
//       try {
//         subscriber_->subscribe("cc");  // subscription seems to be needed in
//                                        // this thread for consumption to work
//         subscriber_->consume();
//       } catch (const sw::redis::TimeoutError &e) {
//         // Try again.
//         auto _ = e.what();
//         continue;
//       } catch (const std::exception &e) {
//         LOG_ERROR << "\n\n\n\n\n\n\nRedis subscriber error: " << e.what();
//         // recreate subscriber
//         try {
//           subscriber_ = ServiceManager::get_instance()
//                             .get_publisher()
//                             .get_redis_subscriber();
//           subscriber_->on_message(
//               [this](const std::string &channel, const std::string &message)
//               {
//                 conn_mgr_.broadcast(channel, message);
//               });
//           subscriber_->subscribe("cc");
//           // attempt to restart thread
//           run();
//           LOG_INFO << "Recreated Redis Subscriber";
//           return;
//         } catch (const std::exception &e) {
//           LOG_INFO << "failed to recreate redis subscriber: " << e.what();
//         }
//       }
//     }
//   });
// }
