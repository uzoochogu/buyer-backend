#ifndef SCENARIO_SPECIFIC_UTILS_HPP
#define SCENARIO_SPECIFIC_UTILS_HPP

#include <drogon/drogon.h>
#include <drogon/orm/DbClient.h>

#include "../services/service_manager.hpp"

/**
 * Assumptions for quick_process_media_attachments, process_media_attachments
 * and get_media_attachments,
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
 * @return boolean.
 * false means there was an error,
 * returns true if processing is successful even if it didn't process all.
 */
inline drogon::Task<bool> quick_process_media_attachments(
    Json::Value&& media_array,
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    int current_user_id, std::string media_table_prefix,
    int media_table_prefix_id) {
  if (!media_array.isArray()) {
    co_return false;
  };

  for (const auto& media_item : media_array) {
    if (!media_item.isString()) {
      LOG_WARN << "Invalid media item format, skipping";
      continue;
    }

    std::string storage_key = media_item.asString();

    MediaInfo info =
        co_await ServiceManager::get_instance().get_s3_service().get_media_info(
            "media", storage_key);
    if (info.etag.empty()) {
      LOG_ERROR << "Media info not found for " << storage_key;
      continue;
    }

    std::string file_name = storage_key.substr(storage_key.find('_') + 1);
    std::string mime_type = !info.content_type.empty()
                                ? info.content_type
                                : "application/octet-stream";
    int64_t size = info.content_length;

    auto media_result = co_await transaction->execSqlCoro(
        "INSERT INTO media (uploader_id, storage_key, file_name, "
        "mime_type, size) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (storage_key) DO UPDATE SET "
        "file_name = EXCLUDED.file_name, "
        "mime_type = EXCLUDED.mime_type, "
        "size = EXCLUDED.size "
        "RETURNING id",
        current_user_id, storage_key, file_name, mime_type, size);

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
 * @return Json::Value. Error is represented as a Json::nullValue.
 * If the some media is valid, a Json::arrayValue containing the processed
 * media info is returned.
 * Parameters passed by value/moved to avoid dangling references.
 */
inline drogon::Task<Json::Value> process_media_attachments(
    Json::Value&& media_array,
    const std::shared_ptr<drogon::orm::Transaction>& transaction,
    int current_user_id, std::string media_table_prefix,
    int media_table_prefix_id) {
  if (!media_array.isArray()) {
    co_return Json::nullValue;
  };

  Json::Value processed_media(Json::arrayValue);

  for (const auto& media_item : media_array) {
    if (!media_item.isString()) {
      LOG_WARN << "Invalid media item format, skipping";
      continue;
    }

    std::string storage_key = media_item.asString();
    MediaInfo info =
        co_await ServiceManager::get_instance().get_s3_service().get_media_info(
            "media", storage_key);
    if (info.etag.empty()) {
      LOG_ERROR << "Media info not found for " << storage_key;
      continue;
    }

    std::string file_name = storage_key.substr(storage_key.find('_') + 1);
    std::string mime_type = !info.content_type.empty()
                                ? info.content_type
                                : "application/octet-stream";
    int64_t size = info.content_length;

    auto media_result = co_await transaction->execSqlCoro(
        "INSERT INTO media (uploader_id, storage_key, file_name, "
        "mime_type, size) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (storage_key) DO UPDATE SET "
        "file_name = EXCLUDED.file_name, "
        "mime_type = EXCLUDED.mime_type, "
        "size = EXCLUDED.size "
        "RETURNING id",
        current_user_id, storage_key, file_name, mime_type, size);

    if (!media_result.empty()) {
      int media_id = media_result[0]["id"].as<int>();
      std::string query =
          std::format("INSERT INTO {}_media ({}_id, media_id) VALUES ($1, $2)",
                      media_table_prefix, media_table_prefix);
      // Link media to particular table
      co_await transaction->execSqlCoro(query, media_table_prefix_id, media_id);

      Json::Value processed_item;
      processed_item["id"] = media_id;
      processed_item["storage_key"] = storage_key;
      processed_item["file_name"] = file_name;
      processed_item["mime_type"] = mime_type;
      processed_item["size"] = size;

      // Alternatively Generate presigned URL for viewing
      // try {
      //   auto presigned_url = co_await ServiceManager::get_instance()
      //                            .get_s3_service()
      //                            .generate_presigned_url("media",
      //                            storage_key,
      //                            drogon::HttpMethod::Get,
      //                            mime_type);
      //   processed_item["presigned_url"] = presigned_url;
      // } catch (const std::exception& e) {
      //   LOG_ERROR << "Failed to generate presigned URL: " << e.what();
      //   processed_item["presigned_url"] = "";
      // }

      processed_media.append(processed_item);
    }
  }
  co_return processed_media;
}

/**
 * @brief Fetches available media.
 * It runs using an existing db transaction.
 * @return Json::Value.
 * Error is represented as a Json::nullValue.
 * If the some media is valid, a Json::arrayValue containing the fetched media.
 * Parameters passed by value to avoid dangling references.
 */
inline drogon::Task<Json::Value> get_media_attachments(
    std::string media_table_prefix, int media_table_prefix_id) {
  auto db = drogon::app().getDbClient();
  auto media_result = co_await db->execSqlCoro(
      std::format(
          "SELECT med.id, med.storage_key, med.file_name, med.mime_type, "
          "med.size, med.metadata "
          "FROM {}_media om "
          "JOIN media med ON om.media_id = med.id "
          "WHERE om.{}_id = $1",
          media_table_prefix, media_table_prefix),
      media_table_prefix_id);

  if (!media_result.empty()) {
    Json::Value media_array(Json::arrayValue);

    for (const auto& media_row : media_result) {
      Json::Value media_item;
      media_item["id"] = media_row["id"].as<int>();
      media_item["object_key"] = media_row["storage_key"].as<std::string>();
      media_item["file_name"] = media_row["file_name"].as<std::string>();
      media_item["mime_type"] = media_row["mime_type"].as<std::string>();
      media_item["size"] = media_row["size"].as<int64_t>();

      // alternatively Generate presigned URL for viewing
      // std::string storage_key =
      // media_row["storage_key"].as<std::string>(); std::string mime_type =
      // media_row["mime_type"].as<std::string>();

      // try {
      //   auto presigned_url = co_await ServiceManager::get_instance()
      //                            .get_s3_service()
      //                            .generate_presigned_url("media",
      //                            storage_key,
      //                            drogon::HttpMethod::Get,
      //                            mime_type);
      //   media_item["presigned_url"] = presigned_url;
      // } catch (const std::exception& e) {
      //   LOG_ERROR << "Failed to generate presigned URL: " << e.what();
      //   media_item["presigned_url"] = "";
      // }

      if (!media_row["metadata"].isNull()) {
        media_item["metadata"] = media_row["metadata"].as<std::string>();
      }

      media_array.append(media_item);
    }

    co_return media_array;
  }
  co_return Json::nullValue;
}

#endif  // SCENARIO_SPECIFIC_UTILS_HPP
