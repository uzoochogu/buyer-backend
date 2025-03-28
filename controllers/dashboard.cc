#include "dashboard.hpp"

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>
#include <drogon/orm/Criteria.h>
#include <drogon/orm/DbClient.h>
#include <drogon/orm/Exception.h>
#include <drogon/orm/Field.h>
#include <drogon/orm/Mapper.h>
#include <drogon/orm/Result.h>
#include <drogon/orm/Row.h>
#include <drogon/orm/SqlBinder.h>

// #include <drogon/orm/Transaction.h>
#include <drogon/orm/ResultIterator.h>
// #include <drogon/orm/ResultStream.h>
// #include <drogon/orm/ResultStreamIterator.h>
// #include <drogon/orm/ResultStreamRow.h>
// #include <drogon/orm/ResultStreamField.h>
// #include <drogon/orm/ResultStreamException.h>

using namespace drogon;
using namespace drogon::orm;

using namespace api::v1;

void Dashboard::get_dashboard_data(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto db = app().getDbClient();

  // Fetch orders data
  db->execSqlAsync(
      "SELECT status, COUNT(*) FROM orders GROUP BY status",
      [callback](const Result& result) {
        Json::Value ordersData;
        for (const auto& row : result) {
          ordersData[row["status"].as<std::string>()] = row["count"].as<int>();
        }

        // Prepare response
        Json::Value ret;
        ret["orders"] = ordersData;
        auto resp = HttpResponse::newHttpJsonResponse(ret);
        callback(resp);
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      });
}
