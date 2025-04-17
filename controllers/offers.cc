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
            "is_public, status) "
            "VALUES ($1, $2, $3, $4, $5, $6, 'pending') "
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
            post_id, current_user_id, title, description, price, is_public);
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
                // Reject all other offers for this post
                trans->execSqlAsync(
                    "UPDATE offers SET status = 'rejected', updated_at = NOW() "
                    "WHERE post_id = $1 AND id != $2 AND status = 'pending'",
                    [callback, trans, post_id](const Result& result) {
                      // Update post status to reflect that an offer was
                      // accepted
                      trans->execSqlAsync(
                          "UPDATE posts SET request_status = 'fulfilled' WHERE "
                          "id = $1",
                          [callback, trans](const Result& result) {
                            // Commit the transaction
                            trans->setCommitCallback([callback](
                                                         bool committed) {
                              if (committed) {
                                Json::Value ret;
                                ret["status"] = "success";
                                ret["message"] = "Offer accepted successfully";
                                auto resp =
                                    HttpResponse::newHttpJsonResponse(ret);
                                callback(resp);
                              } else {
                                Json::Value error;
                                error["error"] = "Failed to commit transaction";
                                auto resp =
                                    HttpResponse::newHttpJsonResponse(error);
                                resp->setStatusCode(k500InternalServerError);
                                callback(resp);
                              }
                            });
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
                          post_id);
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
                    post_id, id);
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
