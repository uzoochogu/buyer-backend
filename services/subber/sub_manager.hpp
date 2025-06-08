#ifndef SUB_MANAGER_HPP
#define SUB_MANAGER_HPP

#include <stop_token>
#include <thread>
#include <zmq.hpp>

#include "connection_manager.hpp"


class SubManager {
 public:
  SubManager(zmq::context_t &context, ConnectionManager &manager)
      : socket_(context, zmq::socket_type::sub), conn_mgr_(manager) {
    socket_.connect("inproc://pubsub");
  }
  void subscribe(const std::string &topic) {
    socket_.set(zmq::sockopt::subscribe, topic);
  }
  void run() {
    // // TODO: BENCHMARK which is best

    // sub_thread_ = std::jthread([this](std::stop_token stoken) {
    //     zmq::recv_result_t res;
    //     while (!stoken.stop_requested()) {
    //         zmq::message_t topic_msg;
    //         zmq::message_t data_msg;
    //         res = socket_.recv(topic_msg);
    //         if(!res.has_value()) {
    //             std::cout << "Error receiving message: " <<
    //             topic_msg.to_string() << std::endl;
    //         }
    //         res = socket_.recv(data_msg);
    //         if(!res.has_value()) {
    //             std::cout << "Error receiving message: " <<
    //             data_msg.to_string() << std::endl;
    //         }

    //         // std::string topic(static_cast<char*>(topic_msg.data()),
    //         topic_msg.size()); std::string topic = topic_msg.to_string();
    //         std::string message = data_msg.to_string();
    //         conn_mgr_.broadcast(topic, message);
    //     }
    // });

    // with a stop token, no blocking + timeout
    sub_thread_ = std::jthread([this](std::stop_token stoken) {
      zmq::recv_result_t res;
      while (!stoken.stop_requested()) {
        zmq::message_t topic_msg;
        zmq::message_t data_msg;
        res = socket_.recv(topic_msg, zmq::recv_flags::dontwait);
        if (!res.has_value()) {
          if (zmq_errno() == EAGAIN) {
            // No message available, wait for a short period before trying again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          } else {
            // Handle other errors
            std::cerr << "Error receiving a message";
          }
        }
        res = socket_.recv(data_msg, zmq::recv_flags::dontwait);
        if (!res.has_value()) {
          if (zmq_errno() == EAGAIN) {
            // No message available, wait for a short period before trying again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
          } else {
            // Handle other errors
            std::cerr << "Error receiving a message";
          }
        }

        // std::string topic(static_cast<char*>(topic_msg.data()),
        // topic_msg.size());
        std::string topic = topic_msg.to_string();
        std::string message = data_msg.to_string();
        conn_mgr_.broadcast(topic, message);
      }
    });

    // polling implementation
    // sub_thread_ = std::jthread([this](std::stop_token stoken) {
    //     zmq::recv_result_t res;
    //     zmq::pollitem_t items[] = {{socket_, 0, ZMQ_POLLIN, 0}};
    //     zmq::message_t topic_msg;
    //     zmq::message_t data_msg;
    //     while (!stoken.stop_requested()) {
    //         zmq::poll(items, 1, 10); // 10ms timeout
    //         if (items[0].revents & ZMQ_POLLIN) {
    //             // Message available, receive it
    //             res = socket_.recv(topic_msg);
    //             if (res.has_value()) {
    //                 // Process the message
    //                 res = socket_.recv(data_msg);
    //                 if (res.has_value()) {
    //                     std::string topic = topic_msg.to_string();
    //                     std::string message = data_msg.to_string();
    //                     conn_mgr_.broadcast(topic, message);
    //                 }
    //             }
    //         }
    //     }
    // });
  }

  void stop() {
    if (sub_thread_.joinable()) {
      sub_thread_.request_stop();
      sub_thread_.join();
    }
  }

  ~SubManager() { stop(); }

 private:
  zmq::socket_t socket_;
  ConnectionManager &conn_mgr_;
  std::jthread sub_thread_;
};

#endif  // SUB_MANAGER_HPP
