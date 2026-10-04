#include "NetworkService.h"
#include "WireGuardConfig.h"
#include "FullTunnelTest.h"
#include <Arduino.h>
#include <WiFi.h>
#include "cJSON.h"
#include "esp_random.h"
#include <stdlib.h>
#include "lwip/sockets.h"
extern "C" {
#include "mbedtls/constant_time.h"
}

namespace appliance {
namespace {
class Lock {
public:
  explicit Lock(SemaphoreHandle_t mutex) : mutex(mutex) { xSemaphoreTake(mutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mutex); }
private:
  SemaphoreHandle_t mutex;
};
struct PrivateInput {
  char* text;
  explicit PrivateInput(size_t bytes) : text(static_cast<char*>(calloc(bytes, 1))), bytes(bytes) {}
  ~PrivateInput() { if (text) { clearSecret(text, bytes); free(text); } }
private:
  size_t bytes;
};
esp_err_t reply(httpd_req_t* req, const char* status, const char* body) {
  HttpAuth::responseHeaders(req);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, body);
}
bool header(httpd_req_t* req, const char* name, char* value, size_t size) {
  const size_t length = httpd_req_get_hdr_value_len(req, name);
  return length && length < size && httpd_req_get_hdr_value_str(req, name, value, size) == ESP_OK;
}
uint32_t socketAddress(httpd_req_t* req) {
  sockaddr_storage address = {};
  socklen_t bytes = sizeof(address);
  if (getsockname(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr*>(&address), &bytes)) return 0;
  if (address.ss_family == AF_INET) return reinterpret_cast<sockaddr_in*>(&address)->sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
  // IDF HTTP uses a dual-stack listener: an IPv4 client can have an IPv4-mapped
  // sockaddr_in6. This is still IPv4 proof, not support for native IPv6 routes.
  if (address.ss_family == AF_INET6) {
    auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&address);
    if (IN6_IS_ADDR_V4MAPPED(ipv6->sin6_addr.un.u32_addr)) {
      uint32_t ipv4; memcpy(&ipv4, ipv6->sin6_addr.s6_addr + 12, sizeof(ipv4)); return ipv4;
    }
  }
#endif
  return 0;
}
bool atAP(httpd_req_t* req) {
  const uint32_t ip = WiFi.softAPIP();
  return (WiFi.getMode() & WIFI_AP) && ip && socketAddress(req) == ip;
}
bool atLAN(httpd_req_t* req) {
  const uint32_t ip = WiFi.localIP();
  return WiFi.status() == WL_CONNECTED && ip && socketAddress(req) == ip;
}
void randomToken(char (&output)[65]) {
  uint8_t bytes[32]; esp_fill_random(bytes, sizeof(bytes));
  const char* digits = "0123456789abcdef";
  for (unsigned i = 0; i < 32; ++i) { output[i*2] = digits[bytes[i] >> 4]; output[i*2+1] = digits[bytes[i] & 15]; }
  output[64] = 0; clearSecret(bytes, sizeof(bytes));
}
bool sameToken(const char* supplied, const char* expected) {
  return expected[0] && strlen(supplied) == 64 && mbedtls_ct_memcmp(supplied, expected, 64) == 0;
}
bool flatJson(const char* text, size_t bytes) {
  bool quoted = false, escaped = false; unsigned depth = 0;
  for (size_t i = 0; i < bytes; ++i) {
    char c = text[i];
    if (quoted) {
      if (escaped) {
        if (c == 'u' && i + 4 < bytes && !memcmp(text+i+1, "0000", 4)) return false;
        escaped = false;
      } else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '[' || c == ']') return false;
    else if (c == '{') { if (++depth != 1) return false; }
    else if (c == '}') { if (depth != 1) return false; --depth; }
  }
  return !quoted && !depth;
}
void destroy(cJSON* root) {
  for (auto* item = root ? root->child : nullptr; item; item = item->next) {
    if (item->valuestring) clearSecret(item->valuestring, strlen(item->valuestring));
    if (item->string) clearSecret(item->string, strlen(item->string));
  }
  cJSON_Delete(root);
}
cJSON* readJson(httpd_req_t* req,size_t maximum=1024) {
  char type[64] = {};
  if (strchr(req->uri, '?') || !header(req, "Content-Type", type, sizeof(type))
    || (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8"))
    || !req->content_len || req->content_len > maximum || req->content_len > 1536) return nullptr;
  char raw[1537] = {}; size_t received = 0;
  while (received < req->content_len) {
    int count = httpd_req_recv(req, raw + received, req->content_len - received);
    if (count <= 0) { clearSecret(raw, sizeof(raw)); return nullptr; }
    received += count;
  }
  cJSON* root = !memchr(raw, 0, received) && flatJson(raw, received)
    ? cJSON_ParseWithLengthOpts(raw, received + 1, nullptr, true) : nullptr;
  clearSecret(raw, sizeof(raw)); return root;
}
template<size_t N> bool field(cJSON* root, const char* name, char (&out)[N]) {
  auto* item = cJSON_GetObjectItemCaseSensitive(root, name);
  if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) >= N) return false;
  strlcpy(out, item->valuestring, N); return true;
}
} // namespace

