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

#include "../services/service_manager.hpp"
#include "../utilities/conversion.hpp"
#include "scenario_specific_utils.hpp"

using api::v1::Offers;
using drogon::app;
using drogon::HttpResponse;
using drogon::HttpResponsePtr;
using drogon::k400BadRequest;
using drogon::k403Forbidden;
using drogon::k404NotFound;
using drogon::k500InternalServerError;
using drogon::Task;
using drogon::orm::DrogonDbException;
using drogon::orm::Result;
using drogon::orm::Transaction;

// Get all offers for a post
Task<> Offers::get_offers_for_post(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string post_id) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  try {
    // First check if the user is the owner of the post to determine if they
    // should see private offers
    auto result = co_await db->execSqlCoro(
        "SELECT user_id FROM posts WHERE id = $1", std::stoi(post_id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Post not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int post_user_id = result[0]["user_id"].as<int>();
    bool is_post_owner = (post_user_id == std::stoi(current_user_id));

    // Query to get offers, including private ones if the user is the post owner
    std::string query;
    Result offers_result;

    if (is_post_owner) {
      query =
          "SELECT o.*, u.username FROM offers o "
          "JOIN users u ON o.user_id = u.id "
          "WHERE o.post_id = $1 "
          "ORDER BY o.created_at DESC";

      // Execute query with one parameter
      offers_result = co_await db->execSqlCoro(query, std::stoi(post_id));
    } else {
      query =
          "SELECT o.*, u.username FROM offers o "
          "JOIN users u ON o.user_id = u.id "
          "WHERE o.post_id = $1 AND (o.is_public = TRUE OR o.user_id = $2) "
          "ORDER BY o.created_at DESC";

      offers_result = co_await db->execSqlCoro(query, std::stoi(post_id),
                                               std::stoi(current_user_id));
    }

    Json::Value offers_array(Json::arrayValue);
    for (const auto& row : offers_result) {
      Json::Value offer;
      int offer_id = row["id"].as<int>();
      offer["id"] = offer_id;
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

      offer["media"] = co_await get_media_attachments("offer", offer_id);
      offers_array.append(offer);
    }

    auto resp = HttpResponse::newHttpJsonResponse(offers_array);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Create a new offer for a post
Task<> Offers::create_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string post_id) {
  auto json = req->getJsonObject();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Malformed route
  if (!convert::string_to_int(post_id).has_value() ||
      convert::string_to_int(post_id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid post id";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  // Extract offer data
  std::string title = (*json)["title"].asString();
  std::string description = (*json)["description"].asString();
  double price = (*json)["price"].asDouble();
  bool is_public =
      json->isMember("is_public") ? (*json)["is_public"].asBool() : true;

  Json::Value media_array = json->isMember("media")
                                ? (*json)["media"]
                                : Json::Value(Json::arrayValue);

  if (media_array.size() > MAX_MEDIA_SIZE) {
    Json::Value error;
    error["error"] = "Maximum of 5 media items allowed per offer";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // First check if the post exists and get the post owner
    auto result = co_await db->execSqlCoro(
        "SELECT user_id FROM posts WHERE id = $1", std::stoi(post_id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Post not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    int post_user_id = result[0]["user_id"].as<int>();

    // Don't allow users to make offers on their own posts
    if (post_user_id == std::stoi(current_user_id)) {
      Json::Value error;
      error["error"] = "You cannot make offers on your own posts";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    auto transaction = co_await db->newTransactionCoro();
    try {
      auto insert_result = co_await transaction->execSqlCoro(
          "INSERT INTO offers (post_id, user_id, title, description, price, "
          "original_price, is_public, status) "
          "VALUES ($1, $2, $3, $4, $5, $6, $7, 'pending') "
          "RETURNING id, created_at",
          std::stoi(post_id), std::stoi(current_user_id), title, description,
          price, price, is_public);

      if (insert_result.empty()) {
        throw std::runtime_error("Initial insert failed");
      }

      int offer_id = insert_result[0]["id"].as<int>();
      std::string created_at = insert_result[0]["created_at"].as<std::string>();
      std::size_t media_array_size = media_array.size();
      Json::Value processed_media = co_await process_media_attachments(
          std::move(media_array), transaction, std::stoi(current_user_id),
          "offer", offer_id);
      if (processed_media == Json::nullValue ||
          processed_media.size() < media_array_size) {
        LOG_ERROR << " Some Media info was not found";
        transaction->rollback();
        std::string error_string;
        for (const auto& media_item : processed_media) {
          error_string += media_item["file_name"].asString() + ", ";
        }
        Json::Value error;
        error["error"] = std::format(
            "Media info not found or processed, only the following media items "
            "were processed:\n{}",
            error_string);
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        co_return;
      }

      // notification:
      // post owner
      co_await transaction->execSqlCoro(
          "INSERT INTO offer_notifications (offer_id, user_id, is_read) "
          "VALUES ($1, $2, FALSE)",
          offer_id, post_user_id);

      // Auto-subscribe offer owner to their offer
      std::string offer_topic =
          create_topic("offer", insert_result[0]["id"].as<std::string>());
      ServiceManager::get_instance().get_subscriber().subscribe(offer_topic);
      ServiceManager::get_instance().get_connection_manager().subscribe(
          offer_topic, current_user_id);
      store_user_subscription(current_user_id, offer_topic);
      LOG_INFO << "User " << current_user_id
               << " subscribed to offer topic: " << offer_topic;

      Json::Value offer_data_json;
      offer_data_json["type"] = "offer_created";
      offer_data_json["id"] = offer_id;
      offer_data_json["message"] =
          "New Offer: " + std::to_string(price) + ", " + title;
      offer_data_json["modified_at"] = created_at;

      Json::FastWriter writer;
      std::string offer_data = writer.write(offer_data_json);

      std::string post_topic = create_topic("post", post_id);
      ServiceManager::get_instance().get_publisher().publish(post_topic,
                                                             offer_data);
      LOG_INFO << "Published new offer to post channel: " << post_topic;

      Json::Value ret;
      ret["status"] = "success";
      ret["offer_id"] = offer_id;
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } catch (const std::exception& e) {
      transaction->rollback();
      LOG_ERROR << "Error creating offer: " << e.what();
      Json::Value error;
      error["error"] = std::format("Error creating offer: {}", e.what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get a specific offer
Task<> Offers::get_offer(HttpRequestPtr req,
                         std::function<void(const HttpResponsePtr&)> callback,
                         std::string id) {
  auto db = app().getDbClient();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Malformed route
  if (!convert::string_to_int(id).has_value() ||
      convert::string_to_int(id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid offer id";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  try {
    int current_user = std::stoi(current_user_id);
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, u.username, p.user_id as post_owner_id "
        "FROM offers o "
        "JOIN users u ON o.user_id = u.id "
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

    const auto& row = result[0];
    int offer_user_id = row["user_id"].as<int>();
    int post_owner_id = row["post_owner_id"].as<int>();
    bool is_public = row["is_public"].as<bool>();

    // Check if user has permission to view this offer
    bool has_permission = (current_user == offer_user_id ||
                           current_user == post_owner_id || is_public);

    if (!has_permission) {
      Json::Value error;
      error["error"] = "You don't have permission to view this offer";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
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
    offer["is_owner"] = (current_user == offer_user_id);
    offer["is_post_owner"] = (current_user == post_owner_id);

    offer["media"] = co_await get_media_attachments("offer", std::stoi(id));

    auto resp = HttpResponse::newHttpJsonResponse(offer);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Update an offer
Task<> Offers::update_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  auto json = req->getJsonObject();
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  // Validation: Malformed route
  if (!convert::string_to_int(id).has_value() ||
      convert::string_to_int(id).value() < 0) {
    Json::Value error;
    error["error"] = "Invalid offer id";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  Json::Value media_array = json->isMember("media")
                                ? (*json)["media"]
                                : Json::Value(Json::arrayValue);

  if (media_array.size() > MAX_MEDIA_SIZE) {
    Json::Value error;
    error["error"] = "Maximum of 5 media items allowed per offer";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k400BadRequest);
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // First check if the user is the owner of the offer
    auto result = co_await db->execSqlCoro(
        "SELECT user_id, status FROM offers WHERE id = $1", std::stoi(id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Offer not found";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
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
      co_return;
    }

    // Check if offer can be updated (only pending offers can be updated)
    if (status != "pending") {
      Json::Value error;
      error["error"] = "Only pending offers can be updated";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    // Check which fields are provided for update
    bool has_title = json->isMember("title");
    bool has_description = json->isMember("description");
    bool has_price = json->isMember("price");
    bool has_is_public = json->isMember("is_public");

    // Extract values (empty/default if not provided)
    std::string title = has_title ? (*json)["title"].asString() : "";
    std::string description =
        has_description ? (*json)["description"].asString() : "";
    double price = has_price ? (*json)["price"].asDouble() : 0.0;
    bool is_public = has_is_public ? (*json)["is_public"].asBool() : true;

    auto transaction = co_await db->newTransactionCoro();

    try {
      // CASE expressions to handle optional updates
      auto update_result = co_await db->execSqlCoro(
          "UPDATE offers SET "
          "title = CASE WHEN $2 THEN $3 ELSE title END, "
          "description = CASE WHEN $4 THEN $5 ELSE description END, "
          "price = CASE WHEN $6 THEN $7 ELSE price END, "
          "is_public = CASE WHEN $8 THEN $9 ELSE is_public END, "
          "updated_at = NOW() "
          "WHERE id = $1 "
          "RETURNING id, updated_at",
          std::stoi(id), has_title, title, has_description, description,
          has_price, price, has_is_public, is_public);

      if (update_result.empty()) {
        throw std::runtime_error("initial update failed");
      }

      std::string updated_at = update_result[0]["updated_at"].as<std::string>();

      // Update media
      std::size_t media_array_size = media_array.size();
      Json::Value processed_media = co_await process_media_attachments(
          std::move(media_array), transaction, stoi(current_user_id), "offer",
          std::stoi(id));
      if (processed_media == Json::nullValue ||
          processed_media.size() < media_array_size) {
        LOG_ERROR << " Some Media info was not found";
        transaction->rollback();
        std::string error_string;
        for (const auto& media_item : processed_media) {
          error_string += media_item["file_name"].asString() + ", ";
        }
        Json::Value error;
        error["error"] = std::format(
            "Media info not found or processed, only the following media items "
            "were processed:\n{}",
            error_string);
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        co_return;
      }

      // notification
      std::string offer_topic = create_topic("offer", id);
      Json::Value offer_data_json;
      offer_data_json["type"] = "offer_updated";
      offer_data_json["id"] = id;
      offer_data_json["message"] = "Offer updated";
      offer_data_json["modified_at"] = updated_at;

      Json::FastWriter writer;
      std::string offer_data = writer.write(offer_data_json);

      ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                             offer_data);
      Json::Value ret;
      ret["status"] = "success";
      ret["message"] = "Offer updated successfully";
      ret["offer_id"] = id;
      if (!processed_media.empty()) {
        ret["media"] = processed_media;
      }
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);

    } catch (const std::exception& e) {
      transaction->rollback();
      LOG_ERROR << "Error updating offer: " << e.what();
      Json::Value error;
      error["error"] = std::format("Error updating offer: {}", e.what());
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Helper function to update message metadata when an offer or negotiation
// status changes
void update_message_metadata(std::shared_ptr<Transaction> transaction,
                             std::string id, std::string new_status,
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
Task<> Offers::accept_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    // First check if the user is the owner of the post
    auto result = co_await db->execSqlCoro(
        "SELECT o.post_id, p.user_id as post_owner_id, o.status "
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
      co_return;
    }

    // Check if offer can be accepted (only pending offers can be accepted)
    if (status != "pending") {
      Json::Value error;
      error["error"] = "Only pending offers can be accepted";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    // Create a transaction to handle all the updates atomically
    auto trans = co_await db->newTransactionCoro();

    try {
      // Update the accepted offer
      auto update_result = co_await trans->execSqlCoro(
          "UPDATE offers SET status = 'accepted', negotiation_status = "
          "'completed', updated_at = NOW() WHERE id = $1 "
          "RETURNING updated_at",
          std::stoi(id));

      // Get the latest price negotiation for this offer (if any) and accept it
      auto negotiation_result = co_await trans->execSqlCoro(
          "SELECT id FROM price_negotiations "
          "WHERE offer_id = $1 AND status = 'pending' "
          "ORDER BY created_at DESC LIMIT 1",
          std::stoi(id));

      drogon::orm::Result
          rejected_offers_result;  // nullptr, always inited in both branches

      // If there are pending negotiations, accept the latest one and reject
      // others
      if (!negotiation_result.empty()) {
        int latest_negotiation_id = negotiation_result[0]["id"].as<int>();
        std::string negotiation_id_str = std::to_string(latest_negotiation_id);

        // Accept the latest negotiation
        co_await trans->execSqlCoro(
            "UPDATE price_negotiations "
            "SET status = 'accepted', updated_at = NOW() "
            "WHERE id = $1",
            latest_negotiation_id);

        // Update message metadata for the accepted negotiation
        update_message_metadata(trans, negotiation_id_str, "accepted", true);

        // Reject all other negotiations for this offer
        auto other_negotiations_result = co_await trans->execSqlCoro(
            "UPDATE price_negotiations "
            "SET status = 'rejected', updated_at = NOW() "
            "WHERE offer_id = $1 AND id != $2 AND status = 'pending' "
            "RETURNING id",
            std::stoi(id), latest_negotiation_id);

        LOG_INFO << "other_negotiations_result: "
                 << other_negotiations_result.size() << "\n";

        // Update message metadata for rejected negotiations
        for (const auto& row : other_negotiations_result) {
          std::string rejected_negotiation_id = row["id"].as<std::string>();
          update_message_metadata(trans, rejected_negotiation_id, "rejected",
                                  true);
        }

        // Continue with rejecting other offers
        rejected_offers_result = co_await trans->execSqlCoro(
            "UPDATE offers SET status = 'rejected', negotiation_status = "
            "'completed', updated_at = NOW() WHERE post_id = $1 AND id != $2 "
            "AND status = 'pending' RETURNING id, updated_at",
            post_id, std::stoi(id));

        // Update message metadata for all rejected offers
        for (const auto& row : rejected_offers_result) {
          std::string rejected_offer_id = row["id"].as<std::string>();
          update_message_metadata(trans, rejected_offer_id, "rejected");
        }

        // Reject all pending price negotiations for other offers
        auto rejected_negotiations_result = co_await trans->execSqlCoro(
            "UPDATE price_negotiations pn "
            "SET status = 'rejected', updated_at = NOW() "
            "FROM offers o "
            "WHERE pn.offer_id = o.id AND o.post_id = $1 AND "
            "o.id != $2 AND pn.status = 'pending' "
            "RETURNING pn.id",
            post_id, std::stoi(id));

        // Update message metadata for rejected negotiations from other offers
        for (const auto& row : rejected_negotiations_result) {
          std::string rejected_negotiation_id = row["id"].as<std::string>();
          update_message_metadata(trans, rejected_negotiation_id, "rejected",
                                  true);
        }

        // Update post status to reflect that an offer was accepted
        co_await trans->execSqlCoro(
            "UPDATE posts SET request_status = 'fulfilled' WHERE id = $1",
            post_id);
      } else {
        // No pending negotiations, just reject other offers
        rejected_offers_result = co_await trans->execSqlCoro(
            "UPDATE offers SET status = 'rejected', updated_at = NOW() "
            "WHERE post_id = $1 AND id != $2 AND status = 'pending' "
            "RETURNING id, updated_at",
            post_id, std::stoi(id));

        // Update message metadata for all rejected offers
        for (const auto& row : rejected_offers_result) {
          std::string rejected_offer_id = row["id"].as<std::string>();
          update_message_metadata(trans, rejected_offer_id, "rejected");
        }

        // Reject all pending price negotiations for other offers
        auto rejected_negotiations_result = co_await trans->execSqlCoro(
            "UPDATE price_negotiations pn "
            "SET status = 'rejected', updated_at = NOW() "
            "FROM offers o "
            "WHERE pn.offer_id = o.id AND o.post_id = $1 AND "
            "o.id != $2 AND pn.status = 'pending' "
            "RETURNING pn.id",
            post_id, std::stoi(id));

        // Update message metadata for rejected negotiations
        for (const auto& row : rejected_negotiations_result) {
          std::string rejected_negotiation_id =
              std::to_string(row["id"].as<int>());
          update_message_metadata(trans, rejected_negotiation_id, "rejected",
                                  true);
        }

        // Update post status to reflect that an offer was accepted
        co_await trans->execSqlCoro(
            "UPDATE posts SET request_status = 'fulfilled' WHERE id = $1",
            post_id);
      }

      // notify only after a successful commit
      // notify the accepted offer
      std::string offer_topic = create_topic("offer", id);
      // create offer data
      Json::Value offer_data_json;
      offer_data_json["type"] = "offer_accepted";
      offer_data_json["id"] = id;
      offer_data_json["message"] = "Offer was accepted";
      offer_data_json["modified_at"] =
          update_result[0]["updated_at"].as<std::string>();

      Json::FastWriter writer;
      std::string offer_data = writer.write(offer_data_json);

      // Publish to offer subscribers
      ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                             offer_data);

      // notify rejected offers
      for (const auto& row : rejected_offers_result) {
        std::string rejected_offer_id = row["id"].as<std::string>();

        offer_topic = create_topic("offer", rejected_offer_id);

        // create offer data
        offer_data_json["type"] = "offer_rejected";
        offer_data_json["id"] = rejected_offer_id;
        offer_data_json["message"] = "Offer was rejected";
        offer_data_json["modified_at"] =
            rejected_offers_result[0]["updated_at"].as<std::string>();

        offer_data = writer.write(offer_data_json);

        ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                               offer_data);
      }

      // Delete all offer and subscriptions for that post?

      Json::Value ret;
      ret["status"] = "success";
      ret["message"] = "Offer accepted successfully";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      trans->rollback();
      Json::Value error;
      error["error"] = "Database error";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  } catch (const std::exception& e) {
    LOG_ERROR << "Error: " << e.what();
    Json::Value error;
    error["error"] = "Internal server error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Accept a counter offer (for offer creators)
Task<> Offers::accept_counter_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    // First check if the user is the creator of the offer
    auto result = co_await db->execSqlCoro(
        "SELECT o.user_id, o.post_id, p.user_id as post_owner_id, o.status "
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
    int post_id = result[0]["post_id"].as<int>();
    std::string status = result[0]["status"].as<std::string>();

    // Check if user has permission to accept this counter-offer
    if (offer_user_id != std::stoi(current_user_id)) {
      Json::Value error;
      error["error"] = "Only the offer creator can accept counter-offers";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Check if offer can be accepted (only pending offers can be accepted)
    if (status != "pending") {
      Json::Value error;
      error["error"] = "Only pending offers can be accepted";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    // Create a transaction to handle all the updates atomically
    auto trans = co_await db->newTransactionCoro();

    try {
      // First, get the latest negotiated price and its ID
      auto price_result = co_await trans->execSqlCoro(
          "SELECT id, proposed_price FROM price_negotiations "
          "WHERE offer_id = $1 AND status = 'pending' "
          "ORDER BY created_at DESC LIMIT 1",
          std::stoi(id));

      if (price_result.empty()) {
        Json::Value error;
        error["error"] = "No pending negotiations found";
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k400BadRequest);
        callback(resp);
        co_return;
      }

      int latest_negotiation_id = price_result[0]["id"].as<int>();
      std::string negotiation_id_str = std::to_string(latest_negotiation_id);
      double latest_price = price_result[0]["proposed_price"].as<double>();

      // Update the accepted offer with the latest negotiated price
      auto update_result = co_await trans->execSqlCoro(
          "UPDATE offers SET status = 'accepted', price = $2, "
          "negotiation_status = 'completed', updated_at = NOW() WHERE id = $1 "
          "RETURNING updated_at",
          std::stoi(id), latest_price);

      // Update message metadata for the accepted offer
      update_message_metadata(trans, id, "accepted");

      // Accept only the latest negotiation
      co_await trans->execSqlCoro(
          "UPDATE price_negotiations "
          "SET status = 'accepted', updated_at = NOW() "
          "WHERE id = $1",
          latest_negotiation_id);

      // Update message metadata for the accepted negotiation
      update_message_metadata(trans, negotiation_id_str, "accepted", true);

      // Reject all other negotiations for this offer
      auto rejected_negotiations_result = co_await trans->execSqlCoro(
          "UPDATE price_negotiations "
          "SET status = 'rejected', updated_at = NOW() "
          "WHERE offer_id = $1 AND id != $2 AND status = 'pending' "
          "RETURNING id",
          std::stoi(id), latest_negotiation_id);

      // Update message metadata for rejected negotiations
      for (const auto& row : rejected_negotiations_result) {
        std::string rejected_negotiation_id = row["id"].as<std::string>();
        update_message_metadata(trans, rejected_negotiation_id, "rejected",
                                true);
      }

      // Reject all other offers for this post
      auto rejected_offers_result = co_await trans->execSqlCoro(
          "UPDATE offers SET status = 'rejected', negotiation_status = "
          "'completed', updated_at = NOW()"
          "WHERE post_id = $1 AND id != $2 AND status = 'pending' RETURNING "
          "id, updated_at",
          post_id, std::stoi(id));

      // Update message metadata for rejected offers
      for (const auto& row : rejected_offers_result) {
        std::string rejected_offer_id = row["id"].as<std::string>();
        update_message_metadata(trans, rejected_offer_id, "rejected");
      }

      // Reject all pending price negotiations for other offers
      auto other_negotiations_result = co_await trans->execSqlCoro(
          "UPDATE price_negotiations pn "
          "SET status = 'rejected', updated_at = NOW() "
          "FROM offers o "
          "WHERE pn.offer_id = o.id AND o.post_id = $1 AND o.id != $2 AND "
          "pn.status = 'pending' "
          "RETURNING pn.id",
          post_id, std::stoi(id));

      // Update message metadata for rejected negotiations from other offers
      for (const auto& row : other_negotiations_result) {
        std::string rejected_negotiation_id = row["id"].as<std::string>();
        update_message_metadata(trans, rejected_negotiation_id, "rejected",
                                true);
      }

      // Update post status to reflect that an offer was accepted
      co_await trans->execSqlCoro(
          "UPDATE posts SET request_status = 'fulfilled' WHERE id = $1",
          post_id);

      // notify accepted offer only after a successful commit
      std::string offer_topic = create_topic("offer", id);
      Json::Value offer_data_json;
      offer_data_json["type"] = "offer_accepted";
      offer_data_json["id"] = id;
      offer_data_json["message"] = "Offer was accepted";
      offer_data_json["modified_at"] =
          update_result[0]["updated_at"].as<std::string>();

      Json::FastWriter writer;
      std::string offer_data = writer.write(offer_data_json);

      // Publish to offer subscribers
      ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                             offer_data);

      // notify other rejected offers
      for (const auto& row : rejected_offers_result) {
        std::string rejected_offer_id = row["id"].as<std::string>();

        offer_topic = create_topic("offer", rejected_offer_id);

        // create offer data
        offer_data_json["type"] = "offer_rejected";
        offer_data_json["id"] = rejected_offer_id;
        offer_data_json["message"] = "Offer was rejected";
        offer_data_json["modified_at"] =
            rejected_offers_result[0]["updated_at"].as<std::string>();

        offer_data = writer.write(offer_data_json);

        ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                               offer_data);
      }

      // delete all offers subscriptions for this post?
      // co_await trans->execSqlCoro(
      //     "DELETE FROM user_subscriptions WHERE subscription = $1",
      //     post_topic);
      //

      Json::Value ret;
      ret["status"] = "success";
      ret["message"] = "Counter-offer accepted successfully";
      auto resp = HttpResponse::newHttpJsonResponse(ret);
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      trans->rollback();
      Json::Value error;
      error["error"] = "Database error";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k500InternalServerError);
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Reject an offer
Task<> Offers::reject_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    // First check if the user is the owner of the post
    auto result = co_await db->execSqlCoro(
        "SELECT o.post_id, p.user_id as post_owner_id, o.status "
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

    int post_owner_id = result[0]["post_owner_id"].as<int>();
    std::string status = result[0]["status"].as<std::string>();

    // Check if user has permission to reject this offer
    if (post_owner_id != std::stoi(current_user_id)) {
      Json::Value error;
      error["error"] = "Only the post owner can reject offers";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k403Forbidden);
      callback(resp);
      co_return;
    }

    // Check if offer can be rejected (only pending offers can be rejected)
    if (status != "pending") {
      Json::Value error;
      error["error"] = "Only pending offers can be rejected";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k400BadRequest);
      callback(resp);
      co_return;
    }

    // Update the offer status
    auto update_result = co_await db->execSqlCoro(
        "UPDATE offers SET status = 'rejected', negotiation_status = "
        "'completed', updated_at = NOW() WHERE id = $1 "
        "RETURNING updated_at",
        std::stoi(id));

    // notify the rejected offer
    std::string offer_topic = create_topic("offer", id);

    Json::Value offer_data_json;
    offer_data_json["type"] = "offer_rejected";
    offer_data_json["id"] = id;
    offer_data_json["message"] = "Offer was rejected";
    offer_data_json["modified_at"] =
        update_result[0]["updated_at"].as<std::string>();

    Json::FastWriter writer;
    std::string offer_data = writer.write(offer_data_json);

    // Publish to offer subscribers
    ServiceManager::get_instance().get_publisher().publish(offer_topic,
                                                           offer_data);
    LOG_INFO << "Rejected offer: " << offer_topic;

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Offer rejected successfully";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get all offers made by the current user
Task<> Offers::get_my_offers(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.content as post_content, u.username as "
        "post_owner_username "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "JOIN users u ON p.user_id = u.id "
        "WHERE o.user_id = $1 "
        "ORDER BY o.updated_at DESC",
        std::stoi(current_user_id));

    Json::Value offers_array(Json::arrayValue);
    for (const auto& row : result) {
      Json::Value offer;
      int offer_id = row["id"].as<int>();
      offer["id"] = offer_id;
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

      offer["media"] = co_await get_media_attachments("offer", offer_id);
      offers_array.append(offer);
    }

    auto resp = HttpResponse::newHttpJsonResponse(offers_array);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get all offers received for the current user's posts
Task<> Offers::get_received_offers(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, p.content as post_content, u.username as offer_username "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "JOIN users u ON o.user_id = u.id "
        "WHERE p.user_id = $1 "
        "ORDER BY o.updated_at DESC",
        std::stoi(current_user_id));

    Json::Value offers_array(Json::arrayValue);
    for (const auto& row : result) {
      Json::Value offer;
      int offer_id = row["id"].as<int>();
      offer["id"] = offer_id;
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

      offer["media"] = co_await get_media_attachments("offer", offer_id);
      offers_array.append(offer);
    }

    auto resp = HttpResponse::newHttpJsonResponse(offers_array);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Get notifications for the current user
Task<> Offers::get_notifications(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "SELECT n.id, n.offer_id, n.is_read, n.created_at, "
        "o.title as offer_title, o.status as offer_status, "
        "u.username as offer_username, p.content as post_content "
        "FROM offer_notifications n "
        "JOIN offers o ON n.offer_id = o.id "
        "JOIN users u ON o.user_id = u.id "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE n.user_id = $1 "
        "ORDER BY n.created_at DESC",
        std::stoi(current_user_id));

    Json::Value notifications_array(Json::arrayValue);
    for (const auto& row : result) {
      Json::Value notification;
      notification["id"] = row["id"].as<int>();
      notification["offer_id"] = row["offer_id"].as<int>();
      notification["is_read"] = row["is_read"].as<bool>();
      notification["created_at"] = row["created_at"].as<std::string>();
      notification["offer_title"] = row["offer_title"].as<std::string>();
      notification["offer_status"] = row["offer_status"].as<std::string>();
      notification["offer_username"] = row["offer_username"].as<std::string>();
      notification["post_content"] = row["post_content"].as<std::string>();

      notifications_array.append(notification);
    }

    auto resp = HttpResponse::newHttpJsonResponse(notifications_array);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Mark a notification as read
Task<> Offers::mark_notification_read(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    auto result = co_await db->execSqlCoro(
        "UPDATE offer_notifications SET is_read = TRUE "
        "WHERE id = $1 AND user_id = $2 "
        "RETURNING id",
        std::stoi(id), std::stoi(current_user_id));

    if (result.empty()) {
      Json::Value error;
      error["error"] = "Notification not found or you don't have permission";
      auto resp = HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(k404NotFound);
      callback(resp);
      co_return;
    }

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "Notification marked as read";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}

// Mark all notifications as read
Task<> Offers::mark_all_notifications_read(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  auto db = app().getDbClient();

  try {
    co_await db->execSqlCoro(
        "UPDATE offer_notifications SET is_read = TRUE "
        "WHERE user_id = $1 AND is_read = FALSE",
        std::stoi(current_user_id));

    Json::Value ret;
    ret["status"] = "success";
    ret["message"] = "All notifications marked as read";
    auto resp = HttpResponse::newHttpJsonResponse(ret);
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    Json::Value error;
    error["error"] = "Database error";
    auto resp = HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(k500InternalServerError);
    callback(resp);
  }

  co_return;
}
