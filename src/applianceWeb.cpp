#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceWeb.h"
#include "applianceMedia.h"
#include "applianceOta.h"
#include "applianceSettings.h"
#include "applianceEvents.h"
#include "applianceTimelapse.h"
#include "applianceJobs.h"
#include "applianceBle.h"
#include "applianceAssets.h"
#include <HttpAuth.h>
#include <NetworkService.h>
#include "esp_mac.h"
#include <math.h>
#include <atomic>
#include <memory>
#include "esp_heap_caps.h"
#include <ESPmDNS.h>

namespace {
constexpr char deviceTimezone[] = "CET-1CEST,M3.5.0/2,M10.5.0/3";
appliance::HttpAuth auth;
std::atomic<bool> resetPending{false};
bool mutationBusy(){return applianceMaintenance()||resetPending.load();}
#if !defined(XIAO_APPLIANCE_AUTH_TEST)
appliance::NetworkService network(auth, "XIAO-Camera-084C");
void connected() {
  MDNS.end();MDNS.begin("xiao-camera");MDNS.addService("http","tcp",80);
  LOG_INF("Appliance LAN ready at %s", WiFi.localIP().toString().c_str());
  if (statusCheckHandle) xTaskNotifyGive(statusCheckHandle);
}
#endif
appliance::MaskedStatus configStatus;
bool configReady = false;

esp_err_t json(httpd_req_t* req, const char* status, const char* body) {
  appliance::HttpAuth::responseHeaders(req);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, body);
}
bool cameraReady() { return appSetupDone && !startupFailure[0] && !applianceMaintenance(); }
esp_err_t asset(httpd_req_t* req) {
  appliance::HttpAuth::responseHeaders(req);
  httpd_resp_set_hdr(req, "Content-Security-Policy",
    "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' blob:; "
    "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
  if (!strcmp(req->uri, "/app.js")) {
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    return httpd_resp_send(req, applianceJs, sizeof(applianceJs) - 1);
  }
  if (!strcmp(req->uri, "/i18n.js")) {
    httpd_resp_set_type(req, "text/javascript; charset=utf-8");
    return httpd_resp_send(req, applianceI18n, sizeof(applianceI18n)-1);
  }
  if (!strcmp(req->uri, "/style.css")) {
    httpd_resp_set_type(req, "text/css; charset=utf-8");
    return httpd_resp_send(req, applianceCss, sizeof(applianceCss) - 1);
  }
  // A healthy recorder and an actually reachable embedded HTTP page are the
  // AP-only equivalent of the upstream gateway+assets boot-health check.
  if (cameraReady()) resetCrashLoop();
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, applianceHtml, sizeof(applianceHtml) - 1);
}
esp_err_t bootstrap(httpd_req_t* req) {
#if !defined(XIAO_APPLIANCE_AUTH_TEST)
  return network.bootstrap(req);
#else
  // Only account readiness is public. Never expose device metrics/config/logs.
  return json(req, "200 OK", auth.configured()
    ? "{\"adminConfigured\":true}" : "{\"adminConfigured\":false}");
#endif
}
esp_err_t status(httpd_req_t* req) {
  if (!auth.checkRequest(req)) return ESP_OK;
  // The response outlives the nested Wi-Fi send call. Keeping 4.8 KB of
  // formatting buffers on HTTP's 8 KB stack exhausted it in the real route.
  struct Payload{char response[3072],lan[16],events[320],timelapse[640],conversion[640],ble[128];};
  std::unique_ptr<Payload,decltype(&free)> payload(static_cast<Payload*>(heap_caps_calloc(1,sizeof(Payload),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)),&free);
  if(!payload)return json(req,"503 Service Unavailable","{\"error\":\"status_memory_unavailable\"}");
#if !defined(XIAO_APPLIANCE_AUTH_TEST)
  const auto net = network.status(); configStatus = net.config; configReady = net.config.hasActive;
  const auto wg = network.wireguardStatus();
#else
  const appliance::WireGuardStatus wg;
#endif
  // Explicit primitives only: do not call the upstream config serializer.
  const float measured = readInternalTemp();
  const float temperature = isfinite(measured) ? measured : 0;
  const bool sdReady = SD.cardType() != CARD_NONE;
  const uint64_t total = sdReady ? SD.totalBytes() : 0;
  const uint64_t used = sdReady ? SD.usedBytes() : 0;
  float cpu[2]; bool cpuAvailable; applianceCpuUsage(cpu, cpuAvailable);
  auto& response=payload->response;auto& lan=payload->lan;auto& events=payload->events;
  auto& timelapse=payload->timelapse;auto& conversion=payload->conversion;auto& ble=payload->ble;
  applianceEventsStatus(events,sizeof(events));
  applianceTimelapseStatus(timelapse,sizeof(timelapse));applianceConversionStatus(conversion,sizeof(conversion));applianceBleStatus(ble,sizeof(ble));
  IPAddress ip = WiFi.localIP();
  snprintf(lan, sizeof(lan), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  const time_t clockEpoch = getEpoch();
  struct tm localClock = {}; char clockText[20] = {};
  localtime_r(&clockEpoch, &localClock);
  strftime(clockText, sizeof(clockText), "%Y-%m-%d %H:%M:%S", &localClock);
  snprintf(response, sizeof(response),
    "{\"cameraReady\":%s,\"startupFailed\":%s,\"recording\":%s,\"recordRequested\":%s,"
    "\"fps\":%u,\"audioEnabled\":%s,\"audioInputRunning\":%s,\"temperatureC\":%.1f,\"uptimeSeconds\":%lu,"
    "\"freeHeap\":%lu,\"totalHeap\":%lu,\"minimumFreeHeap\":%lu,\"freePsram\":%lu,\"totalPsram\":%lu,"
    "\"cpuUsageAvailable\":%s,\"cpu0Percent\":%.1f,\"cpu1Percent\":%.1f,\"cpuMHz\":%u,\"captureIdle\":%s,\"captureBusy\":%s,"
    "\"cameraSensorSleeping\":%s,\"lastWakeFrameMs\":%lu,"
    "\"videoWidth\":%u,\"videoHeight\":%u,\"micGain\":%d,\"timeSynchronized\":%s,"
    "\"timezone\":\"%s\",\"localTime\":\"%s\",\"epochSeconds\":%lld,"
    "\"sdReady\":%s,\"sdTotalBytes\":%llu,\"sdFreeBytes\":%llu,"
    "\"wifiConnected\":%s,\"lanIP\":\"%s\",\"rssi\":%d,"
    "\"configReady\":%s,\"configRevision\":%lu,\"wireguardConfigured\":%s,\"wireguardRunning\":%s,\"maintenance\":%s,\"events\":%s,\"timelapse\":%s,\"conversion\":%s,\"ble\":%s}",
    cameraReady() ? "true" : "false", startupFailure[0] ? "true" : "false",
    (isCapturing || stopPlayback) ? "true" : "false", forceRecord ? "true" : "false", FPS,
    micGain ? "true" : "false", applianceAudioInputRunning() ? "true" : "false", temperature, (unsigned long)(millis() / 1000),
    (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getHeapSize(), (unsigned long)ESP.getMinFreeHeap(),
    (unsigned long)ESP.getFreePsram(), (unsigned long)ESP.getPsramSize(),
    cpuAvailable ? "true" : "false", cpu[0], cpu[1], getCpuFrequencyMhz(),
    applianceCaptureIdle() ? "true" : "false", applianceCaptureBusy() ? "true" : "false",
    applianceCameraSensorSleeping() ? "true" : "false", (unsigned long)applianceLastWakeFrameMs(),
    frameData[fsizePtr].frameWidth, frameData[fsizePtr].frameHeight, micGain, timeSynchronized ? "true" : "false",
    timezone, clockText, (long long)clockEpoch,
    sdReady ? "true" : "false", (unsigned long long)total, (unsigned long long)(total >= used ? total - used : 0),
    WiFi.status() == WL_CONNECTED ? "true" : "false", lan, WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0,
    configReady ? "true" : "false", (unsigned long)configStatus.activeRevision,
    configStatus.wgEnabled ? "true" : "false", wg.running ? "true" : "false", applianceMaintenance() ? "true" : "false",events,timelapse,conversion,ble);
  return json(req, "200 OK", response);
}
esp_err_t still(httpd_req_t* req) {
  if (!auth.checkRequest(req)) return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready()||applianceCaptureBusy())return json(req,"409 Conflict","{\"error\":\"capture_busy\"}");
  if (!cameraReady()) return json(req, "503 Service Unavailable", "{\"error\":\"camera_unavailable\"}");
  return appSpecificWebHandler(req, "still", "1");
}
bool emptyMutation(httpd_req_t* req) {
  if (!auth.checkRequest(req, true)) return false;
  if (strchr(req->uri, '?')) {
    json(req, "400 Bad Request", "{\"error\":\"query_not_allowed\"}"); return false;
  }
  if (req->content_len) {
    json(req, "400 Bad Request", "{\"error\":\"body_must_be_empty\"}"); return false;
  }
  return true;
}
esp_err_t record(httpd_req_t* req) {
  if (!emptyMutation(req)) return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  if (!cameraReady() || SD.cardType() == CARD_NONE)
    return json(req, "503 Service Unavailable", "{\"error\":\"recording_unavailable\"}");
  const bool start = !strcmp(req->uri, "/api/record/start");
  if(applianceEventsArmed())return json(req,"409 Conflict","{\"error\":\"event_mode_active\"}");
  if (start && (!doRecording || doPlayback || stopPlayback || !micGain))
    return json(req, "409 Conflict", "{\"error\":\"recording_not_ready\"}");
  if (start && applianceCaptureBusy()) return json(req, "409 Conflict", "{\"error\":\"capture_busy\"}");
  if (start && !applianceStorageSpaceAvailable()) return json(req, "507 Insufficient Storage", "{\"error\":\"minimum_free_space\"}");
  if (start) applianceCaptureWake();
  forceRecord = start;
  // Capture task owns file open/finalization; HTTP never touches its buffers.
  return json(req, "202 Accepted", start ? "{\"recordRequested\":true}" : "{\"recordRequested\":false}");
}
esp_err_t restart(httpd_req_t* req) {
  if (!emptyMutation(req)) return ESP_OK;
  if(!applianceTimelapseStopAndWait())return json(req,"409 Conflict","{\"error\":\"photo_still_saving\"}");
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  if (forceRecord || isCapturing || stopPlayback || applianceEventsArmed())
    return json(req, "409 Conflict", "{\"error\":\"recording_in_progress\"}");
  applianceTimelapseStop();
  esp_err_t sent = json(req, "202 Accepted", "{\"restarting\":true}");
  doRestart("authenticated user restart");
  return sent;
}
esp_err_t stream(httpd_req_t* req){if(!auth.checkRequest(req))return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready()||applianceCaptureBusy())return json(req,"409 Conflict","{}");
  return appSpecificSustainHandler(req);
}
} // namespace

