#include <drogon/HttpController.h>
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
#include <drogon/utils/Utilities.h>

#include <format>
#include <string>
#include <vector>

#include "../services/service_manager.hpp"
#include "offers.hpp"

using drogon::app;
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::k400BadRequest;
using drogon::k403Forbidden;
using drogon::k404NotFound;
using drogon::k500InternalServerError;
using drogon::Task;
using drogon::orm::DrogonDbException;
using drogon::orm::Transaction;

using api::v1::Offers;

// Helper function to create a conversation between two users
Task<std::string> create_or_get_conversation(std::string user1_id,
                                             std::string user2_id,
                                             std::string offer_id) {
  auto db = app().getDbClient();

  // Check if a conversation already exists between these users
  try {
    auto result = co_await db->execSqlCoro(
        "SELECT c.id FROM conversations c "
        "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id "
        "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
        "WHERE cp1.user_id = $1 AND cp2.user_id = $2 "
        "LIMIT 1",
        std::stoi(user1_id), std::stoi(user2_id));

    if (!result.empty()) {
      // conversation already exists
      co_return std::to_string(result[0]["id"].as<int>());
    }

    // Create a new conversation
    auto conv_result = co_await db->execSqlCoro(
        "INSERT INTO conversations (name) VALUES ($1) RETURNING id",
        "Offer #" + offer_id + " Negotiation");

    if (conv_result.empty()) {
      co_return "";
    }

    int conversation_id = conv_result[0]["id"].as<int>();

    // Add participants
    co_await db->execSqlCoro(
        "INSERT INTO conversation_participants (conversation_id, user_id) "
        "VALUES "
        "($1, $2), ($1, $3)",
        conversation_id, std::stoi(user1_id), std::stoi(user2_id));

    co_return std::to_string(conversation_id);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error in create_or_get_conversation: "
              << e.base().what();
    co_return "";
  }
}

// Create/get conversations during a transaction
Task<std::string> create_or_get_conversation_transaction(
    const std::shared_ptr<Transaction>& transaction, std::string user1_id,
    std::string user2_id, std::string offer_id) {
  try {
    // Check if a conversation already exists between these users
    auto result = co_await transaction->execSqlCoro(
        "SELECT c.id FROM conversations c "
        "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id "
        "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
        "WHERE cp1.user_id = $1 AND cp2.user_id = $2 "
        "LIMIT 1",
        std::stoi(user1_id), std::stoi(user2_id));

    if (!result.empty()) {
      co_return std::to_string(result[0]["id"].as<int>());
    }

    LOG_INFO << "No existing conversation found, creating new one";
    // Create a new conversation
    auto conv_result = co_await transaction->execSqlCoro(
        "INSERT INTO conversations (name) VALUES ($1) RETURNING id",
        "Offer #" + offer_id + " Negotiation");

    if (conv_result.empty()) {
      LOG_ERROR << "Failed to create new conversation";
      co_return "";
    }

    int conversation_id = conv_result[0]["id"].as<int>();

    // Add participants
    co_await transaction->execSqlCoro(
        "INSERT INTO conversation_participants (conversation_id, user_id) "
        "VALUES "
        "($1, $2), ($1, $3)",
        conversation_id, std::stoi(user1_id), std::stoi(user2_id));

    co_return std::to_string(conversation_id);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error in create_or_get_conversation: "
              << e.base().what();
    co_return "";
  }
}

