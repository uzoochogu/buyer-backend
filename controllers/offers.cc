#include "offers.hpp"

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

#include <string>
#include <vector>

using namespace drogon;
using namespace drogon::orm;
using namespace api::v1;

// Get all offers for a post
void Offers::get_offers_for_post(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& post_id) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // First check if the user is the owner of the post to determine if they
  // should see private offers
  db->execSqlAsync(
      "SELECT user_id FROM posts WHERE id = $1",
      [callback, db, post_id, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Post not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int post_user_id = result[0]["user_id"].as<int>();
        bool is_post_owner = (post_user_id == std::stoi(current_user_id));

        // Query to get offers, including private ones if the user is the post
        // owner
        std::string query;
        if (is_post_owner) {
          query =
              "SELECT o.*, u.username FROM offers o "
              "JOIN users u ON o.user_id = u.id "
              "WHERE o.post_id = $1 "
              "ORDER BY o.created_at DESC";

          // Execute query with one parameter
          db->execSqlAsync(
              query,
              [callback, is_post_owner](const Result& offers_result) {
                Json::Value offers_array(Json::arrayValue);
                for (const auto& row : offers_result) {
                  Json::Value offer;
                  offer["id"] = row["id"].as<int>();
                  offer["post_id"] = row["post_id"].as<int>();
                  offer["user_id"] = row["user_id"].as<int>();
                  offer["username"] = row["username"].as<std::string>();
                  offer["title"] = row["title"].as<std::string>();
                  offer["description"] = row["description"].as<std::string>();
                  offer["price"] = row["price"].as<double>();
                  offer["original_price"] = row["original_price"].as<double>();
                  offer["is_public"] = row["is_public"].as<bool>();
                  offer["status"] = row["status"].as<std::string>();
                  offer["created_at"] = row["created_at"].as<std::string>();
                  offer["updated_at"] = row["updated_at"].as<std::string>();
                  offer["is_post_owner"] = is_post_owner;

                  offers_array.append(offer);
                }

                auto resp = HttpResponse::newHttpJsonResponse(offers_array);
                callback(resp);
              },
              [callback](const DrogonDbException& e) {
                LOG_ERROR << "Database error: " << e.base().what();
                Json::Value error;
                error["error"] = "Database error";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              post_id);
        } else {
          query =
              "SELECT o.*, u.username FROM offers o "
              "JOIN users u ON o.user_id = u.id "
              "WHERE o.post_id = $1 AND (o.is_public = TRUE OR o.user_id = $2) "
              "ORDER BY o.created_at DESC";

          // Execute query with two parameters
          db->execSqlAsync(
              query,
              [callback, is_post_owner](const Result& offers_result) {
                Json::Value offers_array(Json::arrayValue);
                for (const auto& row : offers_result) {
                  Json::Value offer;
                  offer["id"] = row["id"].as<int>();
                  offer["post_id"] = row["post_id"].as<int>();
                  offer["user_id"] = row["user_id"].as<int>();
                  offer["username"] = row["username"].as<std::string>();
                  offer["title"] = row["title"].as<std::string>();
                  offer["description"] = row["description"].as<std::string>();
                  offer["price"] = row["price"].as<double>();
                  offer["is_public"] = row["is_public"].as<bool>();
                  offer["status"] = row["status"].as<std::string>();
                  offer["created_at"] = row["created_at"].as<std::string>();
                  offer["updated_at"] = row["updated_at"].as<std::string>();
                  offer["is_post_owner"] = is_post_owner;

                  offers_array.append(offer);
                }

                auto resp = HttpResponse::newHttpJsonResponse(offers_array);
                callback(resp);
              },
              [callback](const DrogonDbException& e) {
                LOG_ERROR << "Database error: " << e.base().what();
                Json::Value error;
                error["error"] = "Database error";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              post_id, current_user_id);
        }
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      post_id);
}

// Create a new offer for a post
void Offers::create_offer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& post_id) {
  auto json = req->getJsonObject();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Extract offer data
  std::string title = (*json)["title"].asString();
  std::string description = (*json)["description"].asString();
  double price = (*json)["price"].asDouble();
  bool is_public =
      json->isMember("is_public") ? (*json)["is_public"].asBool() : true;

  auto db = app().getDbClient();

  // First check if the post exists and get the post owner
  db->execSqlAsync(
      "SELECT user_id FROM posts WHERE id = $1",
      [callback, db, post_id, current_user_id, title, description, price,
       is_public](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Post not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int post_user_id = result[0]["user_id"].as<int>();

        // Don't allow users to make offers on their own posts
        if (post_user_id == std::stoi(current_user_id)) {
          Json::Value error;
          error["error"] = "You cannot make offers on your own posts";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k400BadRequest);
          callback(resp);
          return;
        }

        // Insert the offer
        db->execSqlAsync(
            "INSERT INTO offers (post_id, user_id, title, description, price, "
            "original_price, is_public, status) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, 'pending') "
            "RETURNING id, created_at",
            [callback, db, post_user_id](const Result& insert_result) {
              if (insert_result.size() > 0) {
                int offer_id = insert_result[0]["id"].as<int>();

                // Create a notification for the post owner
                db->execSqlAsync(
                    "INSERT INTO offer_notifications (offer_id, user_id, "
                    "is_read) "
                    "VALUES ($1, $2, FALSE)",
                    [callback, offer_id](const Result&) {
                      Json::Value ret;
                      ret["status"] = "success";
                      ret["offer_id"] = offer_id;
                      auto resp = HttpResponse::newHttpJsonResponse(ret);
                      callback(resp);
                    },
                    [callback, offer_id](const DrogonDbException& e) {
                      LOG_ERROR << "Database error creating notification: "
                                << e.base().what();
                      Json::Value ret;
                      ret["status"] = "success";
                      ret["offer_id"] = offer_id;
                      ret["warning"] = "Offer created but notification failed";
                      auto resp = HttpResponse::newHttpJsonResponse(ret);
                      callback(resp);
                    },
                    offer_id, post_user_id);
              } else {
                Json::Value error;
                error["error"] = "Failed to create offer";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              }
            },
            [callback](const DrogonDbException& e) {
              LOG_ERROR << "Database error: " << e.base().what();
              Json::Value error;
              error["error"] = "Database error";
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            post_id, current_user_id, title, description, price, price,
            is_public);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      post_id);
}

// Get a specific offer
void Offers::get_offer(const HttpRequestPtr& req,
                       std::function<void(const HttpResponsePtr&)>&& callback,
                       const std::string& id) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  db->execSqlAsync(
      "SELECT o.*, u.username, p.user_id as post_owner_id "
      "FROM offers o "
      "JOIN users u ON o.user_id = u.id "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [callback, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        const auto& row = result[0];
        int offer_user_id = row["user_id"].as<int>();
        int post_owner_id = row["post_owner_id"].as<int>();
        bool is_public = row["is_public"].as<bool>();

        // Check if user has permission to view this offer
        bool can_view = (offer_user_id == std::stoi(current_user_id)) ||
                        (post_owner_id == std::stoi(current_user_id)) ||
                        is_public;

        if (!can_view) {
          Json::Value error;
          error["error"] = "You don't have permission to view this offer";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }

        Json::Value offer;
        offer["id"] = row["id"].as<int>();
        offer["post_id"] = row["post_id"].as<int>();
        offer["user_id"] = offer_user_id;
        offer["username"] = row["username"].as<std::string>();
        offer["title"] = row["title"].as<std::string>();
        offer["description"] = row["description"].as<std::string>();
        offer["price"] = row["price"].as<double>();
        offer["original_price"] = row["original_price"].as<double>();
        offer["is_public"] = is_public;
        offer["status"] = row["status"].as<std::string>();
        offer["created_at"] = row["created_at"].as<std::string>();
        offer["updated_at"] = row["updated_at"].as<std::string>();
        offer["is_owner"] = (offer_user_id == std::stoi(current_user_id));
        offer["is_post_owner"] = (post_owner_id == std::stoi(current_user_id));

        auto resp = HttpResponse::newHttpJsonResponse(offer);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      id);
}

// Update an offer
void Offers::update_offer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  auto json = req->getJsonObject();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  // First check if the user is the owner of the offer
  db->execSqlAsync(
      "SELECT user_id, status FROM offers WHERE id = $1",
      [callback, db, id, json, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int offer_user_id = result[0]["user_id"].as<int>();
        std::string status = result[0]["status"].as<std::string>();

        // Check if user has permission to update this offer
        if (offer_user_id != std::stoi(current_user_id)) {
          Json::Value error;
          error["error"] = "You don't have permission to update this offer";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }

        // Check if offer can be updated (only pending offers can be updated)
        if (status != "pending") {
          Json::Value error;
          error["error"] = "Only pending offers can be updated";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k400BadRequest);
          callback(resp);
          return;
        }

        // Extract updated fields with default values from the existing offer
        std::string title =
            json->isMember("title") ? (*json)["title"].asString() : "";
        std::string description = json->isMember("description")
                                      ? (*json)["description"].asString()
                                      : "";
        double price =
            json->isMember("price") ? (*json)["price"].asDouble() : 0.0;
        bool is_public =
            json->isMember("is_public") ? (*json)["is_public"].asBool() : true;

        // Use a fixed query with CASE expressions to handle optional updates
        db->execSqlAsync(
            "UPDATE offers SET "
            "title = CASE WHEN $2 = '' THEN title ELSE $2 END, "
            "description = CASE WHEN $3 = '' THEN description ELSE $3 END, "
            "price = CASE WHEN $4 = 0.0 THEN price ELSE $4 END, "
            "is_public = $5, "
            "updated_at = NOW() "
            "WHERE id = $1 "
            "RETURNING id",
            [callback](const Result& update_result) {
              if (update_result.size() > 0) {
                Json::Value ret;
                ret["status"] = "success";
                ret["message"] = "Offer updated successfully";
                auto resp = HttpResponse::newHttpJsonResponse(ret);
                callback(resp);
              } else {
                Json::Value error;
                error["error"] = "Failed to update offer";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              }
            },
            [callback](const DrogonDbException& e) {
              LOG_ERROR << "Database error: " << e.base().what();
              Json::Value error;
              error["error"] = "Database error";
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            id, title, description, price, is_public);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      id);
}

// Helper function to update message metadata when an offer or negotiation
// status changes
void update_message_metadata(std::shared_ptr<Transaction> transaction,
                             const std::string& id,
                             const std::string& new_status,
                             bool is_negotiation = false) {
  if (is_negotiation) {
    LOG_INFO << "Updating message metadata for price negotiation: " << id
             << " to status: " << new_status;

    transaction->execSqlAsync(
        "UPDATE messages "
        "SET metadata = jsonb_set(metadata, '{offer_status}', '\"" +
            new_status +
            "\"') "
            "WHERE context_id = $1 AND context_type = 'negotiation'",
        [=](const Result& result) {
          LOG_INFO << "Price negotiation message metadata updated successfully "
                      "for ID "
                   << id << " (rows affected: " << result.affectedRows() << ")";
        },
        [=](const DrogonDbException& e) {
          LOG_ERROR << "Database error updating price negotiation message "
                       "metadata for ID "
                    << id << ": " << e.base().what();
        },
        id);
  } else {
    LOG_INFO << "Updating message metadata for offer: " << id
             << " to status: " << new_status;

    transaction->execSqlAsync(
        "UPDATE messages "
        "SET metadata = jsonb_set(metadata, '{offer_status}', '\"" +
            new_status +
            "\"') "
            "WHERE metadata->>'offer_id' = $1 AND context_type = 'negotiation'",
        [=](const Result& result) {
          LOG_INFO << "Message metadata updated successfully for offer ID "
                   << id << " (rows affected: " << result.affectedRows() << ")";
        },
        [=](const DrogonDbException& e) {
          LOG_ERROR << "Database error updating message metadata for offer ID "
                    << id << ": " << e.base().what();
        },
        id);
  }
}

// Accept an offer
void Offers::accept_offer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  // First check if the user is the owner of the post
  db->execSqlAsync(
      "SELECT o.post_id, p.user_id as post_owner_id, o.status "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [callback, db, id, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int post_owner_id = result[0]["post_owner_id"].as<int>();
        int post_id = result[0]["post_id"].as<int>();
        std::string status = result[0]["status"].as<std::string>();

        // Check if user has permission to accept this offer
        if (post_owner_id != std::stoi(current_user_id)) {
          Json::Value error;
          error["error"] = "Only the post owner can accept offers";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }

        // Check if offer can be accepted (only pending offers can be accepted)
        if (status != "pending") {
          Json::Value error;
          error["error"] = "Only pending offers can be accepted";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k400BadRequest);
          callback(resp);
          return;
        }

        // Create a transaction to handle all the updates atomically
        db->newTransactionAsync([callback, id, post_id](
                                    const std::shared_ptr<Transaction>& trans) {
          if (!trans) {
            Json::Value error;
            error["error"] = "Failed to create transaction";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
            return;
          }

          // Update the accepted offer
          trans->execSqlAsync(
              "UPDATE offers SET status = 'accepted', updated_at = NOW() WHERE "
              "id = $1",
              [callback, trans, post_id, id](const Result& result) {
                // Update message metadata for the accepted offer
                update_message_metadata(trans, id, "accepted");

                // Get the latest price negotiation for this offer (if any) and
                // accept it
                trans->execSqlAsync(
                    "SELECT id FROM price_negotiations "
                    "WHERE offer_id = $1 AND status = 'pending' "
                    "ORDER BY created_at DESC LIMIT 1",
                    [callback, trans, post_id,
                     id](const Result& negotiation_result) {
                      // If there are pending negotiations, accept the latest
                      // one and reject others
                      if (negotiation_result.size() > 0) {
                        int latest_negotiation_id =
                            negotiation_result[0]["id"].as<int>();
                        std::string negotiation_id_str =
                            std::to_string(latest_negotiation_id);

                        // Accept the latest negotiation
                        trans->execSqlAsync(
                            "UPDATE price_negotiations "
                            "SET status = 'accepted', updated_at = NOW() "
                            "WHERE id = $1",
                            [callback, trans, post_id, id,
                             negotiation_id_str](const Result&) {
                              // Update message metadata for the accepted
                              // negotiation
                              update_message_metadata(trans, negotiation_id_str,
                                                      "accepted", true);

                              // Reject all other negotiations for this offer
                              trans->execSqlAsync(
                                  "UPDATE price_negotiations "
                                  "SET status = 'rejected', updated_at = NOW() "
                                  "WHERE offer_id = $1 AND id != $2 AND status "
                                  "= 'pending' "
                                  "RETURNING id",
                                  [callback, trans, post_id, id](
                                      const Result& other_negotiations_result) {
                                    LOG_INFO << "other_negotiations_result: "
                                             << other_negotiations_result.size()
                                             << "\n";
                                    // Update message metadata for rejected
                                    // negotiations
                                    for (const auto& row :
                                         other_negotiations_result) {
                                      std::string rejected_negotiation_id =
                                          std::to_string(row["id"].as<int>());
                                      update_message_metadata(
                                          trans, rejected_negotiation_id,
                                          "rejected", true);
                                    }

                                    // Continue with rejecting other offers
                                    trans->execSqlAsync(
                                        "UPDATE offers SET status = "
                                        "'rejected', updated_at = NOW() "
                                        "WHERE post_id = $1 AND id != $2 AND "
                                        "status = 'pending'",
                                        [callback, trans, post_id,
                                         id](const Result& result) {
                                          // Update message metadata for all
                                          // rejected offers
                                          for (const auto& row : result) {
                                            std::string rejected_offer_id =
                                                std::to_string(
                                                    row["id"].as<int>());
                                            update_message_metadata(
                                                trans, rejected_offer_id,
                                                "rejected");
                                          }

                                          // Reject all pending price
                                          // negotiations for other offers
                                          trans->execSqlAsync(
                                              "UPDATE price_negotiations pn "
                                              "SET status = 'rejected', "
                                              "updated_at = NOW() "
                                              "FROM offers o "
                                              "WHERE pn.offer_id = o.id AND "
                                              "o.post_id = $1 AND "
                                              "o.id != $2 AND pn.status = "
                                              "'pending' "
                                              "RETURNING pn.id",
                                              [callback, trans, post_id](
                                                  const Result&
                                                      rejected_negotiations_result) {
                                                // Update message metadata for
                                                // rejected negotiations from
                                                // other offers
                                                for (
                                                    const auto& row :
                                                    rejected_negotiations_result) {
                                                  std::string
                                                      rejected_negotiation_id =
                                                          std::to_string(
                                                              row["id"]
                                                                  .as<int>());
                                                  update_message_metadata(
                                                      trans,
                                                      rejected_negotiation_id,
                                                      "rejected", true);
                                                }

                                                // Update post status to reflect
                                                // that an offer was accepted
                                                trans->execSqlAsync(
                                                    "UPDATE posts SET "
                                                    "request_status = "
                                                    "'fulfilled' WHERE "
                                                    "id = $1",
                                                    [callback, trans](
                                                        const Result& result) {
                                                      // Commit the transaction
                                                      trans->setCommitCallback(
                                                          [callback](
                                                              bool committed) {
                                                            if (committed) {
                                                              Json::Value ret;
                                                              ret["status"] =
                                                                  "success";
                                                              ret["message"] =
                                                                  "Offer "
                                                                  "accepted "
                                                                  "successfull"
                                                                  "y";
                                                              auto resp =
                                                                  HttpResponse::
                                                                      newHttpJsonResponse(
                                                                          ret);
                                                              callback(resp);
                                                            } else {
                                                              Json::Value error;
                                                              error["error"] =
                                                                  "Failed to "
                                                                  "commit "
                                                                  "transaction";
                                                              auto resp =
                                                                  HttpResponse::
                                                                      newHttpJsonResponse(
                                                                          error);
                                                              resp->setStatusCode(
                                                                  k500InternalServerError);
                                                              callback(resp);
                                                            }
                                                          });
                                                    },
                                                    [callback, trans](
                                                        const DrogonDbException&
                                                            e) {
                                                      LOG_ERROR
                                                          << "Database error: "
                                                          << e.base().what();
                                                      trans->rollback();
                                                      Json::Value error;
                                                      error["error"] =
                                                          "Database error";
                                                      auto resp = HttpResponse::
                                                          newHttpJsonResponse(
                                                              error);
                                                      resp->setStatusCode(
                                                          k500InternalServerError);
                                                      callback(resp);
                                                    },
                                                    post_id);
                                              },
                                              [callback, trans](
                                                  const DrogonDbException& e) {
                                                LOG_ERROR << "Database error: "
                                                          << e.base().what();
                                                trans->rollback();
                                                Json::Value error;
                                                error["error"] =
                                                    "Database error";
                                                auto resp = HttpResponse::
                                                    newHttpJsonResponse(error);
                                                resp->setStatusCode(
                                                    k500InternalServerError);
                                                callback(resp);
                                              },
                                              post_id, id);
                                        },
                                        [callback,
                                         trans](const DrogonDbException& e) {
                                          LOG_ERROR << "Database error: "
                                                    << e.base().what();
                                          trans->rollback();
                                          Json::Value error;
                                          error["error"] = "Database error";
                                          auto resp =
                                              HttpResponse::newHttpJsonResponse(
                                                  error);
                                          resp->setStatusCode(
                                              k500InternalServerError);
                                          callback(resp);
                                        },
                                        post_id, id);
                                  },
                                  [callback,
                                   trans](const DrogonDbException& e) {
                                    LOG_ERROR << "Database error: "
                                              << e.base().what();
                                    trans->rollback();
                                    Json::Value error;
                                    error["error"] = "Database error";
                                    auto resp =
                                        HttpResponse::newHttpJsonResponse(
                                            error);
                                    resp->setStatusCode(
                                        k500InternalServerError);
                                    callback(resp);
                                  },
                                  id, negotiation_id_str);
                            },
                            [callback, trans](const DrogonDbException& e) {
                              LOG_ERROR << "Database error: "
                                        << e.base().what();
                              trans->rollback();
                              Json::Value error;
                              error["error"] = "Database error";
                              auto resp =
                                  HttpResponse::newHttpJsonResponse(error);
                              resp->setStatusCode(k500InternalServerError);
                              callback(resp);
                            },
                            negotiation_id_str);
                      } else {
                        // No pending negotiations, just reject other offers
                        trans->execSqlAsync(
                            "UPDATE offers SET status = 'rejected', updated_at "
                            "= NOW() "
                            "WHERE post_id = $1 AND id != $2 AND status = "
                            "'pending' RETURNING id",
                            [callback, trans, post_id,
                             id](const Result& result) {
                              // Update message metadata for all rejected offers
                              for (const auto& row : result) {
                                std::string rejected_offer_id =
                                    std::to_string(row["id"].as<int>());
                                update_message_metadata(
                                    trans, rejected_offer_id, "rejected");
                              }

                              // Reject all pending price negotiations for other
                              // offers
                              trans->execSqlAsync(
                                  "UPDATE price_negotiations pn "
                                  "SET status = 'rejected', updated_at = NOW() "
                                  "FROM offers o "
                                  "WHERE pn.offer_id = o.id AND o.post_id = $1 "
                                  "AND "
                                  "o.id != $2 AND pn.status = 'pending' "
                                  "RETURNING pn.id",
                                  [callback, trans,
                                   post_id](const Result&
                                                rejected_negotiations_result) {
                                    // Update message metadata for rejected
                                    // negotiations
                                    for (const auto& row :
                                         rejected_negotiations_result) {
                                      std::string rejected_negotiation_id =
                                          std::to_string(row["id"].as<int>());
                                      update_message_metadata(
                                          trans, rejected_negotiation_id,
                                          "rejected", true);
                                    }

                                    // Update post status to reflect that an
                                    // offer was accepted
                                    trans->execSqlAsync(
                                        "UPDATE posts SET request_status = "
                                        "'fulfilled' WHERE "
                                        "id = $1",
                                        [callback,
                                         trans](const Result& result) {
                                          // Commit the transaction
                                          trans->setCommitCallback(
                                              [callback](bool committed) {
                                                if (committed) {
                                                  Json::Value ret;
                                                  ret["status"] = "success";
                                                  ret["message"] =
                                                      "Offer accepted "
                                                      "successfully";
                                                  auto resp = HttpResponse::
                                                      newHttpJsonResponse(ret);
                                                  callback(resp);
                                                } else {
                                                  Json::Value error;
                                                  error["error"] =
                                                      "Failed to commit "
                                                      "transaction";
                                                  auto resp = HttpResponse::
                                                      newHttpJsonResponse(
                                                          error);
                                                  resp->setStatusCode(
                                                      k500InternalServerError);
                                                  callback(resp);
                                                }
                                              });
                                        },
                                        [callback,
                                         trans](const DrogonDbException& e) {
                                          LOG_ERROR << "Database error: "
                                                    << e.base().what();
                                          trans->rollback();
                                          Json::Value error;
                                          error["error"] = "Database error";
                                          auto resp =
                                              HttpResponse::newHttpJsonResponse(
                                                  error);
                                          resp->setStatusCode(
                                              k500InternalServerError);
                                          callback(resp);
                                        },
                                        post_id);
                                  },
                                  [callback,
                                   trans](const DrogonDbException& e) {
                                    LOG_ERROR << "Database error: "
                                              << e.base().what();
                                    trans->rollback();
                                    Json::Value error;
                                    error["error"] = "Database error";
                                    auto resp =
                                        HttpResponse::newHttpJsonResponse(
                                            error);
                                    resp->setStatusCode(
                                        k500InternalServerError);
                                    callback(resp);
                                  },
                                  post_id, id);
                            },
                            [callback, trans](const DrogonDbException& e) {
                              LOG_ERROR << "Database error: "
                                        << e.base().what();
                              trans->rollback();
                              Json::Value error;
                              error["error"] = "Database error";
                              auto resp =
                                  HttpResponse::newHttpJsonResponse(error);
                              resp->setStatusCode(k500InternalServerError);
                              callback(resp);
                            },
                            post_id, id);
                      }
                    },
                    [callback, trans](const DrogonDbException& e) {
                      LOG_ERROR << "Database error: " << e.base().what();
                      trans->rollback();
                      Json::Value error;
                      error["error"] = "Database error";
                      auto resp = HttpResponse::newHttpJsonResponse(error);
                      resp->setStatusCode(k500InternalServerError);
                      callback(resp);
                    },
                    id);
              },
              [callback, trans](const DrogonDbException& e) {
                LOG_ERROR << "Database error: " << e.base().what();
                trans->rollback();
                Json::Value error;
                error["error"] = "Database error";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              id);
        });
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      id);
}

// Accept a counter offer (for offer creators)
void Offers::accept_counter_offer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  // First check if the user is the creator of the offer
  db->execSqlAsync(
      "SELECT o.user_id, o.post_id, p.user_id as post_owner_id, o.status "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [callback, db, id, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int offer_user_id = result[0]["user_id"].as<int>();
        int post_owner_id = result[0]["post_owner_id"].as<int>();
        int post_id = result[0]["post_id"].as<int>();
        std::string status = result[0]["status"].as<std::string>();

        // Check if user has permission to accept this counter-offer
        if (offer_user_id != std::stoi(current_user_id)) {
          Json::Value error;
          error["error"] = "Only the offer creator can accept counter-offers";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }

        // Check if offer can be accepted (only pending offers can be accepted)
        if (status != "pending") {
          Json::Value error;
          error["error"] = "Only pending offers can be accepted";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k400BadRequest);
          callback(resp);
          return;
        }

        // Create a transaction to handle all the updates atomically
        db->newTransactionAsync([callback, id, post_id](
                                    const std::shared_ptr<Transaction>& trans) {
          if (!trans) {
            Json::Value error;
            error["error"] = "Failed to create transaction";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
            return;
          }

          // First, get the latest negotiated price and its ID
          trans->execSqlAsync(
              "SELECT id, proposed_price FROM price_negotiations "
              "WHERE offer_id = $1 AND status = 'pending' "
              "ORDER BY created_at DESC LIMIT 1",
              [callback, trans, post_id, id](const Result& price_result) {
                if (price_result.size() == 0) {
                  trans->rollback();
                  Json::Value error;
                  error["error"] = "No pending negotiations found";
                  auto resp = HttpResponse::newHttpJsonResponse(error);
                  resp->setStatusCode(k400BadRequest);
                  callback(resp);
                  return;
                }

                int latest_negotiation_id = price_result[0]["id"].as<int>();
                std::string negotiation_id_str =
                    std::to_string(latest_negotiation_id);
                double latest_price =
                    price_result[0]["proposed_price"].as<double>();

                // Update the accepted offer with the latest negotiated price
                trans->execSqlAsync(
                    "UPDATE offers SET status = 'accepted', price = $2, "
                    "updated_at = NOW() WHERE "
                    "id = $1",
                    [callback, trans, post_id, id, latest_price,
                     negotiation_id_str](const Result& result) {
                      // Update message metadata for the accepted offer
                      update_message_metadata(trans, id, "accepted");

                      // Accept only the latest negotiation
                      trans->execSqlAsync(
                          "UPDATE price_negotiations "
                          "SET status = 'accepted', updated_at = NOW() "
                          "WHERE id = $1",
                          [callback, trans, post_id, id,
                           negotiation_id_str](const Result& result) {
                            // Update message metadata for the accepted
                            // negotiation
                            update_message_metadata(trans, negotiation_id_str,
                                                    "accepted", true);

                            // Reject all other negotiations for this offer
                            trans->execSqlAsync(
                                "UPDATE price_negotiations "
                                "SET status = 'rejected', updated_at = NOW() "
                                "WHERE offer_id = $1 AND id != $2 AND status = "
                                "'pending' "
                                "RETURNING id",
                                [callback, trans, post_id,
                                 id](const Result&
                                         rejected_negotiations_result) {
                                  // Update message metadata for rejected
                                  // negotiations
                                  for (const auto& row :
                                       rejected_negotiations_result) {
                                    std::string rejected_negotiation_id =
                                        std::to_string(row["id"].as<int>());
                                    update_message_metadata(
                                        trans, rejected_negotiation_id,
                                        "rejected", true);
                                  }

                                  // Reject all other offers for this post
                                  trans->execSqlAsync(
                                      "UPDATE offers SET status = 'rejected', "
                                      "updated_at = NOW() "
                                      "WHERE post_id = $1 AND id != $2 AND "
                                      "status = 'pending' "
                                      "RETURNING id",
                                      [callback, trans, post_id,
                                       id](const Result&
                                               rejected_offers_result) {
                                        // Update message metadata for rejected
                                        // offers
                                        for (const auto& row :
                                             rejected_offers_result) {
                                          std::string rejected_offer_id =
                                              std::to_string(
                                                  row["id"].as<int>());
                                          update_message_metadata(
                                              trans, rejected_offer_id,
                                              "rejected");
                                        }

                                        // Reject all pending price negotiations
                                        // for other offers
                                        trans->execSqlAsync(
                                            "UPDATE price_negotiations pn "
                                            "SET status = 'rejected', "
                                            "updated_at = NOW() "
                                            "FROM offers o "
                                            "WHERE pn.offer_id = o.id AND "
                                            "o.post_id = $1 AND o.id != $2 AND "
                                            "pn.status = 'pending' "
                                            "RETURNING pn.id",
                                            [callback, trans, post_id](
                                                const Result&
                                                    other_negotiations_result) {
                                              // Update message metadata for
                                              // rejected negotiations from
                                              // other offers
                                              for (const auto& row :
                                                   other_negotiations_result) {
                                                std::string
                                                    rejected_negotiation_id =
                                                        std::to_string(
                                                            row["id"]
                                                                .as<int>());
                                                update_message_metadata(
                                                    trans,
                                                    rejected_negotiation_id,
                                                    "rejected", true);
                                              }

                                              // Update post status to reflect
                                              // that an offer was accepted
                                              trans->execSqlAsync(
                                                  "UPDATE posts SET "
                                                  "request_status = "
                                                  "'fulfilled' WHERE "
                                                  "id = $1",
                                                  [callback, trans](
                                                      const Result& result) {
                                                    // Commit the transaction
                                                    trans->setCommitCallback(
                                                        [callback](
                                                            bool committed) {
                                                          if (committed) {
                                                            Json::Value ret;
                                                            ret["status"] =
                                                                "success";
                                                            ret["message"] =
                                                                "Counter-offer "
                                                                "accepted "
                                                                "successfully";
                                                            auto resp =
                                                                HttpResponse::
                                                                    newHttpJsonResponse(
                                                                        ret);
                                                            callback(resp);
                                                          } else {
                                                            Json::Value error;
                                                            error["error"] =
                                                                "Failed to "
                                                                "commit "
                                                                "transaction";
                                                            auto resp =
                                                                HttpResponse::
                                                                    newHttpJsonResponse(
                                                                        error);
                                                            resp->setStatusCode(
                                                                k500InternalServerError);
                                                            callback(resp);
                                                          }
                                                        });
                                                  },
                                                  [callback, trans](
                                                      const DrogonDbException&
                                                          e) {
                                                    LOG_ERROR
                                                        << "Database error: "
                                                        << e.base().what();
                                                    trans->rollback();
                                                    Json::Value error;
                                                    error["error"] =
                                                        "Database error";
                                                    auto resp = HttpResponse::
                                                        newHttpJsonResponse(
                                                            error);
                                                    resp->setStatusCode(
                                                        k500InternalServerError);
                                                    callback(resp);
                                                  },
                                                  post_id);
                                            },
                                            [callback, trans](
                                                const DrogonDbException& e) {
                                              LOG_ERROR << "Database error: "
                                                        << e.base().what();
                                              trans->rollback();
                                              Json::Value error;
                                              error["error"] = "Database error";
                                              auto resp = HttpResponse::
                                                  newHttpJsonResponse(error);
                                              resp->setStatusCode(
                                                  k500InternalServerError);
                                              callback(resp);
                                            },
                                            post_id, id);
                                      },
                                      [callback,
                                       trans](const DrogonDbException& e) {
                                        LOG_ERROR << "Database error: "
                                                  << e.base().what();
                                        trans->rollback();
                                        Json::Value error;
                                        error["error"] = "Database error";
                                        auto resp =
                                            HttpResponse::newHttpJsonResponse(
                                                error);
                                        resp->setStatusCode(
                                            k500InternalServerError);
                                        callback(resp);
                                      },
                                      post_id, id);
                                },
                                [callback, trans](const DrogonDbException& e) {
                                  LOG_ERROR << "Database error: "
                                            << e.base().what();
                                  trans->rollback();
                                  Json::Value error;
                                  error["error"] = "Database error";
                                  auto resp =
                                      HttpResponse::newHttpJsonResponse(error);
                                  resp->setStatusCode(k500InternalServerError);
                                  callback(resp);
                                },
                                id, negotiation_id_str);
                          },
                          [callback, trans](const DrogonDbException& e) {
                            LOG_ERROR << "Database error: " << e.base().what();
                            trans->rollback();
                            Json::Value error;
                            error["error"] = "Database error";
                            auto resp =
                                HttpResponse::newHttpJsonResponse(error);
                            resp->setStatusCode(k500InternalServerError);
                            callback(resp);
                          },
                          negotiation_id_str);
                    },
                    [callback, trans](const DrogonDbException& e) {
                      LOG_ERROR << "Database error: " << e.base().what();
                      trans->rollback();
                      Json::Value error;
                      error["error"] = "Database error";
                      auto resp = HttpResponse::newHttpJsonResponse(error);
                      resp->setStatusCode(k500InternalServerError);
                      callback(resp);
                    },
                    id, latest_price);
              },
              [callback, trans](const DrogonDbException& e) {
                LOG_ERROR << "Database error: " << e.base().what();
                trans->rollback();
                Json::Value error;
                error["error"] = "Database error";
                auto resp = HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
              },
              id);
        });
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      id);
}

// Reject an offer
void Offers::reject_offer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  // First check if the user is the owner of the post
  db->execSqlAsync(
      "SELECT o.post_id, p.user_id as post_owner_id, o.status "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE o.id = $1",
      [callback, db, id, current_user_id](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] = "Offer not found";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        int post_owner_id = result[0]["post_owner_id"].as<int>();
        std::string status = result[0]["status"].as<std::string>();

        // Check if user has permission to reject this offer
        if (post_owner_id != std::stoi(current_user_id)) {
          Json::Value error;
          error["error"] = "Only the post owner can reject offers";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k403Forbidden);
          callback(resp);
          return;
        }

        // Check if offer can be rejected (only pending offers can be rejected)
        if (status != "pending") {
          Json::Value error;
          error["error"] = "Only pending offers can be rejected";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k400BadRequest);
          callback(resp);
          return;
        }

        // Update the offer status
        db->execSqlAsync(
            "UPDATE offers SET status = 'rejected', updated_at = NOW() WHERE "
            "id = $1",
            [callback](const Result&) {
              Json::Value ret;
              ret["status"] = "success";
              ret["message"] = "Offer rejected successfully";
              auto resp = HttpResponse::newHttpJsonResponse(ret);
              callback(resp);
            },
            [callback](const DrogonDbException& e) {
              LOG_ERROR << "Database error: " << e.base().what();
              Json::Value error;
              error["error"] = "Database error";
              auto resp = HttpResponse::newHttpJsonResponse(error);
              resp->setStatusCode(k500InternalServerError);
              callback(resp);
            },
            id);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      id);
}

