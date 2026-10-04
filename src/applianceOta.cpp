#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceWeb.h"
#include "applianceJobs.h"
#include "applianceTimelapse.h"
#include "applianceMedia.h"
#include "applianceSettings.h"
#include "applianceEvents.h"
#include "applianceOta.h"
#include <Update.h>
#include <HttpAuth.h>
#include <atomic>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"

#if !CONFIG_APP_ROLLBACK_ENABLE
#error Appliance OTA requires the pinned SDK bootloader rollback support
#endif
// The Arduino default accepts a pending image before setup. Defer that decision
// until the appliance's recovery HTTP service has actually answered a request.
extern "C" bool verifyRollbackLater() { return true; }

namespace {
constexpr size_t PREFIX_BYTES = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
constexpr uint32_t UPLOAD_MS = 300000;
enum class State { Idle, Checking, Preparing, Uploading, Verifying, Rebooting, Failed };
std::atomic<bool> maintenance {false};
std::atomic<State> state {State::Idle};
std::atomic<size_t> received {0}, total {0};
std::atomic<const char*> lastError {"none"};
std::atomic<bool> httpReady {false};
struct Job {
  httpd_req_t* request = nullptr;
  char token[65] = {}, hash[65] = {};
};
Job job;

esp_err_t reply(httpd_req_t* req, const char* status, const char* body) {
  httpd_resp_set_status(req, status); httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, body);
}
esp_err_t reject(httpd_req_t* req, const char* status, const char* body) {
  const esp_err_t sent = reply(req, status, body);
  httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
  return sent;
}
const char* stateName(State value) {
  switch (value) {
    case State::Checking: return "checking";
    case State::Preparing: return "preparing";
    case State::Uploading: return "uploading";
    case State::Verifying: return "verifying";
    case State::Rebooting: return "rebooting";
    case State::Failed: return "failed";
    default: return "idle";
  }
}
size_t imageLimit() {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  return target ? target->size * 80ULL / 100 : 0;
}
esp_err_t status(httpd_req_t* req) {
  if (!applianceCheckAuth(req)) return ESP_OK;
  const esp_partition_t* current = esp_ota_get_running_partition();
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  char body[384];
  snprintf(body, sizeof(body),
    "{\"state\":\"%s\",\"maintenance\":%s,\"receivedBytes\":%u,\"totalBytes\":%u,"
    "\"error\":\"%s\",\"runningSlot\":\"%s\",\"targetSlot\":\"%s\",\"maximumBytes\":%u,"
    "\"automaticBootRollback\":true}", stateName(state.load()), maintenance.load() ? "true" : "false",
    (unsigned)received.load(), (unsigned)total.load(), lastError.load(),
    current ? current->label : "unknown", next ? next->label : "unavailable", (unsigned)imageLimit());
  return reply(req, "200 OK", body);
}
bool prefixValid(const uint8_t* buffer) {
  esp_image_header_t image; esp_app_desc_t app;
  memcpy(&image, buffer, sizeof(image));
  memcpy(&app, buffer + sizeof(image) + sizeof(esp_image_segment_header_t), sizeof(app));
  return image.magic == ESP_IMAGE_HEADER_MAGIC && image.chip_id == ESP_CHIP_ID_ESP32S3
    && image.segment_count && image.segment_count <= ESP_IMAGE_MAX_SEGMENTS
    && app.magic_word == ESP_APP_DESC_MAGIC_WORD;
}
void worker(void*) {
  applianceTimelapseStop();
  ApplianceForeground foreground;
  const uint32_t stoppedAt=millis();while(applianceTimelapseActive()&&uint32_t(millis()-stoppedAt)<3000)delay(5);
  auto* req = job.request;
  auto* buffer = static_cast<uint8_t*>(heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  const uint32_t started = millis();
  const char* error = !foreground.ready()?"converter_not_stopped":applianceTimelapseActive()?"timelapse_not_stopped":buffer ? nullptr : "memory_unavailable";
  bool quiesced = false, updating = false, success = false;
  size_t prefix = 0;
  // Read/validate the image metadata before stopping the camera or touching flash.
  while (!error && prefix < PREFIX_BYTES) {
    if (!applianceSessionValid(job.token)) { error = "session_revoked"; break; }
    if (uint32_t(millis() - started) >= UPLOAD_MS) { error = "upload_timeout"; break; }
    const int count = httpd_req_recv(req, reinterpret_cast<char*>(buffer) + prefix, PREFIX_BYTES - prefix);
    if (count <= 0) { error = "incomplete_upload"; break; }
    prefix += count;
  }
  if (!error && !prefixValid(buffer)) error = "invalid_esp32s3_image";
  if (!error) {
    state.store(State::Preparing);
    // Maintenance authorization closes existing preview loops. Wait for their
    // async request cleanup before the upstream prerequisite deletes the tasks.
    for (unsigned i = 0; i < 150 && (applianceStreamsBusy() || applianceCaptureBusy()); ++i) delay(100);
    if (applianceStreamsBusy()) error = "preview_not_stopped";
    else if (applianceCaptureBusy()) error = "capture_not_stopped";
    else if (!applianceSessionValid(job.token)) error = "session_revoked";
    else {
      appSetupDone = false;
      OTAprereq(); quiesced = true;
      LOG_INF("Authenticated OTA started");
      if (!Update.begin(total.load(), U_FLASH)) error = "update_begin_failed";
      else {
        updating = true;
        if (!Update.setSHA256(job.hash)) error = "hash_setup_failed";
        else if (Update.write(buffer, prefix) != prefix) error = "flash_write_failed";
        else received.store(prefix);
      }
    }
  }
  state.store(error ? State::Failed : State::Uploading);
  while (!error && received.load() < total.load()) {
    if (!applianceSessionValid(job.token)) { error = "session_revoked"; break; }
    if (uint32_t(millis() - started) >= UPLOAD_MS) { error = "upload_timeout"; break; }
    const size_t remaining = total.load() - received.load();
    const int count = httpd_req_recv(req, reinterpret_cast<char*>(buffer), min(remaining, size_t(4096)));
    if (count <= 0) { error = "incomplete_upload"; break; }
    if (Update.write(buffer, count) != size_t(count)) { error = "flash_write_failed"; break; }
    received.fetch_add(count);
    delay(1); // Network/HTTP tasks and idle watchdog retain their scheduling.
  }
  if (!error) {
    state.store(State::Verifying);
    if (!applianceSessionValid(job.token)) error = "session_revoked";
    // The pinned updater checks the complete SHA-256 and ESP image before
    // esp_ota_set_boot_partition. Never accept a partial payload via end(true).
    else if (!Update.end(false)) error = Update.getError() == UPDATE_ERROR_SHA256
      ? "sha256_mismatch" : "image_verification_failed";
    else { success = true; updating = false; }
  }
  if (updating) Update.abort();
  if (buffer) { memset(buffer, 0, 4096); free(buffer); }
  lastError.store(error ? error : "none");
  state.store(success || quiesced ? State::Rebooting : State::Failed);
  char body[160];
  if (success) reply(req, "200 OK", "{\"updated\":true,\"restarting\":true}");
  else {
    snprintf(body, sizeof(body), "{\"error\":\"%s\",\"restarting\":%s}", error, quiesced ? "true" : "false");
    reply(req, "400 Bad Request", body);
  }
  // Rejected requests can still have unread body bytes; close the socket so
  // those bytes cannot be mistaken for another request on this connection.
  httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
  httpd_req_async_handler_complete(req);
  applianceClearToken(job.token); memset(job.hash, 0, sizeof(job.hash)); job.request = nullptr;
  if (quiesced) doRestart(success ? "authenticated OTA complete" : "OTA aborted; retain current firmware");
  maintenance.store(false);
  vTaskDelete(nullptr);
}
esp_err_t upload(httpd_req_t* req) {
  if (!applianceCheckAuth(req, true)) {
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req)); return ESP_OK;
  }
  char type[64] = {}, digest[65] = {};
  if (strchr(req->uri, '?') || httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK
      || strcmp(type, "application/octet-stream") || req->content_len < PREFIX_BYTES
      || req->content_len > imageLimit())
    return reject(req, "400 Bad Request", "{\"error\":\"invalid_ota_request\"}");
  if (httpd_req_get_hdr_value_len(req, "X-Firmware-SHA256") != 64
      || httpd_req_get_hdr_value_str(req, "X-Firmware-SHA256", digest, sizeof(digest)) != ESP_OK)
    return reject(req, "400 Bad Request", "{\"error\":\"sha256_required\"}");
  for (unsigned i = 0; i < 64; ++i)
    if (!(digest[i] >= '0' && digest[i] <= '9') && !(digest[i] >= 'a' && digest[i] <= 'f'))
      return reject(req, "400 Bad Request", "{\"error\":\"invalid_sha256\"}");
  if (forceRecord || isCapturing || stopPlayback || applianceMediaBusy() || applianceNetworkBusy() || applianceCaptureBusy() || applianceEventsArmed())
    return reject(req, "409 Conflict", "{\"error\":\"device_busy\"}");
  bool expected = false;
  if (!maintenance.compare_exchange_strong(expected, true))
    return reject(req, "409 Conflict", "{\"error\":\"maintenance_in_progress\"}");
  if (!applianceStreamToken(req, job.token) || httpd_req_async_handler_begin(req, &job.request) != ESP_OK) {
    applianceClearToken(job.token); maintenance.store(false);
    return reject(req, "503 Service Unavailable", "{\"error\":\"ota_unavailable\"}");
  }
  memcpy(job.hash, digest, sizeof(digest));
  received.store(0); total.store(req->content_len); lastError.store("none"); state.store(State::Checking);
  if (xTaskCreatePinnedToCore(worker, "otaUpload", 8192, nullptr, 2, nullptr, 0) != pdPASS) {
    reply(job.request, "503 Service Unavailable", "{\"error\":\"ota_unavailable\"}");
    httpd_sess_trigger_close(job.request->handle, httpd_req_to_sockfd(job.request));
    httpd_req_async_handler_complete(job.request); job.request = nullptr;
    applianceClearToken(job.token); memset(job.hash, 0, sizeof(job.hash)); maintenance.store(false);
  }
  return ESP_OK;
}
void bootHealth(void*) {
  const uint32_t started = millis();
  bool healthy = false;
  while (uint32_t(millis() - started) < 20000) {
    if (httpReady.load() && ESP.getFreeHeap() >= 32768 && psramFound()) {
      WiFiClient client; client.setTimeout(1500);
      if (client.connect(IPAddress(127,0,0,1), 80, 1500)) {
        client.print("GET /api/bootstrap HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
        healthy = client.readStringUntil('\n').startsWith("HTTP/1.1 200");
      }
      client.stop();
      // Confirm the recovery plane, including with a missing SD card. A
      // removable-card/sensor fault is not evidence that this image cannot boot.
      if (healthy && uint32_t(millis() - started) >= 10000) break;
    }
    delay(250);
  }
#if defined(XIAO_APPLIANCE_OTA_ROLLBACK_TEST)
  // Explicit known-board fixture only. Exercises the SDK rollback path; it
  // does not erase credentials or alter configuration/storage/partition layout.
  const uint8_t expected[] = {0xe0,0x72,0xa1,0xfb,0x4c,0x08}; uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK || memcmp(mac, expected, sizeof(mac)))
    LOG_ERR("Rollback fixture refuses a different board");
  healthy = false;
#endif
  if (healthy) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) LOG_INF("OTA boot health accepted: recovery HTTP responds");
    else LOG_ERR("OTA boot health confirmation failed");
  } else {
    LOG_WRN("OTA boot health failed; restore previous valid image");
    resetCrashLoop(); flush_log(true);
    if (esp_ota_mark_app_invalid_rollback_and_reboot() != ESP_OK) {
      // Leaving the pending image unconfirmed lets the bootloader fall back on
      // the next reset. Avoid an unbounded handler/task wait.
      delay(500); ESP.restart();
    }
  }
  vTaskDelete(nullptr);
}
} // namespace
bool applianceMaintenance() { return maintenance.load(); }
void applianceOtaHttpReady() { httpReady.store(true); }
bool applianceBeginOtaHealth() {
  esp_ota_img_states_t current;
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running || esp_ota_get_state_partition(running, &current) != ESP_OK
      || current != ESP_OTA_IMG_PENDING_VERIFY) return true;
  return xTaskCreatePinnedToCore(bootHealth, "otaBootHealth", 4096, nullptr, 1, nullptr, 0) == pdPASS;
}
bool applianceRegisterOtaRoutes(httpd_handle_t server) {
  const httpd_uri_t routes[] = {
    {.uri="/api/ota", .method=HTTP_GET, .handler=status, .user_ctx=nullptr},
    {.uri="/api/ota", .method=HTTP_POST, .handler=upload, .user_ctx=nullptr},
  };
  for (const auto& route : routes) if (httpd_register_uri_handler(server, &route) != ESP_OK) return false;
  return true;
}
#endif