// Dead code: Helper function to add a negotiation message to a conversation
void add_negotiation_message(
    std::shared_ptr<Transaction> transaction, std::string conversation_id,
    std::string sender_id, double proposed_price, std::string message,
    int negotiation_id, std::string offer_id,
    std::function<void(const HttpResponsePtr&)> callback) {
  // Create message content
  std::string negotiation_message =
      std::format("Proposed new price: ${:.2f}", proposed_price);

  if (!message.empty()) {
    negotiation_message += "\nMessage: " + message;
  }

  // Create metadata JSON
  Json::Value metadata;
  metadata["offer_id"] = offer_id;
  metadata["offer_status"] = "pending";  // Initial status is pending

  // Convert to string
  Json::FastWriter writer;
  std::string metadata_str = writer.write(metadata);

  transaction->execSqlAsync(
      "INSERT INTO messages "
      "(conversation_id, sender_id, content, context_type, context_id, "
      "metadata) "
      "VALUES ($1, $2, $3, 'negotiation', $4, $5::jsonb)",
      [=](const drogon::orm::Result&) {
        LOG_INFO << "Message added successfully, completing transaction";

        Json::Value response;
        response["status"] = "success";
        response["message"] = "Negotiation started";
        response["negotiation_id"] = negotiation_id;
        response["conversation_id"] = conversation_id;
        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);
      },
      [=](const DrogonDbException& e) {
        LOG_ERROR << "Database error adding message: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        transaction->rollback();
      },
      std::stoi(conversation_id), std::stoi(sender_id), negotiation_message,
      negotiation_id, metadata_str);
}

