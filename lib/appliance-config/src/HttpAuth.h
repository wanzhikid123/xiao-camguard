#pragma once
#include "AdminAuth.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace appliance {
// HTTP adapter with asynchronous password work. No camera, SD or NVS access.
// RF must already be active. The owner lives for the firmware's lifetime.
// setAdmin invalidates sessions and in-flight login results on every change.
class HttpAuth {
public:
  // 80 MHz product login measured 168 seconds. Keep one bounded job/ticket
  // with enough margin; do not weaken the verifier or change session expiry.
  static constexpr uint32_t LOGIN_TICKET_MS = 300000;
  static constexpr uint32_t PASSWORD_CHANGE_MS = 600000;
  bool begin();
  bool registerEndpoints(httpd_handle_t server);
  bool setAdmin(const Config& config);
  bool configured();
  bool checkRequest(httpd_req_t* request, bool mutation = false);
  void setMutationGuard(bool (*blocked)()) { mutationsBlocked = blocked; }
  bool streamSessionValid(const char* token);
  static bool tokenFromRequest(httpd_req_t* request, char (&token)[SESSION_TOKEN_CHARS + 1]);
  static bool sameOrigin(httpd_req_t* request);
  static void responseHeaders(httpd_req_t* request);
  esp_err_t requestPasswordChange(httpd_req_t* request,const char* current,const char* replacement,uint32_t revision,bool& accepted);
  esp_err_t passwordChangeResult(httpd_req_t* request);
  bool passwordChangePending();
  // Called only by the configuration owner. Rechecks the originating session
  // and generation under the auth lock through the small atomic NVS commit.
  void commitPasswordChange(Error (*commit)(void*,const Config&,uint32_t),void* context);
private:
  SemaphoreHandle_t mutex = nullptr;
  TaskHandle_t task = nullptr;
  bool (*mutationsBlocked)() = nullptr;
  Config verifier = {};
  Sessions sessions;
  LoginLimiter limiter;
  uint32_t generation = 0;
  struct Job {
    char password[129] = {};
    size_t bytes = 0;
    char ticket[SESSION_TOKEN_CHARS + 1] = {};
    uint32_t generation = 0;
    uint32_t submitted = 0;
    AuthError result = AuthError::Denied;
    bool occupied = false;
    bool done = false;
  } job;
  struct PasswordChange {
    char current[129]={}, replacement[129]={};
    char ticket[SESSION_TOKEN_CHARS+1]={}, session[SESSION_TOKEN_CHARS+1]={};
    Config verified={};
    uint32_t generation=0,submitted=0,revision=0;
    const char* error="none";
    bool occupied=false,ready=false,done=false;
  } change;
  void derivePasswordChange();
  static void worker(void* context);
  static esp_err_t login(httpd_req_t* request);
  static esp_err_t result(httpd_req_t* request);
  static esp_err_t session(httpd_req_t* request);
  static esp_err_t logout(httpd_req_t* request);
};
} // namespace appliance
