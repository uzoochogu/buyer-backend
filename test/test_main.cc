#define DROGON_TEST_MAIN
#include <drogon/drogon.h>
#include <drogon/drogon_test.h>

#include <array>
#include <filesystem>
#include <iostream>

// Helper function to find the test_config.json file
std::string find_config_file() {
  // Try different relative paths from the executable location
  const std::array<std::string, 6> possible_paths = {
      "test_config.json",           // Same directory
      "../test_config.json",        // One level up
      "../../test_config.json",     // Two levels up
      "../../../test_config.json",  // Three levels up
      "test/test_config.json",      // In test subdirectory
      "../test/test_config.json"    // In test subdirectory one level up
  };

  for (const auto& path : possible_paths) {
    if (std::filesystem::exists(path)) {
      std::cout << "Found config file at: " << path << std::endl;
      return path;
    }
  }

  // If not found, default to the original path and log a warning
  std::cerr << "Warning: Could not find test_config.json in any of the "
               "expected locations."
            << std::endl;
  std::cerr << "Will try with ../../test_config.json" << std::endl;
  return "../../test_config.json";
}

int main(int argc, char** argv) {
  //  Note that "../../../test_config.json" is relative to the executable in
  //  MSVC

  // Find and load test configuration
  std::string config_path = find_config_file();
  std::cout << "Loading configuration from: " << config_path << std::endl;

  try {
    drogon::app().loadConfigFile(config_path);
  } catch (const std::exception& e) {
    std::cerr << "Error loading config file: " << e.what() << std::endl;
    std::cerr << "Working directory: " << std::filesystem::current_path()
              << std::endl;
    return 1;
  }

  std::promise<void> p1;
  std::future<void> f1 = p1.get_future();

  // Start the main loop on another thread
  std::thread thr([&]() {
    // Queues the promise to be fulfilled after starting the loop
    drogon::app().getLoop()->queueInLoop([&p1]() { p1.set_value(); });
    drogon::app().run();
  });

  // The future is only satisfied after the event loop started
  f1.get();
  const int status = drogon::test::run(argc, argv);
  std::this_thread::sleep_for(std::chrono::milliseconds(
      1500));  // prevents seg faults in release builds.

  // Ask the event loop to shutdown and wait
  drogon::app().getLoop()->queueInLoop([]() { drogon::app().quit(); });
  thr.join();
  return status;
}
