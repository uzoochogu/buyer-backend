#include "authentication.hpp"

#include <argon2.h>
#include <drogon/HttpResponse.h>

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
#include "../utilities/json_manipulation.hpp"
#include "../utilities/validation.hpp"
#include "common_req_n_resp.hpp"

#define ARGON2_HASH_LEN 32
#define ARGON2_SALT_LEN 16

using drogon::app;
using drogon::CT_APPLICATION_JSON;
using drogon::HttpResponse;
using drogon::k400BadRequest;
using drogon::k401Unauthorized;
using drogon::k500InternalServerError;
using drogon::orm::DrogonDbException;

using api::v1::Authentication;

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

std::string generate_jwt(int user_id, const std::string& username) {
  const std::string secret = config::JWT_SECRET;

  auto now = std::chrono::system_clock::now();
  auto exp = now + std::chrono::hours(1);

  using traits = jwt::traits::open_source_parsers_jsoncpp;

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

std::string generate_refresh_token() { return generate_random_string(64); }

std::string hash_password_with_argon2(const std::string& password) {
  uint8_t salt[ARGON2_SALT_LEN];
  std::random_device rd;
  std::mt19937 generator(rd());
  std::uniform_int_distribution<short> distribution(0, 255);

  for (size_t i = 0; i < ARGON2_SALT_LEN; ++i) {
    salt[i] = static_cast<uint8_t>(distribution(generator));
  }

  // Argon2 parameters
  uint32_t t_cost = 3;        // Number of iterations
  uint32_t m_cost = 1 << 16;  // 64 MiB memory cost
  uint32_t parallelism = 1;   // Number of threads

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

bool verify_password_with_argon2(const std::string& password,
                                 const std::string& hash) {
  // Format: $argon2id$v=19$m=65536,t=3,p=1$<salt>$<hash>
  // Parses and verifies parses encoded hash
  int result =
      argon2id_verify(hash.c_str(), password.c_str(), password.length());

  return result == ARGON2_OK;
}

struct LoginCredentials {
  std::string username;
  std::string password;
};

struct CredentialsResponse {
  std::string status;
  std::string token;
  std::string refresh_token;
  int user_id;
  std::string username;
};

struct RefreshRequest {
  std::string refresh_token;
};

struct RegisterRequest {
  std::string username;
  std::string email;
  std::string password;
};

drogon::Task<> Authentication::login(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  auto body = req->getBody();
  LoginCredentials creds;
  auto parse_error = utilities::strict_read_json(creds, body);

  if (parse_error || creds.username.empty() || creds.password.empty()) {
    LOG_WARN << "Wrong credentials schema";
    SimpleError ret{.error =
                        "Invalid request, requires valid username & password"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  // for debugging hashes
  // LOG_INFO << "Password hash:" << hash_password_with_argon2(password) <<
  // std::endl;

  auto db = app().getDbClient();
  try {
    auto result = co_await db->execSqlCoro(
        "SELECT id, username, password_hash FROM users WHERE username = $1",
        creds.username);

    if (result.empty()) {
      SimpleError ret{.error = "Invalid username/password"};
      auto resp =
          HttpResponse::newHttpResponse(k401Unauthorized, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }
    const auto& row = result[0];
    int user_id = row["id"].as<int>();
    std::string stored_hash = row["password_hash"].as<std::string>();

    bool password_match =
        verify_password_with_argon2(creds.password, stored_hash);

    if (!password_match) {
      SimpleError ret{.error = "Invalid username/password"};
      auto resp =
          HttpResponse::newHttpResponse(k401Unauthorized, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    std::string token = generate_jwt(user_id, creds.username);

    std::string refresh_token = generate_refresh_token();

    auto expiry = std::chrono::system_clock::now() +
                  std::chrono::hours(24 * 7);  // 1 week
    auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

    try {
      co_await db->execSqlCoro(
          "INSERT INTO user_sessions (user_id, token, refresh_token, "
          "expires_at) VALUES ($1, $2, $3, to_timestamp($4)) "
          "ON CONFLICT (token) DO NOTHING",
          user_id, token, refresh_token, static_cast<double>(expiry_time));

      CredentialsResponse response{.status = "success",
                                   .token = token,
                                   .refresh_token = refresh_token,
                                   .user_id = user_id,
                                   .username = creds.username};

      auto resp =
          HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(response).value_or(""));
      callback(resp);
      co_return;
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Failed to store session: " << e.base().what();
      SimpleError ret{.error = "An error occurred"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "An error occurred"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  co_return;
}

drogon::Task<> Authentication::logout(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  const std::string& auth_header = req->getHeader("Authorization");

  if (!auth_header.empty() && auth_header.substr(0, 7) == "Bearer ") {
    std::string token = auth_header.substr(7);

    auto db = app().getDbClient();
    try {
      co_await db->execSqlCoro("DELETE FROM user_sessions WHERE token = $1",
                               token);

      SimpleStatus ret{.status = "success"};
      auto resp =
          HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      SimpleError ret{.error = "An error occurred"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
    }
  } else {
    SimpleStatus ret{.status = "success"};  // Still return success as the user
                                            // is effectively logged out
    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}

drogon::Task<> Authentication::refresh(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  RefreshRequest refresh_req;
  auto parse_error = utilities::strict_read_json(refresh_req, req->getBody());

  if (parse_error || refresh_req.refresh_token.empty()) {
    SimpleError ret{.error = "Refresh token is required"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();
  try {
    auto result = co_await db->execSqlCoro(
        "SELECT user_id, expires_at FROM user_sessions WHERE refresh_token ="
        "$1",
        refresh_req.refresh_token);

    if (result.empty()) {
      SimpleError ret{.error = "Invalid refresh token"};
      auto resp =
          HttpResponse::newHttpResponse(k401Unauthorized, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }
    const auto& row = result[0];
    int user_id = row["user_id"].as<int>();

    auto expiry_str = row["expires_at"].as<std::string>();

    // Parse timestamp from PostgreSQL format (e.g., "2025-04-25 12:34:56")
    std::chrono::system_clock::time_point expiry_time_point;
    std::istringstream ss(expiry_str);
    // std::tm tm = {};
    // ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    ss >> std::chrono::parse("%Y-%m-%d %H:%M:%S", expiry_time_point);
    if (ss.fail()) {
      LOG_ERROR << "Failed to parse expiry time: " << expiry_str;
      SimpleError ret{.error = "An error occurred"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    // auto old_expiry_time = std::mktime(&tm); // to time_t to compare
    // auto now = std::chrono::system_clock::to_time_t(
    //    std::chrono::system_clock::now());

    auto now = std::chrono::system_clock::now();

    if (expiry_time_point < now) {
      LOG_INFO << "Refresh token has expired";
      SimpleError ret{.error = "Refresh token has expired"};
      auto resp =
          HttpResponse::newHttpResponse(k401Unauthorized, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    }

    // Token is still valid, proceed with refresh
    // Get username for the token
    try {
      auto user_result = co_await db->execSqlCoro(
          "SELECT username FROM users WHERE id = $1", user_id);

      if (user_result.empty()) {
        SimpleError ret{.error = "Invalid refresh token"};
        auto resp = HttpResponse::newHttpResponse(k401Unauthorized,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
        co_return;
      }
      std::string username = user_result[0]["username"].as<std::string>();

      std::string new_token = generate_jwt(user_id, username);

      std::string new_refresh_token = generate_refresh_token();

      auto expiry =
          std::chrono::system_clock::now() + std::chrono::hours(24 * 7);
      auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

      try {
        co_await db->execSqlCoro(
            "UPDATE user_sessions SET token = $1, refresh_token = "
            "$2, expires_at = to_timestamp($3) WHERE refresh_token = $4",
            new_token, new_refresh_token, static_cast<double>(expiry_time),
            refresh_req.refresh_token);

        CredentialsResponse response{.status = "success",
                                     .token = new_token,
                                     .refresh_token = new_refresh_token,
                                     .user_id = user_id,
                                     .username = username};

        auto resp =
            HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(response).value_or(""));
        callback(resp);
      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Failed to update session: " << e.base().what();
        SimpleError ret{.error = "An error occurred"};
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
      }

    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      SimpleError ret{.error = "An error occurred"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
    }

  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "An error occurred"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}

drogon::Task<> Authentication::register_user(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr&)> callback) {
  RegisterRequest register_req;
  auto parse_error = utilities::strict_read_json(register_req, req->getBody());

  if (parse_error || register_req.username.empty() ||
      utilities::is_email_valid(register_req.email) ||
      register_req.password.empty()) {
    SimpleError ret{.error = "Username, email, and password are required"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  std::string password_hash;
  try {
    password_hash = hash_password_with_argon2(register_req.password);
  } catch (const std::exception& e) {
    LOG_ERROR << "Failed to hash password: " << e.what();
    SimpleError ret{.error = "An error occurred during registration"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT id FROM users WHERE username = $1 OR email = $2",
        register_req.username, register_req.email);

    if (!result.empty()) {
      SimpleError ret{.error = "Username or email already exists"};
      auto resp = HttpResponse::newHttpResponse(drogon::k409Conflict,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(ret).value_or(""));
      callback(resp);
      co_return;
    } else {
      try {
        auto insert_result = co_await db->execSqlCoro(
            "INSERT INTO users (username, email, password_hash) VALUES ($1, "
            "$2, $3) RETURNING id",
            register_req.username, register_req.email, password_hash);

        if (insert_result.empty()) {
          SimpleError ret{.error = "Failed to create user"};
          auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                    CT_APPLICATION_JSON);
          resp->setBody(glz::write_json(ret).value_or(""));
          callback(resp);
          co_return;
        }
        int user_id = insert_result[0]["id"].as<int>();

        std::string token = generate_jwt(user_id, register_req.username);

        std::string refresh_token = generate_refresh_token();
        auto expiry =
            std::chrono::system_clock::now() + std::chrono::hours(24 * 7);
        auto expiry_time = std::chrono::system_clock::to_time_t(expiry);

        try {
          co_await db->execSqlCoro(
              "INSERT INTO user_sessions (user_id, token, refresh_token, "
              "expires_at) VALUES ($1, $2, $3, to_timestamp($4)) "
              "ON CONFLICT (token) DO NOTHING",
              user_id, token, refresh_token, static_cast<double>(expiry_time));

          CredentialsResponse response{.status = "success",
                                       .token = token,
                                       .refresh_token = refresh_token,
                                       .user_id = user_id,
                                       .username = register_req.username};

          auto resp = HttpResponse::newHttpResponse(drogon::k200OK,
                                                    CT_APPLICATION_JSON);
          resp->setBody(glz::write_json(response).value_or(""));
          callback(resp);
        } catch (const DrogonDbException& e) {
          LOG_ERROR << "Failed to store session: " << e.base().what();
          SimpleError ret{.error =
                              "Registration successful, but failed to log in"};
          auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                    CT_APPLICATION_JSON);
          resp->setBody(glz::write_json(ret).value_or(""));
          callback(resp);
        }

      } catch (const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        SimpleError ret{.error = "An error occurred during registration"};
        auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                  CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(ret).value_or(""));
        callback(resp);
      }
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError ret{.error = "An error occurred during registration"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(ret).value_or(""));
    callback(resp);
  }

  co_return;
}
