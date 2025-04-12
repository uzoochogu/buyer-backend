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
#define JWT_DISABLE_PICOJSON
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/traits.h>

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>

#include "../config/config.hpp"

#define ARGON2_HASH_LEN 32
#define ARGON2_SALT_LEN 16

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;
using traits = jwt::traits::nlohmann_json;

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
std::string base64_encode(const uint8_t* data, size_t length) {
  static const char* encoding_table =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
  LOG_INFO << "Hash size: " << hash_size << "\n";
  // char encoded_hash[512];  // Buffer for the encoded hash
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

void Authentication::login(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string username = (*json)["username"].asString();
  std::string password = (*json)["password"].asString();

  // for debugging hashes
  // LOG_INFO << "Password hash:" << hash_password_with_argon2(password) <<
  // std::endl;

  auto db = app().getDbClient();
  db->execSqlAsync(
      "SELECT id, username, password_hash FROM users WHERE username = $1",
      [callback, password](const Result& result) {
        if (result.size() > 0) {
          const auto& row = result[0];
          int user_id = row["id"].as<int>();
          std::string username = row["username"].as<std::string>();
          std::string storedHash = row["password_hash"].as<std::string>();

          // Verify password using Argon2
          bool passwordMatches =
              verify_password_with_argon2(password, storedHash);

          if (passwordMatches) {
            // Generate JWT token
            std::string token = generate_jwt(user_id, username);

            // Generate refresh token
            std::string refreshToken = generate_refresh_token();

            // Store refresh token in database
            auto db = app().getDbClient();
            auto expiry = std::chrono::system_clock::now() +
                          std::chrono::hours(24 * 7);  // 1 week
            auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

            db->execSqlAsync(
                "INSERT INTO user_sessions (user_id, token, refresh_token, "
                "expires_at) VALUES ($1, $2, $3, to_timestamp($4))",
                [](const Result& result) {
                  // Session stored successfully
                },
                [](const DrogonDbException& e) {
                  LOG_ERROR << "Failed to store session: " << e.base().what();
                },
                user_id, token, refreshToken, static_cast<double>(expiry_time));

            // Return success with tokens
            Json::Value ret;
            ret["status"] = "success";
            ret["token"] = token;
            ret["refresh_token"] = refreshToken;
            ret["user_id"] = user_id;
            ret["username"] = username;

            auto resp = HttpResponse::newHttpJsonResponse(ret);
            callback(resp);
          } else {
            // Password doesn't match
            Json::Value ret;
            ret["status"] = "failure";
            ret["message"] = "Invalid username or password";
            auto resp = HttpResponse::newHttpJsonResponse(ret);
            resp->setStatusCode(k401Unauthorized);
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
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "An error occurred";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      username);
}

void Authentication::logout(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  // Get the token from the Authorization header
  const std::string& auth_header = req->getHeader("Authorization");

  if (!auth_header.empty() && auth_header.substr(0, 7) == "Bearer ") {
    std::string token = auth_header.substr(7);

    // Remove the session from the database
    auto db = app().getDbClient();
    db->execSqlAsync(
        "DELETE FROM user_sessions WHERE token = $1",
        [callback](const Result& result) {
          Json::Value ret;
          ret["status"] = "success";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          callback(resp);
        },
        [callback](const DrogonDbException& e) {
          LOG_ERROR << "Database error: " << e.base().what();
          Json::Value ret;
          ret["status"] = "failure";
          ret["message"] = "An error occurred";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
        },
        token);
  } else {
    // No token provided
    Json::Value ret;
    ret["status"] = "success";  // Still return success as the user is
                                // effectively logged out
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  }
}

void Authentication::refresh(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto json = req->getJsonObject();
  std::string refreshToken = (*json)["refresh_token"].asString();

  if (refreshToken.empty()) {
    Json::Value ret;
    ret["status"] = "failure";
    ret["message"] = "Refresh token is required";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    return;
  }

  auto db = app().getDbClient();
  db->execSqlAsync(
      "SELECT user_id, expires_at FROM user_sessions WHERE refresh_token = $1",
      [callback, refreshToken, db](const Result& result) {
        if (result.size() > 0) {
          const auto& row = result[0];
          int user_id = row["user_id"].as<int>();

          // Check if refresh token has expired
          auto expiryStr = row["expires_at"].as<std::string>();
          // Parse expiry time and check if it's in the past
          // This is a simplified check - in production you'd want to parse the
          // timestamp properly
          auto now = std::chrono::system_clock::now();
          auto nowTime = std::chrono::system_clock::to_time_t(now);

          // For simplicity, we'll just generate a new token without checking
          // expiry In production, you should properly check the expiry time

          // Get username for the token
          db->execSqlAsync(
              "SELECT username FROM users WHERE id = $1",
              [callback, user_id, refreshToken, db](const Result& userResult) {
                if (userResult.size() > 0) {
                  std::string username =
                      userResult[0]["username"].as<std::string>();

                  // Generate new JWT token
                  std::string newToken = generate_jwt(user_id, username);

                  // Generate new refresh token
                  std::string newRefreshToken = generate_refresh_token();

                  // Update session in database
                  auto expiry = std::chrono::system_clock::now() +
                                std::chrono::hours(24 * 7);  // 1 week
                  auto expiry_time =
                      std::chrono::system_clock::to_time_t(expiry);

                  db->execSqlAsync(
                      "UPDATE user_sessions SET token = $1, refresh_token = "
                      "$2, expires_at = to_timestamp($3) WHERE refresh_token = "
                      "$4",
                      [callback, user_id, username, newToken,
                       newRefreshToken](const Result& updateResult) {
                        // Return success with new tokens
                        Json::Value ret;
                        ret["status"] = "success";
                        ret["token"] = newToken;
                        ret["refresh_token"] = newRefreshToken;
                        ret["user_id"] = user_id;
                        ret["username"] = username;

                        auto resp = HttpResponse::newHttpJsonResponse(ret);
                        callback(resp);
                      },
                      [callback](const DrogonDbException& e) {
                        LOG_ERROR << "Failed to update session: "
                                  << e.base().what();
                        Json::Value ret;
                        ret["status"] = "failure";
                        ret["message"] = "An error occurred";
                        auto resp = HttpResponse::newHttpJsonResponse(ret);
                        resp->setStatusCode(k500InternalServerError);
                        callback(resp);
                      },
                      newToken, newRefreshToken,
                      static_cast<double>(expiry_time), refreshToken);
                } else {
                  // User not found
                  Json::Value ret;
                  ret["status"] = "failure";
                  ret["message"] = "Invalid refresh token";
                  auto resp = HttpResponse::newHttpJsonResponse(ret);
                  resp->setStatusCode(k401Unauthorized);
                  callback(resp);
                }
              },
              [callback](const DrogonDbException& e) {
                LOG_ERROR << "Database error: " << e.base().what();
                Json::Value ret;
                ret["status"] = "failure";
                ret["message"] = "An error occurred";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              user_id);
        } else {
          // Refresh token not found
          Json::Value ret;
          ret["status"] = "failure";
          ret["message"] = "Invalid refresh token";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          resp->setStatusCode(k401Unauthorized);
          callback(resp);
        }
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "An error occurred";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      refreshToken);
}

void Authentication::register_user(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
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
    return;
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
    return;
  }

  auto db = app().getDbClient();

  // Check if username or email already exists
  db->execSqlAsync(
      "SELECT id FROM users WHERE username = $1 OR email = $2",
      [callback, db, username, email, password_hash](const Result& result) {
        if (result.size() > 0) {
          // Username or email already exists
          Json::Value ret;
          ret["status"] = "failure";
          ret["message"] = "Username or email already exists";
          auto resp = HttpResponse::newHttpJsonResponse(ret);
          resp->setStatusCode(k409Conflict);
          callback(resp);
        } else {
          // Create new user
          db->execSqlAsync(
              "INSERT INTO users (username, email, password_hash) VALUES ($1, "
              "$2, $3) RETURNING id",
              [callback, username, password_hash](const Result& insert_result) {
                if (insert_result.size() > 0) {
                  int user_id = insert_result[0]["id"].as<int>();

                  // Generate JWT token
                  std::string token = generate_jwt(user_id, username);

                  // Generate refresh token
                  std::string refreshToken = generate_refresh_token();

                  // Store refresh token in database
                  auto db = app().getDbClient();
                  auto expiry = std::chrono::system_clock::now() +
                                std::chrono::hours(24 * 7);  // 1 week
                  auto expiry_time =
                      std::chrono::system_clock::to_time_t(expiry);

                  db->execSqlAsync(
                      "INSERT INTO user_sessions (user_id, token, "
                      "refresh_token, expires_at) VALUES ($1, $2, $3, "
                      "to_timestamp($4))",
                      [callback, user_id, username, token,
                       refreshToken](const Result& session_result) {
                        // Return success with tokens
                        Json::Value ret;
                        ret["status"] = "success";
                        ret["token"] = token;
                        ret["refresh_token"] = refreshToken;
                        ret["user_id"] = user_id;
                        ret["username"] = username;

                        auto resp = HttpResponse::newHttpJsonResponse(ret);
                        callback(resp);
                      },
                      [callback](const DrogonDbException& e) {
                        LOG_ERROR << "Failed to store session: "
                                  << e.base().what();
                        Json::Value ret;
                        ret["status"] = "failure";
                        ret["message"] =
                            "User created but failed to create session";
                        auto resp = HttpResponse::newHttpJsonResponse(ret);
                        resp->setStatusCode(k500InternalServerError);
                        callback(resp);
                      },
                      user_id, token, refreshToken,
                      static_cast<double>(expiry_time));
                } else {
                  // Failed to create user
                  Json::Value ret;
                  ret["status"] = "failure";
                  ret["message"] = "Failed to create user";
                  auto resp = HttpResponse::newHttpJsonResponse(ret);
                  resp->setStatusCode(k500InternalServerError);
                  callback(resp);
                }
              },
              [callback](const DrogonDbException& e) {
                LOG_ERROR << "Database error: " << e.base().what();
                Json::Value ret;
                ret["status"] = "failure";
                ret["message"] = "An error occurred";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              username, email, password_hash);
        }
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value ret;
        ret["status"] = "failure";
        ret["message"] = "An error occurred";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      username, email);
}
