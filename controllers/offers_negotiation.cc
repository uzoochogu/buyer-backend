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

#include "offers.hpp"

using namespace drogon;
using namespace drogon::orm;
using namespace api::v1;

// Helper function to create a conversation between two users
std::string create_or_get_conversation(const std::string& user1_id,
                                       const std::string& user2_id,
                                       const std::string& offer_id) {
  auto db = app().getDbClient();

  // Check if a conversation already exists between these users
  auto result = db->execSqlSync(
      "SELECT c.id FROM conversations c "
      "JOIN conversation_participants cp1 ON c.id = cp1.conversation_id "
      "JOIN conversation_participants cp2 ON c.id = cp2.conversation_id "
      "WHERE cp1.user_id = $1 AND cp2.user_id = $2 "
      "LIMIT 1",
      std::stoi(user1_id), std::stoi(user2_id));

  if (result.size() > 0) {
    // Conversation exists, return its ID
    return std::to_string(result[0]["id"].as<int>());
  }

  // Create a new conversation
  auto conv_result = db->execSqlSync(
      "INSERT INTO conversations (name) VALUES ($1) RETURNING id",
      "Offer #" + offer_id + " Negotiation");

  if (conv_result.size() == 0) {
    return "";
  }

  int conversation_id = conv_result[0]["id"].as<int>();

  // Add participants
  db->execSqlSync(
      "INSERT INTO conversation_participants (conversation_id, user_id) VALUES "
      "($1, $2), ($1, $3)",
      conversation_id, std::stoi(user1_id), std::stoi(user2_id));

  return std::to_string(conversation_id);
}

