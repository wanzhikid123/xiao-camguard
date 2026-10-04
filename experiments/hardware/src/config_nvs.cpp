// Hardware NVS integration tests. Connectivity proofs are simulated here;
// real Wi-Fi/WG rollback must be tested through the product network manager.
#include <Arduino.h>
#include <ApplianceConfig.h>
#include <Preferences.h>
#include <WiFi.h>
#include <string.h>
#include "nvs.h"
#include "mbedtls/base64.h"
using namespace appliance;

static ConfigStore store;
static Config candidate, selected;
static Preferences control;
static constexpr uint32_t OWNER = 0x43544631;
static bool ready = false;
static bool failed = false;
static uint32_t revision = 0;
// Large fault-injection fixtures belong to the test's static storage, not the
// 8 KB Arduino task stack used by the actual reboot checkpoints.
static uint8_t buffer[2048] = {};
static uint8_t checkBuffer[2048] = {};
static const ConnectivityProof proof = {true, true, false, false};

static bool expect(bool condition, const char* label) {
  Serial.printf("NVS TEST %s %s\n", condition ? "PASS" : "FAIL", label);
  if (!condition) failed = true;
  return condition;
}
#define CHECK(condition, label) do { if (!expect((condition), (label))) return; } while (0)

static bool own(const char* ns, bool reset) {
  Preferences p;
  if (!p.begin(ns, false)) return false;
  // Refuse to clear an existing namespace that was not created by this suite.
  nvs_iterator_t iterator = nullptr;
  esp_err_t search = nvs_entry_find("nvs", ns, NVS_TYPE_ANY, &iterator);
  bool empty = search == ESP_ERR_NVS_NOT_FOUND;
  if (search != ESP_ERR_INVALID_ARG) nvs_release_iterator(iterator);
  bool ours = p.getUInt("owner", 0) == OWNER;
  bool ok = (empty || ours) && (!reset || p.clear())
            && p.putUInt("owner", OWNER) == sizeof(uint32_t);
  p.end();
  return ok;
}

static void restartAt(unsigned step) {
  if (!expect(control.putUInt("step", step) == sizeof(uint32_t), "persist_test_step")) return;
  store.close();
  control.end();
  Serial.printf("NVS TEST reboot_to_step=%u\n", step);
  Serial.flush();
  delay(300);
  ESP.restart();
}

