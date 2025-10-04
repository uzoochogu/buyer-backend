#ifndef SCENARIO_SPECIFIC_UTILS_HPP
#define SCENARIO_SPECIFIC_UTILS_HPP

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>

#include "../services/service_manager.hpp"
#include "../utilities/json_manipulation.hpp"
#include "common_req_n_resp.hpp"

/**
 * @note
 * Assumptions for quick_process_media_attachments, process_media_attachments
 * process_media_attachments_with_response,
 * get_media_attachments and get_media_attachments_with_response,
 * media db tables have the following structure:
 * table name - "prefix"_media e.g. offer_media, post_media, message_media
 * table prefix id - "prefix"_id e.g. offer_id, post_id, message_id
 * media table prefix - "prefix" e.g. offer, post, message
 *
 */

/**
 * @brief Naive processing of available media.
 * It runs using an existing db transaction.
 * Processing involves checks for validity of object key making necessary
 * media attachment inserts.
 * @return boolean. false means there was an error,
 * true if processing is successful even if it didn't process all.
 */
inline drogon::Task<bool> quick_process_media_attachments(
    std::vector<std::string>&& object_keys,
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    std::string current_user_id, std::string media_table_prefix,
    std::string media_table_prefix_id) {
  for (const auto& object_key : object_keys) {
    MediaInfo info =
        co_await ServiceManager::get_instance().get_s3_service().get_media_info(
            service::BUCKET_NAME, object_key);
    if (info.etag.empty()) {
      LOG_ERROR << "Media info not found for " << object_key;
      continue;
    }

    std::string file_name = object_key.substr(object_key.find('_') + 1);
    std::string mime_type = !info.content_type.empty()
                                ? info.content_type
                                : "application/octet-stream";

    auto media_result = co_await transaction->execSqlCoro(
        "INSERT INTO media (uploader_id, storage_key, file_name, "
        "mime_type, size) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (storage_key) DO UPDATE SET "
        "file_name = EXCLUDED.file_name, "
        "mime_type = EXCLUDED.mime_type, "
        "size = EXCLUDED.size "
        "RETURNING id",
        current_user_id, object_key, file_name, mime_type, info.content_length);

    if (!media_result.empty()) {
      int media_id = media_result[0]["id"].as<int>();
      std::string query =
          std::format("INSERT INTO {}_media ({}_id, media_id) VALUES ($1, $2)",
                      media_table_prefix, media_table_prefix);
      // Link media to particular table
      co_await transaction->execSqlCoro(query, media_table_prefix_id, media_id);
    }
  }
  co_return true;
}

/**
 * @brief Full processing of available media, returning processed media.
 * It runs using an existing db transaction.
 * Processing involves checks for validity of object key making necessary
 * media attachment inserts.
 * @return std::vector<MediaQuickInfo> containing the fetched media
 * if successful or an error string.
 * @note Parameters passed by value/moved to avoid dangling references.
 */
inline drogon::Task<std::expected<std::vector<MediaQuickInfo>, std::string>>
process_media_attachments(
    std::vector<std::string>&& object_keys,
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    int current_user_id, std::string media_table_prefix,
    int media_table_prefix_id) {
  try {
    std::vector<MediaQuickInfo> processed_media;
    processed_media.reserve(object_keys.size());
    for (const auto& object_key : object_keys) {
      MediaInfo info = co_await ServiceManager::get_instance()
                           .get_s3_service()
                           .get_media_info(service::BUCKET_NAME, object_key);
      if (info.etag.empty()) {
        continue;
      }

      std::string filename = object_key.substr(object_key.find('_') + 1);
      std::string mime_type = !info.content_type.empty()
                                  ? info.content_type
                                  : "application/octet-stream";
      int64_t& size = info.content_length;

      auto media_result = co_await transaction->execSqlCoro(
          "INSERT INTO media (uploader_id, storage_key, file_name, "
          "mime_type, size) "
          "VALUES ($1, $2, $3, $4, $5) "
          "ON CONFLICT (storage_key) DO UPDATE SET "
          "file_name = EXCLUDED.file_name, "
          "mime_type = EXCLUDED.mime_type, "
          "size = EXCLUDED.size "
          "RETURNING id",
          current_user_id, object_key, filename, mime_type, size);

      if (!media_result.empty()) {
        auto media_id = media_result[0]["id"].as<int>();
        std::string query = std::format(
            "INSERT INTO {}_media ({}_id, media_id) VALUES ($1, $2)",
            media_table_prefix, media_table_prefix);
        // Link media to particular table
        co_await transaction->execSqlCoro(query, media_table_prefix_id,
                                          media_id);

        processed_media.emplace_back(MediaQuickInfo{.media_id = media_id,
                                                    .object_key = object_key,
                                                    .filename = filename,
                                                    .mime_type = mime_type,
                                                    .size = size});

        // Alternatively Generate presigned URL for viewing
        // try {
        //   auto presigned_url = co_await ServiceManager::get_instance()
        //                         .get_s3_service()
        //                         .generate_presigned_url(service::BUCKET_NAME,
        //                         object_key,
        //                         drogon::HttpMethod::Get,
        //                         mime_type);
        //   processed_media.emplace_back(MediaQuickInfo{.media_id = media_id,
        //       .object_key = object_key,
        //       .filename = filename,
        //       .mime_type = mime_type,
        //       .size = size,
        //       .presigned_url = presigned_url});
        // } catch (const std::exception& e) {
        //   LOG_ERROR << "Failed to generate presigned URL: " << e.what();
        // processed_media.emplace_back(MediaQuickInfo{.media_id = media_id,
        //                                             .object_key = object_key,
        //                                             .filename = filename,
        //                                             .mime_type = mime_type,
        //                                             .size = size});
        // }
      }
    }
    co_return processed_media;

  } catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    co_return std::unexpected(e.base().what());
  } catch (const std::exception& e) {
    LOG_ERROR << "Database error: " << e.what();
    co_return std::unexpected(e.what());
  }
}

