#include "authentication.hpp"

#include <argon2.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/ResultIterator.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/SqlBinder.h>

#ifndef JWT_DISABLE_PICOJSON
#define JWT_DISABLE_PICOJSON
#endif
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/open-source-parsers-jsoncpp/traits.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <span>
#include <sstream>

#include "../config/config.hpp"

#define ARGON2_HASH_LEN 32
#define ARGON2_SALT_LEN 16

using drogon::app;
using drogon::HttpResponse;
using drogon::k400BadRequest;
using drogon::k401Unauthorized;
using drogon::k500InternalServerError;
using drogon::orm::DrogonDbException;

using api::v1::Authentication;

// Helper function to generate a random string for tokens
std::string generate_random_string(size_t length) {
  const std::string chars =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<> distribution(
      0, static_cast<int>(chars.size() - 1));

  std::string random_string;
  for (size_t i = 0; i < length; ++i) {
    random_string += chars[distribution(generator)];
  }
  return random_string;
}

// Helper function for base64 encoding
std::string base64_encode(std::span<const uint8_t> data) {
  static const char* encoding_table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t length = data.size();
  std::string encoded;
  encoded.reserve(4 * ((length + 2) / 3));

  for (size_t i = 0; i < length; i += 3) {
    uint32_t octet_a = i < length ? data[i] : 0;
    uint32_t octet_b = i + 1 < length ? data[i + 1] : 0;
    uint32_t octet_c = i + 2 < length ? data[i + 2] : 0;

    uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

    encoded.push_back(encoding_table[(triple >> 18) & 0x3F]);
    encoded.push_back(encoding_table[(triple >> 12) & 0x3F]);
    encoded.push_back(encoding_table[(triple >> 6) & 0x3F]);
    encoded.push_back(encoding_table[triple & 0x3F]);
  }

  // Add padding
  size_t mod = length % 3;
  if (mod) {
    encoded[encoded.size() - 1] = '=';
    if (mod == 1) {
      encoded[encoded.size() - 2] = '=';
    }
  }

  return encoded;
}

// Helper function to generate JWT token
std::string generate_jwt(int user_id, const std::string& username) {
  const std::string secret = config::JWT_SECRET;

  // Current time and expiration time (1 hour from now)
  auto now = std::chrono::system_clock::now();
  auto exp = now + std::chrono::hours(1);

  using traits = jwt::traits::open_source_parsers_jsoncpp;

  // Create JWT token
  auto token =
      jwt::create<traits>()
          .set_issuer("buyer-app")
          .set_issued_at(now)
          .set_expires_at(exp)
          .set_payload_claim("user_id",
                             jwt::basic_claim<traits>(std::to_string(user_id)))
          .set_payload_claim("username", jwt::basic_claim<traits>(username))
          .sign(jwt::algorithm::hs256{secret});

  return token;
}

// Helper function to generate refresh token
std::string generate_refresh_token() { return generate_random_string(64); }

// Helper function to hash password with Argon2
std::string hash_password_with_argon2(const std::string& password) {
  // Generate a random salt
  uint8_t salt[ARGON2_SALT_LEN];
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<short> distribution(0, 255);

  for (size_t i = 0; i < ARGON2_SALT_LEN; ++i) {
    salt[i] = static_cast<uint8_t>(distribution(generator));
  }

  // Configure Argon2 parameters
  uint32_t t_cost = 3;        // Number of iterations
  uint32_t m_cost = 1 << 16;  // 64 MiB memory cost
  uint32_t parallelism = 1;   // Number of threads

  // Hash the password
  size_t hash_size =
      argon2_encodedlen(t_cost, m_cost, parallelism, ARGON2_SALT_LEN,
                        ARGON2_HASH_LEN, Argon2_type::Argon2_id);
  std::string encoded_hash(hash_size, '\0');

  int result = argon2id_hash_encoded(t_cost, m_cost, parallelism,
                                     password.c_str(), password.length(), salt,
                                     ARGON2_SALT_LEN, ARGON2_HASH_LEN,
                                     encoded_hash.data(), encoded_hash.size());

  if (result != ARGON2_OK) {
    LOG_ERROR << "Failed to hash password: " << argon2_error_message(result);
    throw std::runtime_error("Failed to hash password");
  }

  return encoded_hash;
}