static void run(unsigned step) {
  CHECK(store.open("cfgtest") == Error::Ok, "open_owned_store");
  if (step == 1) {
    CHECK(!store.status().hasActive, "empty_store_requires_setup");
    candidate = defaults();
    strcpy(candidate.wifiSSID, "nvs-test-A");
    strcpy(candidate.wifiPassword, "synthetic-test-password");
    CHECK(store.stage(candidate, revision) == Error::Ok, "stage_initial");
    CHECK(!store.status().hasActive && store.status().hasPending, "stage_does_not_activate");
    CHECK(store.beginTrial(selected, revision) == Error::Ok, "persist_attempt_before_trial");
    CHECK(store.confirmTrial(revision, {}) == Error::ProofMissing, "reject_missing_connectivity_proof");
    CHECK(store.confirmTrial(revision, proof) == Error::Ok, "promote_initial_after_proof");
    CHECK(store.status().wifiPasswordSet && !store.status().wgPrivateKeySet, "public_status_has_presence_flags");
    restartAt(2);
  } else if (step == 2) {
    CHECK(store.loadActive(selected) == Error::Ok && !strcmp(selected.wifiSSID, "nvs-test-A"), "active_survives_reboot");
    candidate = selected;
    memset(candidate.wifiSSID, 'x', sizeof(candidate.wifiSSID));
    CHECK(store.stage(candidate, revision) == Error::InvalidConfig, "reject_unterminated_input");
    candidate = selected;
    strcpy(candidate.wg.privateKey, "invalid-key");
    CHECK(store.stage(candidate, revision) == Error::InvalidConfig, "reject_bad_key_before_nvs_write");
    candidate = selected;
    strcpy(candidate.wifiSSID, "nvs-test-B");
    CHECK(store.stage(candidate, revision) == Error::Ok, "stage_second_candidate");
    CHECK(store.beginTrial(selected, revision) == Error::Ok, "begin_interrupted_trial");
    restartAt(3);
  } else if (step == 3) {
    CHECK(store.status().bootState == BootState::TrialInterrupted, "detect_interrupted_trial");
    CHECK(store.loadActive(selected) == Error::Ok && !strcmp(selected.wifiSSID, "nvs-test-A"), "rollback_to_old_active");
    uint32_t attempted = store.status().pendingRevision;
    CHECK(store.beginTrial(candidate, revision) == Error::AlreadyTried, "do_not_retry_bad_candidate_on_boot");
    CHECK(store.confirmTrial(attempted, proof) == Error::AlreadyTried, "do_not_confirm_trial_after_reset");
    CHECK(store.discardPending() == Error::Ok, "discard_failed_candidate");
    candidate = selected;
    strcpy(candidate.wifiSSID, "nvs-test-B");
    CHECK(store.stage(candidate, revision) == Error::Ok && revision > attempted, "new_trial_has_new_revision");
    CHECK(store.beginTrial(selected, revision) == Error::Ok, "retry_new_revision");
    CHECK(store.confirmTrial(revision - 1, proof) == Error::StaleRevision, "reject_stale_confirmation");
    CHECK(store.confirmTrial(revision, proof) == Error::Ok, "promote_to_other_bank");
    restartAt(4);
  } else if (step == 4) {
    CHECK(store.loadActive(selected) == Error::Ok && !strcmp(selected.wifiSSID, "nvs-test-B"), "second_bank_survives_reboot");
    candidate = selected;
    strcpy(candidate.wifiSSID, "nvs-test-C");
    CHECK(store.stage(candidate, revision) == Error::Ok, "stage_interrupted_promotion");
    CHECK(store.beginTrial(selected, revision) == Error::Ok, "mark_interrupted_promotion_trial");
    // Simulate the exact persisted checkpoint after inactive-bank write but
    // before selector commit. This is fault injection into our own test NVS.
    Preferences p;
    CHECK(p.begin("cfgtest", false), "open_fault_injection_namespace");
    size_t size = p.getBytesLength("pending");
    CHECK(size && size <= sizeof(buffer) && p.getBytes("pending", buffer, size) == size, "read_pending_checkpoint");
    const char* inactive = p.getUChar("selected", 255) == 0 ? "active1" : "active0";
    CHECK(p.putBytes(inactive, buffer, size) == size, "inject_bank_written_before_selector");
    p.end();
    restartAt(5);
  } else if (step == 5) {
    CHECK(store.loadActive(selected) == Error::Ok && !strcmp(selected.wifiSSID, "nvs-test-B"), "selector_keeps_old_active_after_interruption");
    CHECK(store.status().bootState == BootState::TrialInterrupted, "interrupted_promotion_not_retried");
    CHECK(store.discardPending() == Error::Ok, "discard_uncommitted_candidate");
    candidate = selected;
    strcpy(candidate.wifiSSID, "nvs-test-D");
    CHECK(store.stage(candidate, revision) == Error::Ok, "stage_replacement_after_interruption");
    CHECK(store.beginTrial(selected, revision) == Error::Ok && store.confirmTrial(revision, proof) == Error::Ok, "promote_replacement");
    Preferences p;
    CHECK(p.begin("cfgtest", false), "open_crc_fault_namespace");
    const char* key = p.getUChar("selected", 255) == 0 ? "active0" : "active1";
    size_t size = p.getBytesLength(key);
    CHECK(size && size <= sizeof(buffer) && p.getBytes(key, buffer, size) == size, "read_crc_fault_record");
    buffer[size - 1] ^= 0x80;
    CHECK(p.putBytes(key, buffer, size) == size, "inject_crc_corruption");
    p.end();
    restartAt(6);
  } else if (step == 6) {
    CHECK(store.status().bootState == BootState::RecoveredBank, "recover_previous_valid_bank");
    CHECK(store.loadActive(selected) == Error::Ok && !strcmp(selected.wifiSSID, "nvs-test-B"), "crc_fault_recovers_previous_config");
    CHECK(own("cfgmigtest", true) && own("cfglegacytest", true), "own_isolated_migration_namespaces");
    Preferences legacy;
    CHECK(legacy.begin("cfglegacytest", false), "open_legacy_fixture");
    CHECK(legacy.putString("ST_SSID", "synthetic-legacy") > 0
          && legacy.putString("ST_Pass", "synthetic-legacy-password") > 0
          && legacy.putString("Auth_Pass", "do-not-copy-plaintext") > 0, "write_synthetic_legacy_fixture");
    legacy.end();
    candidate = selected;
    candidate.wg.enabled = candidate.wg.fullTunnel = 1;
    strcpy(candidate.wg.address, "10.8.0.5/24");
    strcpy(candidate.wg.endpoint, "synthetic.example.invalid");
    strcpy(candidate.wg.allowedIPs, "0.0.0.0/0");
    uint8_t syntheticKey[32];
    memset(syntheticKey, 1, sizeof(syntheticKey));
    size_t encoded = 0;
    CHECK(mbedtls_base64_encode(reinterpret_cast<uint8_t*>(candidate.wg.privateKey),
          sizeof(candidate.wg.privateKey), &encoded, syntheticKey, sizeof(syntheticKey)) == 0,
          "make_synthetic_key_fixture");
    strcpy(candidate.wg.publicKey, candidate.wg.privateKey);
    strcpy(candidate.wg.presharedKey, candidate.wg.privateKey);
    CHECK(validate(candidate) == Error::Ok, "valid_ipv4_wg_schema");
    strcpy(candidate.wg.allowedIPs, "::/0");
    CHECK(validate(candidate) == Error::InvalidConfig, "reject_ipv6_in_ipv4_schema");
    strcpy(candidate.wg.allowedIPs, "0.0.0.0/0");
    CHECK(store.stage(candidate, revision) == Error::Ok && store.beginTrial(selected, revision) == Error::Ok,
          "stage_synthetic_wg_proof_gate");
    CHECK(store.confirmTrial(revision, proof) == Error::ProofMissing, "wg_promotion_requires_peer_session");
    CHECK(store.confirmTrial(revision, {true, true, true, false}) == Error::ProofMissing,
          "full_tunnel_promotion_requires_route_validation");
    CHECK(store.confirmTrial(revision, {true, true, true, true}) == Error::Ok, "accept_complete_simulated_proof");
    CHECK(store.status().wgPrivateKeySet && store.status().wgPresharedKeySet, "masked_wg_presence_flags");
    store.close();
    CHECK(store.open("cfgmigtest") == Error::Ok && store.importLegacy("cfglegacytest") == Error::Ok, "import_legacy_to_pending");
    CHECK(!store.status().hasActive && store.status().hasPending, "migration_requires_validation");
    CHECK(store.beginTrial(selected, revision) == Error::Ok && selected.legacyImported
          && !selected.admin.configured, "migration_requires_new_admin_enrollment");
    CHECK(store.confirmTrial(revision, proof) == Error::Ok, "promote_validated_migration");
    CHECK(legacy.begin("cfglegacytest", true), "reopen_legacy_fixture");
    CHECK(legacy.getString("ST_Pass") == "synthetic-legacy-password"
          && legacy.getString("Auth_Pass") == "do-not-copy-plaintext", "legacy_values_preserved");
    legacy.end();
    store.close();
    Preferences p;
    CHECK(p.begin("cfgmigtest", false), "open_future_version_fixture");
    const char* key = p.getUChar("selected", 255) == 0 ? "active0" : "active1";
    size_t size = p.getBytesLength(key);
    CHECK(size && size <= sizeof(buffer) && p.getBytes(key, buffer, size) == size, "read_version_fixture");
    uint16_t future = CONFIG_VERSION + 1;
    memcpy(buffer + 4, &future, sizeof(future));
    CHECK(p.putBytes(key, buffer, size) == size, "inject_future_schema_version");
    p.end();
    CHECK(store.open("cfgmigtest") == Error::UnsupportedVersion, "preserve_unknown_schema_for_recovery");
    CHECK(store.stage(defaults(), revision) == Error::UnsupportedVersion
          && store.discardPending() == Error::UnsupportedVersion, "do_not_overwrite_future_schema");
    CHECK(p.begin("cfgmigtest", true), "reopen_future_version_fixture");
    CHECK(p.getBytes(key, checkBuffer, size) == size && !memcmp(checkBuffer, buffer, size), "unknown_schema_bytes_unchanged");
    p.end();
    store.close();
    control.end();
    // Remove only fixtures bearing this suite's ownership marker.
    for (const char* ns : {"cfgtest", "cfgmigtest", "cfglegacytest", "cfgtestctl"}) {
      Preferences cleanup;
      CHECK(cleanup.begin(ns, false) && cleanup.getUInt("owner", 0) == OWNER
            && cleanup.clear(), "cleanup_owned_fixture_namespace");
      cleanup.end();
    }
    Serial.println("CONFIG NVS TEST COMPLETE; product and upstream namespaces untouched");
  } else CHECK(false, "unknown_test_step");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  // This test suite is restricted to the already inspected connected board.
  if (ESP.getEfuseMac() != 0x084cfba172e0ULL) {
    Serial.println("NVS TEST FAIL unexpected_target");
    failed = true;
    return;
  }
  if (!control.begin("cfgtestctl", false)) { failed = true; return; }
  uint32_t step = control.getUInt("step", 0);
  if (step && control.getUInt("owner", 0) == OWNER) run(step);
  else {
    control.end();
    ready = true;
    Serial.println("CONFIG NVS TEST READY; send RUN_CONFIG_NVS_TEST");
  }
}

void loop() {
  if (ready && !failed && Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command == "RUN_CONFIG_NVS_TEST") {
      ready = false;
      if (!expect(own("cfgtestctl", true) && own("cfgtest", true), "own_test_namespaces")) return;
      if (!control.begin("cfgtestctl", false)) { failed = true; return; }
      if (!expect(control.putUInt("step", 1) == sizeof(uint32_t), "persist_first_step")) return;
      run(1);
    }
  }
  delay(50);
}