bool appliancePrepare() {
  appliance::Config config = appliance::defaults();
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  uint8_t mac[6];
  const uint8_t expectedMac[] = {0xe0,0x72,0xa1,0xfb,0x4c,0x08};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK || memcmp(mac, expectedMac, sizeof(mac))) {
    LOG_ERR("Refusing authentication fixture on a different board"); return false;
  }
  // Known-answer verifier for synthetic-admin-password. Test build only,
  // RAM only: neither opens nor writes the product camcfg namespace.
  config.admin.configured = 1;
  config.admin.iterations = appliance::PASSWORD_ITERATIONS;
  for (unsigned i = 0; i < 16; ++i) config.admin.salt[i] = i + 1;
  const uint8_t expected[] = {0xbe,0xb2,0xc2,0xc0,0xfd,0x57,0x04,0xb8,0xdc,0x6a,0xbd,0xd6,0x91,0xf5,0xfb,0x06,
    0x76,0x0d,0xbb,0x3b,0x8e,0xf1,0x12,0x1c,0x08,0x21,0x45,0x95,0xd7,0xe9,0x25,0xe7};
  memcpy(config.admin.verifier, expected, sizeof(expected));
  snprintf(AP_SSID, MAX_HOST_LEN, "XIAO-Camera-Test-084C");
