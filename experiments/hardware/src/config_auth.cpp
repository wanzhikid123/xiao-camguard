// Isolated module checks, not a product login/setup server. No NVS/SD writes.
#include <Arduino.h>
#include <WiFi.h>
#include <AdminAuth.h>
#include <WireGuardConfig.h>
#include <string.h>
using namespace appliance;
static bool ready = false, failed = false;
static Config candidate, original;
static Sessions sessions;
static const char* PASSWORD = "synthetic-admin-password";
static const char* CONF =
  "[Interface]\r\nPrivateKey = AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\r\n"
  "Address = 10.8.0.5/24, fd00::5/128\r\nDNS = 1.1.1.1\r\nMTU = 1280\r\n"
  "[Peer]\r\nPublicKey = AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\r\n"
  "PresharedKey = AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEBAQE=\r\n"
  "Endpoint = vpn.example.invalid:51820\r\nAllowedIPs = 0.0.0.0/0, ::/0\r\nPersistentKeepalive = 25\r\n";
static bool expect(bool condition, const char* label) {
  Serial.printf("AUTH TEST %s %s\n", condition ? "PASS" : "FAIL", label);
  if (!condition) failed = true;
  return condition;
}
#define CHECK(condition, label) do { if (!expect((condition), (label))) return; } while (0)
static bool badImport(String text, ImportError error, const char* label) {
  candidate = original;
  ImportSummary summary;
  bool ok = importWireGuard(text.c_str(), text.length(), candidate, summary) == error
            && !memcmp(&candidate, &original, sizeof(candidate));
  return expect(ok, label);
}

static void runParser() {
  candidate = defaults();
  strcpy(candidate.deviceName, "keep-this-name");
  strcpy(candidate.wifiSSID, "synthetic-wifi");
  strcpy(candidate.wifiPassword, "synthetic-wifi-password");
  candidate.minimumFreeMB = 2048;
  ImportSummary summary;
  CHECK(importWireGuard(CONF, strlen(CONF), candidate, summary) == ImportError::Ok, "standard_config_crlf_and_ipv6");
  CHECK(!strcmp(candidate.wg.address, "10.8.0.5/24") && candidate.wg.mtu == 1280
        && summary.ipv4Networks == 1 && summary.excludedIPv6Networks == 1
        && summary.excludedIPv6Addresses == 1, "explicit_ipv4_import_and_ipv6_exclusions");
  CHECK(summary.fullTunnelRequested && !candidate.wg.fullTunnel && candidate.wg.enabled,
        "default_route_does_not_silently_activate_wg_b");
  CHECK(!strcmp(candidate.wifiSSID, "synthetic-wifi") && !strcmp(candidate.deviceName, "keep-this-name")
        && candidate.minimumFreeMB == 2048 && validate(candidate) == Error::Ok, "non_wg_fields_preserved");
  original = candidate;
  String text = String("\xef\xbb\xbf# comment\n; comment\n") + CONF;
  text.replace("[Interface]", " [interface] ");
  text.replace("PrivateKey =", "privatekey =");
  text.replace("AllowedIPs = 0.0.0.0/0, ::/0", "AllowedIPs = 10.8.0.25/24, 192.168.7.9/32 # route");
  CHECK(importWireGuard(text.c_str(), text.length(), candidate, summary) == ImportError::Ok
        && !strcmp(candidate.wg.allowedIPs, "10.8.0.0/24,192.168.7.9/32") && !summary.fullTunnelRequested,
        "bom_comments_case_and_network_normalization");
  text = CONF; text += "PostUp = echo unsafe\n";
  CHECK(badImport(text, ImportError::UnsupportedField, "reject_hooks_atomically"), "hook_fixture");
  text = CONF; text += "PublicKey = duplicate\n";
  CHECK(badImport(text, ImportError::Duplicate, "reject_duplicate_field"), "duplicate_fixture");
  text = CONF; text += "[Peer]\n";
  CHECK(badImport(text, ImportError::Duplicate, "reject_multiple_peers"), "peer_fixture");
  text = CONF; text.replace("Endpoint =", "Unknown =");
  CHECK(badImport(text, ImportError::UnsupportedField, "reject_unknown_field"), "unknown_fixture");
  text = CONF; text.replace("Endpoint = vpn.example.invalid:51820\r\n", "");
  CHECK(badImport(text, ImportError::MissingField, "require_endpoint"), "missing_fixture");
  text = CONF; text.replace("MTU = 1280", "ListenPort = 12345");
  CHECK(badImport(text, ImportError::UnsupportedListenPort, "report_unsupported_listen_port"), "listen_fixture");
  text = CONF; text.replace("Endpoint = vpn.example.invalid:51820", "Endpoint = [2001:db8::1]:51820");
  CHECK(badImport(text, ImportError::InvalidValue, "reject_ipv6_endpoint"), "endpoint_fixture");
  text = CONF; text.replace("DNS = 1.1.1.1", "DNS = 1.1.1.1, 8.8.8.8");
  CHECK(badImport(text, ImportError::InvalidValue, "report_unsupported_dns_list"), "dns_fixture");
  text = CONF; text.replace("Address = 10.8.0.5/24, fd00::5/128", "Address = 10.8.0.5/32, 10.9.0.5/32");
  CHECK(badImport(text, ImportError::TooManyNetworks, "reject_multiple_ipv4_addresses"), "address_fixture");
  text = CONF; text.replace("AllowedIPs = 0.0.0.0/0, ::/0", "AllowedIPs = ::/0");
  CHECK(badImport(text, ImportError::InvalidValue, "require_ipv4_route"), "ipv6_fixture");
  text = CONF; text.replace("AllowedIPs = 0.0.0.0/0, ::/0",
    "AllowedIPs = 10.0.0.1/32,10.0.0.2/32,10.0.0.3/32,10.0.0.4/32,10.0.0.5/32,10.0.0.6/32,10.0.0.7/32,10.0.0.8/32");
  CHECK(importWireGuard(text.c_str(), text.length(), candidate, summary) == ImportError::Ok
        && summary.ipv4Networks == 8, "accept_eight_ipv4_networks");
  text.replace("10.0.0.8/32", "10.0.0.8/32,10.0.0.9/32");
  CHECK(badImport(text, ImportError::TooManyNetworks, "reject_ninth_route_atomically"), "capacity_fixture");
  char withNul[] = {'[', 0, ']'};
  CHECK(importWireGuard(withNul, sizeof(withNul), candidate, summary) == ImportError::InvalidText,
        "reject_embedded_nul");
  CHECK(importWireGuard(CONF, 4097, candidate, summary) == ImportError::TooLarge, "bound_input_before_read");
}

