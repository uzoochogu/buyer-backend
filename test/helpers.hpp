#include <drogon/drogon.h>

namespace helpers {
inline void cleanup_db() {
  try {
    auto db_client = drogon::app().getDbClient();

    db_client->execSqlSync("DELETE from user_subscriptions");
    db_client->execSqlSync("DELETE from notifications");
    db_client->execSqlSync("DELETE from locations");
    db_client->execSqlSync("DELETE from conversations");
    db_client->execSqlSync("DELETE from posts");
    db_client->execSqlSync("DELETE from media");
    db_client->execSqlSync("DELETE from orders");
    db_client->execSqlSync("DELETE from users");
  } catch (...) {
  }
}

}  // namespace