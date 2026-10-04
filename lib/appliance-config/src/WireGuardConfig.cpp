#include "WireGuardConfig.h"
#include <ctype.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

namespace appliance {
namespace {
enum Field : uint32_t {
  Private = 1, Address = 2, DNS = 4, MTU = 8, Listen = 16,
  Public = 32, PSK = 64, Endpoint = 128, Allowed = 256, Keepalive = 512
};
constexpr uint32_t REQUIRED = Private | Address | Public | Endpoint | Allowed;
char* trim(char* value) {
  while (*value == ' ' || *value == '\t' || *value == '\r') ++value;
  char* end = value + strlen(value);
  while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
  return value;
}
bool equal(const char* a, const char* b) {
  while (*a && *b) {
    if (tolower(static_cast<unsigned char>(*a++)) != tolower(static_cast<unsigned char>(*b++))) return false;
  }
  return *a == *b;
}
template<size_t N> bool copy(char (&target)[N], const char* value) {
  size_t length = strlen(value);
  if (length >= N) return false;
  memset(target, 0, N);
  memcpy(target, value, length);
  return true;
}
bool number(const char* text, unsigned maximum, unsigned& value) {
  if (!text[0] || strlen(text) > 5) return false;
  for (const char* p = text; *p; ++p) if (*p < '0' || *p > '9') return false;
  value = strtoul(text, nullptr, 10);
  return value <= maximum;
}
// Normalize IPv4 networks, but retain host bits for the local interface address.
bool network(char* token, bool localAddress, char (&output)[44], bool& ipv6, unsigned& prefix) {
  char* slash = strchr(token, '/');
  if (!slash || strchr(slash + 1, '/')) return false;
  *slash++ = 0;
  ipv6 = strchr(token, ':') != nullptr;
  if (!number(slash, ipv6 ? 128 : 32, prefix)) return false;
  if (ipv6) {
    struct in6_addr address;
    return inet_pton(AF_INET6, token, &address) == 1;
  }
  struct in_addr address;
  if (inet_pton(AF_INET, token, &address) != 1) return false;
  uint32_t host = ntohl(address.s_addr);
  if (!localAddress) host &= prefix ? (0xffffffffu << (32 - prefix)) : 0;
  snprintf(output, sizeof(output), "%u.%u.%u.%u/%u", unsigned(host >> 24),
           unsigned((host >> 16) & 255), unsigned((host >> 8) & 255), unsigned(host & 255), prefix);
  return true;
}
ImportError list(char* value, bool addresses, Config& config, ImportSummary& summary) {
  char* start = value;
  unsigned ipv4Count = 0;
  do {
    char* comma = strchr(start, ',');
    if (comma) *comma = 0;
    char canonical[44] = {};
    bool ipv6;
    unsigned prefix;
    if (!network(trim(start), addresses, canonical, ipv6, prefix)) return ImportError::InvalidValue;
    if (ipv6) {
      if (addresses) ++summary.excludedIPv6Addresses;
      else ++summary.excludedIPv6Networks;
    } else {
      ++ipv4Count;
      if (addresses) {
        if (ipv4Count > 1) return ImportError::TooManyNetworks;
        if (!copy(config.wg.address, canonical)) return ImportError::InvalidValue;
      } else {
        if (ipv4Count > 8) return ImportError::TooManyNetworks;
        size_t used = strlen(config.wg.allowedIPs);
        if (used + strlen(canonical) + (used ? 1 : 0) >= sizeof(config.wg.allowedIPs)) return ImportError::TooManyNetworks;
        if (used) strcat(config.wg.allowedIPs, ",");
        strcat(config.wg.allowedIPs, canonical);
        if (!prefix) summary.fullTunnelRequested = true;
      }
    }
    if (!comma) break;
    start = comma + 1;
    if (!*start) return ImportError::InvalidValue;
  } while (*start);
  if (!ipv4Count) return ImportError::InvalidValue;
  if (!addresses) summary.ipv4Networks = ipv4Count;
  return ImportError::Ok;
}
} // namespace

const char* importErrorName(ImportError error) {
  switch (error) {
    case ImportError::Ok: return "ok";
    case ImportError::TooLarge: return "config_exceeds_4096_bytes";
    case ImportError::InvalidText: return "invalid_config_text";
    case ImportError::Section: return "requires_one_interface_and_one_peer";
    case ImportError::Duplicate: return "duplicate_section_or_field";
    case ImportError::UnsupportedField: return "unsupported_config_field";
    case ImportError::MissingField: return "required_field_missing";
    case ImportError::InvalidValue: return "invalid_config_value";
    case ImportError::UnsupportedListenPort: return "listen_port_must_be_51820";
    case ImportError::TooManyNetworks: return "too_many_ipv4_networks";
  }
  return "unknown_import_error";
}

ImportError importWireGuard(const char* input, size_t length, Config& target, ImportSummary& summary) {
  summary = {};
  if (length > 4096) return ImportError::TooLarge;
  if (!input || !length || memchr(input, 0, length)) return ImportError::InvalidText;
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[length + 1]);
  if (!buffer) return ImportError::TooLarge;
  // Wipe transient private key text on every return path.
  struct Wipe { char* bytes; size_t count; ~Wipe() {
    volatile char* p = bytes; while (count--) *p++ = 0;
  }} wipe{buffer.get(), length + 1};
  memcpy(buffer.get(), input, length); buffer[length] = 0;
  Config parsed = defaults();
  Wipe wipeParsed{reinterpret_cast<char*>(&parsed), sizeof(parsed)};
  parsed.wg.enabled = 1;
  // Phase WG-A is remote access only; WG-B is enabled separately after tests.
  parsed.wg.fullTunnel = 0;
  enum Section { None, Interface, Peer } section = None;
  unsigned sections = 0;
  uint32_t fields = 0;
  char* line = buffer.get();
  if (length >= 3 && static_cast<uint8_t>(line[0]) == 0xef
      && static_cast<uint8_t>(line[1]) == 0xbb && static_cast<uint8_t>(line[2]) == 0xbf) line += 3;
  while (line) {
    ++summary.errorLine;
    char* next = strchr(line, '\n');
    if (next) *next++ = 0;
    char* comment = strchr(line, '#');
    if (comment) *comment = 0;
    char* content = trim(line);
    if (!*content || *content == ';') { line = next; continue; }
    for (char* p = content; *p; ++p)
      if ((static_cast<uint8_t>(*p) < 32 && *p != '\t') || *p == 127) return ImportError::InvalidText;
    if (*content == '[') {
      if (equal(content, "[Interface]")) {
        if (sections & 1) return ImportError::Duplicate;
        sections |= 1; section = Interface;
      } else if (equal(content, "[Peer]")) {
        if (sections & 2) return ImportError::Duplicate;
        sections |= 2; section = Peer;
      } else return ImportError::Section;
      line = next; continue;
    }
    if (section == None) return ImportError::Section;
    char* equals = strchr(content, '=');
    if (!equals) return ImportError::InvalidText;
    *equals++ = 0;
    char* key = trim(content);
    char* value = trim(equals);
    Field field;
    if (section == Interface && equal(key, "PrivateKey")) field = Private;
    else if (section == Interface && equal(key, "Address")) field = Address;
    else if (section == Interface && equal(key, "DNS")) field = DNS;
    else if (section == Interface && equal(key, "MTU")) field = MTU;
    else if (section == Interface && equal(key, "ListenPort")) field = Listen;
    else if (section == Peer && equal(key, "PublicKey")) field = Public;
    else if (section == Peer && equal(key, "PresharedKey")) field = PSK;
    else if (section == Peer && equal(key, "Endpoint")) field = Endpoint;
    else if (section == Peer && equal(key, "AllowedIPs")) field = Allowed;
    else if (section == Peer && equal(key, "PersistentKeepalive")) field = Keepalive;
    else return ImportError::UnsupportedField;
    if (fields & field) return ImportError::Duplicate;
    fields |= field;
    unsigned numeric;
    bool valid = true;
    switch (field) {
      case Private: valid = copy(parsed.wg.privateKey, value); break;
      case Public: valid = copy(parsed.wg.publicKey, value); break;
      case PSK: valid = copy(parsed.wg.presharedKey, value); break;
      case DNS: valid = copy(parsed.wg.dns, value); break;
      case MTU:
        valid = number(value, 1500, numeric) && numeric >= 576;
        if (valid) parsed.wg.mtu = numeric;
        break;
      case Listen:
        if (!number(value, 65535, numeric) || numeric != 51820) return ImportError::UnsupportedListenPort;
        break;
      case Keepalive:
        valid = number(value, 65535, numeric);
        if (valid) parsed.wg.keepalive = numeric;
        break;
      case Endpoint: {
        char* colon = strrchr(value, ':');
        if (!colon) return ImportError::InvalidValue;
        *colon++ = 0;
        valid = number(colon, 65535, numeric) && numeric && copy(parsed.wg.endpoint, value);
        if (valid) parsed.wg.port = numeric;
        break;
      }
      case Address: case Allowed: {
        ImportError error = list(value, field == Address, parsed, summary);
        if (error != ImportError::Ok) return error;
        break;
      }
    }
    if (!valid) return ImportError::InvalidValue;
    line = next;
  }
  if (sections != 3) return ImportError::Section;
  if ((fields & REQUIRED) != REQUIRED) return ImportError::MissingField;
  // Validate WG fields independently of an as-yet incomplete onboarding form.
  strcpy(parsed.wifiSSID, "pending-wifi");
  if (validate(parsed) != Error::Ok) return ImportError::InvalidValue;
  target.wg = parsed.wg;
  summary.errorLine = 0;
  return ImportError::Ok;
}
} // namespace appliance