static void runSessions() {
  SessionCredentials credentials, others[Sessions::CAPACITY];
  CHECK(sessions.issue(100, credentials), "issue_session");
  CHECK(strlen(credentials.token) == 64 && strlen(credentials.csrf) == 64
        && strcmp(credentials.token, credentials.csrf), "independent_random_256_bit_tokens");
  CHECK(sessions.authorize(credentials.token, nullptr, false, 101), "session_read_authorized");
  CHECK(!sessions.authorize(credentials.token, nullptr, true, 102), "mutation_requires_csrf");
  CHECK(!sessions.authorize(credentials.token, credentials.token, true, 103), "wrong_csrf_denied");
  CHECK(sessions.authorize(credentials.token, credentials.csrf, true, 104), "matching_csrf_authorized");
  CHECK(!sessions.authorize("bad", nullptr, false, 105), "malformed_token_denied");
  CHECK(sessions.logout(credentials.token) && !sessions.authorize(credentials.token, nullptr, false, 106),
        "logout_revokes_immediately");
  CHECK(sessions.issue(0, credentials) && !sessions.authorize(credentials.token, nullptr, false, Sessions::IDLE_MS),
        "idle_expiry_boundary");
  CHECK(sessions.issue(0, credentials), "new_session_after_expiry");
  for (uint32_t now = 60000; now < Sessions::ABSOLUTE_MS; now += 60000)
    if (!sessions.authorize(credentials.token, nullptr, false, now)) { expect(false, "activity_until_absolute_expiry"); return; }
  CHECK(!sessions.authorize(credentials.token, nullptr, false, Sessions::ABSOLUTE_MS), "absolute_expiry_despite_activity");
  sessions.clear();
  CHECK(sessions.issue(0xffffff00u, credentials)
        && sessions.authorize(credentials.token, nullptr, false, 0x100u), "millis_wrap_keeps_unexpired_session");
  sessions.clear();
  CHECK(!sessions.authorize(credentials.token, nullptr, false, 0x101u), "clear_revokes_all_sessions");
  for (unsigned i = 0; i < Sessions::CAPACITY; ++i)
    CHECK(sessions.issue(200, others[i]), "fill_bounded_session_capacity");
  CHECK(!sessions.issue(201, credentials) && !credentials.token[0], "capacity_does_not_evict_or_leak_previous_token");
  CHECK(sessions.logout(others[1].token) && sessions.issue(202, credentials), "logout_frees_one_slot");
  sessions.clear();
  LoginLimiter limiter;
  CHECK(limiter.admit(100) && limiter.admit(101) && limiter.admit(102) && !limiter.admit(103), "three_attempts_per_minute");
  CHECK(limiter.retryAfterMs(103) == 59997 && !limiter.admit(60099) && limiter.admit(60100), "bounded_retry_window");
  LoginLimiter wrapped;
  CHECK(wrapped.admit(0xffffff00u) && wrapped.admit(0xffffff00u) && wrapped.admit(0xffffff00u)
        && !wrapped.admit(0x100u) && wrapped.admit(0xffffff00u + 60000u), "limiter_millis_wrap");
}

