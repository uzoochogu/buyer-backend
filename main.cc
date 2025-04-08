#include <drogon/drogon.h>

int main() {
  // Set HTTP listener address and port
  drogon::app().addListener("0.0.0.0", 5555);
  // Load config file
  drogon::app().loadConfigFile("../../config.json");
  // drogon::app().loadConfigFile("../config.yaml");

  drogon::app().run();
  return 0;
}
