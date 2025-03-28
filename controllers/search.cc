#include "search.hpp"

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

void Search::search(const HttpRequestPtr& req,
                    std::function<void(const HttpResponsePtr&)>&& callback) {
  auto query = req->getParameter("query");

  auto db = app().getDbClient();

  // Search orders
  db->execSqlAsync(
      "SELECT * FROM orders WHERE status ILIKE $1",
      [callback](const Result& ordersResult) {
        Json::Value searchResults;

        for (const auto& row : ordersResult) {
          Json::Value order;
          order["type"] = "Order";
          order["details"] = "Order #" + row["id"].as<std::string>() + " - " +
                             row["status"].as<std::string>();
          searchResults.append(order);
        }

        // Prepare response
        auto resp = HttpResponse::newHttpJsonResponse(searchResults);
        callback(resp);
      },
      [](const DrogonDbException& e) {
        LOG_ERROR << "Database error: " << e.base().what();
      },
      "%" + query + "%");
}
