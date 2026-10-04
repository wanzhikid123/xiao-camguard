#include "AdminAuth.h"
#include <string.h>
#include "esp_random.h"
#include "mbedtls/pkcs5.h"
extern "C" {
#include "mbedtls/constant_time.h"
}

namespace appliance {
void clearSecret(void* memory, size_t bytes) {
  volatile uint8_t* p = static_cast<volatile uint8_t*>(memory);
  while (bytes--) *p++ = 0;
}
namespace {
bool equal(const uint8_t* a, const uint8_t* b, size_t bytes) {
  return mbedtls_ct_memcmp(a, b, bytes) == 0;
}
bool nonzero(const uint8_t* value, size_t bytes) {
  uint8_t result = 0;
  for (size_t i = 0; i < bytes; ++i) result |= value[i];
  return result != 0;
}
bool validPassword(const char* text, size_t bytes) {
  if (!text || bytes > 128 || bytes < PASSWORD_MIN_CHARACTERS) return false;
  size_t characters = 0;
  for (size_t i = 0; i < bytes; ++characters) {
    uint32_t c = static_cast<uint8_t>(text[i++]);
    if (c < 32 || c == 127) return false;
    unsigned trailing = 0;
    uint32_t minimum = 0;
    if (c < 128) continue;
    if (c >= 0xc2 && c <= 0xdf) { trailing = 1; minimum = 0x80; c &= 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { trailing = 2; minimum = 0x800; c &= 0x0f; }
    else if (c >= 0xf0 && c <= 0xf4) { trailing = 3; minimum = 0x10000; c &= 7; }
    else return false;
    if (i + trailing > bytes) return false;
    while (trailing--) {
      uint8_t next = static_cast<uint8_t>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      c = (c << 6) | (next & 0x3f);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
  }
  return characters >= PASSWORD_MIN_CHARACTERS;
}
int derive(const char* password, size_t bytes, const uint8_t* salt, uint32_t iterations, uint8_t* output) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
      reinterpret_cast<const uint8_t*>(password), bytes, salt, 16, iterations, 32, output);
}
void hex(const uint8_t* bytes, char* output) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < SESSION_TOKEN_BYTES; ++i) {
    output[i * 2] = digits[bytes[i] >> 4];
    output[i * 2 + 1] = digits[bytes[i] & 15];
  }
  output[SESSION_TOKEN_CHARS] = 0;
}
bool unhex(const char* text, uint8_t* output) {
  if (!text || strnlen(text, SESSION_TOKEN_CHARS + 1) != SESSION_TOKEN_CHARS) return false;
  for (size_t i = 0; i < SESSION_TOKEN_BYTES; ++i) {
    uint8_t byte = 0;
    for (unsigned half = 0; half < 2; ++half) {
      char c = text[i * 2 + half];
      unsigned value;
      if (c >= '0' && c <= '9') value = c - '0';
      else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
      else return false;
      byte = (byte << 4) | value;
    }
    output[i] = byte;
  }
  return true;
}
} // namespace

bool passwordMeetsPolicy(const char* password, size_t bytes) { return validPassword(password, bytes); }
const char* authErrorName(AuthError error) {
  switch (error) {
    case AuthError::Ok: return "ok";
    case AuthError::PasswordPolicy: return "password_policy";
    case AuthError::NotConfigured: return "admin_not_configured";
    case AuthError::InvalidVerifier: return "invalid_verifier";
    case AuthError::Denied: return "invalid_credentials";
    case AuthError::Crypto: return "password_verifier_failed";
  }
  return "unknown_auth_error";
}
AuthError enrollPassword(Config& config, const char* password, size_t bytes) {
  if (!validPassword(password, bytes)) return AuthError::PasswordPolicy;
  decltype(config.admin) candidate = {};
  candidate.iterations = PASSWORD_ITERATIONS;
  esp_fill_random(candidate.salt, sizeof(candidate.salt));
  int result = derive(password, bytes, candidate.salt, candidate.iterations, candidate.verifier);
  bool success = result == 0 && nonzero(candidate.salt, sizeof(candidate.salt))
                 && nonzero(candidate.verifier, sizeof(candidate.verifier));
  if (success) {
    candidate.configured = 1;
    config.admin = candidate;
  }
  clearSecret(&candidate, sizeof(candidate));
  return success ? AuthError::Ok : AuthError::Crypto;
}
AuthError verifyPassword(const Config& config, const char* password, size_t bytes) {
  if (!config.admin.configured) return AuthError::NotConfigured;
  if (config.admin.configured != 1 || config.admin.iterations < 10000 || config.admin.iterations > 1000000
      || !nonzero(config.admin.salt, sizeof(config.admin.salt))
      || !nonzero(config.admin.verifier, sizeof(config.admin.verifier))) return AuthError::InvalidVerifier;
  if (!validPassword(password, bytes)) return AuthError::Denied;
  uint8_t derived[32] = {};
  int result = derive(password, bytes, config.admin.salt, config.admin.iterations, derived);
  bool matches = equal(derived, config.admin.verifier, sizeof(derived));
  clearSecret(derived, sizeof(derived));
  return result != 0 ? AuthError::Crypto : matches ? AuthError::Ok : AuthError::Denied;
}

