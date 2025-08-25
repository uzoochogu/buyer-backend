#pragma once

#include <drogon/HttpController.h>

namespace api {
namespace v1 {
using drogon::Get;
using drogon::HttpRequestPtr;
using drogon::HttpResponsePtr;
using drogon::Options;
using drogon::Post;
using drogon::Put;

class Offers : public drogon::HttpController<Offers> {
 public:
  METHOD_LIST_BEGIN
  // Get all offers for a post
  ADD_METHOD_TO(Offers::get_offers_for_post, "/api/v1/posts/{post_id}/offers",
                Get, Options, "CorsMiddleware", "AuthMiddleware");

  // Create a new offer for a post
  ADD_METHOD_TO(Offers::create_offer, "/api/v1/posts/{post_id}/offers", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::get_offer, "/api/v1/offers/{id}", Get, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::update_offer, "/api/v1/offers/{id}", Put, Options,
                "CorsMiddleware", "AuthMiddleware");

  // Accept an offer
  ADD_METHOD_TO(Offers::accept_offer, "/api/v1/offers/{id}/accept", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::accept_counter_offer,
                "/api/v1/offers/{id}/accept-counter", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  // Reject an offer
  ADD_METHOD_TO(Offers::reject_offer, "/api/v1/offers/{id}/reject", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  // Get all offers made by the current user
  ADD_METHOD_TO(Offers::get_my_offers, "/api/v1/offers/my-offers", Get, Options,
                "CorsMiddleware", "AuthMiddleware");

  // Get all offers received for the current user's posts
  ADD_METHOD_TO(Offers::get_received_offers, "/api/v1/offers/received", Get,
                Options, "CorsMiddleware", "AuthMiddleware");

  // Get notifications for the current user
  ADD_METHOD_TO(Offers::get_notifications, "/api/v1/offers/notifications", Get,
                Options, "CorsMiddleware", "AuthMiddleware");

  // Mark a notification as read
  ADD_METHOD_TO(Offers::mark_notification_read,
                "/api/v1/offers/notifications/{id}/read", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::mark_all_notifications_read,
                "/api/v1/offers/notifications/read-all", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::negotiate_offer, "/api/v1/offers/{id}/negotiate", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::get_negotiations, "/api/v1/offers/{id}/negotiations",
                Get, Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::request_proof, "/api/v1/offers/{id}/proof/request",
                Post, Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::submit_proof, "/api/v1/offers/{id}/proof/submit", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::get_proofs, "/api/v1/offers/{id}/proofs", Get, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::approve_proof,
                "/api/v1/offers/{id}/proof/{proof_id}/approve", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::reject_proof,
                "/api/v1/offers/{id}/proof/{proof_id}/reject", Post, Options,
                "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::create_escrow, "/api/v1/offers/{id}/escrow", Post,
                Options, "CorsMiddleware", "AuthMiddleware");

  ADD_METHOD_TO(Offers::get_escrow, "/api/v1/offers/{id}/escrow", Get, Options,
                "CorsMiddleware", "AuthMiddleware");
  METHOD_LIST_END

  static drogon::Task<> get_offers_for_post(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string post_id);

  static drogon::Task<> create_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string post_id);

  static drogon::Task<> get_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> update_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> accept_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> accept_counter_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> reject_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> get_my_offers(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback);

  static drogon::Task<> get_received_offers(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback);

  static drogon::Task<> get_notifications(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback);

  static drogon::Task<> mark_notification_read(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> mark_all_notifications_read(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback);

  static drogon::Task<> negotiate_offer(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> get_negotiations(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  // upcoming features
  static drogon::Task<> request_proof(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> submit_proof(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> get_proofs(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> approve_proof(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id, std::string proof_id);

  static drogon::Task<> reject_proof(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id, std::string proof_id);

  static drogon::Task<> create_escrow(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);

  static drogon::Task<> get_escrow(
      HttpRequestPtr req, std::function<void(const HttpResponsePtr&)> callback,
      std::string id);
};
}  // namespace v1
}  // namespace api
