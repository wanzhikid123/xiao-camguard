// Independent backend spike; not part of the camera application.
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <WireGuard-ESP32.h>
#include "esp_system.h"
#include <WebServer.h>

#if __has_include("secrets.h") && !defined(WG_NO_LOCAL_SECRETS)
#include "secrets.h"
#else
#include "secrets.h.example"
#endif

static WireGuard wg;
static WebServer diagnosticHTTP(8080);
static bool prepared = false;
static uint32_t cycle = 0;
static uint32_t baselineHeap = 0;
static uint32_t cycleAt = 0;
static constexpr uint32_t WIFI_TIMEOUT_MS = 60000;
static constexpr uint32_t TIME_TIMEOUT_MS = 30000;
static constexpr uint32_t HANDSHAKE_WAIT_MS = 10000;
static constexpr uint32_t RECONNECT_INTERVAL_MS = 15000;
static constexpr uint32_t MAX_CYCLES = 20;

static void memoryState(const char* phase) {
  Serial.printf("%s cycle=%lu heap=%lu min_heap=%lu psram_free=%lu baseline_heap=%lu\n",
                phase, static_cast<unsigned long>(cycle),
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getFreePsram()),
                static_cast<unsigned long>(baselineHeap));
}

static bool syncTime() {
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  uint32_t start = millis();
  while (time(nullptr) < 1704067200 && millis() - start < TIME_TIMEOUT_MS) delay(100);
  return time(nullptr) >= 1704067200;
}

static void stopTunnel() {
  wg.end();
  Serial.println("WireGuard stopped; verify LAN access and route restoration externally");
  memoryState("after_stop");
}

static void probeLAN() {
  WiFiClient client;
  client.setTimeout(3000);
  bool connected = client.connect(WiFi.gatewayIP(), 80, 3000);
  if (connected) {
    client.printf("GET / HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", WiFi.gatewayIP().toString().c_str());
    char status[64] = {};
    size_t count = client.readBytesUntil('\n', status, sizeof(status) - 1);
    connected = count >= 12 && !strncmp(status, "HTTP/", 5);
  }
  client.stop();
  Serial.printf("LAN gateway HTTP reachable=%s\n", connected ? "true" : "false");
}

static bool probeTunnelService() {
  IPAddress host;
  if (!host.fromString(WG_TEST_HOST)) {
    Serial.println("Probe error: WG_TEST_HOST must be an IPv4 address inside the tunnel");
    return false;
  }
  WiFiClient client;
  client.setTimeout(3000);
  if (!client.connect(host, WG_TEST_PORT, 3000)) {
    Serial.println("Tunnel HTTP probe failed to connect");
    return false;
  }
  client.printf("GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", WG_TEST_PATH, WG_TEST_HOST);
  uint32_t start = millis();
  while (!client.available() && client.connected() && millis() - start < 3000) delay(10);
  // Only inspect a bounded status line; never dump response content or credentials.
  char status[64] = {};
  size_t count = client.readBytesUntil('\n', status, sizeof(status) - 1);
  client.stop();
  bool ok = count >= 12 && !strncmp(status, "HTTP/", 5) && status[9] == '2';
  int code = 0;
  if (count) sscanf(status, "HTTP/%*s %d", &code);
  Serial.printf("Tunnel HTTP probe status=%d; 2xx=%s\n", code, ok ? "true" : "false");
#ifdef WG_IDF5_SPIKE
  uint32_t sent, received;
  wg.trafficBytes(sent, received);
  Serial.printf("Authenticated tunnel plaintext bytes: tx=%lu rx=%lu\n", (unsigned long)sent, (unsigned long)received);
#endif
  return ok;
}