// Negotiate an offer
Task<> Offers::negotiate_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Parse request body
  auto json = req->getJsonObject();
  if (!json) {
    Json::Value error;
    error["error"] = "Invalid JSON";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  double proposed_price = (*json)["price"].asDouble();
  std::string message = (*json)["message"].asString();

  LOG_INFO << "Starting negotiate_offer for offer ID: " << id;

  // Start a transaction to ensure atomicity
  try {
    auto transaction = co_await db->newTransactionCoro();

    try {
      // First, check if the offer exists and get the other user's ID
      auto result = co_await transaction->execSqlCoro(
          "SELECT o.*, p.user_id AS post_user_id "
          "FROM offers o "
          "JOIN posts p ON o.post_id = p.id "
          "WHERE o.id = $1",
          std::stoi(id));

      if (result.empty()) {
        Json::Value error;
        error["error"] = "Offer not found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k404NotFound);
        callback(resp);
        transaction->rollback();
        co_return;
      }

      // Check if user is authorized (must be either offer creator or post
      // owner)
      int offer_user_id = result[0]["user_id"].as<int>();
      int post_user_id = result[0]["post_user_id"].as<int>();

      LOG_INFO << "offer_user_id: " << offer_user_id;
      LOG_INFO << "current_user_id: " << current_user_id;
      LOG_INFO << "post_user_id: " << post_user_id;

      if (std::stoi(current_user_id) != offer_user_id &&
          std::stoi(current_user_id) != post_user_id) {
        Json::Value error;
        error["error"] = "Unauthorized";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k403Forbidden);
        callback(resp);
        transaction->rollback();
        co_return;
      }

      // Update offer negotiation status
      co_await transaction->execSqlCoro(
          "UPDATE offers SET negotiation_status = 'in_progress' WHERE id = $1",
          std::stoi(id));

      // Create a price negotiation record
      auto neg_result = co_await transaction->execSqlCoro(
          "INSERT INTO price_negotiations "
          "(offer_id, user_id, proposed_price, message) "
          "VALUES ($1, $2, $3, $4) "
          "RETURNING id, created_at",
          std::stoi(id), std::stoi(current_user_id), proposed_price, message);

      if (neg_result.empty()) {
        Json::Value error;
        error["error"] = "Failed to create negotiation";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        transaction->rollback();
        co_return;
      }

      int negotiation_id = neg_result[0]["id"].as<int>();

      // Determine the other user ID
      std::string other_user_id = std::to_string(
          std::stoi(current_user_id) == offer_user_id ? post_user_id
                                                      : offer_user_id);

      std::string conversation_id =
          co_await create_or_get_conversation_transaction(
              transaction, current_user_id, other_user_id, id);

      if (conversation_id.empty()) {
        Json::Value error;
        error["error"] = "Failed to create or get conversation";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
        transaction->rollback();
        co_return;
      }

      // Create message content
      std::string negotiation_message =
          std::format("Proposed new price: ${:.2f}", proposed_price);

      if (!message.empty()) {
        negotiation_message += "\nMessage: " + message;
      }

      LOG_INFO << "Negotiation message: " << negotiation_message;

      // Create metadata JSON
      Json::Value metadata;
      metadata["offer_id"] = id;
      metadata["offer_status"] = "pending";  // Initial status is pending

      // Convert to string
      Json::FastWriter writer;
      std::string metadata_str = writer.write(metadata);

      // Add a message to the conversation about the negotiation
      co_await transaction->execSqlCoro(
          "INSERT INTO messages "
          "(conversation_id, sender_id, content, context_type, context_id, "
          "metadata) "
          "VALUES ($1, $2, $3, 'negotiation', $4, $5::jsonb)",
          std::stoi(conversation_id), std::stoi(current_user_id),
          negotiation_message, negotiation_id, metadata_str);

      std::string offer_topic = create_topic("offer", id);

      Json::Value offer_data_json;
      offer_data_json["type"] = "offer_negotiated";
      offer_data_json["id"] = id;
      offer_data_json["message"] =
          "New Negotiation: " + std::to_string(proposed_price);
      offer_data_json["modified_at"] =
          neg_result[0]["created_at"].as<std::string>();

      std::string offer_data = writer.write(offer_data_json);

      // Publish to offer subscribers
      ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                             offer_data);
      Json::Value response;
      response["status"] = "success";
      response["message"] = "Negotiation started";
      response["negotiation_id"] = negotiation_id;
      response["conversation_id"] = conversation_id;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
      LOG_INFO << "Negotiation Transaction committed successfully";
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error in transaction: " << e.base().what();
      transaction->rollback();
      Json::Value error;
      error["error"] = "Database error: " + std::string(e.base().what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const std::exception& e) {
    LOG_ERROR << "Exception: " << e.what();
    Json::Value error;
    error["error"] = "Error: " + std::string(e.what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get negotiations for an offer
Task<> Offers::get_negotiations(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    // Check if user is authorized to view negotiations
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    if (std::stoi(current_user_id) != offer_user_id &&
        std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Unauthorized";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Get all negotiations for this offer
    auto neg_result = co_await db->execSqlCoro(
        "SELECT pn.*, u.username "
        "FROM price_negotiations pn "
        "JOIN users u ON pn.user_id = u.id "
        "WHERE pn.offer_id = $1 "
        "ORDER BY pn.created_at DESC",
        std::stoi(id));

    Json::Value negotiations(Json::arrayValue);

    for (const auto& row : neg_result) {
      Json::Value negotiation;
      negotiation["id"] = row["id"].as<int>();
      negotiation["offer_id"] = row["offer_id"].as<int>();
      negotiation["user_id"] = row["user_id"].as<int>();
      negotiation["username"] = row["username"].as<std::string>();
      negotiation["proposed_price"] = row["proposed_price"].as<double>();
      negotiation["status"] = row["status"].as<std::string>();
      negotiation["message"] =
          row["message"].isNull() ? "" : row["message"].as<std::string>();
      negotiation["created_at"] = row["created_at"].as<std::string>();
      negotiation["updated_at"] = row["updated_at"].as<std::string>();

      negotiations.append(negotiation);
    }

    Json::Value response;
    response["status"] = "success";
    response["negotiations"] = negotiations;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Partial implementations:

// Request proof of product
Task<> Offers::request_proof(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Parse request body
  auto json = req->getJsonObject();
  if (!json) {
    Json::Value error;
    error["error"] = "Invalid JSON";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  std::string message = (*json)["message"].asString();

  try {
    // Check if user is authorized (must be post owner)
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    // Only post owner can request proof
    if (std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Only the post owner can request proof";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Create or get conversation between the two users
    std::string conversation_id = co_await create_or_get_conversation(
        current_user_id, std::to_string(offer_user_id), id);

    if (conversation_id.empty()) {
      Json::Value error;
      error["error"] = "Failed to create conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    // Add a message to the conversation about the proof request
    std::string proof_message = "Proof of product requested";
    if (!message.empty()) {
      proof_message += "\nMessage: " + message;
    }

    try {
      co_await db->execSqlCoro(
          "INSERT INTO messages "
          "(conversation_id, sender_id, content, context_type, context_id) "
          "VALUES ($1, $2, $3, 'proof_request', $4)",
          std::stoi(conversation_id), std::stoi(current_user_id), proof_message,
          std::stoi(id));

      Json::Value response;
      response["status"] = "success";
      response["message"] = "Proof requested";
      response["conversation_id"] = conversation_id;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
    } catch (const DrogonDbException& e) {
      Json::Value error;
      error["error"] = "Database error: " + std::string(e.base().what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Submit proof of product
Task<> Offers::submit_proof(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Parse request body
  auto json = req->getJsonObject();
  if (!json) {
    Json::Value error;
    error["error"] = "Invalid JSON";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  std::string image_url = (*json)["image_url"].asString();
  std::string description = (*json)["description"].asString();

  try {
    // Check if user is authorized (must be offer creator)
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    // Only offer creator can submit proof
    if (std::stoi(current_user_id) != offer_user_id) {
      Json::Value error;
      error["error"] = "Only the offer creator can submit proof";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Create product proof record
    auto proof_result = co_await db->execSqlCoro(
        "INSERT INTO product_proofs "
        "(offer_id, user_id, image_url, description) "
        "VALUES ($1, $2, $3, $4) "
        "RETURNING id",
        std::stoi(id), std::stoi(current_user_id), image_url, description);

    if (proof_result.empty()) {
      Json::Value error;
      error["error"] = "Failed to create proof";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    int proof_id = proof_result[0]["id"].as<int>();

    // Create or get conversation between the two users
    std::string conversation_id = co_await create_or_get_conversation(
        current_user_id, std::to_string(post_user_id), id);

    if (conversation_id.empty()) {
      Json::Value error;
      error["error"] = "Failed to create conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    // Add a message to the conversation about the proof submission
    std::string proof_message = "Proof of product submitted";
    if (!description.empty()) {
      proof_message += "\nDescription: " + description;
    }
    proof_message += "\nImage: " + image_url;

    try {
      co_await db->execSqlCoro(
          "INSERT INTO messages "
          "(conversation_id, sender_id, content, context_type, context_id) "
          "VALUES ($1, $2, $3, 'proof_submission', $4)",
          std::stoi(conversation_id), std::stoi(current_user_id), proof_message,
          proof_id);

      Json::Value response;
      response["status"] = "success";
      response["message"] = "Proof submitted";
      response["proof_id"] = proof_id;
      response["conversation_id"] = conversation_id;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
    } catch (const DrogonDbException& e) {
      Json::Value error;
      error["error"] = "Database error: " + std::string(e.base().what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get proofs for an offer
Task<> Offers::get_proofs(HttpRequestPtr req,
                          std::function<void(const HttpResponsePtr&)> callback,
                          std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    // Check if user is authorized to view proofs
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    if (std::stoi(current_user_id) != offer_user_id &&
        std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Unauthorized";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Get all proofs for this offer
    auto proof_result = co_await db->execSqlCoro(
        "SELECT pp.*, u.username "
        "FROM product_proofs pp "
        "JOIN users u ON pp.user_id = u.id "
        "WHERE pp.offer_id = $1 "
        "ORDER BY pp.created_at DESC",
        std::stoi(id));

    Json::Value proofs(Json::arrayValue);

    for (const auto& row : proof_result) {
      Json::Value proof;
      proof["id"] = row["id"].as<int>();
      proof["offer_id"] = row["offer_id"].as<int>();
      proof["user_id"] = row["user_id"].as<int>();
      proof["username"] = row["username"].as<std::string>();
      proof["image_url"] = row["image_url"].as<std::string>();
      proof["description"] = row["description"].isNull()
                                 ? ""
                                 : row["description"].as<std::string>();
      proof["status"] = row["status"].as<std::string>();
      proof["created_at"] = row["created_at"].as<std::string>();

      proofs.append(proof);
    }

    Json::Value response;
    response["status"] = "success";
    response["proofs"] = proofs;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Approve a proof
Task<> Offers::approve_proof(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id, std::string proof_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    // Check if user is authorized (must be post owner)
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    // Only post owner can approve proof
    if (std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Only the post owner can approve proof";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Update proof status
    auto update_result = co_await db->execSqlCoro(
        "UPDATE product_proofs SET status = 'approved' WHERE id = $1 AND "
        "offer_id = $2 RETURNING id",
        std::stoi(proof_id), std::stoi(id));

    if (update_result.empty()) {
      Json::Value error;
      error["error"] = "Proof not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    // Create or get conversation between the two users
    std::string conversation_id = co_await create_or_get_conversation(
        current_user_id, std::to_string(offer_user_id), id);

    if (conversation_id.empty()) {
      Json::Value error;
      error["error"] = "Failed to create conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    // Add a message to the conversation about the proof approval
    std::string approval_message = "Proof of product has been approved";

    try {
      co_await db->execSqlCoro(
          "INSERT INTO messages "
          "(conversation_id, sender_id, content, context_type, context_id) "
          "VALUES ($1, $2, $3, 'proof_approval', $4)",
          std::stoi(conversation_id), std::stoi(current_user_id),
          approval_message, std::stoi(proof_id));

      Json::Value response;
      response["status"] = "success";
      response["message"] = "Proof approved";

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
    } catch (const DrogonDbException& e) {
      Json::Value error;
      error["error"] = "Database error: " + std::string(e.base().what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Reject a proof
Task<> Offers::reject_proof(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id, std::string proof_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Parse request body
  auto json = req->getJsonObject();
  std::string reason = json ? (*json)["reason"].asString() : "";

  try {
    // Check if user is authorized (must be post owner)
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    // Only post owner can reject proof
    if (std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Only the post owner can reject proof";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Update proof status
    auto update_result = co_await db->execSqlCoro(
        "UPDATE product_proofs SET status = 'rejected' WHERE id = $1 AND "
        "offer_id = $2 RETURNING id",
        std::stoi(proof_id), std::stoi(id));

    if (update_result.empty()) {
      Json::Value error;
      error["error"] = "Proof not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    // Create or get conversation between the two users
    std::string conversation_id = co_await create_or_get_conversation(
        current_user_id, std::to_string(offer_user_id), id);

    if (conversation_id.empty()) {
      Json::Value error;
      error["error"] = "Failed to create conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    // Add a message to the conversation about the proof rejection
    std::string rejection_message = "Proof of product has been rejected";
    if (!reason.empty()) {
      rejection_message += "\nReason: " + reason;
    }

    try {
      co_await db->execSqlCoro(
          "INSERT INTO messages "
          "(conversation_id, sender_id, content, context_type, context_id) "
          "VALUES ($1, $2, $3, 'proof_rejection', $4)",
          std::stoi(conversation_id), std::stoi(current_user_id),
          rejection_message, std::stoi(proof_id));

      Json::Value response;
      response["status"] = "success";
      response["message"] = "Proof rejected";

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
    } catch (const DrogonDbException& e) {
      Json::Value error;
      error["error"] = "Database error: " + std::string(e.base().what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Create escrow transaction
Task<> Offers::create_escrow(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Parse request body
  auto json = req->getJsonObject();
  if (!json) {
    Json::Value error;
    error["error"] = "Invalid JSON";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  double amount = (*json)["amount"].asDouble();

  try {
    // Check if user is authorized (must be post owner)
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    // Only post owner can create escrow
    if (std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Only the post owner can create escrow";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Create escrow transaction
    auto escrow_result = co_await db->execSqlCoro(
        "INSERT INTO escrow_transactions "
        "(offer_id, buyer_id, seller_id, amount) "
        "VALUES ($1, $2, $3, $4) "
        "RETURNING id",
        std::stoi(id), std::stoi(current_user_id), offer_user_id, amount);

    if (escrow_result.empty()) {
      Json::Value error;
      error["error"] = "Failed to create escrow";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    int escrow_id = escrow_result[0]["id"].as<int>();

    // Create or get conversation between the two users
    std::string conversation_id = co_await create_or_get_conversation(
        current_user_id, std::to_string(offer_user_id), id);

    if (conversation_id.empty()) {
      Json::Value error;
      error["error"] = "Failed to create conversation";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
      co_return;
    }

    // Add a message to the conversation about the escrow
    std::string escrow_message =
        "Escrow transaction created for $" + std::to_string(amount);

    try {
      co_await db->execSqlCoro(
          "INSERT INTO messages "
          "(conversation_id, sender_id, content, context_type, "
          "context_id) "
          "VALUES ($1, $2, $3, 'escrow', $4)",
          std::stoi(conversation_id), std::stoi(current_user_id),
          escrow_message, escrow_id);

      Json::Value response;
      response["status"] = "success";
      response["message"] = "Escrow created";
      response["escrow_id"] = escrow_id;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
    } catch (const DrogonDbException& e) {
      Json::Value error;
      error["error"] = "Database error: " + std::string(e.base().what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get escrow transaction for an offer
Task<> Offers::get_escrow(HttpRequestPtr req,
                          std::function<void(const HttpResponsePtr&)> callback,
                          std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  try {
    // Check if user is authorized to view escrow
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_user_id = result[0]["post_user_id"].as<int>();

    if (std::stoi(current_user_id) != offer_user_id &&
        std::stoi(current_user_id) != post_user_id) {
      Json::Value error;
      error["error"] = "Unauthorized";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Get escrow transaction for this offer
    auto escrow_result = co_await db->execSqlCoro(
        "SELECT et.*, "
        "buyer.username AS buyer_username, "
        "seller.username AS seller_username "
        "FROM escrow_transactions et "
        "JOIN users buyer ON et.buyer_id = buyer.id "
        "JOIN users seller ON et.seller_id = seller.id "
        "WHERE et.offer_id = $1 "
        "ORDER BY et.created_at DESC "
        "LIMIT 1",
        std::stoi(id));

    if (escrow_result.empty()) {
      Json::Value response;
      response["status"] = "success";
      response["has_escrow"] = false;

      auto resp = HttpResponse::newHttpJsonResponse(response);
      callback(resp);
      co_return;
    }

    const auto& row = escrow_result[0];

    Json::Value escrow;
    escrow["id"] = row["id"].as<int>();
    escrow["offer_id"] = row["offer_id"].as<int>();
    escrow["buyer_id"] = row["buyer_id"].as<int>();
    escrow["buyer_username"] = row["buyer_username"].as<std::string>();
    escrow["seller_id"] = row["seller_id"].as<int>();
    escrow["seller_username"] = row["seller_username"].as<std::string>();
    escrow["amount"] = row["amount"].as<double>();
    escrow["status"] = row["status"].as<std::string>();
    escrow["created_at"] = row["created_at"].as<std::string>();
    escrow["updated_at"] = row["updated_at"].as<std::string>();

    Json::Value response;
    response["status"] = "success";
    response["has_escrow"] = true;
    response["escrow"] = escrow;

    auto resp = HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const DrogonDbException& e) {
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}
