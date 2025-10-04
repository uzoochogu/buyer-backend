#ifndef S3_SERVICE_HPP
#define S3_SERVICE_HPP

#include <aws/core/Aws.h>
#include <aws/s3/S3Client.h>
#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

#include <memory>
#include <string>

struct MediaInfo {
  std::string object_key;
  std::string content_type;
  long long content_length = 0;
  std::string last_modified;
  std::string etag;
  std::unordered_map<std::string, std::string> custom_metadata;
};

class S3Service {
 public:
  S3Service();

  drogon::Task<std::string> generate_presigned_url(
      const std::string& bucket_name, const std::string& object_key,
      drogon::HttpMethod method = drogon::HttpMethod::Put,
      const std::string& content_type = "application/octet-stream",
      long long expiration_sec = 3600);

  drogon::Task<bool> process_media(const std::string& bucket_name,
                                   const std::string& object_key);

  // Ensures a bucket exists, creating it if necessary
  drogon::Task<bool> ensure_bucket_exists(const std::string& bucket_name);

  drogon::Task<MediaInfo> get_media_info(std::string_view bucket_name,
                                         const std::string& object_key);

  Aws::S3::S3Client* get_client() { return s3_client_.get(); }

 private:
  std::unique_ptr<Aws::S3::S3Client> s3_client_;
};

#endif  // S3_SERVICE_HPP