static void runPasswords() {
  candidate = defaults();
  CHECK(verifyPassword(candidate, PASSWORD, strlen(PASSWORD)) == AuthError::NotConfigured, "missing_admin_requires_setup");
  CHECK(enrollPassword(candidate, "too-short", 9) == AuthError::PasswordPolicy && !candidate.admin.configured,
        "short_password_not_enrolled");
  CHECK(enrollPassword(candidate, "12345678901\n", 12) == AuthError::PasswordPolicy, "password_controls_rejected");
  CHECK(enrollPassword(candidate, "\xc0\xaf" "123456789012", 14) == AuthError::PasswordPolicy, "malformed_utf8_rejected");
  CHECK(enrollPassword(candidate, "\xe4\xb8\xad\xe6\x96\x87\xe5\xaf\x86\xe7\xa0\x81", 12) == AuthError::PasswordPolicy,
        "minimum_counts_unicode_characters");
  candidate.admin.configured = 1;
  candidate.admin.iterations = PASSWORD_ITERATIONS;
  for (unsigned i = 0; i < 16; ++i) candidate.admin.salt[i] = i + 1;
  // Independently generated by Python hashlib.pbkdf2_hmac SHA256, 600000.
  const uint8_t expected[32] = {0xbe,0xb2,0xc2,0xc0,0xfd,0x57,0x04,0xb8,0xdc,0x6a,0xbd,0xd6,0x91,0xf5,0xfb,0x06,
    0x76,0x0d,0xbb,0x3b,0x8e,0xf1,0x12,0x1c,0x08,0x21,0x45,0x95,0xd7,0xe9,0x25,0xe7};
  memcpy(candidate.admin.verifier, expected, sizeof(expected));
  uint32_t started = millis();
  CHECK(verifyPassword(candidate, PASSWORD, strlen(PASSWORD)) == AuthError::Ok, "python_pbkdf2_known_answer");
  Serial.printf("AUTH KDF verify_ms=%lu iterations=%lu\n", (unsigned long)(millis() - started), (unsigned long)PASSWORD_ITERATIONS);
  CHECK(verifyPassword(candidate, "incorrect-admin-password", 24) == AuthError::Denied, "wrong_password_denied");
  original = candidate;
  CHECK(enrollPassword(candidate, "too-short", 9) == AuthError::PasswordPolicy
        && !memcmp(&candidate, &original, sizeof(candidate)), "failed_rotation_preserves_verifier");
  started = millis();
  CHECK(enrollPassword(candidate, PASSWORD, strlen(PASSWORD)) == AuthError::Ok, "enroll_with_random_salt");
  Serial.printf("AUTH KDF enroll_ms=%lu\n", (unsigned long)(millis() - started));
  CHECK(memcmp(candidate.admin.salt, original.admin.salt, sizeof(candidate.admin.salt))
        && memcmp(candidate.admin.verifier, original.admin.verifier, sizeof(candidate.admin.verifier)), "salt_changes_same_password_verifier");
  CHECK(verifyPassword(candidate, PASSWORD, strlen(PASSWORD)) == AuthError::Ok, "verify_new_enrollment");
  candidate.admin.iterations = 1;
  CHECK(verifyPassword(candidate, PASSWORD, strlen(PASSWORD)) == AuthError::InvalidVerifier, "reject_invalid_stored_work_factor");
  clearSecret(&candidate, sizeof(candidate));
  clearSecret(&original, sizeof(original));
  CHECK(!candidate.admin.configured && !candidate.wifiPassword[0], "explicit_secret_wipe");
}

static void worker(void*) {
  runParser();
  if (!failed) runSessions();
  if (!failed) runPasswords();
  if (!failed) Serial.println("CONFIG AUTH TEST COMPLETE; no NVS or SD writes");
  vTaskDelete(nullptr);
}
void setup() {
  Serial.begin(115200);
  delay(1000);
  if (ESP.getEfuseMac() != 0x084cfba172e0ULL) { failed = true; Serial.println("AUTH TEST FAIL unexpected_target"); return; }
  WiFi.persistent(false);
  // Running RF supplies hardware entropy; no Wi-Fi credentials or connection.
  if (!WiFi.mode(WIFI_STA)) { failed = true; Serial.println("AUTH TEST FAIL wifi_entropy_start"); return; }
  ready = true;
  Serial.println("CONFIG AUTH TEST READY; send RUN_CONFIG_AUTH_TEST");
}
void loop() {
  if (ready && Serial.available()) {
    String command = Serial.readStringUntil('\n'); command.trim();
    if (command == "RUN_CONFIG_AUTH_TEST") {
      ready = false;
      // The pinned SDK watches idle CPU0, not CPU1. A priority-one worker on
      // CPU1 keeps networking/CPU0 responsive; camera/HTTP have higher priority.
#if CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
#error Revalidate bounded password work scheduling on a changed SDK
#endif
      if (xTaskCreatePinnedToCore(worker, "auth-test", 8192, nullptr, 1, nullptr, 1) != pdPASS)
        Serial.println("AUTH TEST FAIL worker_start");
    }
  }
  delay(50);
}