bool Sessions::expired(const Entry& entry, uint32_t now) {
  return uint32_t(now - entry.created) >= ABSOLUTE_MS || uint32_t(now - entry.accessed) >= IDLE_MS;
}
bool Sessions::issue(uint32_t now, SessionCredentials& credentials) {
  clearSecret(&credentials, sizeof(credentials));
  for (Entry& entry : entries) {
    if (entry.occupied && expired(entry, now)) clearSecret(&entry, sizeof(entry));
    if (!entry.occupied) {
      esp_fill_random(entry.token, sizeof(entry.token));
      esp_fill_random(entry.csrf, sizeof(entry.csrf));
      entry.created = entry.accessed = now;
      entry.occupied = true;
      hex(entry.token, credentials.token);
      hex(entry.csrf, credentials.csrf);
      return true;
    }
  }
  return false;
}
bool Sessions::authorize(const char* token, const char* csrf, bool mutation, uint32_t now, bool touch) {
  uint8_t decoded[SESSION_TOKEN_BYTES] = {}, decodedCsrf[SESSION_TOKEN_BYTES] = {};
  bool valid = unhex(token, decoded) && (!mutation || unhex(csrf, decodedCsrf));
  bool accepted = false;
  for (Entry& entry : entries) {
    if (entry.occupied && expired(entry, now)) clearSecret(&entry, sizeof(entry));
    if (valid && entry.occupied && equal(decoded, entry.token, sizeof(decoded))
        && (!mutation || equal(decodedCsrf, entry.csrf, sizeof(decodedCsrf)))) {
      accepted = true;
      if (touch) entry.accessed = now;
    }
  }
  clearSecret(decoded, sizeof(decoded));
  clearSecret(decodedCsrf, sizeof(decodedCsrf));
  return accepted;
}
bool Sessions::logout(const char* token) {
  uint8_t decoded[SESSION_TOKEN_BYTES] = {};
  if (!unhex(token, decoded)) { clearSecret(decoded, sizeof(decoded)); return false; }
  bool removed = false;
  for (Entry& entry : entries) {
    if (entry.occupied && equal(decoded, entry.token, sizeof(decoded))) {
      clearSecret(&entry, sizeof(entry));
      removed = true;
    }
  }
  clearSecret(decoded, sizeof(decoded));
  return removed;
}
bool Sessions::csrfFor(const char* token, char (&csrf)[SESSION_TOKEN_CHARS + 1], uint32_t now) {
  clearSecret(csrf, sizeof(csrf));
  uint8_t decoded[SESSION_TOKEN_BYTES] = {};
  bool valid = unhex(token, decoded), found = false;
  for (Entry& entry : entries) {
    if (entry.occupied && expired(entry, now)) clearSecret(&entry, sizeof(entry));
    if (valid && entry.occupied && equal(decoded, entry.token, sizeof(decoded))) {
      hex(entry.csrf, csrf);
      found = true;
    }
  }
  clearSecret(decoded, sizeof(decoded));
  return found;
}
void Sessions::clear() { clearSecret(entries, sizeof(entries)); }
bool LoginLimiter::admit(uint32_t now) {
  if (!attempts || uint32_t(now - started) >= 60000) { started = now; attempts = 0; }
  if (attempts >= 3) return false;
  ++attempts;
  return true;
}
uint32_t LoginLimiter::retryAfterMs(uint32_t now) const {
  uint32_t elapsed = uint32_t(now - started);
  return attempts < 3 || elapsed >= 60000 ? 0 : 60000 - elapsed;
}
} // namespace appliance