// Get all offers made by the current user
void Offers::get_my_offers(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT o.*, p.content as post_content, u.username as "
      "post_owner_username "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "JOIN users u ON p.user_id = u.id "
      "WHERE o.user_id = $1 "
      "ORDER BY o.updated_at DESC",
      [callback](const Result& result) {
        Json::Value offers_array(Json::arrayValue);
        for (const auto& row : result) {
          Json::Value offer;
          offer["id"] = row["id"].as<int>();
          offer["post_id"] = row["post_id"].as<int>();
          offer["post_content"] = row["post_content"].as<std::string>();
          offer["post_owner_username"] =
              row["post_owner_username"].as<std::string>();
          offer["title"] = row["title"].as<std::string>();
          offer["description"] = row["description"].as<std::string>();
          offer["price"] = row["price"].as<double>();
          offer["original_price"] = row["original_price"].as<double>();
          offer["is_public"] = row["is_public"].as<bool>();
          offer["status"] = row["status"].as<std::string>();
          offer["created_at"] = row["created_at"].as<std::string>();
          offer["updated_at"] = row["updated_at"].as<std::string>();

          offers_array.append(offer);
        }

        auto resp = HttpResponse::newHttpJsonResponse(offers_array);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      current_user_id);
}

// Get all offers received for the current user's posts
void Offers::get_received_offers(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT o.*, p.content as post_content, u.username as offer_username "
      "FROM offers o "
      "JOIN posts p ON o.post_id = p.id "
      "JOIN users u ON o.user_id = u.id "
      "WHERE p.user_id = $1 "
      "ORDER BY o.updated_at DESC",
      [callback](const Result& result) {
        Json::Value offers_array(Json::arrayValue);
        for (const auto& row : result) {
          Json::Value offer;
          offer["id"] = row["id"].as<int>();
          offer["post_id"] = row["post_id"].as<int>();
          offer["post_content"] = row["post_content"].as<std::string>();
          offer["user_id"] = row["user_id"].as<int>();
          offer["offer_username"] = row["offer_username"].as<std::string>();
          offer["title"] = row["title"].as<std::string>();
          offer["description"] = row["description"].as<std::string>();
          offer["price"] = row["price"].as<double>();
          offer["original_price"] = row["original_price"].as<double>();
          offer["is_public"] = row["is_public"].as<bool>();
          offer["status"] = row["status"].as<std::string>();
          offer["created_at"] = row["created_at"].as<std::string>();
          offer["updated_at"] = row["updated_at"].as<std::string>();

          offers_array.append(offer);
        }

        auto resp = HttpResponse::newHttpJsonResponse(offers_array);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      current_user_id);
}

