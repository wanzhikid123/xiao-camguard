// Wi-Fi/NVS integration experiment only. No camera/SD/product namespace.
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WifiDriver.h>
#include <AdminAuth.h>
#include <cJSON.h>
#include <nvs.h>

using namespace appliance;
namespace {
constexpr uint32_t OWNER = 0x4e543031;
WifiDriver driver("XIAO-Network-Test-084C");
appliance::NetworkManager manager(driver);
WebServer http(8081);
Preferences control;
Config good, candidate, scratch;
char token[65] = {}, input[512] = {};
bool running = false, failed = false, announced = false, lossObserved = false, httpStarted = false;
uint32_t step = 0, checks = 0, phaseAt = 0, firstRevision = 0, trialRevision = 0;

bool check(bool ok, const char* name) {
  Serial.printf("NETWORK TRIAL %s %s\n", ok ? "PASS" : "FAIL", name);
  checks++;
  if (!ok) { failed = true; running = false; }
  return ok;
}
bool own(const char* ns) {
  Preferences p;
  if (!p.begin(ns, false)) return false;
  if (p.isKey("owner")) return p.getUInt("owner", 0) == OWNER;
  nvs_iterator_t iterator = nullptr;
  esp_err_t found = nvs_entry_find("nvs", ns, NVS_TYPE_ANY, &iterator);
  nvs_release_iterator(iterator);
  if (found != ESP_ERR_NVS_NOT_FOUND) return false;
  return p.putUInt("owner", OWNER) == sizeof(uint32_t);
}
bool cleanup() {
  if (httpStarted) { http.stop(); httpStarted = false; }
  manager.stop(); control.end();
  clearSecret(&good, sizeof(good)); clearSecret(&candidate, sizeof(candidate));
  clearSecret(&scratch, sizeof(scratch)); clearSecret(token, sizeof(token));
  bool ok = true;
  for (const char* ns : {"nettrial", "nettrialctl"}) {
    Preferences p;
    if (!p.begin(ns, false) || p.getUInt("owner", 0) != OWNER || !p.clear()) ok = false;
  }
  return ok;
}
bool authorized() { return token[0] && http.header("X-Network-Test") == token; }
void sendStatus() {
  if (!authorized()) { http.send(401, "application/json", "{\"error\":\"test_auth_required\"}"); return; }
  NetworkStatus s = manager.status();
  char text[256];
  snprintf(text, sizeof(text), "{\"experiment\":true,\"state\":\"%s\",\"error\":\"%s\","
    "\"wifiConnected\":%s,\"rescueAP\":%s,\"activeRevision\":%lu,\"trialRevision\":%lu}",
    networkStateName(s.state), networkErrorName(s.error), s.readiness.wifiConnected ? "true" : "false",
    s.rescueAP ? "true" : "false", (unsigned long)s.config.activeRevision, (unsigned long)s.trialRevision);
  http.send(200, "application/json", text);
}
void confirm() {
  if (!authorized()) { http.send(403, "application/json", "{\"error\":\"test_auth_required\"}"); return; }
  // Prove the actual local destination of this socket, never trust Host or a
  // caller's boolean. AP access cannot confirm the candidate LAN configuration.
  if (WiFi.status() != WL_CONNECTED || http.client().localIP() != WiFi.localIP()) {
    http.send(403, "application/json", "{\"error\":\"candidate_lan_required\"}"); return;
  }
  String value = http.arg("revision");
  uint32_t revision = strtoul(value.c_str(), nullptr, 10);
  NetworkError result = manager.confirmLocalAccess(revision, millis());
  http.send(result == NetworkError::None ? 200 : 409, "application/json",
    result == NetworkError::None ? "{\"confirmed\":true}" : "{\"confirmed\":false}");
}
void announce(const char* phase) {
  Serial.printf("NETWORK TRIAL LAN phase=%s address=%s revision=%lu\n", phase,
    WiFi.localIP().toString().c_str(), (unsigned long)manager.status().trialRevision);
  announced = true;
}
void enter(uint32_t next) {
  step = next; phaseAt = millis(); announced = false;
  if (!check(control.putUInt("step", next) == sizeof(uint32_t), "persist_fixture_checkpoint")) return;
}
bool parseInput(size_t bytes) {
  clearSecret(input, sizeof(input));
  if (!bytes || bytes >= sizeof(input)) return false;
  Serial.println("NETWORK TRIAL SEND");
  size_t received = Serial.readBytes(input, bytes);
  cJSON* root = received == bytes ? cJSON_ParseWithLength(input, bytes + 1) : nullptr;
  bool valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 3;
  good = defaults();
  unsigned fields = 0;
  for (cJSON* item = root ? root->child : nullptr; item; item = item->next) {
    unsigned field = item->string && !strcmp(item->string, "ssid") ? 1 :
      item->string && !strcmp(item->string, "password") ? 2 :
      item->string && !strcmp(item->string, "testToken") ? 4 : 0;
    if (!field || (fields & field) || !cJSON_IsString(item) || !item->valuestring) valid = false;
    else if (field == 1 && strlen(item->valuestring) < sizeof(good.wifiSSID)) strcpy(good.wifiSSID, item->valuestring);
    else if (field == 2 && strlen(item->valuestring) < sizeof(good.wifiPassword)) strcpy(good.wifiPassword, item->valuestring);
    else if (field == 4 && strlen(item->valuestring) == 64) strcpy(token, item->valuestring);
    else valid = false;
    fields |= field;
    if (item->valuestring) clearSecret(item->valuestring, strlen(item->valuestring));
  }
  cJSON_Delete(root); clearSecret(input, sizeof(input));
  return valid && fields == 7 && validate(good) == Error::Ok && good.wifiSSID[0];
}
void start(size_t bytes) {
  if (!check(parseInput(bytes), "private_input_validated_without_logging")) return;
  if (!check(own("nettrial") && own("nettrialctl"), "own_isolated_namespaces")) return;
  if (!check(control.begin("nettrialctl", false), "open_fixture_control")) return;
  if (!check(control.putString("token", token) == strlen(token), "persist_temporary_fixture_token")) return;
  if (!check(manager.begin(millis(), "nettrial") == Error::Ok, "start_real_network_manager")) return;
  // Socket APIs require Network/Wi-Fi initialization on this pinned core.
  http.begin(); httpStarted = true;
  if (!check(manager.status().state == NetworkState::Setup && manager.status().rescueAP
             && (WiFi.getMode() & WIFI_AP), "empty_configuration_enters_real_setup_ap")) return;
  if (!check(manager.submit(good, millis(), trialRevision) == NetworkError::None, "stage_and_apply_real_wifi_trial")) return;
  uint32_t ignored;
  if (!check(manager.submit(good, millis(), ignored) == NetworkError::Busy, "refuse_overlapping_trial")) return;
  if (!check(manager.confirmLocalAccess(trialRevision + 1, millis()) == NetworkError::StaleRevision,
             "stale_confirmation_rejected")) return;
  if (!check(manager.confirmLocalAccess(trialRevision, millis()) == NetworkError::ProofMissing,
             "connection_required_before_confirmation")) return;
  running = true; enter(1);
}
void advance() {
  manager.tick(millis());
  NetworkStatus s = manager.status();
  if (step == 1) {
    if (s.state == NetworkState::TrialReady && !announced) announce("first");
    if (s.state == NetworkState::Ready && !s.trialRevision) {
      // Let the LAN confirmation response leave the socket before changing
      // Wi-Fi again for the deliberately failing test.
      if (!firstRevision) { firstRevision = s.config.activeRevision; phaseAt = millis(); return; }
      if (millis() - phaseAt < 1500) return;
      if (!check(s.config.hasActive && s.readiness.wifiConnected && !s.rescueAP
                 && !(WiFi.getMode() & WIFI_AP), "real_lan_confirmation_promotes_and_closes_ap")) return;
      if (!check(manager.loadActive(good) == Error::Ok, "load_real_last_known_good")) return;
      candidate = good; strcpy(candidate.wifiSSID, "codex-missing-wifi-084c");
      if (!check(manager.submit(candidate, millis(), trialRevision) == NetworkError::None,
                 "apply_nonexistent_wifi_candidate")) return;
      if (!check(manager.status().config.activeRevision == firstRevision, "bad_trial_preserves_selected_bank")) return;
      enter(2);
    }
  } else if (step == 2 && s.state == NetworkState::Ready && !s.trialRevision) {
    if (!check(s.error == NetworkError::ConnectTimeout && s.config.activeRevision == firstRevision
               && !s.config.hasPending && s.readiness.wifiConnected, "failed_wifi_restores_real_last_known_good")) return;
    if (!check(manager.loadActive(scratch) == Error::Ok && !strcmp(scratch.wifiSSID, good.wifiSSID)
               && !strcmp(scratch.wifiPassword, good.wifiPassword), "selected_credentials_unchanged_after_failure")) return;
    if (!check(control.putUInt("firstRev", firstRevision) == sizeof(uint32_t), "persist_expected_active_revision")) return;
    if (!check(manager.submit(candidate, millis(), trialRevision) == NetworkError::None,
               "start_real_interrupted_trial")) return;
    if (!check(manager.status().config.pendingTried, "tried_marker_precedes_restart")) return;
    if (!check(control.putUInt("checks", checks + 2) == sizeof(uint32_t), "persist_check_count")) return;
    enter(3);
    if (failed) return;
    Serial.println("NETWORK TRIAL deliberate_restart_during_unconfirmed_connection");
    delay(200); ESP.restart();
  } else if (step == 3 && s.state == NetworkState::Ready) {
    if (!check(s.error == NetworkError::Interrupted && !s.config.hasPending
               && s.config.activeRevision == firstRevision && s.readiness.wifiConnected,
               "reboot_recovers_active_without_retrying_candidate")) return;
    if (!check(manager.confirmLocalAccess(firstRevision + 2, millis()) == NetworkError::NotTrial,
               "late_confirmation_after_reset_rejected")) return;
    WiFi.disconnect(false, false); enter(4);
  } else if (step == 4) {
    if (!s.readiness.wifiConnected) lossObserved = true;
    if (millis() - phaseAt > 1000 && s.state == NetworkState::Ready && s.readiness.wifiConnected) {
      if (!check(lossObserved && s.config.activeRevision == firstRevision && !s.rescueAP,
                 "runtime_wifi_loss_reconnects_without_reboot")) return;
      announce("recovered"); enter(5);
    }
  } else if (step == 5 && millis() - phaseAt > 6000) {
    if (!check(cleanup(), "cleanup_only_owned_network_fixtures")) return;
    running = false;
    Serial.printf("NETWORK TRIAL COMPLETE checks=%lu failures=0 product_nvs_untouched=1\n", (unsigned long)checks);
  }
  if (running && millis() - phaseAt > 110000) check(false, "bounded_phase_timeout");
}
} // namespace
void setup() {
  Serial.begin(115200); Serial.setTimeout(5000); delay(1000);
  if (ESP.getEfuseMac() != 0x084cfba172e0ULL) { check(false, "unexpected_target"); return; }
  const char* headers[] = {"X-Network-Test"}; http.collectHeaders(headers, 1);
  http.on("/", HTTP_GET, [](){ http.send(200, "application/json", "{\"network_trial_experiment\":true}"); });
  http.on("/network-test/status", HTTP_GET, sendStatus);
  http.on("/network-test/confirm", HTTP_POST, confirm);
  if (control.begin("nettrialctl", true) && control.getUInt("owner", 0) == OWNER && control.getUInt("step", 0) == 3) {
    checks = control.getUInt("checks", 0); firstRevision = control.getUInt("firstRev", 0);
    String saved = control.getString("token", ""); strlcpy(token, saved.c_str(), sizeof(token));
    clearSecret(saved.begin(), saved.length()); control.end();
    if (!check(control.begin("nettrialctl", false) && manager.begin(millis(), "nettrial") == Error::Ok,
               "resume_after_real_software_reset")) return;
    step = 3; phaseAt = millis(); running = true;
  } else { control.end(); Serial.println("NETWORK TRIAL READY"); }
  if (running) { http.begin(); httpStarted = true; }
}
void loop() {
  if (httpStarted) http.handleClient();
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command == "ABORT_NETWORK_TEST") {
      running = false;
      Serial.printf("NETWORK TRIAL ABORTED cleanup=%u\n", cleanup());
    } else if (!running && !failed && command.startsWith("RUN_NETWORK_TRIAL ")) start(strtoul(command.c_str() + 18, nullptr, 10));
  }
  if (running && !failed) advance();
  delay(20);
}
