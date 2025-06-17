#ifndef PUB_MANAGER_HPP
#define PUB_MANAGER_HPP

#include <string>
#include <zmq.hpp>

class PubManager {
 public:
  PubManager() = delete;
  // Move only
  PubManager(const PubManager &) = delete;
  PubManager &operator=(const PubManager &) = delete;

  PubManager(zmq::context_t &context)
      : socket_(context, zmq::socket_type::pub) {
    socket_.bind("inproc://pubsub");
  }
  void publish(const std::string &topic, const std::string &message) {
    zmq::message_t topic_msg(topic);
    zmq::message_t data_msg(message);
    socket_.send(topic_msg, zmq::send_flags::sndmore);
    socket_.send(data_msg, zmq::send_flags::none);
  }

 private:
  zmq::socket_t socket_;
};

#endif  // PUB_MANAGER_HPP