/**
 * @brief Full processing of available media and continues the response.
 * It runs using an existing db transaction.
 * It checks for validity of object key, does necessary media attachment
 * inserts and return a response to client.
 * @note Parameters passed by value/moved to avoid dangling references.
 * This is more efficient for processing only operations.
 */
inline drogon::Task<> process_media_attachments_with_response(
    std::function<void(const drogon::HttpResponsePtr&)> callback,
    std::vector<std::string>&& object_keys,
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    std::string current_user_id, std::string media_table_prefix,
    std::string media_table_prefix_id) {
  try {
    MediaResponse media_resp;
    media_resp.media_ids.reserve(object_keys.size());
    auto& processed_media = media_resp.media_ids;
    for (const auto& object_key : object_keys) {
      MediaInfo info = co_await ServiceManager::get_instance()
                           .get_s3_service()
                           .get_media_info(service::BUCKET_NAME, object_key);
      if (info.etag.empty()) {
        continue;
      }

      std::string filename = object_key.substr(object_key.find('_') + 1);
      std::string mime_type = !info.content_type.empty()
                                  ? info.content_type
                                  : "application/octet-stream";
      int64_t& size = info.content_length;

      auto media_result = co_await transaction->execSqlCoro(
          "INSERT INTO media (uploader_id, storage_key, file_name, "
          "mime_type, size) "
          "VALUES ($1, $2, $3, $4, $5) "
          "ON CONFLICT (storage_key) DO UPDATE SET "
          "file_name = EXCLUDED.file_name, "
          "mime_type = EXCLUDED.mime_type, "
          "size = EXCLUDED.size "
          "RETURNING id",
          current_user_id, object_key, filename, mime_type, size);

      if (!media_result.empty()) {
        auto media_id = media_result[0]["id"].as<int>();
        std::string query = std::format(
            "INSERT INTO {}_media ({}_id, media_id) VALUES ($1, $2)",
            media_table_prefix, media_table_prefix);
        // Link media to particular table
        co_await transaction->execSqlCoro(query, media_table_prefix_id,
                                          media_id);

        processed_media.emplace_back(media_id);
      }
    }

    if (processed_media.size() < object_keys.size()) {
      LOG_ERROR << " Some Media info was not found";
      transaction->rollback();
      std::string error_string;
      for (const auto& name : processed_media) {
        error_string += std::format("{},", name);
      }
      SimpleError error{.error =
                            std::format("Media info not found or processed, "
                                        "only the following media items "
                                        "were processed:\n{}",
                                        error_string)};
      auto resp = drogon::HttpResponse::newHttpResponse(
          drogon::k400BadRequest, drogon::CT_APPLICATION_JSON);
      resp->setBody(glz::write_json(error).value_or(""));
      callback(resp);
      co_return;
    }

    auto resp = drogon::HttpResponse::newHttpResponse(
        drogon::k200OK, drogon::CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(media_resp).value_or(""));
    callback(resp);
    co_return;

  } catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << "Database error: " << e.base().what();
    SimpleError error{.error = "error during processing"};
    auto resp = drogon::HttpResponse::newHttpResponse(
        drogon::k500InternalServerError, drogon::CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  } catch (const std::exception& e) {
    LOG_ERROR << "Error: " << e.what();
    SimpleError error{.error = "error during processing"};
    auto resp = drogon::HttpResponse::newHttpResponse(
        drogon::k500InternalServerError, drogon::CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }
}

/**
 * @brief Fetches available media.
 * It runs using an existing db transaction.
 * @return std::vector<MediaQuickInfo> containing the fetched media
 * if successful or an error string.
 * @note Parameters passed by value to avoid dangling references.
 */
inline drogon::Task<std::expected<std::vector<MediaQuickInfo>, std::string>>
get_media_attachments(std::string media_table_prefix,
                      int media_table_prefix_id) {
  auto db = drogon::app().getDbClient();
  try {
    auto media_result = co_await db->execSqlCoro(
        std::format(
            "SELECT med.id, med.storage_key, med.file_name, med.mime_type, "
            "med.size, med.metadata "
            "FROM {}_media om "
            "INNER JOIN media med ON om.media_id = med.id "
            "WHERE om.{}_id = $1",  // ORDER BY med.created_at DESC
            media_table_prefix, media_table_prefix),
        media_table_prefix_id);

    std::vector<MediaQuickInfo> media_array;
    if (!media_result.empty()) {
      media_array.reserve(media_result.size());

      for (const auto& media_row : media_result) {
        media_array.emplace_back(MediaQuickInfo{
            .media_id = media_row["id"].as<int>(),
            .object_key = media_row["storage_key"].as<std::string>(),
            .filename = media_row["file_name"].as<std::string>(),
            .mime_type = media_row["mime_type"].as<std::string>(),
            .size = media_row["size"].as<int64_t>()});
        // alternatively Generate presigned URL for viewing
        // std::string object_key =
        // media_row["storage_key"].as<std::string>();
        // std::string mime_type = media_row["mime_type"].as<std::string>();

        // try {
        //   auto presigned_url = co_await ServiceManager::get_instance()
        //                        .get_s3_service()
        //                        .generate_presigned_url(service::BUCKET_NAME,
        //                         object_key,
        //                         drogon::HttpMethod::Get,
        //                         mime_type);
        //   media_item["presigned_url"] = presigned_url;
        // } catch (const std::exception& e) {
        //   LOG_ERROR << "Failed to generate presigned URL: " << e.what();
        //   media_item["presigned_url"] = "";
        // }
      }
    }
    co_return media_array;
  } catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << std::format("Database error: getting {} media: {}",
                             media_table_prefix, e.base().what());
    co_return std::unexpected(e.base().what());
  } catch (const std::exception& e) {
    LOG_ERROR << std::format("Error getting {} media: {}", media_table_prefix,
                             e.what());
    co_return std::unexpected(e.what());
  }
}