// Helper function to verify password with Argon2
bool verify_password_with_argon2(const std::string& password,
                                 const std::string& hash) {
  // Format: $argon2id$v=19$m=65536,t=3,p=1$<salt>$<hash>
  // Parses and verifies parses encoded hash
  int result =
      argon2id_verify(hash.c_str(), password.c_str(), password.length());

  return result == ARGON2_OK;
}

drogon::Task<> Authentication::login(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto json = req->getJsonObject();
  std::string username = (*json)["username"].asString();
  std::string password = (*json)["password"].asString();

  // for debugging hashes
  // LOG_INFO << "Password hash:" << hash_password_with_argon2(password) <<
  // std::endl;

  auto db = app().getDbClient();
  try {
    auto result = co_await db->execSqlCoro(
        "SELECT id, username, password_hash FROM users WHERE username = $1",
        username);

    if (!result.empty()) {
      const auto& row = result[0];
      int user_id = row["id"].as<int>();
      std::string stored_hash = row["password_hash"].as<std::string>();

      // Verify password using Argon2
      bool password_match = verify_password_with_argon2(password, stored_hash);

      if (!password_match) {
        // Password doesn't match
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "Invalid username or password";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
      }

      // Generate JWT token
      std::string token = generate_jwt(user_id, username);

      // Generate refresh token
      std::string refresh_token = generate_refresh_token();

      // Store refresh token in database
      auto expiry = std::chrono::system_clock::now() +
                    std::chrono::hours(24 * 7);  // 1 week
      auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

      try {
        co_await db->execSqlCoro(
            "INSERT INTO user_sessions (user_id, token, refresh_token, "
            "expires_at) VALUES ($1, $2, $3, to_timestamp($4)) "
            "ON CONFLICT (token) DO NOTHING",
            user_id, token, refresh_token, static_cast<double>(expiry_time));

        // Return success with tokens
        Json::Value ret;
        ret["status"] = "success";
        ret["token"] = token;
        ret["refresh_token"] = refresh_token;
        ret["user_id"] = user_id;
        ret["username"] = username;

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Failed to store session: " << e.base().what();
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "An error occurred";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      }
    } else {
      // User not found
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "Invalid username or password";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k401Unauthorized);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "An error occurred";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

drogon::Task<> Authentication::logout(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  // Get the token from the Authorization header
  const std::string& auth_header = req->getHeader("Authorization");

  if (!auth_header.empty() && auth_header.substr(0, 7) == "Bearer ") {
    std::string token = auth_header.substr(7);

    // Remove the session from the database
    auto db = app().getDbClient();
    try {
      co_await db->execSqlCoro("DELETE FROM user_sessions WHERE token = $1",
                               token);

      Json::Value ret;
      ret["status"] = "success";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "An error occurred";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } else {
    // No token provided
    Json::Value ret;
    ret["status"] = "success";  // Still return success as the user is
                                // effectively logged out
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  }

  co_return;
}

drogon::Task<> Authentication::refresh(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto json = req->getJsonObject();
  std::string refresh_token = (*json)["refresh_token"].asString();

  if (refresh_token.empty()) {
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "Refresh token is required";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();
  try {
    auto result = co_await db->execSqlCoro(
        "SELECT user_id, expires_at FROM user_sessions WHERE refresh_token ="
        "$1",
        refresh_token);

    if (result.empty()) {
      // Refresh token not found
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "Invalid refresh token";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k401Unauthorized);
      callback(resp);
      co_return;
    }
    const auto& row = result[0];
    int user_id = row["user_id"].as<int>();

    // Check if refresh token has expired
    auto expiry_str = row["expires_at"].as<std::string>();

    // Parse timestamp from PostgreSQL format (e.g., "2025-04-25 12:34:56")
    std::chrono::system_clock::time_point expiry_time_point;
    std::istringstream ss(expiry_str);
    // std::tm tm = {};
    // ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    ss >> std::chrono::parse("%Y-%m-%d %H:%M:%S", expiry_time_point);
    if (ss.fail()) {
      LOG_ERROR << "Failed to parse expiry time: " << expiry_str;
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "An error occurred";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    // auto old_expiry_time = std::mktime(&tm); // to time_t to compare
    // auto now = std::chrono::system_clock::to_time_t(
    //    std::chrono::system_clock::now());

    auto now = std::chrono::system_clock::now();

    // Check if token has expired
    if (expiry_time_point < now) {
      LOG_INFO << "Refresh token has expired";
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "Refresh token has expired";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k401Unauthorized);
      callback(resp);
      co_return;
    }

    // Token is still valid, proceed with refresh
    // Get username for the token
    try {
      auto user_result = co_await db->execSqlCoro(
          "SELECT username FROM users WHERE id = $1", user_id);

      if (user_result.empty()) {
        // User not found
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "Invalid refresh token";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k401Unauthorized);
        callback(resp);
        co_return;
      }
      std::string username = user_result[0]["username"].as<std::string>();

      // Generate new JWT token
      std::string new_token = generate_jwt(user_id, username);

      // Generate new refresh token
      std::string new_refresh_token = generate_refresh_token();

      // Update session in database
      auto expiry = std::chrono::system_clock::now() +
                    std::chrono::hours(24 * 7);  // 1 week
      auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

      try {
        co_await db->execSqlCoro(
            "UPDATE user_sessions SET token = $1, refresh_token = "
            "$2, expires_at = to_timestamp($3) WHERE refresh_token = $4",
            new_token, new_refresh_token, static_cast<double>(expiry_time),
            refresh_token);

        // Return success with new tokens
        Json::Value ret;
        ret["status"] = "success";
        ret["token"] = new_token;
        ret["refresh_token"] = new_refresh_token;
        ret["user_id"] = user_id;
        ret["username"] = username;

        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Failed to update session: " << e.base().what();
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "An error occurred";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      }

    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "An error occurred";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }

  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "An error occurred";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

drogon::Task<> Authentication::register_user(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto json = req->getJsonObject();
  std::string username = (*json)["username"].asString();
  std::string email = (*json)["email"].asString();
  std::string password = (*json)["password"].asString();

  // Validate input
  if (username.empty() || email.empty() || password.empty()) {
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "Username, email, and password are required";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  // Hash password with Argon2
  std::string password_hash;
  try {
    password_hash = hash_password_with_argon2(password);
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to hash password: " << e.what();
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "An error occurred during registration";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // Check if username or email already exists
    auto result = co_await db->execSqlCoro(
        "SELECT id FROM users WHERE username = $1 OR email = $2", username,
        email);

    if (!result.empty()) {
      // Username or email already exists
      Json::Value ret;
      ret["status"] = "failure";
      ret["message"] = "Username or email already exists";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      resp->setStatusCode(drogon::k409Conflict);
      callback(resp);
      co_return;
    } else {
      // Create new user
      try {
        auto insert_result = co_await db->execSqlCoro(
            "INSERT INTO users (username, email, password_hash) VALUES ($1, "
            "$2, $3) RETURNING id",
            username, email, password_hash);

        if (insert_result.empty()) {
          // Failed to insert user
          Json::Value ret;
          ret["status"] = "failure";
          ret["message"] = "Failed to create user";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
          co_return;
        }
        int user_id = insert_result[0]["id"].as<int>();

        // Generate JWT token
        std::string token = generate_jwt(user_id, username);

        // Generate refresh token
        std::string refresh_token = generate_refresh_token();
        // Store refresh token in database
        auto expiry = std::chrono::system_clock::now() +
                      std::chrono::hours(24 * 7);  // 1 week
        auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

        try {
          co_await db->execSqlCoro(
              "INSERT INTO user_sessions (user_id, token, refresh_token, "
              "expires_at) VALUES ($1, $2, $3, to_timestamp($4)) "
              "ON CONFLICT (token) DO NOTHING",
              user_id, token, refresh_token, static_cast<double>(expiry_time));

          // Return success with tokens
          Json::Value ret;
          ret["status"] = "success";
          ret["token"] = token;
          ret["refresh_token"] = refresh_token;
          ret["user_id"] = user_id;
          ret["username"] = username;

          auto resp = HttpResponse::newHttpJsonResponse(ret);
          callback(resp);
        } catch (const DrogonDbException& e) {
          LOG_ERROR << "Failed to store session: " << e.base().what();
          Json::Value ret;
          ret["status"] = "failure";
          ret["message"] = "Registration successful, but failed to log in";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
        }

      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "An error occurred during registration";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      }
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "An error occurred during registration";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}
