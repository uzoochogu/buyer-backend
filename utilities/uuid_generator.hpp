#ifndef UUID_GENERATOR_HPP
#define UUID_GENERATOR_HPP

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

class UuidGenerator {
 public:
  static std::string generate_uuid() {
    boost::uuids::random_generator generator;
    boost::uuids::uuid uuid = generator();
    return boost::uuids::to_string(uuid);
  }
};

#endif  // UUID_GENERATOR_HPP