#endif
  // The production manager consumes full-size versioned credentials directly.
  // Legacy plaintext credentials never control this profile's network/auth.
  ST_SSID[0] = ST_Pass[0] = 0;
  ST_ip[0] = ST_sn[0] = ST_gw[0] = ST_ns1[0] = ST_ns2[0] = 0;
  AP_Pass[0] = AP_ip[0] = AP_sn[0] = AP_gw[0] = 0;
  Auth_Name[0] = Auth_Pass[0] = 0;
  netMode = 0;
  allowAP = true; // Fixture only; production AP is owned by NetworkService.
  doGetExtIP = false;
  dataFilesChecked = true; // Embedded UI has no SD asset/download dependency.
  // NTP sets UTC; filenames and visible event clocks use Berlin local time.
  // Replace the legacy SD value in RAM too: prepRecording reloads that vector.
  strlcpy(timezone, deviceTimezone, FILE_NAME_LEN);
  updateConfigVect("timezone", timezone);
  configTzTime(timezone, ntpServer);
  char frameSize[4];
  snprintf(frameSize, sizeof(frameSize), "%u", (unsigned)FRAMESIZE_VGA);
  updateConfigVect("framesize", frameSize);
  updateConfigVect("fps", "15");
  updateConfigVect("micGain", "3");
  fsizePtr = FRAMESIZE_VGA; FPS = 15; micGain = MIC_GAIN_CENTER;
  const bool ready = auth.begin() && auth.setAdmin(config) && applianceBeginOtaHealth() && applianceSettingsBegin();
  auth.setMutationGuard(mutationBusy);
  appliance::clearSecret(&config, sizeof(config));
  return ready;
}
bool applianceBeginNetwork() {
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  return false; // The explicit RAM fixture retains its isolated upstream AP.
#else
  return network.begin(connected);
#endif
}
bool applianceRegisterRoutes(httpd_handle_t server) {
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  extern bool applianceRegisterCodecTestRoutes(httpd_handle_t);
  if(!applianceRegisterCodecTestRoutes(server))return false;
#endif
  if (!auth.registerEndpoints(server)) return false;
  if (!applianceRegisterMediaRoutes(server)) return false;
  if (!applianceRegisterOtaRoutes(server)) return false;
  if (!applianceRegisterSettingsRoutes(server)) return false;
  if (!applianceRegisterEventRoutes(server)) return false;
  if(!applianceTimelapseBegin(server)){LOG_ERR("Interval photos initialization failed; heap %u",ESP.getFreeHeap());return false;}
  if(!applianceJobsBegin(server)){LOG_ERR("Idle converter initialization failed; heap %u",ESP.getFreeHeap());return false;}
  if(!applianceBleBegin(server)){LOG_ERR("BLE initialization failed; heap %u",ESP.getFreeHeap());return false;}
  LOG_INF("Management services initialized; heap %u",ESP.getFreeHeap());
#if !defined(XIAO_APPLIANCE_AUTH_TEST)
  if (!network.registerEndpoints(server)) return false;
#endif
  const httpd_uri_t routes[] = {
    {.uri="/", .method=HTTP_GET, .handler=asset, .user_ctx=nullptr},
    {.uri="/app.js", .method=HTTP_GET, .handler=asset, .user_ctx=nullptr},
    {.uri="/i18n.js", .method=HTTP_GET, .handler=asset, .user_ctx=nullptr},
    {.uri="/style.css", .method=HTTP_GET, .handler=asset, .user_ctx=nullptr},
    {.uri="/api/bootstrap", .method=HTTP_GET, .handler=bootstrap, .user_ctx=nullptr},
    {.uri="/api/status", .method=HTTP_GET, .handler=status, .user_ctx=nullptr},
    {.uri="/api/camera/still", .method=HTTP_GET, .handler=still, .user_ctx=nullptr},
    {.uri="/api/camera/stream", .method=HTTP_GET, .handler=stream, .user_ctx=nullptr},
    {.uri="/api/record/start", .method=HTTP_POST, .handler=record, .user_ctx=nullptr},
    {.uri="/api/record/stop", .method=HTTP_POST, .handler=record, .user_ctx=nullptr},
    {.uri="/api/device/restart", .method=HTTP_POST, .handler=restart, .user_ctx=nullptr},
  };
  for (const auto& route : routes)
    if (httpd_register_uri_handler(server, &route) != ESP_OK) return false;
  applianceOtaHttpReady();
  return true;
}
bool applianceCheckAuth(httpd_req_t* req, bool mutation) { return auth.checkRequest(req, mutation); }
bool applianceStreamToken(httpd_req_t* req, char (&token)[65]) { return auth.tokenFromRequest(req, token); }
bool applianceStreamAuthorized(const char* token) { return cameraReady() && auth.streamSessionValid(token); }
bool applianceSessionValid(const char* token) { return auth.streamSessionValid(token); }
void applianceClearToken(char (&token)[65]) { appliance::clearSecret(token, sizeof(token)); }
bool applianceNetworkBusy() {
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  return false;
#else
  return network.busy();
#endif
}
bool applianceBleWifi(const char* ssid,const char* password){
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  return false;
#else
  return network.submitAuthenticatedBleWifi(ssid,password);
#endif
}
void applianceBleNetworkStatus(char* wifi,size_t wifiBytes,char* ssid,size_t ssidBytes,char* wg,size_t wgBytes){
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  strlcpy(wifi,"TEST",wifiBytes);strlcpy(ssid,"",ssidBytes);strlcpy(wg,"TEST",wgBytes);
#else
  auto net=network.status();strlcpy(wifi,appliance::networkStateName(net.state),wifiBytes);strlcpy(ssid,net.config.wifiSSID,ssidBytes);
  auto status=network.wireguardStatus();strlcpy(wg,!net.config.wgEnabled?"DISABLED":status.running?"CONNECTED":"DISCONNECTED",wgBytes);
#endif
}
bool applianceRequestRecording(bool start){ApplianceForeground foreground;
  if(!foreground.ready()||!cameraReady()||applianceEventsArmed()||applianceCaptureBusy())return false;
  if(start&&(!doRecording||doPlayback||stopPlayback||!micGain||!applianceStorageSpaceAvailable()))return false;
  if(start)applianceCaptureWake();forceRecord=start;return true;
}
bool applianceRequestFactoryReset(){
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  return false;
#else
  if(!network.requestFactoryReset())return false;resetCrashLoop();resetPending.store(true);appSetupDone=false;return true;
#endif
}
#endif
