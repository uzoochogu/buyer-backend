#include <drogon/drogon.h>

#include <filesystem>
#include <format>
#include <iostream>
#include <string>
#include <vector>

#include "services/service_manager.hpp"

void print_help() {
  std::cout << "Usage: buyer-backend [OPTIONS]\n\n"
               "Options:\n"
               "  --test, -t       Run in test mode using test_config.json.\n"
               "                   Searches up to 3 parent directories up.\n"
               "  --config <file>  Use specified config file.\n"
               "                   Searches up to 3 parent directories up.\n"
               "  --help, -h       Display this help message and exit.\n";
}

std::string find_config_file(const std::string& filename) {
  // Search up to 3 parent directories up.
  std::vector<std::string> possible_paths = {
      filename, "../" + filename, "../../" + filename, "../../../" + filename};

  for (const auto& path : possible_paths) {
    if (std::filesystem::exists(path)) {
      std::puts(std::format("Found config file at: {}", path).c_str());
      return path;
    }
  }

  // If not found, return the original path and log a warning
  std::cerr << std::format(
      "Warning: Could not find {} in any of the expected locations.", filename);
  std::cerr << std::format("Will try with {} directly.", filename);
  return filename;
}

int main(int argc, char* argv[]) {
  bool test_mode = false;
  std::string config = "";

  // Parse command line arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--test" || arg == "-t") {
      test_mode = true;
      std::puts("Running in test mode");
    } else if (arg == "--config" && i + 1 < argc) {
      config = argv[i + 1];
      i++;  // Skip the next argument
      std::puts(std::format("Using config: {}", config).c_str());
    } else if (arg == "--help" || arg == "-h") {
      print_help();
      return 0;
    } else {
      std::puts(std::format("Unknown option: {}", arg).c_str());
      print_help();
      return 1;
    }
  }

  // Set HTTP listener address and port
  drogon::app().addListener("0.0.0.0", 5555);
  // Load config file
  try {
    if (test_mode) {
      std::string test_config_path = find_config_file("test_config.json");
      std::puts(
          std::format("Loading test configuration from: {}", test_config_path)
              .c_str());
      drogon::app().loadConfigFile(test_config_path);
    } else if (!config.empty()) {
      // Use user-specified config file
      std::puts(std::format("Loading configuration from: {}", config).c_str());
      drogon::app().loadConfigFile(config);
    } else {
      // Use default config file
      std::string default_config_path = find_config_file("config.json");
      std::puts(std::format("Loading default configuration from: {}",
                            default_config_path)
                    .c_str());
      drogon::app().loadConfigFile(default_config_path);
    }
  } catch (const std::exception& e) {
    std::puts(std::format("Error loading configuration: {}", e.what()).c_str());
    return 1;
  }

  try {
    ServiceManager::get_instance().initialize();
  } catch (const std::exception& e) {
    std::puts(std::format("Error initializing service manager: {}", e.what())
                  .c_str());
    return 1;
  }

  if (test_mode) {
    drogon::app().getLoop()->queueInLoop([]() {
      std::cout << std::format(
          "Starting server on 0.0.0.0:5555, DB Connection Info: {}\n",
          drogon::app().getDbClient()->connectionInfo());
    });
  }

  // Create buckets
  drogon::app().getLoop()->runInLoop([]() {
    drogon::sync_wait([]() -> drogon::Task<void> {
      bool bucket_created = co_await ServiceManager::get_instance()
                                .get_s3_service()
                                .ensure_bucket_exists("media");
      if (!bucket_created) {
        LOG_ERROR << "Failed to create or verify 'media' bucket";
        exit(1);
      } else {
        LOG_INFO << "Media bucket is ready";
      }
    }());
  });

  drogon::app().run();

  // Cleanup on shutdown
  std::atexit([]() { ServiceManager::get_instance().shutdown(); });
}