/**
 * @brief Fetches available media and continues the response.
 * It runs using an existing db transaction.
 * @return std::vector<MediaQuickInfo> containing the fetched media
 * if successful or an error string.
 * @note Parameters passed by value to avoid dangling references.
 */
inline drogon::Task<> get_media_attachments_with_response(
    std::function<void(const drogon::HttpResponsePtr&)> callback,
    std::string media_table_prefix, std::string media_table_prefix_id) {
  auto db = drogon::app().getDbClient();
  try {
    auto media_result = co_await db->execSqlCoro(
        std::format(
            "SELECT med.id, med.storage_key, med.file_name, med.mime_type, "
            "med.size, med.metadata "
            "FROM {}_media om "
            "INNER JOIN media med ON om.media_id = med.id "
            "WHERE om.{}_id = $1",  // ORDER BY med.created_at DESC
            media_table_prefix, media_table_prefix),
        media_table_prefix_id);
    MediaInfoResponse media_resp;
    if (!media_result.empty()) {
      auto& media_array = media_resp.media;
      media_array.reserve(media_result.size());

      for (const auto& media_row : media_result) {
        media_array.emplace_back(MediaQuickInfo{
            .media_id = media_row["id"].as<int>(),
            .object_key = media_row["storage_key"].as<std::string>(),
            .filename = media_row["file_name"].as<std::string>(),
            .mime_type = media_row["mime_type"].as<std::string>(),
            .size = media_row["size"].as<int64_t>()});
        // alternatively Generate presigned URL for viewing
        // std::string object_key =
        // media_row["storage_key"].as<std::string>();
        // std::string mime_type = media_row["mime_type"].as<std::string>();

        // try {
        //   auto presigned_url = co_await ServiceManager::get_instance()
        //                        .get_s3_service()
        //                        .generate_presigned_url(service::BUCKET_NAME,
        //                         object_key,
        //                         drogon::HttpMethod::Get,
        //                         mime_type);
        //   media_item["presigned_url"] = presigned_url;
        // } catch (const std::exception& e) {
        //   LOG_ERROR << "Failed to generate presigned URL: " << e.what();
        //   media_item["presigned_url"] = "";
        // }
      }
    }
    auto resp = drogon::HttpResponse::newHttpResponse(
        drogon::k200OK, drogon::CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(media_resp).value_or(""));
    callback(resp);
    co_return;
  } catch (const drogon::orm::DrogonDbException& e) {
    LOG_ERROR << std::format("Database error: getting {} media: {}",
                             media_table_prefix, e.base().what());
    SimpleError error{.error = "error during fetch"};
    auto resp = drogon::HttpResponse::newHttpResponse(
        drogon::k500InternalServerError, drogon::CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  } catch (const std::exception& e) {
    LOG_ERROR << std::format("Error getting {} media: {}", media_table_prefix,
                             e.what());
    SimpleError error{.error = "error during fetch"};
    auto resp = drogon::HttpResponse::newHttpResponse(
        drogon::k500InternalServerError, drogon::CT_APPLICATION_JSON);
    resp->setBody(glz::write_json(error).value_or(""));
    callback(resp);
    co_return;
  }
}

#endif  // SCENARIO_SPECIFIC_UTILS_HPP
