// Bounded HTTP auth experiment. No camera/media, product NVS or SD writes.
// The visible known-answer password is synthetic; never use it for enrollment.
#include <Arduino.h>
#include <WiFi.h>
#include <HttpAuth.h>
#include <WireGuardConfig.h>
using namespace appliance;
static HttpAuth auth;
static httpd_handle_t server = nullptr;
static uint32_t mutations = 0;
static Config verifier;
static void fixtureVerifier() {
  verifier = defaults();
  verifier.admin.configured = 1;
  verifier.admin.iterations = PASSWORD_ITERATIONS;
  for (unsigned i = 0; i < 16; ++i) verifier.admin.salt[i] = i + 1;
  const uint8_t expected[32] = {0xbe,0xb2,0xc2,0xc0,0xfd,0x57,0x04,0xb8,0xdc,0x6a,0xbd,0xd6,0x91,0xf5,0xfb,0x06,
    0x76,0x0d,0xbb,0x3b,0x8e,0xf1,0x12,0x1c,0x08,0x21,0x45,0x95,0xd7,0xe9,0x25,0xe7};
  memcpy(verifier.admin.verifier, expected, sizeof(expected));
}
static esp_err_t page(httpd_req_t* req) {
  HttpAuth::responseHeaders(req);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_sendstr(req, "<!doctype html><title>XIAO auth fixture</title><p>Isolated authentication test. No camera or media.</p>");
}
static esp_err_t protectedStatus(httpd_req_t* req) {
  if (!auth.checkRequest(req)) return ESP_OK;
  char json[96];
  snprintf(json, sizeof(json), "{\"fixture\":true,\"mutations\":%lu}", (unsigned long)mutations);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, json);
}
static esp_err_t mutation(httpd_req_t* req) {
  if (!auth.checkRequest(req, true)) return ESP_OK;
  ++mutations;
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"changed\":true}");
}
static esp_err_t rotateFixture(httpd_req_t* req) {
  if (!auth.checkRequest(req, true)) return ESP_OK;
  // Repeat same verifier to test revocation even when password is unchanged.
  auth.setAdmin(verifier);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, "{\"sessions_revoked\":true}");
}
void setup() {
  Serial.begin(115200); delay(1000);
  if (ESP.getEfuseMac() != 0x084cfba172e0ULL) { Serial.println("AUTH HTTP FAIL unexpected_target"); return; }
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  if (!WiFi.softAP("XIAO-Auth-Test-084C")) { Serial.println("AUTH HTTP FAIL ap_start"); return; }
  fixtureVerifier();
  if (!auth.begin() || !auth.setAdmin(verifier)) { Serial.println("AUTH HTTP FAIL auth_start"); return; }
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 10;
  config.stack_size = 6144;
  config.task_priority = 5;
  config.max_open_sockets = 6;
  config.lru_purge_enable = true;
  config.recv_wait_timeout = 3; config.send_wait_timeout = 3;
  if (httpd_start(&server, &config) != ESP_OK || !auth.registerEndpoints(server)) {
    Serial.println("AUTH HTTP FAIL server_start"); return;
  }
  const httpd_uri_t handlers[] = {
    {.uri="/", .method=HTTP_GET, .handler=page},
    {.uri="/api/test/status", .method=HTTP_GET, .handler=protectedStatus},
    {.uri="/api/test/mutate", .method=HTTP_POST, .handler=mutation},
    {.uri="/api/test/rotate", .method=HTTP_POST, .handler=rotateFixture},
  };
  for (const auto& handler : handlers) if (httpd_register_uri_handler(server, &handler) != ESP_OK) {
    Serial.println("AUTH HTTP FAIL handler_start"); return;
  }
  Serial.println("AUTH HTTP READY; synthetic verifier, no NVS or SD writes");
}
void loop() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command.startsWith("IMPORT_WG ")) {
      unsigned bytes = command.substring(10).toInt();
      if (!bytes || bytes > 4096) { Serial.println("PRIVATE IMPORT FAIL invalid_length"); }
      else {
        char* text = static_cast<char*>(malloc(bytes));
        if (!text) Serial.println("PRIVATE IMPORT FAIL allocation");
        else {
          Serial.println("PRIVATE IMPORT SEND");
          Serial.setTimeout(5000);
          size_t received = Serial.readBytes(text, bytes);
          Config candidate = defaults();
          ImportSummary summary;
          ImportError error = received == bytes ? importWireGuard(text, bytes, candidate, summary) : ImportError::InvalidText;
          Serial.printf("PRIVATE IMPORT %s error=%s line=%u ipv4=%u excluded_ipv6=%u excluded_addresses=%u full_requested=%u full_enabled=%u\n",
             error == ImportError::Ok ? "PASS" : "FAIL", importErrorName(error), summary.errorLine,
             summary.ipv4Networks, summary.excludedIPv6Networks, summary.excludedIPv6Addresses,
             summary.fullTunnelRequested, candidate.wg.fullTunnel);
          clearSecret(text, bytes); free(text); clearSecret(&candidate, sizeof(candidate));
        }
      }
    }
  }
  delay(50);
}
