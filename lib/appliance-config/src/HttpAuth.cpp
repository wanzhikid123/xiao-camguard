#include "HttpAuth.h"
#include <Arduino.h>
#include <string.h>
#include "cJSON.h"
#include "esp_random.h"
extern "C" {
#include "mbedtls/constant_time.h"
}

namespace appliance {
namespace {
esp_err_t reply(httpd_req_t* req, const char* status, const char* json) {
  HttpAuth::responseHeaders(req);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, json);
}
class Lock {
public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex(mutex) { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
private:
  SemaphoreHandle_t mutex;
};
bool header(httpd_req_t* req, const char* name, char* text, size_t size) {
  size_t length = httpd_req_get_hdr_value_len(req, name);
  return length && length < size && httpd_req_get_hdr_value_str(req, name, text, size) == ESP_OK;
}
bool flatJson(const char* text, size_t length) {
  // The login schema is one flat object. Bound parser recursion before cJSON
  // sees attacker-controlled nesting, even inside the 512-byte input limit.
  bool quoted = false, escaped = false;
  unsigned depth = 0;
  for (size_t i = 0; i < length; ++i) {
    const char c = text[i];
    if (quoted) {
      if (escaped) {
        if (c == 'u' && i + 4 < length && !memcmp(text + i + 1, "0000", 4)) return false;
        escaped = false;
      }
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
      continue;
    }
    if (c == '"') quoted = true;
    else if (c == '[' || c == ']') return false;
    else if (c == '{') { if (++depth != 1) return false; }
    else if (c == '}') { if (depth != 1) return false; --depth; }
  }
  return !quoted && depth == 0;
}
void randomHex(char* output) {
  uint8_t bytes[SESSION_TOKEN_BYTES];
  esp_fill_random(bytes, sizeof(bytes));
  const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(bytes); ++i) {
    output[2 * i] = digits[bytes[i] >> 4]; output[2 * i + 1] = digits[bytes[i] & 15];
  }
  output[SESSION_TOKEN_CHARS] = 0;
  clearSecret(bytes, sizeof(bytes));
}
} // namespace
void HttpAuth::responseHeaders(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}
bool HttpAuth::sameOrigin(httpd_req_t* req) {
  size_t length = httpd_req_get_hdr_value_len(req, "Origin");
  if (!length) return true; // Non-browser clients still require session + CSRF.
  char origin[192] = {}, host[160] = {}, expected[192] = {};
  if (!header(req, "Origin", origin, sizeof(origin)) || !header(req, "Host", host, sizeof(host))) return false;
  // v1 transport is trusted-LAN HTTP or HTTP inside WireGuard.
  snprintf(expected, sizeof(expected), "http://%s", host);
  return !strcmp(origin, expected);
}
bool HttpAuth::tokenFromRequest(httpd_req_t* req, char (&token)[SESSION_TOKEN_CHARS + 1]) {
  clearSecret(token, sizeof(token));
  char cookies[512] = {};
  if (!header(req, "Cookie", cookies, sizeof(cookies))) return false;
  bool found = false, valid = true;
  char* item = cookies;
  while (item && *item) {
    char* next = strchr(item, ';'); if (next) *next++ = 0;
    while (*item == ' ' || *item == '\t') ++item;
    if (!strncmp(item, "cam_session=", 12)) {
      const char* value = item + 12;
      if (found || strlen(value) != SESSION_TOKEN_CHARS) { valid = false; break; }
      for (size_t i = 0; i < SESSION_TOKEN_CHARS; ++i)
        if (!(value[i] >= '0' && value[i] <= '9') && !(value[i] >= 'a' && value[i] <= 'f')) valid = false;
      if (!valid) break;
      memcpy(token, value, SESSION_TOKEN_CHARS + 1); found = true;
    }
    item = next;
  }
  clearSecret(cookies, sizeof(cookies));
  if (!valid || !found) clearSecret(token, sizeof(token));
  return valid && found;
}
bool HttpAuth::begin() {
  if (mutex) return task != nullptr;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
#error Revalidate bounded password work scheduling on a changed SDK
#endif
  mutex = xSemaphoreCreateMutex();
  if (!mutex) return false;
  // CPU0 idle/network task remain schedulable; camera/HTTP outrank this worker.
  if (xTaskCreatePinnedToCore(worker, "password", 6144, this, 1, &task, 1) != pdPASS) {
    vSemaphoreDelete(mutex); mutex = nullptr; task = nullptr; return false;
  }
  return true;
}
bool HttpAuth::setAdmin(const Config& config) {
  if (!mutex) return false;
  Lock lock(mutex);
  clearSecret(&verifier, sizeof(verifier));
  verifier.admin = config.admin;
  ++generation;
  sessions.clear();
  return true;
}
bool HttpAuth::configured() {
  if (!mutex) return false;
  Lock lock(mutex); return verifier.admin.configured == 1;
}
bool HttpAuth::registerEndpoints(httpd_handle_t server) {
  if (!mutex || !task) return false;
  const httpd_uri_t endpoints[] = {
    {.uri="/api/auth/login", .method=HTTP_POST, .handler=login, .user_ctx=this},
    {.uri="/api/auth/result", .method=HTTP_GET, .handler=result, .user_ctx=this},
    {.uri="/api/auth/session", .method=HTTP_GET, .handler=session, .user_ctx=this},
    {.uri="/api/auth/logout", .method=HTTP_POST, .handler=logout, .user_ctx=this},
  };
  for (const auto& endpoint : endpoints)
    if (httpd_register_uri_handler(server, &endpoint) != ESP_OK) return false;
  return true;
}
bool HttpAuth::checkRequest(httpd_req_t* req, bool mutation) {
  if (!mutex) { reply(req, "503 Service Unavailable", "{\"error\":\"auth_unavailable\"}"); return false; }
  char token[SESSION_TOKEN_CHARS + 1] = {}, csrf[SESSION_TOKEN_CHARS + 1] = {};
  bool valid = tokenFromRequest(req, token)
               && (!mutation || (sameOrigin(req) && header(req, "X-CSRF-Token", csrf, sizeof(csrf))));
  {
    Lock lock(mutex);
    valid = valid && sessions.authorize(token, csrf, mutation, millis());
  }
  clearSecret(token, sizeof(token)); clearSecret(csrf, sizeof(csrf));
  if (!valid) reply(req, mutation ? "403 Forbidden" : "401 Unauthorized", "{\"error\":\"authentication_required\"}");
  else if (mutation && mutationsBlocked && mutationsBlocked() && strcmp(req->uri, "/api/auth/logout")) {
    reply(req, "409 Conflict", "{\"error\":\"maintenance_in_progress\"}"); return false;
  }
  else responseHeaders(req);
  return valid;
}
bool HttpAuth::streamSessionValid(const char* token) {
  if (!mutex) return false;
  Lock lock(mutex); return sessions.authorize(token, nullptr, false, millis(), false);
}
void HttpAuth::worker(void* context) {
  auto& auth = *static_cast<HttpAuth*>(context);
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    bool changing;
    {Lock lock(auth.mutex);changing=auth.change.occupied&&!auth.change.ready&&!auth.change.done;}
    if(changing){auth.derivePasswordChange();continue;}
    Config verifier = {};
    char password[129] = {};
    size_t bytes;
    uint32_t generation;
    {
      Lock lock(auth.mutex);
      if(!auth.job.occupied||auth.job.done)continue;
      verifier.admin = auth.verifier.admin;
      memcpy(password, auth.job.password, sizeof(password)); bytes = auth.job.bytes;
      generation = auth.job.generation;
      clearSecret(auth.job.password, sizeof(auth.job.password));
    }
    AuthError result = verifyPassword(verifier, password, bytes);
    clearSecret(password, sizeof(password)); clearSecret(&verifier, sizeof(verifier));
    {
      Lock lock(auth.mutex);
      auth.job.result = generation == auth.generation ? result : AuthError::Denied;
      auth.job.done = true;
    }
  }
}
esp_err_t HttpAuth::login(httpd_req_t* req) {
  auto& auth = *static_cast<HttpAuth*>(req->user_ctx);
  char contentType[64] = {}, requestMarker[8] = {};
  if (!sameOrigin(req) || !header(req, "X-Appliance-Request", requestMarker, sizeof(requestMarker))
      || strcmp(requestMarker, "1")) return reply(req, "403 Forbidden", "{\"error\":\"request_origin\"}");
  if (!header(req, "Content-Type", contentType, sizeof(contentType))
      || (strcmp(contentType, "application/json") && strcmp(contentType, "application/json; charset=utf-8"))
      || req->content_len < 1 || req->content_len > 512)
    return reply(req, "400 Bad Request", "{\"error\":\"invalid_login_request\"}");
  char body[513] = {};
  size_t received = 0;
  while (received < req->content_len) {
    int count = httpd_req_recv(req, body + received, req->content_len - received);
    if (count <= 0) { clearSecret(body, sizeof(body)); return reply(req, "400 Bad Request", "{\"error\":\"invalid_login_request\"}"); }
    received += count;
  }
  // cJSON strings cannot represent embedded NUL with a separate byte length.
  // Reject that JSON escape before decoding, rather than accepting truncation.
  bool textValid = !memchr(body, 0, received) && flatJson(body, received);
  cJSON* json = textValid ? cJSON_ParseWithLengthOpts(body, received + 1, nullptr, true) : nullptr;
  clearSecret(body, sizeof(body));
  cJSON* value = json ? cJSON_GetObjectItemCaseSensitive(json, "password") : nullptr;
  char password[129] = {};
  bool valid = cJSON_IsObject(json) && cJSON_GetArraySize(json) == 1 && cJSON_IsString(value)
               && value->valuestring && strlen(value->valuestring) <= 128;
  size_t bytes = valid ? strlen(value->valuestring) : 0;
  if (valid) memcpy(password, value->valuestring, bytes);
  // Also wipe duplicate/unknown-field strings on rejected flat requests.
  if (json && json->valuestring) clearSecret(json->valuestring, strlen(json->valuestring));
  for (cJSON* field = json ? json->child : nullptr; field; field = field->next) {
    if (field->valuestring) clearSecret(field->valuestring, strlen(field->valuestring));
    if (field->string) clearSecret(field->string, strlen(field->string));
  }
  cJSON_Delete(json);
  if (!valid) return reply(req, "400 Bad Request", "{\"error\":\"invalid_login_request\"}");
  char response[128] = {};
  char retryDelay[16] = {};
  const char* status = "202 Accepted";
  {
    Lock lock(auth.mutex);
    if (auth.job.occupied && auth.job.done && uint32_t(millis() - auth.job.submitted) > LOGIN_TICKET_MS)
      clearSecret(&auth.job, sizeof(auth.job));
    if (auth.verifier.admin.configured != 1) {
      status = "409 Conflict"; strcpy(response, "{\"error\":\"admin_not_configured\"}");
    } else if (auth.job.occupied || (auth.change.occupied&&!auth.change.done)) {
      status = "503 Service Unavailable"; strcpy(response, "{\"error\":\"login_busy\"}");
    } else if (!auth.limiter.admit(millis())) {
      status = "429 Too Many Requests"; strcpy(response, "{\"error\":\"login_rate_limit\"}");
      snprintf(retryDelay, sizeof(retryDelay), "%lu", (unsigned long)((auth.limiter.retryAfterMs(millis()) + 999) / 1000));
      httpd_resp_set_hdr(req, "Retry-After", retryDelay);
    } else {
      clearSecret(&auth.job, sizeof(auth.job));
      memcpy(auth.job.password, password, bytes); auth.job.bytes = bytes;
      randomHex(auth.job.ticket); auth.job.generation = auth.generation;
      auth.job.submitted = millis(); auth.job.occupied = true;
      snprintf(response, sizeof(response), "{\"pending\":true,\"ticket\":\"%s\"}", auth.job.ticket);
      xTaskNotifyGive(auth.task);
    }
  }
  clearSecret(password, sizeof(password));
  return reply(req, status, response);
}
esp_err_t HttpAuth::result(httpd_req_t* req) {
  auto& auth = *static_cast<HttpAuth*>(req->user_ctx);
  char ticket[SESSION_TOKEN_CHARS + 1] = {}, response[160] = {}, cookie[160] = {};
  if (!header(req, "X-Login-Ticket", ticket, sizeof(ticket)) || strlen(ticket) != SESSION_TOKEN_CHARS)
    return reply(req, "401 Unauthorized", "{\"error\":\"invalid_login_ticket\"}");
  Lock lock(auth.mutex);
  if (!auth.job.occupied || mbedtls_ct_memcmp(ticket, auth.job.ticket, SESSION_TOKEN_CHARS)
      || uint32_t(millis() - auth.job.submitted) > LOGIN_TICKET_MS)
    return reply(req, "401 Unauthorized", "{\"error\":\"invalid_login_ticket\"}");
  if (!auth.job.done) return reply(req, "202 Accepted", "{\"pending\":true}");
  if (auth.job.result != AuthError::Ok || auth.job.generation != auth.generation) {
    clearSecret(&auth.job, sizeof(auth.job));
    return reply(req, "401 Unauthorized", "{\"error\":\"invalid_credentials\"}");
  }
  SessionCredentials credentials;
  if (!auth.sessions.issue(millis(), credentials)) {
    clearSecret(&auth.job, sizeof(auth.job));
    return reply(req, "503 Service Unavailable", "{\"error\":\"session_capacity\"}");
  }
  snprintf(cookie, sizeof(cookie), "cam_session=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=28800", credentials.token);
  httpd_resp_set_hdr(req, "Set-Cookie", cookie);
  snprintf(response, sizeof(response), "{\"authenticated\":true,\"csrf\":\"%s\"}", credentials.csrf);
  clearSecret(&auth.job, sizeof(auth.job));
  clearSecret(&credentials, sizeof(credentials)); clearSecret(ticket, sizeof(ticket));
  esp_err_t sent = reply(req, "200 OK", response);
  clearSecret(response, sizeof(response)); clearSecret(cookie, sizeof(cookie));
  return sent;
}
esp_err_t HttpAuth::session(httpd_req_t* req) {
  auto& auth = *static_cast<HttpAuth*>(req->user_ctx);
  if (!auth.checkRequest(req)) return ESP_OK;
  char token[SESSION_TOKEN_CHARS + 1] = {}, csrf[SESSION_TOKEN_CHARS + 1] = {}, response[160] = {};
  if (!tokenFromRequest(req, token)) return reply(req, "401 Unauthorized", "{\"error\":\"authentication_required\"}");
  {
    Lock lock(auth.mutex);
    if (!auth.sessions.csrfFor(token, csrf, millis())) return reply(req, "401 Unauthorized", "{\"error\":\"authentication_required\"}");
  }
  snprintf(response, sizeof(response), "{\"authenticated\":true,\"csrf\":\"%s\"}", csrf);
  esp_err_t sent = reply(req, "200 OK", response);
  clearSecret(token, sizeof(token)); clearSecret(csrf, sizeof(csrf)); clearSecret(response, sizeof(response));
  return sent;
}
esp_err_t HttpAuth::logout(httpd_req_t* req) {
  auto& auth = *static_cast<HttpAuth*>(req->user_ctx);
  if (!auth.checkRequest(req, true)) return ESP_OK;
  char token[SESSION_TOKEN_CHARS + 1] = {};
  if (tokenFromRequest(req, token)) { Lock lock(auth.mutex); auth.sessions.logout(token); }
  clearSecret(token, sizeof(token));
  httpd_resp_set_hdr(req, "Set-Cookie", "cam_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  return reply(req, "200 OK", "{\"logged_out\":true}");
}
bool HttpAuth::passwordChangePending() {
  if(!mutex)return false;
  Lock lock(mutex);return change.occupied&&!change.done;
}
esp_err_t HttpAuth::requestPasswordChange(httpd_req_t* req,const char* current,const char* replacement,uint32_t revision,bool& accepted) {
  accepted=false;
  char token[SESSION_TOKEN_CHARS+1]={},response[160]={};
  const bool supplied=tokenFromRequest(req,token);
  const char* status="202 Accepted";
  {
    Lock lock(mutex);
    if(change.occupied&&change.done&&uint32_t(millis()-change.submitted)>PASSWORD_CHANGE_MS)clearSecret(&change,sizeof(change));
    if(job.occupied&&job.done&&uint32_t(millis()-job.submitted)>LOGIN_TICKET_MS)clearSecret(&job,sizeof(job));
    if(!supplied||!sessions.authorize(token,nullptr,false,millis(),false)) {
      status="403 Forbidden";strcpy(response,"{\"error\":\"authentication_required\"}");
    } else if(change.occupied||job.occupied) {
      status="409 Conflict";strcpy(response,"{\"error\":\"password_change_busy\"}");
    } else if(!revision||!current||!replacement||!strlen(current)||strlen(current)>128
      ||!passwordMeetsPolicy(replacement,strlen(replacement))) {
      status="400 Bad Request";strcpy(response,"{\"error\":\"invalid_password_change\"}");
    } else if(!limiter.admit(millis())) {
      status="429 Too Many Requests";strcpy(response,"{\"error\":\"login_rate_limit\"}");
      char delay[16];snprintf(delay,sizeof(delay),"%lu",(unsigned long)((limiter.retryAfterMs(millis())+999)/1000));
      httpd_resp_set_hdr(req,"Retry-After",delay);
    } else {
      clearSecret(&change,sizeof(change));change.error="none";
      strcpy(change.current,current);strcpy(change.replacement,replacement);strcpy(change.session,token);
      randomHex(change.ticket);change.generation=generation;change.revision=revision;
      change.submitted=millis();change.occupied=true;
      accepted=true;
      snprintf(response,sizeof(response),"{\"pending\":true,\"ticket\":\"%s\"}",change.ticket);xTaskNotifyGive(task);
    }
  }
  clearSecret(token,sizeof(token));const esp_err_t sent=reply(req,status,response);clearSecret(response,sizeof(response));return sent;
}
void HttpAuth::derivePasswordChange() {
  Config selected={},derived={};char current[129]={},replacement[129]={};uint32_t expected;
  {
    Lock lock(mutex);selected.admin=verifier.admin;expected=change.generation;
    memcpy(current,change.current,sizeof(current));memcpy(replacement,change.replacement,sizeof(replacement));
    clearSecret(change.current,sizeof(change.current));clearSecret(change.replacement,sizeof(change.replacement));
  }
  AuthError outcome=verifyPassword(selected,current,strlen(current));
  clearSecret(current,sizeof(current));clearSecret(&selected,sizeof(selected));
  bool authorized;
  {Lock lock(mutex);authorized=expected==generation&&sessions.authorize(change.session,nullptr,false,millis(),false)
    &&uint32_t(millis()-change.submitted)<=PASSWORD_CHANGE_MS;}
  if(outcome==AuthError::Ok&&authorized)outcome=enrollPassword(derived,replacement,strlen(replacement));
  clearSecret(replacement,sizeof(replacement));
  {
    Lock lock(mutex);
    authorized=authorized&&expected==generation&&sessions.authorize(change.session,nullptr,false,millis(),false)
      &&uint32_t(millis()-change.submitted)<=PASSWORD_CHANGE_MS;
    if(!authorized){change.error="password_change_cancelled";change.done=true;}
    else if(outcome!=AuthError::Ok){change.error=authErrorName(outcome);change.done=true;}
    else {change.verified.admin=derived.admin;change.ready=true;}
    if(change.done)clearSecret(change.session,sizeof(change.session));
  }
  clearSecret(&derived,sizeof(derived));
}
void HttpAuth::commitPasswordChange(Error (*commit)(void*,const Config&,uint32_t),void* context) {
  Lock lock(mutex);
  if(!change.occupied||!change.ready||change.done)return;
  if(change.generation!=generation||!sessions.authorize(change.session,nullptr,false,millis(),false)
    ||uint32_t(millis()-change.submitted)>PASSWORD_CHANGE_MS||(mutationsBlocked&&mutationsBlocked())) {
    change.error="password_change_cancelled";
  } else {
    const Error stored=commit(context,change.verified,change.revision);
    change.error=stored==Error::Ok?"none":errorName(stored);
    if(stored==Error::Ok){verifier.admin=change.verified.admin;++generation;sessions.clear();}
  }
  clearSecret(&change.verified,sizeof(change.verified));clearSecret(change.session,sizeof(change.session));
  change.ready=false;change.done=true;
}
esp_err_t HttpAuth::passwordChangeResult(httpd_req_t* req) {
  char supplied[SESSION_TOKEN_CHARS+1]={},marker[8]={};
  if(!sameOrigin(req)||!header(req,"X-Appliance-Request",marker,sizeof(marker))||strcmp(marker,"1")
    ||!header(req,"X-Password-Change-Ticket",supplied,sizeof(supplied))||strlen(supplied)!=SESSION_TOKEN_CHARS)
    return reply(req,"401 Unauthorized","{\"error\":\"invalid_password_change_ticket\"}");
  Lock lock(mutex);
  const bool valid=change.occupied&&uint32_t(millis()-change.submitted)<=PASSWORD_CHANGE_MS
    &&mbedtls_ct_memcmp(supplied,change.ticket,SESSION_TOKEN_CHARS)==0;
  clearSecret(supplied,sizeof(supplied));
  if(!valid)return reply(req,"401 Unauthorized","{\"error\":\"invalid_password_change_ticket\"}");
  if(!change.done)return reply(req,"202 Accepted","{\"pending\":true}");
  char body[128];const bool success=!strcmp(change.error,"none");
  snprintf(body,sizeof(body),success?"{\"changed\":true,\"signInRequired\":true}":"{\"error\":\"%s\"}",change.error);
  const char* status=success?"200 OK":!strcmp(change.error,"invalid_credentials")?"401 Unauthorized":"409 Conflict";
  clearSecret(&change,sizeof(change));return reply(req,status,body);
}
} // namespace appliance