static void startCycle() {
  IPAddress address;
  if (!address.fromString(WG_ADDRESS)) {
    Serial.println("Invalid WG_ADDRESS: use IPv4 without CIDR for this library spike");
    prepared = false;
    return;
  }
  cycle++;
  memoryState("before_start");
#ifdef WG_IDF5_SPIKE
  IPAddress subnet, allowed, allowedMask;
  subnet.fromString(WG_NETMASK);
  // The supplied experiment config has one IPv4 0.0.0.0/0 route.
  if (strcmp(WG_ALLOWED_IPS, "0.0.0.0/0")) {
    Serial.println("This bounded adapter trial currently requires one IPv4 default route");
    prepared = false;
    return;
  }
  allowed.fromString("0.0.0.0");
  allowedMask.fromString("0.0.0.0");
  bool started = wg.beginConfigured(address, subnet, WG_PRIVATE_KEY, WG_ENDPOINT,
                                    WG_PEER_PUBLIC_KEY, WG_ENDPOINT_PORT, WG_PRESHARED_KEY,
                                    allowed, allowedMask, WG_KEEPALIVE, true);
#else
  bool started = wg.begin(address, WG_PRIVATE_KEY, WG_ENDPOINT, WG_PEER_PUBLIC_KEY, WG_ENDPOINT_PORT);
#endif
  Serial.printf("WireGuard initialized=%s; handshake status unavailable in Arduino wrapper\n", started ? "true" : "false");
  if (started) {
    delay(HANDSHAKE_WAIT_MS);
#ifdef WG_IDF5_SPIKE
    Serial.printf("Authenticated peer session up=%s; last-handshake timestamp not exposed\n", wg.peerSessionUp() ? "true" : "false");
#endif
    if (WG_TEST_HOST[0]) probeTunnelService();
    else Serial.println("Tunnel HTTP probe not configured; no routing/service success claimed");
  } else {
    // This wrapper has no detailed sanitized error API.
    Serial.println("Start failed: inspect sanitized backend diagnostics");
  }
  memoryState("after_start");
  probeLAN();
  cycleAt = millis();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("WireGuard compatibility experiment; no camera firmware integration");
  Serial.printf("flash=%lu psram=%lu reset_reason_pending\n",
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()));
  if (!WIFI_SSID[0] || !WG_ADDRESS[0] || !WG_PRIVATE_KEY[0] ||
      !WG_PEER_PUBLIC_KEY[0] || !WG_ENDPOINT[0]) {
    Serial.println("Set ignored include/secrets.h before running; no credentials printed");
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Reset reason=%d\n", (int)esp_reset_reason());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) delay(100);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi timed out; experiment stopped");
    return;
  }
  Serial.printf("WiFi connected, LAN IP=%s\n", WiFi.localIP().toString().c_str());
  if (!syncTime()) {
    Serial.println("NTP timed out; WireGuard not started");
    return;
  }
  Serial.println("NTP synchronized");
  baselineHeap = ESP.getFreeHeap();
  prepared = true;
  diagnosticHTTP.on("/status", HTTP_GET, []() {
    char status[256];
#ifdef WG_IDF5_SPIKE
    uint32_t sent, received;
    wg.trafficBytes(sent, received);
    snprintf(status, sizeof(status), "{\"experiment\":true,\"cycle\":%lu,\"initialized\":%s,\"peer_session_up\":%s,\"tx_bytes\":%lu,\"rx_bytes\":%lu}",
             (unsigned long)cycle, wg.is_initialized() ? "true" : "false", wg.peerSessionUp() ? "true" : "false",
             (unsigned long)sent, (unsigned long)received);
#else
    snprintf(status, sizeof(status), "{\"experiment\":true,\"initialized\":%s}", wg.is_initialized() ? "true" : "false");
#endif
    diagnosticHTTP.send(200, "application/json", status);
  });
  diagnosticHTTP.begin();
  startCycle();
}

void loop() {
  diagnosticHTTP.handleClient();
  if (!prepared) { delay(1000); return; }
  if (WiFi.status() != WL_CONNECTED) {
    stopTunnel();
    Serial.println("WiFi lost; wait up to 60 seconds for reconnect");
    WiFi.reconnect();
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) delay(100);
    if (WiFi.status() != WL_CONNECTED || !syncTime()) {
      prepared = false;
      Serial.println("Reconnect failed; experiment stopped without reboot loop");
      return;
    }
    startCycle();
  } else if (millis() - cycleAt >= RECONNECT_INTERVAL_MS) {
    stopTunnel();
    probeLAN();
    if (cycle >= MAX_CYCLES) {
      prepared = false;
      Serial.println("WG SPIKE COMPLETE; tunnel stopped and LAN default restored");
      return;
    }
    delay(1000);
    if (cycle % 5 == 0) {
      Serial.println("Controlled WiFi reconnect test");
      WiFi.disconnect(false, false);
      WiFi.reconnect();
      uint32_t start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) delay(100);
      if (WiFi.status() != WL_CONNECTED || !syncTime()) {
        prepared = false;
        Serial.println("Controlled WiFi reconnect failed");
        return;
      }
    }
    startCycle();
  }
  delay(100);
}
