#include <drogon/HttpMiddleware.h>
using namespace drogon;

class CorsMiddleware : public HttpMiddleware<CorsMiddleware> {
 public:
  CorsMiddleware() {};

  void invoke(const HttpRequestPtr &req, MiddlewareNextCallback &&nextCb,
              MiddlewareCallback &&mcb) override {
    const std::string &origin = req->getHeader("origin");
    if (!req->peerAddr().isLoopbackIp()) {
      // intercept directly
      mcb(HttpResponse::newNotFoundResponse(req));
      return;
    }
    // Do something before calling the next middleware
    nextCb([&origin, mcb = std::move(mcb)](const HttpResponsePtr &resp) {
      // Do something after the next middleware returns
      resp->addHeader("Access-Control-Allow-Origin", origin);
      resp->addHeader("Access-Control-Allow-Credentials", "true");
      resp->addHeader("Access-Control-Allow-Methods",
                      "GET, POST, PUT, DELETE, OPTIONS");
      resp->addHeader("Access-Control-Allow-Headers",
                      "Content-Type, Authorization");
      mcb(resp);
    });
  }
};
