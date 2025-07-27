// clang-format off
#include "../utilities/uuid_generator.hpp"
// clang-format on

#include "media_server.hpp"

#include <array>
#include <format>

#include "../services/media_server/s3_service.hpp"
#include "../services/service_manager.hpp"

using namespace api::v1;

constexpr auto allowed_file_types = std::to_array<std::string_view>({
    "image/jpeg", "image/png", "image/gif", "image/webp", "video/mp4",
    "video/webm",  // "video/quicktime",
});

drogon::Task<> MediaController::get_upload_url(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr &)> callback) {
  try {
    auto json = req->getJsonObject();

    if (!json || json->empty() || !(*json).isMember("filename") ||
        (*json)["filename"].asString().empty() ||
        ((*json).isMember("is_proof") && !(*json)["is_proof"].isBool())) {
      LOG_ERROR
          << "At least filename is required, is_proof should be a boolean";
      Json::Value error;
      error["error"] =
          "At least filename is required, is_proof should be a boolean";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(drogon::k400BadRequest);
      callback(resp);
      co_return;
    }

    auto file_name = json->get("filename", "nofile").asString();
    // auto is_proof = json->get("is_proof", false).asBool(); // not used yet
    auto content_type =
        json->get("content_type", "application/octet-stream").asString();

    if (std::find(allowed_file_types.begin(), allowed_file_types.end(),
                  content_type) == allowed_file_types.end()) {
      LOG_ERROR << content_type << " file type not allowed";
      Json::Value error;
      error["error"] = std::format("{} file type not allowed", content_type);
      auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(drogon::k400BadRequest);
      callback(resp);
      co_return;
    }

    auto object_key =
        std::format("uploads/{}_{}", UuidGenerator::generate_uuid(), file_name);

    auto upload_url = co_await ServiceManager::get_instance()
                          .get_s3_service()
                          .generate_presigned_url("media", object_key,
                                                  drogon::Put, content_type);

    if (upload_url.empty()) {
      LOG_ERROR << "Failed to generate upload url";
      Json::Value error;
      error["error"] = "Failed to generate upload url";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(drogon::k500InternalServerError);
      callback(resp);
      co_return;
    }

    Json::Value response;
    response["upload_url"] = upload_url;
    response["object_key"] = object_key;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);

  } catch (const std::exception &e) {
    Json::Value error;
    LOG_ERROR << "Failed to get upload url: " << e.what();
    error["error"] = e.what();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }
  co_return;
}

drogon::Task<> MediaController::verify_object_key(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr &)> callback) {
  try {
    auto object_key = req->getParameter("object_key");

    if (object_key.empty()) {
      LOG_ERROR << "object_key is required";
      Json::Value error;
      error["error"] = "object_key is required";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(drogon::k400BadRequest);
      callback(resp);
      co_return;
    }

    MediaInfo info =
        co_await ServiceManager::get_instance().get_s3_service().get_media_info(
            "media", object_key);

    Json::Value response;
    response["exists"] = info.etag.empty() ? false : true;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const std::exception &e) {
    Json::Value error;
    LOG_ERROR << "Failed confirm media: " << e.what();
    error["error"] = e.what();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }
  co_return;
}

drogon::Task<> MediaController::get_media_metadata(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr &)> callback) {
  try {
    auto object_key = req->getParameter("object_key");

    if (object_key.empty()) {
      LOG_ERROR << "object_key is required";
      Json::Value error;
      error["error"] = "object_key is required";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(drogon::k400BadRequest);
      callback(resp);
      co_return;
    }

    MediaInfo info =
        co_await ServiceManager::get_instance().get_s3_service().get_media_info(
            "media", object_key);

    Json::Value response;
    response["object_key"] = info.object_key;
    response["content_type"] = info.content_type;
    response["content_length"] = info.content_length;
    response["last_modified"] = info.last_modified;
    response["etag"] = info.etag;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const std::exception &e) {
    Json::Value error;
    LOG_ERROR << "Failed confirm media: " << e.what();
    error["error"] = e.what();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }
  co_return;
}

drogon::Task<> MediaController::verify_proof(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr &)> callback) {
  try {
    auto object_key = req->getParameter("object_key");

    if (object_key.empty()) {
      LOG_ERROR << "object_key is required";
      Json::Value error;
      error["error"] = "object_key is required";
      auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
      resp->setStatusCode(drogon::k400BadRequest);
      callback(resp);
      co_return;
    }

    bool is_valid =
        co_await ServiceManager::get_instance().get_s3_service().process_media(
            "media", object_key);

    Json::Value response;
    response["is_valid"] = is_valid;
    response["object_key"] = object_key;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const std::exception &e) {
    Json::Value error;
    LOG_ERROR << "Failed verify proof: " << e.what();
    error["error"] = e.what();
    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
    resp->setStatusCode(drogon::k500InternalServerError);
    callback(resp);
  }
  co_return;
}

drogon::Task<> MediaController::get_media_url(
    const drogon::HttpRequestPtr req,
    std::function<void(const drogon::HttpResponsePtr &)> callback) {
  try {
    auto object_key = req->getParameter("object_key");

    if (object_key.empty()) {
      auto resp = drogon::HttpResponse::newHttpResponse();
      resp->setStatusCode(drogon::k400BadRequest);
      resp->setBody("object_key is required as a query parameter");
      callback(resp);
      co_return;
    }

    std::string content_type = "application/octet-stream";
    if (object_key.ends_with(".png")) {
      content_type = "image/png";
    } else if (object_key.ends_with(".jpg") || object_key.ends_with(".jpeg")) {
      content_type = "image/jpeg";
    } else if (object_key.ends_with(".mp4")) {
      content_type = "video/mp4";
    } else if (object_key.ends_with(".webm")) {
      content_type = "video/webm";
    } else if (object_key.ends_with(".mp3")) {
      content_type = "audio/mpeg";
    } else if (object_key.ends_with(".wav")) {
      content_type = "audio/wav";
    } else if (object_key.ends_with(".pdf")) {
      content_type = "application/pdf";
    }

    auto &s3_service = ServiceManager::get_instance().get_s3_service();

    auto view_url = co_await s3_service.generate_presigned_url(
        "media", object_key, drogon::HttpMethod::Get);

    if (view_url.empty()) {
      LOG_ERROR << "object url not found";
      auto resp = drogon::HttpResponse::newHttpResponse();
      resp->setStatusCode(drogon::k404NotFound);
      resp->setBody("object url not found");
      callback(resp);
      co_return;
    }

    Json::Value response;
    response["download_url"] = view_url;
    response["content_type"] = content_type;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
    callback(resp);
  } catch (const std::exception &e) {
    LOG_ERROR << "Failed to get media: " << e.what();
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k500InternalServerError);
    resp->setBody("Failed to retrieve media");
    callback(resp);
  }
  co_return;
}
