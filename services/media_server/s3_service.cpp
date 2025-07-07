#include "s3_service.hpp"

#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/DateTime.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/s3/model/CreateBucketRequest.h>
#include <aws/s3/model/GetObjectAttributesRequest.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>

#include "../../config/config.hpp"

// Windows SDK compatibility fix
#ifdef GetObject
#undef GetObject
#endif

S3Service::S3Service() {
  Aws::Client::ClientConfiguration config;

  // Get configuration from Drogon's custom config
  std::string endpoint =
      config::get_config_value("minio_endpoint", "http://localhost:9000");
  std::string access_key =
      config::get_config_value("minio_access_key", "minioadmin");
  std::string secret_key =
      config::get_config_value("minio_secret_key", "minioadmin");

  config.endpointOverride = endpoint;
  config.scheme = endpoint.starts_with("https") ? Aws::Http::Scheme::HTTPS
                                                : Aws::Http::Scheme::HTTP;
  config.verifySSL = false;

  LOG_INFO << "Initializing S3 client with endpoint: " << endpoint;

  s3_client_ = std::make_unique<Aws::S3::S3Client>(
      Aws::Auth::AWSCredentials(access_key, secret_key), config,
      Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false);
}

drogon::Task<std::string> S3Service::generate_presigned_url(
    const std::string &bucket_name, const std::string &object_key,
    drogon::HttpMethod method, const std::string &content_type,
    long long expiration_sec) {
  // LOG_INFO << "Generating presigned URL for " << bucket_name << "/"
  //          << object_key;

  Aws::Http::HeaderValueCollection headers;
  std::string url;

  if (method == drogon::HttpMethod::Get) {
    url = s3_client_->GeneratePresignedUrl(bucket_name, object_key,
                                           Aws::Http::HttpMethod::HTTP_GET,
                                           expiration_sec);
    LOG_INFO << "Generated URL: " << url;
    co_return url;
  } else if (method == drogon::HttpMethod::Put) {
    headers["Content-Type"] = content_type;  // for better browser compatibility
    url = s3_client_->GeneratePresignedUrl(bucket_name, object_key,
                                           Aws::Http::HttpMethod::HTTP_PUT,
                                           headers, expiration_sec);
    LOG_INFO << "Generated URL: " << url;
    co_return url;
  }
  LOG_ERROR << "Unsupported HTTP method: " << drogon::to_string(method);
  co_return url;
}

drogon::Task<bool> S3Service::process_media(const std::string &bucket_name,
                                            const std::string &object_key) {
  LOG_INFO << "Processing media: " << bucket_name << "/" << object_key;
  Aws::S3::Model::GetObjectRequest get_request;
  get_request.SetBucket(bucket_name);
  get_request.SetKey(object_key);

// Windows SDK compatibility fix
#ifdef GetObject
#undef GetObject
#endif
  Aws::S3::Model::GetObjectOutcome outcome = s3_client_->GetObject(get_request);
  if (!outcome.IsSuccess()) {
    LOG_ERROR << "Failed to get object: " << outcome.GetError().GetMessage();
    co_return false;
  }

  auto &metadata = outcome.GetResult().GetMetadata();

  LOG_INFO << "Successfully retrieved object, size: "
           << outcome.GetResult().GetContentLength() << " bytes";

  auto content_type = outcome.GetResult().GetContentType();

  /**
   * Todo:
   * media processing logic:
   * - File size limits
   * - Content scanning for inappropriate material
   * - Metadata validation
   * - Virus scanning
   * - Format validation
   */
  if (content_type.find("image/") != std::string::npos) {
    //  or specific formats e.g. content_type.ends_with(".png") ||
    //  content_type.ends_with(".jpg")
    LOG_INFO << "Processing image file: "
             << object_key.substr(object_key.find("_") + 1);
    // image-specific processing here

    co_return true;
  } else if (content_type.find("video/") != std::string::npos) {
    LOG_INFO << "Processing video file: "
             << object_key.substr(object_key.find("_") + 1);
    // video-specific processing here

    co_return true;
  } else {
    LOG_INFO << "No processing for this file-type";
  }

  co_return false;
}

drogon::Task<bool> S3Service::ensure_bucket_exists(
    const std::string &bucket_name) {
  LOG_INFO << "Checking if bucket exists: " << bucket_name;

  Aws::S3::Model::HeadBucketRequest request;
  request.SetBucket(bucket_name);

  auto outcome = s3_client_->HeadBucket(request);
  if (outcome.IsSuccess()) {
    LOG_INFO << "Bucket " << bucket_name << " already exists";
    co_return true;
  }

  LOG_INFO << "Bucket " << bucket_name << " does not exist, creating...";

  Aws::S3::Model::CreateBucketRequest create_request;
  create_request.SetBucket(bucket_name);

  auto create_outcome = s3_client_->CreateBucket(create_request);
  if (!create_outcome.IsSuccess()) {
    LOG_ERROR << "Failed to create bucket: "
              << create_outcome.GetError().GetMessage();
    co_return false;
  }

  LOG_INFO << "Created bucket: " << bucket_name;
  co_return true;
}

drogon::Task<MediaInfo> S3Service::get_media_info(
    const std::string &bucket_name, const std::string &object_key) {
  Aws::S3::Model::HeadObjectRequest head_request;
  head_request.SetBucket(bucket_name);
  head_request.SetKey(object_key);

  auto outcome = s3_client_->HeadObject(head_request);
  if (!outcome.IsSuccess()) {
    LOG_ERROR << "Failed to get object info: "
              << outcome.GetError().GetMessage();
    co_return MediaInfo{};
  }

  auto &result = outcome.GetResult();
  MediaInfo info;
  info.object_key = object_key;
  info.content_type = result.GetContentType();
  info.content_length = result.GetContentLength();
  info.last_modified =
      result.GetLastModified().ToGmtString(Aws::Utils::DateFormat::ISO_8601);
  info.etag = result.GetETag();

  auto metadata = result.GetMetadata();
  for (const auto &pair : metadata) {
    info.custom_metadata[pair.first] = pair.second;
  }

  co_return info;
}
