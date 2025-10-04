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
#include "../utilities/json_manipulation.hpp"
#include "common_req_n_resp.hpp"
#include "scenario_specific_utils.hpp"

using api::v1::Offers;
using drogon::app;
using drogon::CT_APPLICATION_JSON;
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
struct OfferInfo {
  int id;
  int post_id;
  int user_id;
  std::string username;
  std::string title;
  std::string description;
  double price;
  double original_price;
  bool is_public;
  std::string status;
  std::string created_at;
  std::string updated_at;
  bool is_post_owner;
  std::vector<MediaQuickInfo> media;
};

struct CreateOfferRequest {
  std::string title;
  std::string description;
  double price;
  std::optional<bool> is_public{true};
  std::optional<std::vector<std::string>> media;  // Object keys
};

struct CreateOfferResponse {
  std::string status;
  int offer_id;
};

struct GetOfferResponse {
  int id;
  int post_id;
  int user_id;
  std::string username;
  std::string title;
  std::string description;
  double price;
  double original_price;
  bool is_public;
  std::string status;
  std::string created_at;
  std::string updated_at;
  bool is_owner;
  bool is_post_owner;
  std::vector<MediaQuickInfo> media;
};

struct UpdateOfferRequest {
  std::optional<std::string> title;
  std::optional<std::string> description;
  std::optional<double> price;
  std::optional<bool> is_public;
  std::optional<std::vector<std::string>> media;
};

struct UpdateOfferResponse {
  std::string status;
  std::string message;
  std::string offer_id;
  std::optional<std::vector<MediaQuickInfo>> media;
};

struct MyOfferInfo {
  int id;
  int post_id;
  std::string post_content;
  std::string post_owner_username;
  std::string title;
  std::string description;
  double price;
  double original_price;
  bool is_public;
  std::string status;
  std::string created_at;
  std::string updated_at;
  std::vector<MediaQuickInfo> media;
};

struct ReceivedOfferInfo {
  int id;
  int post_id;
  std::string post_content;
  int user_id;
  std::string offer_username;
  std::string title;
  std::string description;
  double price;
  double original_price;
  bool is_public;
  std::string status;
  std::string created_at;
  std::string updated_at;
  std::vector<MediaQuickInfo> media;
};

struct NotificationInfo {
  int id;
  int offer_id;
  bool is_read;
  std::string created_at;
  std::string offer_title;
  std::string offer_status;
  std::string offer_username;
  std::string post_content;
};

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
    auto result =
        co_await db->execSqlCoro("SELECT user_id FROM posts WHERE id = $1",
                                 convert::string_to_int(post_id).value());

    if (result.empty()) {
      SimpleError error{.error = "Post not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    int post_user_id = result[0]["user_id"].as<int>();
    bool is_post_owner =
        (post_user_id == convert::string_to_int(current_user_id).value());

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
      offers_result = co_await db->execSqlCoro(
          query, convert::string_to_int(post_id).value());
    } else {
      query =
          "SELECT o.*, u.username FROM offers o "
          "JOIN users u ON o.user_id = u.id "
          "WHERE o.post_id = $1 AND (o.is_public = TRUE OR o.user_id = $2) "
          "ORDER BY o.created_at DESC";

      offers_result = co_await db->execSqlCoro(
          query, convert::string_to_int(post_id).value(),
          convert::string_to_int(current_user_id).value());
    }

    std::vector<OfferInfo> offers_data;
    offers_data.reserve(offers_result.size());
    for (const auto& row : offers_result) {
      int offer_id = row["id"].as<int>();
      auto media_attachments =
          co_await get_media_attachments("offer", offer_id);
      offers_data.push_back(
          OfferInfo{.id = offer_id,
                    .post_id = row["post_id"].as<int>(),
                    .user_id = row["user_id"].as<int>(),
                    .username = row["username"].as<std::string>(),
                    .title = row["title"].as<std::string>(),
                    .description = row["description"].as<std::string>(),
                    .price = row["price"].as<double>(),
                    .original_price = row["original_price"].as<double>(),
                    .is_public = row["is_public"].as<bool>(),
                    .status = row["status"].as<std::string>(),
                    .created_at = row["created_at"].as<std::string>(),
                    .updated_at = row["updated_at"].as<std::string>(),
                    .is_post_owner = is_post_owner,
                    .media = media_attachments.value_or({})});
    }

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(offers_data).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  }

  co_return;
}