// Get notifications for the current user
void Offers::get_notifications(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  db->execSqlAsync(
      "SELECT n.id, n.offer_id, n.is_read, n.created_at, "
      "o.title as offer_title, o.status as offer_status, "
      "u.username as offer_username, p.content as post_content "
      "FROM offer_notifications n "
      "JOIN offers o ON n.offer_id = o.id "
      "JOIN users u ON o.user_id = u.id "
      "JOIN posts p ON o.post_id = p.id "
      "WHERE n.user_id = $1 "
      "ORDER BY n.created_at DESC",
      [callback](const Result& result) {
        Json::Value notifications_array(Json::arrayValue);
        for (const auto& row : result) {
          Json::Value notification;
          notification["id"] = row["id"].as<int>();
          notification["offer_id"] = row["offer_id"].as<int>();
          notification["is_read"] = row["is_read"].as<bool>();
          notification["created_at"] = row["created_at"].as<std::string>();
          notification["offer_title"] = row["offer_title"].as<std::string>();
          notification["offer_status"] = row["offer_status"].as<std::string>();
          notification["offer_username"] =
              row["offer_username"].as<std::string>();
          notification["post_content"] = row["post_content"].as<std::string>();

          notifications_array.append(notification);
        }

        auto resp = HttpResponse::newHttpJsonResponse(notifications_array);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      current_user_id);
}

// Mark a notification as read
void Offers::mark_notification_read(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  db->execSqlAsync(
      "UPDATE offer_notifications SET is_read = TRUE "
      "WHERE id = $1 AND user_id = $2 "
      "RETURNING id",
      [callback](const Result& result) {
        if (result.size() == 0) {
          Json::Value error;
          error["error"] =
              "Notification not found or you don't have permission";
          auto resp = HttpResponse::newHttpJsonResponse(error);
          resp->setStatusCode(k404NotFound);
          callback(resp);
          return;
        }

        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "Notification marked as read";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      id, current_user_id);
}

// Mark all notifications as read
void Offers::mark_all_notifications_read(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  db->execSqlAsync(
      "UPDATE offer_notifications SET is_read = TRUE "
      "WHERE user_id = $1 AND is_read = FALSE",
      [callback](const Result&) {
        Json::Value ret;
        ret["status"] = "success";
        ret["message"] = "All notifications marked as read";
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      },
      [callback](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
        Json::Value error;
        error["error"] = "Database error";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
      },
      current_user_id);
}
