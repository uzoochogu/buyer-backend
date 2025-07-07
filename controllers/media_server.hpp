#ifndef MEDIA_SERVER_CONTROLLER_HPP
#define MEDIA_SERVER_CONTROLLER_HPP

#include <drogon/HttpController.h>

namespace api {
namespace v1 {

/**
 * @brief Media server controller
 * @note query params used because object_key contains '/' character
 */
class MediaController : public drogon::HttpController<MediaController> {
 public:
  METHOD_LIST_BEGIN
  ADD_METHOD_TO(MediaController::get_upload_url, "/api/v1/media/upload-url",
                drogon::Post, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  ADD_METHOD_TO(MediaController::verify_proof, "/api/v1/media/verify-proof",
                drogon::Get, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  ADD_METHOD_TO(MediaController::get_media_url, "/api/v1/media", drogon::Get,
                drogon::Options, "CorsMiddleware", "AuthMiddleware");
  ADD_METHOD_TO(MediaController::verify_object_key, "/api/v1/media/verify",
                drogon::Get, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  ADD_METHOD_TO(MediaController::get_media_metadata, "/api/v1/media/metadata",
                drogon::Get, drogon::Options, "CorsMiddleware",
                "AuthMiddleware");
  METHOD_LIST_END

  drogon::Task<> get_upload_url(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr &)> callback);

  drogon::Task<> verify_proof(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr &)> callback);

  drogon::Task<> get_media_url(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr &)> callback);

  drogon::Task<> verify_object_key(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr &)> callback);

  drogon::Task<> get_media_metadata(
      const drogon::HttpRequestPtr req,
      std::function<void(const drogon::HttpResponsePtr &)> callback);
};

}  // namespace v1

}  // namespace api

#endif MEDIA_CONTROLLER_HPP  // MEDIA_CONTROLLER_HPP