// Helper function to add a negotiation message to a conversation
void add_negotiation_message(
    std::shared_ptr<Transaction> transaction,
    const std::string& conversation_id, const std::string& sender_id,
    double proposed_price, const std::string& message, int negotiation_id,
    const std::string& offer_id,
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
      [=](const Result&) {
        LOG_INFO << "Message added successfully, completing transaction";

        // Set commit callback to send response after successful commit
        transaction->setCommitCallback([=](bool committed) {
          if (committed) {
            LOG_INFO << "Transaction committed successfully";
            Json::Value response;
            response["status"] = "success";
            response["message"] = "Negotiation started";
            response["negotiation_id"] = negotiation_id;
            response["conversation_id"] = conversation_id;

            auto resp = HttpResponse::newHttpJsonResponse(response);
            callback(resp);
          } else {
            LOG_ERROR << "Transaction failed to commit";
            Json::Value error;
            error["error"] = "Failed to complete negotiation";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
          }
        });
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
void Offers::negotiate_offer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
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
    return;
  }

  double proposed_price = (*json)["price"].asDouble();
  std::string message = (*json)["message"].asString();

  LOG_INFO << "Starting negotiate_offer for offer ID: " << id;

  // Start a transaction to ensure atomicity
  try {
    auto transaction = db->newTransaction();

    // First, check if the offer exists and get the other user's ID
    transaction->execSqlAsync(
        "SELECT o.*, p.user_id AS post_user_id "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        [=](const Result& result) {
          if (result.size() == 0) {
            Json::Value error;
            error["error"] = "Offer not found";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k404NotFound);
            callback(resp);
            transaction->rollback();
            return;
          }

          // Check if user is authorized (must be either offer creator or post
          // owner)
          int offer_user_id = result[0]["user_id"].as<int>();
          int post_user_id = result[0]["post_user_id"].as<int>();

          // Log all values for debugging
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
            return;
          }

          LOG_INFO << "User authorized, updating negotiation status";

          // Update offer negotiation status
          transaction->execSqlAsync(
              "UPDATE offers SET negotiation_status = 'in_progress' WHERE id = "
              "$1",
              [=](const Result&) {
                LOG_INFO
                    << "Negotiation status updated, creating price negotiation";

                // Create a price negotiation record
                transaction->execSqlAsync(
                    "INSERT INTO price_negotiations "
                    "(offer_id, user_id, proposed_price, message) "
                    "VALUES ($1, $2, $3, $4) "
                    "RETURNING id",
                    [=](const Result& negResult) {
                      if (negResult.size() == 0) {
                        Json::Value error;
                        error["error"] = "Failed to create negotiation";
                        auto resp = HttpResponse::newHttpJsonResponse(error);
                        resp->setStatusCode(k500InternalServerError);
                        callback(resp);
                        transaction->rollback();
                        return;
                      }

                      int negotiation_id = negResult[0]["id"].as<int>();
                      LOG_INFO << "Created price negotiation with ID: "
                               << negotiation_id;

                      // Determine the other user ID
                      std::string other_user_id = std::to_string(
                          std::stoi(current_user_id) == offer_user_id
                              ? post_user_id
                              : offer_user_id);

                      LOG_INFO << "Creating conversation between users "
                               << current_user_id << " and " << other_user_id;

                      // Check if a conversation already exists between these
                      // users
                      transaction->execSqlAsync(
                          "SELECT c.id FROM conversations c "
                          "JOIN conversation_participants cp1 ON c.id = "
                          "cp1.conversation_id "
                          "JOIN conversation_participants cp2 ON c.id = "
                          "cp2.conversation_id "
                          "WHERE cp1.user_id = $1 AND cp2.user_id = $2 "
                          "LIMIT 1",
                          [=](const Result& convResult) {
                            std::string conversation_id;

                            if (convResult.size() > 0) {
                              // Conversation exists, use its ID
                              conversation_id =
                                  std::to_string(convResult[0]["id"].as<int>());
                              LOG_INFO << "Found existing conversation: "
                                       << conversation_id;

                              // Add a message to the conversation about the
                              // negotiation
                              add_negotiation_message(
                                  transaction, conversation_id, current_user_id,
                                  proposed_price, message, negotiation_id, id,
                                  callback);
                            } else {
                              LOG_INFO << "No existing conversation found, "
                                          "creating new one";

                              // Create a new conversation
                              transaction->execSqlAsync(
                                  "INSERT INTO conversations (name) VALUES "
                                  "($1) RETURNING id",
                                  [&](const Result& newConvResult) {
                                    if (newConvResult.size() == 0) {
                                      Json::Value error;
                                      error["error"] =
                                          "Failed to create conversation";
                                      auto resp =
                                          HttpResponse::newHttpJsonResponse(
                                              error);
                                      resp->setStatusCode(
                                          k500InternalServerError);
                                      callback(resp);
                                      transaction->rollback();
                                      return;
                                    }

                                    int new_conversation_id =
                                        newConvResult[0]["id"].as<int>();
                                    conversation_id =
                                        std::to_string(new_conversation_id);
                                    LOG_INFO << "Created new conversation: "
                                             << conversation_id;

                                    // Add participants
                                    transaction->execSqlAsync(
                                        "INSERT INTO conversation_participants "
                                        "(conversation_id, user_id) VALUES "
                                        "($1, $2), ($1, $3)",
                                        [=](const Result&) {
                                          LOG_INFO << "Added participants to "
                                                      "conversation";

                                          // Add a message to the conversation
                                          // about the negotiation
                                          add_negotiation_message(
                                              transaction, conversation_id,
                                              current_user_id, proposed_price,
                                              message, negotiation_id, id,
                                              callback);
                                        },
                                        [=](const DrogonDbException& e) {
                                          LOG_ERROR << "Database error adding "
                                                       "participants: "
                                                    << e.base().what();
                                          Json::Value error;
                                          error["error"] =
                                              "Database error: " +
                                              std::string(e.base().what());
                                          auto resp =
                                              HttpResponse::newHttpJsonResponse(
                                                  error);
                                          resp->setStatusCode(
                                              k500InternalServerError);
                                          callback(resp);
                                          transaction->rollback();
                                        },
                                        new_conversation_id,
                                        std::stoi(current_user_id),
                                        std::stoi(other_user_id));
                                  },
                                  [=](const DrogonDbException& e) {
                                    LOG_ERROR << "Database error creating "
                                                 "conversation: "
                                              << e.base().what();
                                    Json::Value error;
                                    error["error"] =
                                        "Database error: " +
                                        std::string(e.base().what());
                                    auto resp =
                                        HttpResponse::newHttpJsonResponse(
                                            error);
                                    resp->setStatusCode(
                                        k500InternalServerError);
                                    callback(resp);
                                    transaction->rollback();
                                  },
                                  "Offer #" + id + " Negotiation");
                            }
                          },
                          [=](const DrogonDbException& e) {
                            LOG_ERROR
                                << "Database error checking for conversation: "
                                << e.base().what();
                            Json::Value error;
                            error["error"] = "Database error: " +
                                             std::string(e.base().what());
                            auto resp =
                                HttpResponse::newHttpJsonResponse(error);
                            resp->setStatusCode(k500InternalServerError);
                            callback(resp);
                            transaction->rollback();
                          },
                          std::stoi(current_user_id), std::stoi(other_user_id));
                    },
                    [=](const DrogonDbException& e) {
                      LOG_ERROR << "Database error creating negotiation: "
                                << e.base().what();
                      Json::Value error;
                      error["error"] =
                          "Database error: " + std::string(e.base().what());
                      auto resp = HttpResponse::newHttpJsonResponse(error);
                      resp->setStatusCode(k500InternalServerError);
                      callback(resp);
                      transaction->rollback();
                    },
                    std::stoi(id), std::stoi(current_user_id), proposed_price,
                    message);
              },
              [=](const DrogonDbException& e) {
                LOG_ERROR << "Database error updating offer status: "
                          << e.base().what();
                Json::Value error;
                error["error"] =
                    "Database error: " + std::string(e.base().what());
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                transaction->rollback();
              },
              std::stoi(id));
        },
        [=](const DrogonDbException& e) {
          LOG_ERROR << "Database error fetching offer: " << e.base().what();
          Json::Value error;
          error["error"] = "Database error: " + std::string(e.base().what());
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
          transaction->rollback();
        },
        std::stoi(id));
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Transaction error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error: " + std::string(e.base().what());
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }
}

