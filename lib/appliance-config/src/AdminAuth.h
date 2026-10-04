#pragma once
#include "ApplianceConfig.h"
#include <stddef.h>

namespace appliance {
constexpr uint32_t PASSWORD_ITERATIONS = 600000;
constexpr size_t PASSWORD_MIN_CHARACTERS = 10;
constexpr size_t SESSION_TOKEN_BYTES = 32;
constexpr size_t SESSION_TOKEN_CHARS = SESSION_TOKEN_BYTES * 2;
enum class AuthError { Ok, PasswordPolicy, NotConfigured, InvalidVerifier, Denied, Crypto };
const char* authErrorName(AuthError error);
bool passwordMeetsPolicy(const char* password, size_t bytes);
// Enrollment is atomic; only the salted verifier is retained. Passwords must
// have 10 or more Unicode characters and at most 128 UTF-8 bytes, without NUL
// or ASCII controls. The caller owns/wipes the input and runs expensive KDFs
// on a worker, never the HTTP/camera task. Wi-Fi RF must be active for entropy.
AuthError enrollPassword(Config& config, const char* password, size_t bytes);
AuthError verifyPassword(const Config& config, const char* password, size_t bytes);
void clearSecret(void* memory, size_t bytes);

struct SessionCredentials {
  char token[SESSION_TOKEN_CHARS + 1] = {};
  char csrf[SESSION_TOKEN_CHARS + 1] = {};
};

// Volatile sessions: four clients, 30 minute idle / 8 hour absolute expiry.
// One owner serializes all access. Supply monotonic millis(), not wall time.
// RF entropy must be active. Sessions never go to NVS or public status.
class Sessions {
public:
  static constexpr unsigned CAPACITY = 4;
  static constexpr uint32_t IDLE_MS = 30 * 60 * 1000;
  static constexpr uint32_t ABSOLUTE_MS = 8 * 60 * 60 * 1000;
  ~Sessions() { clear(); }
  Sessions() = default;
  Sessions(const Sessions&) = delete;
  Sessions& operator=(const Sessions&) = delete;
  bool issue(uint32_t now, SessionCredentials& credentials);
  bool authorize(const char* token, const char* csrf, bool mutation, uint32_t now, bool touch = true);
  bool csrfFor(const char* token, char (&csrf)[SESSION_TOKEN_CHARS + 1], uint32_t now);
  bool logout(const char* token);
  void clear();
private:
  struct Entry {
    uint8_t token[SESSION_TOKEN_BYTES] = {};
    uint8_t csrf[SESSION_TOKEN_BYTES] = {};
    uint32_t created = 0;
    uint32_t accessed = 0;
    bool occupied = false;
  } entries[CAPACITY];
  static bool expired(const Entry& entry, uint32_t now);
};

// Global bounded work admission, including successful attempts. Reserve a
// slot before starting the KDF. There is no permanent lockout or IP loophole.
class LoginLimiter {
public:
  bool admit(uint32_t now);
  uint32_t retryAfterMs(uint32_t now) const;
private:
  uint32_t started = 0;
  unsigned attempts = 0;
};
} // namespace appliance