bool NetworkService::begin(void (*notifyConnected)()) {
  if (task) return true;
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
#error Revalidate bounded enrollment scheduling on a changed SDK
#endif
  connected = notifyConnected;
  mutex = xSemaphoreCreateMutex(); initialized = xSemaphoreCreateBinary();
  if (!mutex || !initialized) return false;
  if (xTaskCreatePinnedToCore(worker, "networkOwner", 8192, this, 1, &task, 1) != pdPASS) return false;
  return xSemaphoreTake(initialized, pdMS_TO_TICKS(15000)) == pdTRUE;
}
NetworkStatus NetworkService::status() { Lock lock(mutex); return snapshot; }
#if defined(XIAO_APPLIANCE)
WireGuardStatus NetworkService::wireguardStatus() { Lock lock(mutex); return wgSnapshot; }
#endif
bool NetworkService::busy() {
  Lock lock(mutex);
  return processing || accountBusy || testBusy() || command != Command::None || snapshot.trialRevision
    || jobState == JobState::Hashing || jobState == JobState::Applying;
}
bool NetworkService::submitAuthenticatedBleWifi(const char* ssid,const char* secret){
  if(!mutex||!auth.configured()||!ssid||!secret||strlen(ssid)>=sizeof(candidate.wifiSSID)||strlen(secret)>=sizeof(candidate.wifiPassword))return false;
  Config next=defaults();strlcpy(next.wifiSSID,ssid,sizeof(next.wifiSSID));strlcpy(next.wifiPassword,secret,sizeof(next.wifiPassword));
  bool valid=next.wifiSSID[0]&&validate(next)==Error::Ok;
  {Lock lock(mutex);if(processing||accountBusy||command!=Command::None||snapshot.trialRevision)valid=false;
    if(valid){candidate=next;bleWifiJob=true;setupJob=false;command=Command::Wifi;applyNotBefore=millis();submitted=millis();jobState=JobState::Applying;jobError=NetworkError::None;xTaskNotifyGive(task);}}
  clearSecret(&next,sizeof(next));return valid;
}
bool NetworkService::requestFactoryReset(){if(!mutex)return false;Lock lock(mutex);
  if(processing||accountBusy||command!=Command::None||snapshot.trialRevision)return false;
  command=Command::FactoryReset;applyNotBefore=millis()+750;xTaskNotifyGive(task);return true;}