// Create a new offer for a post
Task<> Offers::create_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string post_id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  if (!convert::string_to_int(post_id).has_value() ||
      convert::string_to_int(post_id).value() < 0) {
    SimpleError error{.error = "Invalid post id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  CreateOfferRequest create_req;
  auto parse_error = utilities::strict_read_json(create_req, req->getBody());

  if (parse_error || create_req.title.empty() ||
      create_req.description.empty() || create_req.price <= 0.0) {
    SimpleError error{.error = "Invalid JSON or missing fields"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  if (create_req.media.has_value() &&
      create_req.media->size() > service::MAX_MEDIA_SIZE) {
    SimpleError error{.error = "Maximum of 5 media items allowed per offer"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // First check if the post exists and get the post owner
    auto result =
        co_await db->execSqlCoro("SELECT user_id FROM posts WHERE id = $1",
                                 convert::string_to_int(post_id).value());

    if (result.empty()) {
      SimpleError error{.error = "Post not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    int post_user_id = result[0]["user_id"].as<int>();

    // Don't allow users to make offers on their own posts
    if (post_user_id == convert::string_to_int(current_user_id).value()) {
      SimpleError error{.error = "You cannot make offers on your own posts"};
      auto resp =
          HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    auto transaction = co_await db->newTransactionCoro();
    try {
      auto insert_result = co_await transaction->execSqlCoro(
          "INSERT INTO offers (post_id, user_id, title, description, price, "
          "original_price, is_public, status) "
          "VALUES ($1, $2, $3, $4, $5, $5, $6, 'pending') "
          "RETURNING id, created_at",
          convert::string_to_int(post_id).value(),
          convert::string_to_int(current_user_id).value(), create_req.title,
          create_req.description, create_req.price,
          create_req.is_public.value_or(true));

      if (insert_result.empty()) {
        throw std::runtime_error("Initial insert failed");
      }

      int offer_id = insert_result[0]["id"].as<int>();
      std::string created_at = insert_result[0]["created_at"].as<std::string>();
      std::expected<std::vector<MediaQuickInfo>, std::string> processed_media;

      if (create_req.media.has_value()) {
        std::size_t media_array_size = create_req.media->size();
        processed_media = (co_await process_media_attachments(
            std::move(create_req.media.value()), transaction,
            *convert::string_to_int(current_user_id), "offer", offer_id));
        if (!processed_media.has_value() ||
            (processed_media->size() < media_array_size)) {
          LOG_ERROR << " Some Media info was not found";
          transaction->rollback();
          std::string error_string;
          for (const auto& media_item : *processed_media) {
            error_string += media_item.filename + ", ";
          }
          SimpleError error{
              .error = std::format("Media info not found or processed, only "
                                   "the following media items "
                                   "were processed:\n{}",
                                   error_string)};
          auto resp = HttpResponse::newHttpResponse(k400BadRequest,
                                                    CT_APPLICATION_JSON);
          resp->setBody(glz::write_json(error).value_or(""));
          callback(resp);
          co_return;
        }
      }

      // notification:
      // post owner
      co_await transaction->execSqlCoro(
          "INSERT INTO offer_notifications (offer_id, user_id, is_read) "
          "VALUES ($1, $2, FALSE)",
          offer_id, post_user_id);

      // Auto-subscribe offer owner to their offer
      std::string offer_topic = create_topic("offer", std::to_string(offer_id));
      ServiceManager::get_instance().get_subscriber().subscribe(offer_topic);
      ServiceManager::get_instance().get_connection_manager().subscribe(
          offer_topic, current_user_id);
      store_user_subscription(current_user_id, offer_topic);
      LOG_INFO << "User " << current_user_id
               << " subscribed to offer topic: " << offer_topic;

      NotificationMessage msg{
          .type = "offer_created",
          .id = std::to_string(offer_id),
          .message = "New Offer: " + std::to_string(create_req.price) + ", " +
                     create_req.title,
          .modified_at = created_at};

      std::string post_topic = create_topic("post", post_id);
      ServiceManager::get_instance().get_publisher().publish(
          post_topic, glz::write_json(msg).value_or(""));
      LOG_INFO << "Published new offer to post channel: " << post_topic;

      CreateOfferResponse response{.status = "success", .offer_id = offer_id};
      auto resp =
          HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(response).value_or(""));
      callback(resp);
    } catch (const std::exception& e) {
      transaction->rollback();
      LOG_ERROR << "Error creating offer: " << e.what();
      SimpleError error{.error =
                            std::format("Error creating offer: {}", e.what())};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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

  if (!convert::string_to_int(id).has_value() ||
      convert::string_to_int(id).value() < 0) {
    SimpleError error{.error = "Invalid offer id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  try {
    int current_user = convert::string_to_int(current_user_id).value();
    auto result = co_await db->execSqlCoro(
        "SELECT o.*, u.username, p.user_id as post_owner_id "
        "FROM offers o "
        "JOIN users u ON o.user_id = u.id "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        convert::string_to_int(id).value());

    if (result.empty()) {
      SimpleError error{.error = "Offer not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
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
      SimpleError error{.error =
                            "You don't have permission to view this offer"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }
    auto media_attachments =
        co_await get_media_attachments("offer", *convert::string_to_int(id));
    GetOfferResponse response{
        .id = row["id"].as<int>(),
        .post_id = row["post_id"].as<int>(),
        .user_id = offer_user_id,
        .username = row["username"].as<std::string>(),
        .title = row["title"].as<std::string>(),
        .description = row["description"].as<std::string>(),
        .price = row["price"].as<double>(),
        .original_price = row["original_price"].as<double>(),
        .is_public = is_public,
        .status = row["status"].as<std::string>(),
        .created_at = row["created_at"].as<std::string>(),
        .updated_at = row["updated_at"].as<std::string>(),
        .is_owner = (current_user == offer_user_id),
        .is_post_owner = (current_user == post_owner_id),
        .media = media_attachments.value_or({})};

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(response).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  }

  co_return;
}

// Update an offer
Task<> Offers::update_offer(
    HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
    std::string id) {
  std::string current_user_id =
      req->getAttributes()->get<std::string>("current_user_id");

  if (!convert::string_to_int(id).has_value() ||
      convert::string_to_int(id).value() < 0) {
    SimpleError error{.error = "Invalid offer id"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  UpdateOfferRequest update_req;
  auto parse_error = utilities::strict_read_json(update_req, req->getBody());

  if (parse_error) {
    SimpleError error{.error = "Invalid JSON"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  if (update_req.media.has_value() &&
      update_req.media->size() > service::MAX_MEDIA_SIZE) {
    SimpleError error{.error = "Maximum of 5 media items allowed per offer"};
    auto resp =
        HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }

  auto db = app().getDbClient();

  try {
    // First check if the user is the owner of the offer
    auto result = co_await db->execSqlCoro(
        "SELECT user_id, status FROM offers WHERE id = $1",
        convert::string_to_int(id).value());

    if (result.empty()) {
      SimpleError error{.error = "Offer not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    std::string status = result[0]["status"].as<std::string>();

    if (offer_user_id != convert::string_to_int(current_user_id).value()) {
      SimpleError error{.error =
                            "You don't have permission to update this offer"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    if (status != "pending") {
      SimpleError error{.error = "Only pending offers can be updated"};
      auto resp =
          HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    bool has_title = update_req.title.has_value();
    bool has_description = update_req.description.has_value();
    bool has_price = update_req.price.has_value();
    bool has_is_public = update_req.is_public.has_value();

    std::string title = update_req.title.value_or("");
    std::string description = update_req.description.value_or("");
    double price = update_req.price.value_or(0.0);
    bool is_public = update_req.is_public.value_or(true);

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
          convert::string_to_int(id).value(), has_title, title, has_description,
          description, has_price, price, has_is_public, is_public);

      if (update_result.empty()) {
        throw std::runtime_error("initial update failed");
      }

      std::string updated_at = update_result[0]["updated_at"].as<std::string>();

      // Update media
      std::expected<std::vector<MediaQuickInfo>, std::string> processed_media;
      if (update_req.media.has_value()) {
        std::size_t media_array_size = update_req.media->size();
        processed_media = co_await process_media_attachments(
            std::move(update_req.media.value()), transaction,
            convert::string_to_int(current_user_id).value(), "offer",
            convert::string_to_int(id).value());
        if (!processed_media.has_value() ||
            (processed_media->size() < media_array_size)) {
          LOG_ERROR << " Some Media info was not found";
          transaction->rollback();
          std::string error_string;
          for (const auto& media_item : *processed_media) {
            error_string += media_item.filename + ", ";
          }
          SimpleError error{
              .error = std::format("Media info not found or processed, only "
                                   "the following media items "
                                   "were processed:\n{}",
                                   error_string)};
          auto resp = HttpResponse::newHttpResponse(k400BadRequest,
                                                    CT_APPLICATION_JSON);
          resp->setBody(glz::write_json(error).value_or(""));
          callback(resp);
          co_return;
        }
      }

      // notification
      std::string offer_topic = create_topic("offer", id);

      NotificationMessage msg{.type = "offer_updated",
                              .id = id,
                              .message = "Offer updated",
                              .modified_at = updated_at};

      ServiceManager::get_instance().get_publisher().publish(
          offer_topic, glz::write_json(msg).value_or(""));
      UpdateOfferResponse response{
          .status = "success",
          .message = "Offer updated successfully",
          .offer_id = id,
          .media = processed_media->empty()
                       ? std::nullopt
                       : std::make_optional(*processed_media)};
      auto resp =
          HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(response).value_or(""));
      callback(resp);

    } catch (const std::exception& e) {
      transaction->rollback();
      LOG_ERROR << "Error updating offer: " << e.what();
      SimpleError error{.error =
                            std::format("Error updating offer: {}", e.what())};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
        convert::string_to_int(id).value());
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
    auto result = co_await db->execSqlCoro(
        "SELECT o.post_id, p.user_id as post_owner_id, o.status "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        convert::string_to_int(id).value());

    if (result.empty()) {
      SimpleError error{.error = "Offer not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    int post_owner_id = result[0]["post_owner_id"].as<int>();
    int post_id = result[0]["post_id"].as<int>();
    std::string status = result[0]["status"].as<std::string>();

    if (post_owner_id != convert::string_to_int(current_user_id).value()) {
      SimpleError error{.error = "Only the post owner can accept offers"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    if (status != "pending") {
      SimpleError error{.error = "Only pending offers can be accepted"};
      auto resp =
          HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    auto trans = co_await db->newTransactionCoro();

    try {
      auto update_result = co_await trans->execSqlCoro(
          "UPDATE offers SET status = 'accepted', negotiation_status = "
          "'completed', updated_at = NOW() WHERE id = $1 "
          "RETURNING updated_at",
          convert::string_to_int(id).value());

      // Get the latest price negotiation for this offer (if any) and accept it
      auto negotiation_result = co_await trans->execSqlCoro(
          "SELECT id FROM price_negotiations "
          "WHERE offer_id = $1 AND status = 'pending' "
          "ORDER BY created_at DESC LIMIT 1",
          convert::string_to_int(id).value());

      drogon::orm::Result
          rejected_offers_result;  // nullptr, always inited in both branches

      // For pending negotiations, accept the latest one and reject others
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
            convert::string_to_int(id).value(), latest_negotiation_id);

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
            post_id, convert::string_to_int(id).value());

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
            post_id, convert::string_to_int(id).value());

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
            post_id, convert::string_to_int(id).value());

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
            post_id, convert::string_to_int(id).value());

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

      // notify the accepted offer
      std::string offer_topic = create_topic("offer", id);
      NotificationMessage msg{
          .type = "offer_accepted",
          .id = id,
          .message = "Offer was accepted",
          .modified_at = update_result[0]["updated_at"].as<std::string>()};

      ServiceManager::get_instance().get_publisher().publish(
          offer_topic, glz::write_json(msg).value_or(""));

      // notify rejected offers
      for (const auto& row : rejected_offers_result) {
        std::string rejected_offer_id = row["id"].as<std::string>();

        offer_topic = create_topic("offer", rejected_offer_id);

        msg = NotificationMessage{
            .type = "offer_rejected",
            .id = rejected_offer_id,
            .message = "Offer was rejected",
            .modified_at =
                rejected_offers_result[0]["updated_at"].as<std::string>()};
        ServiceManager::get_instance().get_publisher().publish(
            offer_topic, glz::write_json(msg).value_or(""));
      }

      // Delete all offer and subscriptions for that post?

      StatusResponse response{.status = "success",
                              .message = "Offer accepted successfully"};
      auto resp =
          HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(response).value_or(""));
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      trans->rollback();
      SimpleError error{.error = "Database error"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  } catch (const std::exception& e) {
    LOG_ERROR << "Error: " << e.what();
    SimpleError error{.error = "Internal server error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
    auto result = co_await db->execSqlCoro(
        "SELECT o.user_id, o.post_id, p.user_id as post_owner_id, o.status "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        convert::string_to_int(id).value());

    if (result.empty()) {
      SimpleError error{.error = "Offer not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    int offer_user_id = result[0]["user_id"].as<int>();
    int post_id = result[0]["post_id"].as<int>();
    std::string status = result[0]["status"].as<std::string>();

    if (offer_user_id != convert::string_to_int(current_user_id).value()) {
      SimpleError error{.error =
                            "Only the offer creator can accept counter-offers"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    if (status != "pending") {
      SimpleError error{.error = "Only pending offers can be accepted"};
      auto resp =
          HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    auto trans = co_await db->newTransactionCoro();

    try {
      // First, get the latest negotiated price and its ID
      auto price_result = co_await trans->execSqlCoro(
          "SELECT id, proposed_price FROM price_negotiations "
          "WHERE offer_id = $1 AND status = 'pending' "
          "ORDER BY created_at DESC LIMIT 1",
          convert::string_to_int(id).value());

      if (price_result.empty()) {
        SimpleError error{.error = "No pending negotiations found"};
        auto resp =
            HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
        resp->setBody(glz::write_json(error).value_or(""));
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
          convert::string_to_int(id).value(), latest_price);

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
          convert::string_to_int(id).value(), latest_negotiation_id);

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
          post_id, convert::string_to_int(id).value());

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
          "WHERE pn.offer_id = o.id AND o.post_id = $1 AND "
          "o.id != $2 AND pn.status = 'pending' "
          "RETURNING pn.id",
          post_id, convert::string_to_int(id).value());

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
      NotificationMessage msg{
          .type = "offer_accepted",
          .id = id,
          .message = "Offer was accepted",
          .modified_at = update_result[0]["updated_at"].as<std::string>()};

      ServiceManager::get_instance().get_publisher().publish(
          offer_topic, glz::write_json(msg).value_or(""));

      // notify other rejected offers
      for (const auto& row : rejected_offers_result) {
        std::string rejected_offer_id = row["id"].as<std::string>();

        offer_topic = create_topic("offer", rejected_offer_id);

        msg = NotificationMessage{
            .type = "offer_rejected",
            .id = rejected_offer_id,
            .message = "Offer was rejected",
            .modified_at =
                rejected_offers_result[0]["updated_at"].as<std::string>()};
        ServiceManager::get_instance().get_publisher().publish(
            offer_topic, glz::write_json(msg).value_or(""));
      }

      // delete all offers subscriptions for this post?
      // co_await trans->execSqlCoro(
      //     "DELETE FROM user_subscriptions WHERE subscription = $1",
      //     post_topic);

      StatusResponse response{.status = "success",
                              .message = "Counter-offer accepted successfully"};
      auto resp =
          HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(response).value_or(""));
      callback(resp);
    } catch (const DrogonDbException& e) {
      LOG_ERROR << "Database error: " << e.base().what();
      trans->rollback();
      SimpleError error{.error = "Database error"};
      auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                                CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
    }
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
    auto result = co_await db->execSqlCoro(
        "SELECT o.post_id, p.user_id as post_owner_id, o.status "
        "FROM offers o "
        "JOIN posts p ON o.post_id = p.id "
        "WHERE o.id = $1",
        convert::string_to_int(id).value());

    if (result.empty()) {
      SimpleError error{.error = "Offer not found"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    int post_owner_id = result[0]["post_owner_id"].as<int>();
    std::string status = result[0]["status"].as<std::string>();

    if (post_owner_id != convert::string_to_int(current_user_id).value()) {
      SimpleError error{.error = "Only the post owner can reject offers"};
      auto resp =
          HttpResponse::newHttpResponse(k403Forbidden, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    if (status != "pending") {
      SimpleError error{.error = "Only pending offers can be rejected"};
      auto resp =
          HttpResponse::newHttpResponse(k400BadRequest, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    auto update_result = co_await db->execSqlCoro(
        "UPDATE offers SET status = 'rejected', negotiation_status = "
        "'completed', updated_at = NOW() WHERE id = $1 "
        "RETURNING updated_at",
        convert::string_to_int(id).value());

    // notify the rejected offer
    std::string offer_topic = create_topic("offer", id);
    NotificationMessage msg{
        .type = "offer_rejected",
        .id = id,
        .message = "Offer was rejected",
        .modified_at = update_result[0]["updated_at"].as<std::string>()};

    ServiceManager::get_instance().get_publisher().publish(
        offer_topic, glz::write_json(msg).value_or(""));

    LOG_INFO << "Rejected offer: " << offer_topic;

    StatusResponse response{.status = "success",
                            .message = "Offer rejected successfully"};
    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(response).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
        convert::string_to_int(current_user_id).value());

    std::vector<MyOfferInfo> my_offers_data;
    my_offers_data.reserve(result.size());
    for (const auto& row : result) {
      int offer_id = row["id"].as<int>();
      auto media_attachments =
          co_await get_media_attachments("offer", offer_id);
      my_offers_data.emplace_back(MyOfferInfo{
          .id = offer_id,
          .post_id = row["post_id"].as<int>(),
          .post_content = row["post_content"].as<std::string>(),
          .post_owner_username = row["post_owner_username"].as<std::string>(),
          .title = row["title"].as<std::string>(),
          .description = row["description"].as<std::string>(),
          .price = row["price"].as<double>(),
          .original_price = row["original_price"].as<double>(),
          .is_public = row["is_public"].as<bool>(),
          .status = row["status"].as<std::string>(),
          .created_at = row["created_at"].as<std::string>(),
          .updated_at = row["updated_at"].as<std::string>(),
          .media = media_attachments.value_or({})});
    }

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(my_offers_data).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
        convert::string_to_int(current_user_id).value());

    std::vector<ReceivedOfferInfo> received_offers_data;
    received_offers_data.reserve(result.size());
    for (const auto& row : result) {
      int offer_id = row["id"].as<int>();
      auto media_attachments =
          co_await get_media_attachments("offer", offer_id);
      received_offers_data.emplace_back(ReceivedOfferInfo{
          .id = offer_id,
          .post_id = row["post_id"].as<int>(),
          .post_content = row["post_content"].as<std::string>(),
          .user_id = row["user_id"].as<int>(),
          .offer_username = row["offer_username"].as<std::string>(),
          .title = row["title"].as<std::string>(),
          .description = row["description"].as<std::string>(),
          .price = row["price"].as<double>(),
          .original_price = row["original_price"].as<double>(),
          .is_public = row["is_public"].as<bool>(),
          .status = row["status"].as<std::string>(),
          .created_at = row["created_at"].as<std::string>(),
          .updated_at = row["updated_at"].as<std::string>(),
          .media = media_attachments.value_or({})});
    }

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(received_offers_data).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
        convert::string_to_int(current_user_id).value());

    std::vector<NotificationInfo> notifications_data;
    notifications_data.reserve(result.size());
    for (const auto& row : result) {
      notifications_data.push_back(NotificationInfo{
          .id = row["id"].as<int>(),
          .offer_id = row["offer_id"].as<int>(),
          .is_read = row["is_read"].as<bool>(),
          .created_at = row["created_at"].as<std::string>(),
          .offer_title = row["offer_title"].as<std::string>(),
          .offer_status = row["offer_status"].as<std::string>(),
          .offer_username = row["offer_username"].as<std::string>(),
          .post_content = row["post_content"].as<std::string>()});
    }

    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(notifications_data).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
        convert::string_to_int(id).value(),
        convert::string_to_int(current_user_id).value());

    if (result.empty()) {
      SimpleError error{
          .error = "Notification not found or you don't have permission"};
      auto resp =
          HttpResponse::newHttpResponse(k404NotFound, CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    StatusResponse response{.status = "success",
                            .message = "Notification marked as read"};
    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(response).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
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
        convert::string_to_int(current_user_id).value());

    StatusResponse response{.status = "success",
                            .message = "All notifications marked as read"};
    auto resp =
        HttpResponse::newHttpResponse(drogon::k200OK, CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(response).value_or(""));
    callback(resp);
  } catch (const DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "Database error"};
    auto resp = HttpResponse::newHttpResponse(k500InternalServerError,
                                              CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
  }

  co_return;
}