// Get negotiations for an offer
void Offers::get_negotiations(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Check if user is authorized to view negotiations
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Get all negotiations for this offer
        db->execSqlAsync(
            "SELECT pn.*, u.username "
            "FROM price_negotiations pn "
            "JOIN users u ON pn.user_id = u.id "
            "WHERE pn.offer_id = $1 "
            "ORDER BY pn.created_at DESC",
            [=](const Result& negResult) {
              Json::Value negotiations(Json::arrayValue);

              for (const auto& row : negResult) {
                Json::Value negotiation;
                negotiation["id"] = row["id"].as<int>();
                negotiation["offer_id"] = row["offer_id"].as<int>();
                negotiation["user_id"] = row["user_id"].as<int>();
                negotiation["username"] = row["username"].as<std::string>();
                negotiation["proposed_price"] =
                    row["proposed_price"].as<double>();
                negotiation["status"] = row["status"].as<std::string>();
                negotiation["message"] = row["message"].isNull()
                                             ? ""
                                             : row["message"].as<std::string>();
                negotiation["created_at"] = row["created_at"].as<std::string>();
                negotiation["updated_at"] = row["updated_at"].as<std::string>();

                negotiations.append(negotiation);
              }

              Json::Value response;
              response["status"] = "success";
              response["negotiations"] = negotiations;

              auto resp = HttpResponse::newHttpJsonResponse(response);
              callback(resp);
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(id));
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Request proof of product
void Offers::request_proof(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
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
    return;
  }

  std::string message = (*json)["message"].asString();

  // Check if user is authorized (must be post owner)
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Create or get conversation between the two users
        std::string conversation_id = create_or_get_conversation(
            current_user_id, std::to_string(offer_user_id), id);

        if (conversation_id.empty()) {
          Json::Value error;
          error["error"] = "Failed to create conversation";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k500InternalServerError);
          callback(resp);
          return;
        }

        // Add a message to the conversation about the proof request
        std::string proof_message = "Proof of product requested";
        if (!message.empty()) {
          proof_message += "\nMessage: " + message;
        }

        db->execSqlAsync(
            "INSERT INTO messages "
            "(conversation_id, sender_id, content, context_type, context_id) "
            "VALUES ($1, $2, $3, 'proof_request', $4)",
            [=](const Result&) {
              Json::Value response;
              response["status"] = "success";
              response["message"] = "Proof requested";
              response["conversation_id"] = conversation_id;

              auto resp = HttpResponse::newHttpJsonResponse(response);
              callback(resp);
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(conversation_id), std::stoi(current_user_id),
            proof_message, std::stoi(id));
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Submit proof of product
void Offers::submit_proof(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
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
    return;
  }

  std::string image_url = (*json)["image_url"].asString();
  std::string description = (*json)["description"].asString();

  // Check if user is authorized (must be offer creator)
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Create product proof record
        db->execSqlAsync(
            "INSERT INTO product_proofs "
            "(offer_id, user_id, image_url, description) "
            "VALUES ($1, $2, $3, $4) "
            "RETURNING id",
            [=](const Result& proofResult) {
              if (proofResult.size() == 0) {
                Json::Value error;
                error["error"] = "Failed to create proof";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
              }

              int proof_id = proofResult[0]["id"].as<int>();

              // Create or get conversation between the two users
              std::string conversation_id = create_or_get_conversation(
                  current_user_id, std::to_string(post_user_id), id);

              if (conversation_id.empty()) {
                Json::Value error;
                error["error"] = "Failed to create conversation";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
              }

              // Add a message to the conversation about the proof submission
              std::string proof_message = "Proof of product submitted";
              if (!description.empty()) {
                proof_message += "\nDescription: " + description;
              }
              proof_message += "\nImage: " + image_url;

              db->execSqlAsync(
                  "INSERT INTO messages "
                  "(conversation_id, sender_id, content, context_type, "
                  "context_id) "
                  "VALUES ($1, $2, $3, 'proof_submission', $4)",
                  [=](const Result&) {
                    Json::Value response;
                    response["status"] = "success";
                    response["message"] = "Proof submitted";
                    response["proof_id"] = proof_id;
                    response["conversation_id"] = conversation_id;

                    auto resp = HttpResponse::newHttpJsonResponse(response);
                    callback(resp);
                  },
                  [=](const DrogonDbException& e) {
                    Json::Value error;
                    error["error"] =
                        "Database error: " + std::string(e.base().what());
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k500InternalServerError);
                    callback(resp);
                  },
                  std::stoi(conversation_id), std::stoi(current_user_id),
                  proof_message, proof_id);
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(id), std::stoi(current_user_id), image_url, description);
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Get proofs for an offer
void Offers::get_proofs(const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& callback,
                        const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Check if user is authorized to view proofs
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Get all proofs for this offer
        db->execSqlAsync(
            "SELECT pp.*, u.username "
            "FROM product_proofs pp "
            "JOIN users u ON pp.user_id = u.id "
            "WHERE pp.offer_id = $1 "
            "ORDER BY pp.created_at DESC",
            [=](const Result& proofResult) {
              Json::Value proofs(Json::arrayValue);

              for (const auto& row : proofResult) {
                Json::Value proof;
                proof["id"] = row["id"].as<int>();
                proof["offer_id"] = row["offer_id"].as<int>();
                proof["user_id"] = row["user_id"].as<int>();
                proof["username"] = row["username"].as<std::string>();
                proof["image_url"] = row["image_url"].as<std::string>();
                proof["description"] =
                    row["description"].isNull()
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
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(id));
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Approve a proof
void Offers::approve_proof(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id, const std::string& proof_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Check if user is authorized (must be post owner)
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Update proof status
        db->execSqlAsync(
            "UPDATE product_proofs SET status = 'approved' WHERE id = $1 AND "
            "offer_id = $2 RETURNING id",
            [=](const Result& updateResult) {
              if (updateResult.size() == 0) {
                Json::Value error;
                error["error"] = "Proof not found";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k404NotFound);
                callback(resp);
                return;
              }

              // Create or get conversation between the two users
              std::string conversation_id = create_or_get_conversation(
                  current_user_id, std::to_string(offer_user_id), id);

              if (conversation_id.empty()) {
                Json::Value error;
                error["error"] = "Failed to create conversation";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
              }

              // Add a message to the conversation about the proof approval
              std::string approval_message =
                  "Proof of product has been approved";

              db->execSqlAsync(
                  "INSERT INTO messages "
                  "(conversation_id, sender_id, content, context_type, "
                  "context_id) "
                  "VALUES ($1, $2, $3, 'proof_approval', $4)",
                  [=](const Result&) {
                    Json::Value response;
                    response["status"] = "success";
                    response["message"] = "Proof approved";

                    auto resp = HttpResponse::newHttpJsonResponse(response);
                    callback(resp);
                  },
                  [=](const DrogonDbException& e) {
                    Json::Value error;
                    error["error"] =
                        "Database error: " + std::string(e.base().what());
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k500InternalServerError);
                    callback(resp);
                  },
                  std::stoi(conversation_id), std::stoi(current_user_id),
                  approval_message, std::stoi(proof_id));
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(proof_id), std::stoi(id));
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Reject a proof
void Offers::reject_proof(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id, const std::string& proof_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Parse request body
  auto json = req->getJsonObject();
  std::string reason = json ? (*json)["reason"].asString() : "";

  // Check if user is authorized (must be post owner)
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Update proof status
        db->execSqlAsync(
            "UPDATE product_proofs SET status = 'rejected' WHERE id = $1 AND "
            "offer_id = $2 RETURNING id",
            [=](const Result& updateResult) {
              if (updateResult.size() == 0) {
                Json::Value error;
                error["error"] = "Proof not found";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k404NotFound);
                callback(resp);
                return;
              }

              // Create or get conversation between the two users
              std::string conversation_id = create_or_get_conversation(
                  current_user_id, std::to_string(offer_user_id), id);

              if (conversation_id.empty()) {
                Json::Value error;
                error["error"] = "Failed to create conversation";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
              }

              // Add a message to the conversation about the proof rejection
              std::string rejection_message =
                  "Proof of product has been rejected";
              if (!reason.empty()) {
                rejection_message += "\nReason: " + reason;
              }

              db->execSqlAsync(
                  "INSERT INTO messages "
                  "(conversation_id, sender_id, content, context_type, "
                  "context_id) "
                  "VALUES ($1, $2, $3, 'proof_rejection', $4)",
                  [=](const Result&) {
                    Json::Value response;
                    response["status"] = "success";
                    response["message"] = "Proof rejected";

                    auto resp = HttpResponse::newHttpJsonResponse(response);
                    callback(resp);
                  },
                  [=](const DrogonDbException& e) {
                    Json::Value error;
                    error["error"] =
                        "Database error: " + std::string(e.base().what());
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k500InternalServerError);
                    callback(resp);
                  },
                  std::stoi(conversation_id), std::stoi(current_user_id),
                  rejection_message, std::stoi(proof_id));
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(proof_id), std::stoi(id));
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Create escrow transaction
void Offers::create_escrow(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
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
    return;
  }

  double amount = (*json)["amount"].asDouble();

  // Check if user is authorized (must be post owner)
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Create escrow transaction
        db->execSqlAsync(
            "INSERT INTO escrow_transactions "
            "(offer_id, buyer_id, seller_id, amount) "
            "VALUES ($1, $2, $3, $4) "
            "RETURNING id",
            [=](const Result& escrowResult) {
              if (escrowResult.size() == 0) {
                Json::Value error;
                error["error"] = "Failed to create escrow";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
              }

              int escrow_id = escrowResult[0]["id"].as<int>();

              // Create or get conversation between the two users
              std::string conversation_id = create_or_get_conversation(
                  current_user_id, std::to_string(offer_user_id), id);

              if (conversation_id.empty()) {
                Json::Value error;
                error["error"] = "Failed to create conversation";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
              }

              // Add a message to the conversation about the escrow
              std::string escrow_message =
                  "Escrow transaction created for $" + std::to_string(amount);

              db->execSqlAsync(
                  "INSERT INTO messages "
                  "(conversation_id, sender_id, content, context_type, "
                  "context_id) "
                  "VALUES ($1, $2, $3, 'escrow', $4)",
                  [=](const Result&) {
                    Json::Value response;
                    response["status"] = "success";
                    response["message"] = "Escrow created";
                    response["escrow_id"] = escrow_id;

                    auto resp = HttpResponse::newHttpJsonResponse(response);
                    callback(resp);
                  },
                  [=](const DrogonDbException& e) {
                    Json::Value error;
                    error["error"] =
                        "Database error: " + std::string(e.base().what());
                    auto resp = HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(k500InternalServerError);
                    callback(resp);
                  },
                  std::stoi(conversation_id), std::stoi(current_user_id),
                  escrow_message, escrow_id);
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(id), std::stoi(current_user_id), offer_user_id, amount);
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}

// Get escrow transaction for an offer
void Offers::get_escrow(const HttpRequestPtr& req,
                        std::function<void(const HttpResponsePtr&)>&& callback,
                        const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");
  auto db = app().getDbClient();

  // Check if user is authorized to view escrow
  db->execSqlAsync(
      "SELECT o.*, p.user_id AS post_user_id "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [=](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
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
          return;
        }

        // Get escrow transaction for this offer
        db->execSqlAsync(
            "SELECT et.*, "
            "buyer.username AS buyer_username, "
            "seller.username AS seller_username "
            "FROM escrow_transactions et "
            "JOIN users buyer ON et.buyer_id = buyer.id "
            "JOIN users seller ON et.seller_id = seller.id "
            "WHERE et.offer_id = $1 "
            "ORDER BY et.created_at DESC "
            "LIMIT 1",
            [=](const Result& escrowResult) {
              if (escrowResult.size() == 0) {
                Json::Value response;
                response["status"] = "success";
                response["has_escrow"] = false;

                auto resp = HttpResponse::newHttpJsonResponse(response);
                callback(resp);
                return;
              }

              const auto& row = escrowResult[0];

              Json::Value escrow;
              escrow["id"] = row["id"].as<int>();
              escrow["offer_id"] = row["offer_id"].as<int>();
              escrow["buyer_id"] = row["buyer_id"].as<int>();
              escrow["buyer_username"] =
                  row["buyer_username"].as<std::string>();
              escrow["seller_id"] = row["seller_id"].as<int>();
              escrow["seller_username"] =
                  row["seller_username"].as<std::string>();
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
            },
            [=](const DrogonDbException& e) {
              Json::Value error;
              error["error"] =
                  "Database error: " + std::string(e.base().what());
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            std::stoi(id));
      },
      [=](const DrogonDbException& e) {
        Json::Value error;
        error["error"] = "Database error: " + std::string(e.base().what());
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      std::stoi(id));
}