void NetworkService::worker(void* context) {
  auto& service = *static_cast<NetworkService*>(context);
  service.manager.begin(millis());
  Config active = defaults(); service.manager.loadActive(active);
  service.auth.setAdmin(active); clearSecret(&active, sizeof(active));
  {
    Lock lock(service.mutex);
    service.snapshot = service.manager.status(); randomToken(service.nonce);
  }
  xSemaphoreGive(service.initialized);
  bool wasConnected = false;
  for (;;) {
    Command work; Config config = {}; char password[129] = {};
    uint32_t revision = 0, address = 0;
#if defined(XIAO_APPLIANCE)
    bool fullMode = false;
#endif
    {
      Lock lock(service.mutex);
      work = int32_t(millis() - service.applyNotBefore) >= 0 ? service.command : Command::None;
      if (work != Command::None) service.command = Command::None;
      service.processing = work != Command::None;
      if (work == Command::Enroll || work == Command::Wifi) {
        config = service.candidate; clearSecret(&service.candidate, sizeof(service.candidate));
        memcpy(password, service.password, sizeof(password)); clearSecret(service.password, sizeof(service.password));
      }
      revision = service.expectedRevision; address = service.localAddress;
#if defined(XIAO_APPLIANCE)
      fullMode = service.fullModeCandidate;
#endif
    }
    NetworkError outcome = NetworkError::None;
    if (work == Command::Enroll || work == Command::Wifi || work == Command::WGImport || work == Command::WGDisable || work == Command::WGClear || work == Command::WGMode) {
      if (work == Command::Wifi) {
        Config old = {}; service.manager.loadActive(old);
        // HTTP only supplies Wi-Fi fields; keep the selected admin/WG/app values.
        memcpy(old.wifiSSID, config.wifiSSID, sizeof(old.wifiSSID));
        memcpy(old.wifiPassword, config.wifiPassword, sizeof(old.wifiPassword));
        config = old; clearSecret(&old, sizeof(old));
      }
#if defined(XIAO_APPLIANCE)
      if (work == Command::WGImport || work == Command::WGDisable || work == Command::WGClear || work == Command::WGMode) {
        if (service.manager.loadActive(config) != Error::Ok) outcome = NetworkError::Storage;
        if (work == Command::WGImport) {
          ImportSummary summary;
          const ImportError parsed = outcome == NetworkError::None
            ? importWireGuard(service.importText, service.importBytes, config, summary) : ImportError::InvalidValue;
          clearSecret(service.importText, sizeof(service.importText)); service.importBytes = 0;
          if (parsed != ImportError::Ok) outcome = NetworkError::InvalidConfig;
          Lock lock(service.mutex);
          service.importError = importErrorName(parsed);
          service.importedIPv6 = summary.excludedIPv6Networks + summary.excludedIPv6Addresses;
          service.importedDefault = summary.fullTunnelRequested;
        } else if (work == Command::WGMode) {
          config.wg.fullTunnel=fullMode;
          if(!service.driver.supports(config)) {
            outcome=NetworkError::UnsupportedNetwork;
            Lock lock(service.mutex);service.importError="full_tunnel_prerequisites_missing";
          } else { Lock lock(service.mutex);service.importError="none"; }
        } else if (work == Command::WGClear) {
          clearSecret(&config.wg, sizeof(config.wg));
          config.wg = defaults().wg;
        } else { config.wg.enabled = config.wg.fullTunnel = 0; }
      }
#endif
      if (work == Command::Enroll && enrollPassword(config, password, strlen(password)) != AuthError::Ok)
        outcome = NetworkError::InvalidConfig;
      clearSecret(password, sizeof(password));
      if (outcome == NetworkError::None) outcome = service.manager.submit(config, millis(), revision);
      clearSecret(&config, sizeof(config));
      Lock lock(service.mutex);
      service.jobError = outcome; service.expectedRevision = revision;
      service.jobState = outcome == NetworkError::None ? JobState::Connecting : JobState::Failed;
    }
#if defined(XIAO_APPLIANCE)
    else if (work == Command::WGProbe) service.driver.probe(address);
    else if (work == Command::WGRestart) service.driver.restart();
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
    else if(work==Command::WGPolicyFixture) {
      service.driver.policyFixture(service.fixtureAction,service.fixtureHostname);
      clearSecret(service.fixtureAction,sizeof(service.fixtureAction));clearSecret(service.fixtureHostname,sizeof(service.fixtureHostname));
    }
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
    else if (work == Command::WGFullStart) service.driver.fullTestStart(address);
    else if (work == Command::WGFullStop) service.driver.fullTestStop();
    else if (work == Command::WGFullHold) service.driver.fullTestHold();
#endif
#endif
    else if(work==Command::FactoryReset){
      service.manager.stop();
      bool cleared=true;
      for(const char* name:{"camcfg","camcapture","camjobs","camble","ESP-CAM_MJPEG"}){
        Preferences p;if(!p.begin(name,false)){cleared=false;break;}cleared=p.clear()&&cleared;p.end();}
      if(cleared){Config empty=defaults();service.auth.setAdmin(empty);clearSecret(&empty,sizeof(empty));delay(500);ESP.restart();}
      else{service.manager.begin(millis());Lock lock(service.mutex);service.jobError=NetworkError::Storage;}
    }
    else if (work == Command::Confirm) {
      // Recheck the current underlay on the owning task, in addition to the
      // socket proof captured by the authorized handler.
      outcome = WiFi.status() == WL_CONNECTED && address && address == uint32_t(WiFi.localIP())
        ? service.manager.confirmLocalAccess(revision, millis()) : NetworkError::ProofMissing;
      if (outcome == NetworkError::None) {
        Config selected = {}; service.manager.loadActive(selected);
        // Enrollment changes the verifier; Wi-Fi edits keep existing sessions.
        if (service.setupJob) service.auth.setAdmin(selected);
        clearSecret(&selected, sizeof(selected));
      }
      Lock lock(service.mutex);
      service.jobError = outcome;
      if (outcome == NetworkError::None) {
        service.jobState = JobState::Active;
        clearSecret(service.nonce, sizeof(service.nonce));
      }
    } else if (work == Command::Cancel) {
      outcome = service.manager.cancelTrial(millis());
      Lock lock(service.mutex); service.jobError = outcome; service.jobState = JobState::Failed;
    }
    clearSecret(password, sizeof(password)); clearSecret(&config, sizeof(config));
    service.auth.commitPasswordChange(saveAdmin,&service);
    service.manager.tick(millis());
    bool bleJob;{Lock lock(service.mutex);bleJob=service.bleWifiJob;}
    if(bleJob){const auto pending=service.manager.status();
      if(pending.trialRevision){const auto promoted=service.manager.confirmAuthenticatedBleWifi(pending.trialRevision,millis());
        if(promoted==NetworkError::None){Lock lock(service.mutex);service.bleWifiJob=false;service.jobState=JobState::Active;service.jobError=NetworkError::None;}}
      else if(work!=Command::Wifi){Lock lock(service.mutex);service.bleWifiJob=false;}}
    const NetworkStatus current = service.manager.status();
    {
      Lock lock(service.mutex);
      service.snapshot = current; service.processing = false;
      if(!service.auth.passwordChangePending())service.accountBusy=false;
#if defined(XIAO_APPLIANCE)
      service.wgSnapshot = service.driver.status();
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
      service.fullTestActive = fullTunnelTestStatus().active;
#endif
#endif
      if (service.jobState == JobState::Connecting || service.jobState == JobState::AwaitingConfirmation) {
        if (current.state == NetworkState::TrialReady) service.jobState = JobState::AwaitingConfirmation;
        else if (!current.trialRevision) { service.jobState = JobState::Failed; service.jobError = current.error; }
      }
      if (service.ticket[0] && uint32_t(millis() - service.submitted) > 180000
          && service.jobState != JobState::Hashing) clearSecret(service.ticket, sizeof(service.ticket));
    }
    if (current.readiness.wifiConnected && !wasConnected && service.connected) service.connected();
    wasConnected = current.readiness.wifiConnected;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
  }
}
bool NetworkService::setupAuthorized(httpd_req_t* req, bool initial) {
  char supplied[65] = {}, marker[8] = {};
  bool valid = HttpAuth::sameOrigin(req) && header(req, "X-Appliance-Request", marker, sizeof(marker))
    && !strcmp(marker, "1") && header(req, initial ? "X-Setup-Nonce" : "X-Setup-Ticket", supplied, sizeof(supplied));
  {
    Lock lock(mutex);
    valid = valid && sameToken(supplied, initial ? nonce : ticket)
      && (initial ? atAP(req) : (setupJob && uint32_t(millis() - submitted) <= 180000));
  }
  clearSecret(supplied, sizeof(supplied));
  if (!valid) reply(req, "403 Forbidden", "{\"error\":\"setup_authorization\"}");
  return valid;
}
esp_err_t NetworkService::bootstrap(httpd_req_t* req) {
  if (auth.configured()) return reply(req, "200 OK", "{\"adminConfigured\":true}");
  char body[160];
  {
    Lock lock(mutex);
    if (atAP(req) && snapshot.state != NetworkState::StorageFault)
      snprintf(body, sizeof(body), "{\"adminConfigured\":false,\"setupNonce\":\"%s\"}", nonce);
    else strcpy(body, "{\"adminConfigured\":false}");
  }
  return reply(req, "200 OK", body);
}
esp_err_t NetworkService::submit(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  const bool setup = !strcmp(req->uri, "/api/setup");
  if (setup) {
    if (service.auth.configured()) return reply(req, "409 Conflict", "{\"error\":\"admin_already_configured\"}");
    if (!service.setupAuthorized(req, true)) return ESP_OK;
  } else if (!service.auth.checkRequest(req, true)) return ESP_OK;
  auto* root = readJson(req);
  Config config = defaults(); char password[129] = {};
  bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == (setup ? 3 : 2)
    && field(root, "ssid", config.wifiSSID) && field(root, "wifiPassword", config.wifiPassword)
    && config.wifiSSID[0] && validate(config) == Error::Ok
    && (!setup || (field(root, "adminPassword", password) && passwordMeetsPolicy(password, strlen(password))));
  destroy(root);
  char response[160]; const char* status = "202 Accepted";
  {
    Lock lock(service.mutex);
    if (!valid) { status = "400 Bad Request"; strcpy(response, "{\"error\":\"invalid_network_request\"}"); }
    else if (service.processing || service.accountBusy || service.testBusy() || service.command != Command::None || service.snapshot.trialRevision
      || service.jobState == JobState::Hashing || service.jobState == JobState::Applying) {
      status = "409 Conflict"; strcpy(response, "{\"error\":\"network_busy\"}");
    } else {
      service.candidate = config; memcpy(service.password, password, sizeof(password));
      service.command = setup ? Command::Enroll : Command::Wifi;
      service.bleWifiJob=false;
      // Allow the accepted HTTP response to leave before changing interfaces.
      service.applyNotBefore = millis() + 750;
      service.jobState = setup ? JobState::Hashing : JobState::Applying;
      service.setupJob = setup; service.submitted = millis(); service.jobError = NetworkError::None;
      service.expectedRevision = 0; randomToken(service.ticket);
      snprintf(response, sizeof(response), "{\"pending\":true,\"ticket\":\"%s\"}", service.ticket);
      xTaskNotifyGive(service.task);
    }
  }
  clearSecret(&config, sizeof(config)); clearSecret(password, sizeof(password));
  return reply(req, status, response);
}
esp_err_t NetworkService::sendStatus(httpd_req_t* req, bool job) {
  char response[384], lan[16]; const IPAddress ip = WiFi.localIP();
  snprintf(lan, sizeof(lan), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  {
    Lock lock(mutex);
    const char* phase = "idle";
    if (job) switch (jobState) {
      case JobState::Hashing: phase = "hashing"; break;
      case JobState::Applying: phase = "applying"; break;
      case JobState::Connecting: phase = "connecting"; break;
      case JobState::AwaitingConfirmation: phase = "awaiting_local_confirmation"; break;
      case JobState::Active: phase = "active"; break;
      case JobState::Failed: phase = "failed"; break;
      default: break;
    }
    snprintf(response, sizeof(response),
      "{\"phase\":\"%s\",\"state\":\"%s\",\"error\":\"%s\",\"revision\":%lu,"
      "\"activeRevision\":%lu,\"wifiConnected\":%s,\"rescueAP\":%s,\"lanIP\":\"%s\"}",
      phase, networkStateName(snapshot.state), networkErrorName(job ? jobError : snapshot.error),
      (unsigned long)snapshot.trialRevision, (unsigned long)snapshot.config.activeRevision,
      snapshot.readiness.wifiConnected ? "true" : "false", snapshot.rescueAP ? "true" : "false", lan);
  }
  return reply(req, "200 OK", response);
}
esp_err_t NetworkService::result(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  if (!service.setupAuthorized(req, false)) return ESP_OK;
  return service.sendStatus(req, true);
}
esp_err_t NetworkService::networkStatus(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  if (!service.auth.checkRequest(req)) return ESP_OK;
  return service.sendStatus(req, false);
}
esp_err_t NetworkService::confirm(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  const bool setup = !strcmp(req->uri, "/api/setup/confirm");
  if (setup) { if (!service.setupAuthorized(req, false)) return ESP_OK; }
  else if (!service.auth.checkRequest(req, true)) return ESP_OK;
  if (!atLAN(req)) return reply(req, "403 Forbidden", "{\"error\":\"confirm_from_candidate_lan\"}");
  auto* root = readJson(req); auto* value = root ? cJSON_GetObjectItemCaseSensitive(root, "revision") : nullptr;
  const bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsNumber(value)
    && value->valuedouble >= 1 && value->valuedouble <= UINT32_MAX
    && value->valuedouble == double(uint32_t(value->valuedouble));
  const uint32_t revision = valid ? uint32_t(value->valuedouble) : 0; destroy(root);
  if (!valid) return reply(req, "400 Bad Request", "{\"error\":\"invalid_revision\"}");
  {
    Lock lock(service.mutex);
    if (service.processing || service.accountBusy || service.command != Command::None || service.snapshot.state != NetworkState::TrialReady
        || !revision || revision != service.snapshot.trialRevision)
      return reply(req, "409 Conflict", "{\"error\":\"trial_not_ready_or_stale\"}");
    service.expectedRevision = revision; service.localAddress = socketAddress(req);
    service.command = Command::Confirm; service.applyNotBefore = millis() + 750; xTaskNotifyGive(service.task);
  }
  return reply(req, "202 Accepted", "{\"confirming\":true}");
}
esp_err_t NetworkService::cancel(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  if (!service.auth.checkRequest(req, true)) return ESP_OK;
  if (req->content_len || strchr(req->uri, '?')) return reply(req, "400 Bad Request", "{\"error\":\"empty_body_required\"}");
  {
    Lock lock(service.mutex);
    if (service.processing || service.accountBusy || service.command != Command::None || !service.snapshot.trialRevision)
      return reply(req, "409 Conflict", "{\"error\":\"no_cancellable_trial\"}");
    service.command = Command::Cancel; service.applyNotBefore = millis() + 750; xTaskNotifyGive(service.task);
  }
  return reply(req, "202 Accepted", "{\"cancelling\":true}");
}
Error NetworkService::saveAdmin(void* context,const Config& verified,uint32_t revision) {
  return static_cast<NetworkService*>(context)->manager.updateAdmin(verified,revision);
}
esp_err_t NetworkService::account(httpd_req_t* req) {
  auto& service=*static_cast<NetworkService*>(req->user_ctx);
  if(strchr(req->uri,'?'))return reply(req,"400 Bad Request","{\"error\":\"query_not_allowed\"}");
  if(!strcmp(req->uri,"/api/account/password/result")) {
    if(req->content_len)return reply(req,"400 Bad Request","{\"error\":\"empty_body_required\"}");
    return service.auth.passwordChangeResult(req);
  }
  const bool mutation=req->method==HTTP_POST;
  if(!service.auth.checkRequest(req,mutation))return ESP_OK;
  if(!mutation) {
    if(req->content_len)return reply(req,"400 Bad Request","{\"error\":\"empty_body_required\"}");
    char body[192];snprintf(body,sizeof(body),"{\"adminConfigured\":true,\"passwordChangePending\":%s,\"minimumCharacters\":%u,\"maximumBytes\":128}",
      service.auth.passwordChangePending()?"true":"false",unsigned(PASSWORD_MIN_CHARACTERS));
    return reply(req,"200 OK",body);
  }
  auto* root=readJson(req,1536);
  char current[129]={},replacement[129]={},confirmation[129]={};
  const bool valid=cJSON_IsObject(root)&&cJSON_GetArraySize(root)==3
    &&field(root,"currentPassword",current)&&field(root,"newPassword",replacement)&&field(root,"confirmPassword",confirmation)
    &&current[0]&&passwordMeetsPolicy(replacement,strlen(replacement))
    &&!strcmp(replacement,confirmation)&&strcmp(current,replacement);
  destroy(root);
  esp_err_t sent;
  if(!valid)sent=reply(req,"400 Bad Request","{\"error\":\"invalid_password_change\"}");
  else {
    // Reserve admission through the auth queue operation. The owner never
    // takes this mutex from its auth commit callback (Network -> Auth only).
    Lock lock(service.mutex);
    if(service.processing||service.accountBusy||service.testBusy()||service.command!=Command::None||service.snapshot.trialRevision
      ||service.jobState==JobState::Hashing||service.jobState==JobState::Applying)
      sent=reply(req,"409 Conflict","{\"error\":\"network_busy\"}");
    else {
      service.accountBusy=true;bool accepted=false;
      sent=service.auth.requestPasswordChange(req,current,replacement,service.snapshot.config.activeRevision,accepted);
      if(!accepted)service.accountBusy=false;
    }
  }
  clearSecret(current,sizeof(current));clearSecret(replacement,sizeof(replacement));clearSecret(confirmation,sizeof(confirmation));
  return sent;
}
bool NetworkService::registerEndpoints(httpd_handle_t server) {
  if (!task) return false;
  const httpd_uri_t routes[] = {
#if defined(XIAO_APPLIANCE)
    {.uri="/api/wireguard", .method=HTTP_GET, .handler=wireguard, .user_ctx=this},
    {.uri="/api/wireguard/import", .method=HTTP_POST, .handler=wireguard, .user_ctx=this},
    {.uri="/api/wireguard/disable", .method=HTTP_POST, .handler=wireguard, .user_ctx=this},
    {.uri="/api/wireguard/clear", .method=HTTP_POST, .handler=wireguard, .user_ctx=this},
    {.uri="/api/wireguard/restart", .method=HTTP_POST, .handler=wireguard, .user_ctx=this},
    {.uri="/api/wireguard/probe", .method=HTTP_POST, .handler=wireguard, .user_ctx=this},
    {.uri="/api/wireguard/mode", .method=HTTP_POST, .handler=wireguard, .user_ctx=this},
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
    {.uri="/api/wireguard/policy-test",.method=HTTP_GET,.handler=policyFixture,.user_ctx=this},
    {.uri="/api/wireguard/policy-test",.method=HTTP_POST,.handler=policyFixture,.user_ctx=this},
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
    {.uri="/api/wireguard/full-test", .method=HTTP_GET, .handler=fullTest, .user_ctx=this},
    {.uri="/api/wireguard/full-test", .method=HTTP_POST, .handler=fullTest, .user_ctx=this},
#endif
#endif
    {.uri="/api/setup", .method=HTTP_POST, .handler=submit, .user_ctx=this},
    {.uri="/api/setup/result", .method=HTTP_GET, .handler=result, .user_ctx=this},
    {.uri="/api/setup/confirm", .method=HTTP_POST, .handler=confirm, .user_ctx=this},
    {.uri="/api/network/trial", .method=HTTP_POST, .handler=submit, .user_ctx=this},
    {.uri="/api/network", .method=HTTP_GET, .handler=networkStatus, .user_ctx=this},
    {.uri="/api/network/confirm", .method=HTTP_POST, .handler=confirm, .user_ctx=this},
    {.uri="/api/network/cancel", .method=HTTP_POST, .handler=cancel, .user_ctx=this},
    {.uri="/api/account", .method=HTTP_GET, .handler=account, .user_ctx=this},
    {.uri="/api/account/password", .method=HTTP_POST, .handler=account, .user_ctx=this},
    {.uri="/api/account/password/result", .method=HTTP_GET, .handler=account, .user_ctx=this},
  };
  for (const auto& route : routes) if (httpd_register_uri_handler(server, &route) != ESP_OK) return false;
  return true;
}
#if defined(XIAO_APPLIANCE)
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
esp_err_t NetworkService::policyFixture(httpd_req_t* req) {
  auto& service=*static_cast<NetworkService*>(req->user_ctx);
  if(!service.auth.checkRequest(req,req->method==HTTP_POST))return ESP_OK;
  if(strchr(req->uri,'?'))return reply(req,"400 Bad Request","{\"error\":\"query_not_allowed\"}");
  if(req->method==HTTP_GET) {
    const auto guard=fullTunnelPolicyStatus();const auto packets=fullPolicyFixtureStatus();
    const auto probe=service.driver.policyFixtureProbe();bool cold=false;
    { Lock lock(service.mutex);cold=service.wgSnapshot.fixtureClockReset; }
    char body[850],exit[16],dns[3][16];strlcpy(exit,IPAddress(probe.address).toString().c_str(),sizeof(exit));
    for(size_t i=0;i<3;++i)strlcpy(dns[i],IPAddress(guard.dnsServers[i]).toString().c_str(),sizeof(dns[i]));
    snprintf(body,sizeof(body),"{\"hardwareFixture\":true,\"coldClockReset\":%s,\"requested\":%s,\"ready\":%s,\"tunnelPresent\":%s,\"guardedInterfaces\":%lu,\"ipv4Blocked\":%lu,\"ipv6Blocked\":%lu,\"endpointPackets\":%lu,\"routedPackets\":%lu,\"bootstrapDNSPackets\":%lu,\"bootstrapTimePackets\":%lu,\"endpointDNSLease\":%s,\"timeLease\":%s,\"packetChecks\":%lu,\"packetFailures\":%lu,\"outboundDone\":%s,\"outboundHTTP\":%d,\"outboundAddress\":\"%s\",\"dnsServers\":[\"%s\",\"%s\",\"%s\"]}",
      cold?"true":"false",guard.requested?"true":"false",guard.ready?"true":"false",guard.tunnelPresent?"true":"false",
      (unsigned long)guard.guardedInterfaces,(unsigned long)guard.ipv4Blocked,(unsigned long)guard.ipv6Blocked,
      (unsigned long)guard.endpointPackets,(unsigned long)guard.routedPackets,(unsigned long)guard.bootstrapDNSPackets,
      (unsigned long)guard.bootstrapTimePackets,guard.endpointDNSLease?"true":"false",guard.timeLease?"true":"false",
      (unsigned long)packets.checks,(unsigned long)packets.failures,probe.done?"true":"false",probe.http,exit,dns[0],dns[1],dns[2]);
    return reply(req,"200 OK",body);
  }
  auto* root=readJson(req);char action[16]={},hostname[254]={};
  bool valid=cJSON_IsObject(root) && field(root,"action",action);
  const bool bootstrap=!strcmp(action,"bootstrap");
  valid=valid && cJSON_GetArraySize(root)==(bootstrap?2:1)
    && (bootstrap ? field(root,"hostname",hostname) && hostname[0]
      : !strcmp(action,"packets") || !strcmp(action,"hold") || !strcmp(action,"wifi") || !strcmp(action,"bad_endpoint") || !strcmp(action,"restore"));
  if(bootstrap)for(const char* at=hostname;*at;++at)if(!isalnum(uint8_t(*at)) && *at!='-' && *at!='.')valid=false;
  destroy(root);if(!valid)return reply(req,"400 Bad Request","{\"error\":\"invalid_fixture_command\"}");
  Lock lock(service.mutex);
  if(socketAddress(req)!=uint32_t(WiFi.localIP()))return reply(req,"409 Conflict","{\"error\":\"local_fixture_access_required\"}");
  if(service.processing || service.accountBusy || service.command!=Command::None || service.snapshot.trialRevision || !service.wgSnapshot.fullMode)
    return reply(req,"409 Conflict","{\"error\":\"fixture_unavailable\"}");
  memcpy(service.fixtureAction,action,sizeof(action));memcpy(service.fixtureHostname,hostname,sizeof(hostname));
  service.command=Command::WGPolicyFixture;service.applyNotBefore=millis()+100;xTaskNotifyGive(service.task);
  return reply(req,"202 Accepted","{\"fixtureQueued\":true}");
}
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
esp_err_t NetworkService::fullTest(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  if (!service.auth.checkRequest(req, req->method == HTTP_POST)) return ESP_OK;
  if (strchr(req->uri,'?')) return reply(req,"400 Bad Request","{\"error\":\"query_not_allowed\"}");
  if (req->method == HTTP_GET) {
    const auto state = fullTunnelTestStatus();
    char body[700], exit[16], resolvers[3][16]; IPAddress ip(state.outboundAddress); strlcpy(exit,ip.toString().c_str(),sizeof(exit));
    for(size_t i=0;i<3;++i)strlcpy(resolvers[i],IPAddress(state.dnsServers[i]).toString().c_str(),sizeof(resolvers[i]));
    snprintf(body,sizeof(body),
      "{\"hardwareFixture\":true,\"active\":%s,\"tunnelPresent\":%s,\"guardedInterfaces\":%lu,"
      "\"ipv4Blocked\":%lu,\"ipv6Blocked\":%lu,\"endpointPackets\":%lu,\"localPackets\":%lu,"
      "\"routedTunnel\":%lu,\"packetChecks\":%lu,\"packetFailures\":%lu,\"outboundDone\":%s,"
      "\"outboundHTTP\":%d,\"outboundAddress\":\"%s\",\"dnsServers\":[\"%s\",\"%s\",\"%s\"],\"productFullTunnelVerified\":false}",
      state.active?"true":"false",state.tunnelPresent?"true":"false",(unsigned long)state.guardedInterfaces,
      (unsigned long)state.ipv4Blocked,(unsigned long)state.ipv6Blocked,(unsigned long)state.forwardedEndpoint,
      (unsigned long)state.forwardedLocal,(unsigned long)state.routedTunnel,(unsigned long)state.packetChecks,
      (unsigned long)state.packetFailures,state.outboundDone?"true":"false",state.outboundHTTP,exit,resolvers[0],resolvers[1],resolvers[2]);
    return reply(req,"200 OK",body);
  }
  auto* root=readJson(req); char action[16]={}, resolver[16]={}; IPAddress address;
  const bool start = field(root,"action",action) && !strcmp(action,"start");
  const bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root)==(start?2:1)
    && (start ? field(root,"resolver",resolver) && address.fromString(resolver) && address.type()==IPType::IPv4
      : !strcmp(action,"stop") || !strcmp(action,"hold"));
  destroy(root);
  if(!valid) return reply(req,"400 Bad Request","{\"error\":\"invalid_fixture_command\"}");
  Lock lock(service.mutex);
  if(service.processing || service.accountBusy || service.command!=Command::None || service.snapshot.trialRevision)
    return reply(req,"409 Conflict","{\"error\":\"network_busy\"}");
  if(start && (service.fullTestActive || !service.wgSnapshot.peerSessionUp))
    return reply(req,"409 Conflict","{\"error\":\"fixture_unavailable\"}");
  if(start && socketAddress(req)!=uint32_t(WiFi.localIP()))
    return reply(req,"409 Conflict","{\"error\":\"local_fixture_access_required\"}");
  service.command=start?Command::WGFullStart:!strcmp(action,"stop")?Command::WGFullStop:Command::WGFullHold;
  service.localAddress=uint32_t(address);service.applyNotBefore=millis();xTaskNotifyGive(service.task);
  return reply(req,"202 Accepted","{\"fixtureQueued\":true}");
}
#endif
esp_err_t NetworkService::wireguard(httpd_req_t* req) {
  auto& service = *static_cast<NetworkService*>(req->user_ctx);
  const bool mutation = req->method == HTTP_POST;
  if (!service.auth.checkRequest(req, mutation)) return ESP_OK;
  if (strchr(req->uri, '?')) return reply(req, "400 Bad Request", "{\"error\":\"query_not_allowed\"}");
  if (!mutation) {
    char body[1200];
    {
      Lock lock(service.mutex);
      const auto& wg = service.wgSnapshot;
      snprintf(body, sizeof(body),
        "{\"enabled\":%s,\"running\":%s,\"phase\":\"%s\",\"error\":\"%s\",\"peerSessionUp\":%s,"
        "\"address\":\"%s\",\"sentBytes\":%lu,\"receivedBytes\":%lu,\"mode\":\"%s\","
        "\"fullTunnelVerified\":%s,\"guardReady\":%s,\"routeProbeBusy\":%s,\"publicExit\":\"%s\","
        "\"verificationAgeSeconds\":%lu,\"routeProbeHTTP\":%d,\"ipv4Blocked\":%lu,\"ipv6Blocked\":%lu,"
        "\"bootstrapDNSPackets\":%lu,\"bootstrapTimePackets\":%lu,\"privateKeySet\":%s,\"presharedKeySet\":%s,"
        "\"importError\":\"%s\",\"excludedIPv6Entries\":%u,"
        "\"importedDefaultRouteExcluded\":%s,\"trialRevision\":%lu,"
        "\"probeBusy\":%s,\"probeAddress\":\"%s\",\"probeSent\":%lu,\"probeReceived\":%lu}",
        wg.enabled ? "true" : "false", wg.running ? "true" : "false", wg.phase, wg.error,
        wg.peerSessionUp ? "true" : "false", wg.address, (unsigned long)wg.sentBytes, (unsigned long)wg.receivedBytes,
        wg.fullMode?"full_tunnel":"remote_access",wg.fullTunnelVerified?"true":"false",wg.guardReady?"true":"false",
        wg.routeProbeBusy?"true":"false",wg.publicExit,(unsigned long)wg.verificationAgeSeconds,wg.routeProbeHTTP,
        (unsigned long)wg.ipv4Blocked,(unsigned long)wg.ipv6Blocked,(unsigned long)wg.bootstrapDNSPackets,(unsigned long)wg.bootstrapTimePackets,
        service.snapshot.config.wgPrivateKeySet ? "true" : "false",
        service.snapshot.config.wgPresharedKeySet ? "true" : "false",
        service.importError, service.importedIPv6, service.importedDefault && !wg.fullMode ? "true" : "false",
        (unsigned long)service.snapshot.trialRevision, wg.probeBusy ? "true" : "false", wg.probeAddress,
        (unsigned long)wg.probeSent, (unsigned long)wg.probeReceived);
    }
    return reply(req, "200 OK", body);
  }
  if(!strcmp(req->uri,"/api/wireguard/mode")) {
    auto* root=readJson(req);char mode[16]={};
    const bool valid=cJSON_IsObject(root) && cJSON_GetArraySize(root)==1 && field(root,"mode",mode)
      && (!strcmp(mode,"full_tunnel") || !strcmp(mode,"remote_access"));
    destroy(root);
    if(!valid)return reply(req,"400 Bad Request","{\"error\":\"invalid_wireguard_mode\"}");
    const uint32_t address=socketAddress(req);
    if(!address || address!=uint32_t(WiFi.localIP()))
      return reply(req,"409 Conflict","{\"error\":\"local_mode_trial_required\"}");
    Lock lock(service.mutex);
    if(service.processing || service.accountBusy || service.testBusy() || service.command!=Command::None || service.snapshot.trialRevision)
      return reply(req,"409 Conflict","{\"error\":\"network_busy\"}");
    if(!service.wgSnapshot.enabled)return reply(req,"409 Conflict","{\"error\":\"wireguard_disabled\"}");
    const bool full=!strcmp(mode,"full_tunnel");
    if(full==service.wgSnapshot.fullMode)return reply(req,"409 Conflict","{\"error\":\"mode_already_selected\"}");
    service.fullModeCandidate=full;service.command=Command::WGMode;service.applyNotBefore=millis()+750;
    service.setupJob=false;service.jobError=NetworkError::None;service.jobState=JobState::Applying;service.submitted=millis();
    xTaskNotifyGive(service.task);return reply(req,"202 Accepted","{\"networkTrialQueued\":true}");
  }
  if (!strcmp(req->uri, "/api/wireguard/probe")) {
    auto* root = readJson(req); char text[16] = {}; IPAddress address;
    bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && field(root, "address", text)
      && address.fromString(text) && address.type() == IPType::IPv4;
    destroy(root);
    if (!valid || !wireguardDestination(uint32_t(address)))
      return reply(req, "400 Bad Request", "{\"error\":\"target_not_on_wireguard_routes\"}");
    Lock lock(service.mutex);
    if (service.processing || service.command != Command::None || service.wgSnapshot.probeBusy || !service.wgSnapshot.peerSessionUp)
      return reply(req, "409 Conflict", "{\"error\":\"wireguard_probe_busy_or_unavailable\"}");
    service.command = Command::WGProbe; service.localAddress = uint32_t(address); service.applyNotBefore = millis();
    xTaskNotifyGive(service.task);
    return reply(req, "202 Accepted", "{\"probeQueued\":true}");
  }
  if (!strcmp(req->uri, "/api/wireguard/restart")) {
    if (req->content_len) return reply(req, "400 Bad Request", "{\"error\":\"empty_body_required\"}");
    Lock lock(service.mutex);
    if (service.processing || service.accountBusy || service.command != Command::None || service.snapshot.trialRevision)
      return reply(req, "409 Conflict", "{\"error\":\"network_busy\"}");
    if (!service.wgSnapshot.enabled) return reply(req, "409 Conflict", "{\"error\":\"wireguard_disabled\"}");
    service.command = Command::WGRestart; service.applyNotBefore = millis()+750;
    xTaskNotifyGive(service.task);
    return reply(req, "202 Accepted", "{\"restartingWireGuard\":true}");
  }
  const bool importing = !strcmp(req->uri, "/api/wireguard/import");
  char type[64] = {};
  if (importing && (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK
    || (strcmp(type, "text/plain") && strcmp(type, "text/plain; charset=utf-8"))
    || !req->content_len || req->content_len > 4096))
    return reply(req, "400 Bad Request", "{\"error\":\"bounded_text_config_required\"}");
  if (!importing && req->content_len) return reply(req, "400 Bad Request", "{\"error\":\"empty_body_required\"}");
  // Receive into bounded temporary RAM without holding the network owner's
  // mutex across socket waits. Parse/persist only on that owner after queuing.
  PrivateInput input(importing ? 4097 : 0);
  if (importing && !input.text) return reply(req, "503 Service Unavailable", "{\"error\":\"memory_unavailable\"}");
  if (importing) {
    size_t received = 0; const uint32_t started = millis();
    while (received < req->content_len) {
      const int count = httpd_req_recv(req, input.text + received, req->content_len - received);
      if (count <= 0 || uint32_t(millis() - started) >= 10000) {
        return reply(req, "400 Bad Request", "{\"error\":\"incomplete_config\"}");
      }
      received += count;
    }
    if (memchr(input.text, 0, received)) {
      return reply(req, "400 Bad Request", "{\"error\":\"invalid_text\"}");
    }
  }
  Lock lock(service.mutex);
  if (service.processing || service.accountBusy || service.testBusy() || service.command != Command::None || service.snapshot.trialRevision)
    return reply(req, "409 Conflict", "{\"error\":\"network_busy\"}");
  if (!service.snapshot.config.hasActive) return reply(req, "409 Conflict", "{\"error\":\"setup_required\"}");
  if (importing) {
    clearSecret(service.importText, sizeof(service.importText));
    memcpy(service.importText, input.text, req->content_len); service.importBytes = req->content_len;
  }
  service.setupJob = false; service.jobError = NetworkError::None;
  service.jobState = JobState::Applying; service.submitted = millis();
  service.command = importing ? Command::WGImport
    : !strcmp(req->uri, "/api/wireguard/clear") ? Command::WGClear : Command::WGDisable;
  service.applyNotBefore = millis() + 750; xTaskNotifyGive(service.task);
  return reply(req, "202 Accepted", "{\"networkTrialQueued\":true}");
}
#endif
} // namespace appliance
