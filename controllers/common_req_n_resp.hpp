#ifndef COMMON_REQ_N_RESP_HPP
#define COMMON_REQ_N_RESP_HPP

#include <optional>
#include <string>
#include <vector>

struct StatusResponse {
  std::string status;
  std::string message;
};

struct SimpleStatus {
  std::string status;
};
struct SimpleError {
  std::string error;
};

struct MediaQuickInfo {
  int media_id;
  std::string object_key;
  std::string filename;
  std::string mime_type;
  int64_t size = 0;
  // No metadata for now
};

struct MediaInput {
  std::vector<std::string> object_keys;
};

struct DeleteMediaRequest {
  std::vector<int> media_ids;
};

struct MediaResponse {
  std::vector<int> media_ids;
};

struct MediaInfoResponse {
  std::vector<MediaQuickInfo> media;
};

struct NotificationMessage {
  std::string type;
  std::string id;
  std::string message;
  std::string modified_at;
};

#endif  // COMMON_REQ_N_RESP_HPP
