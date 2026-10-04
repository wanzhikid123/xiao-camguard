#include "ApplianceConfig.h"
#include "AdminAuth.h"
#include <algorithm>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>
#include <memory>
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"

namespace appliance {
namespace {
constexpr uint32_t MAGIC = 0x43464731;
constexpr size_t MAX_RECORD_BYTES = 2048;
const char* bankKey(uint8_t bank) { return bank ? "active1" : "active0"; }

uint32_t crc32(const void* data, size_t size) {
  uint32_t crc = 0xffffffff;
  const auto* bytes = static_cast<const uint8_t*>(data);
  while (size--) {
    crc ^= *bytes++;
    for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

template<size_t N> bool text(const char (&value)[N]) {
  const char* end = static_cast<const char*>(memchr(value, 0, N));
  if (!end) return false;
  for (const char* p = value; p < end; ++p) if (static_cast<uint8_t>(*p) < 32 || *p == 127) return false;
  return true;
}
template<size_t N> void copyText(char (&target)[N], const char (&source)[N]) {
  memcpy(target, source, strlen(source) + 1);
}
bool anyByte(const uint8_t* value, size_t size) {
  uint8_t result = 0;
  for (size_t i = 0; i < size; ++i) result |= value[i];
  return result != 0;
}
bool keyValid(const char* value, bool optional) {
  if (!value[0]) return optional;
  if (strlen(value) != 44 || value[43] != '=') return false;
  for (size_t i = 0; i < 43; ++i) {
    char c = value[i];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '+' || c == '/')) return false;
  }
  uint8_t decoded[32] = {};
  size_t size = 0;
  bool ok = mbedtls_base64_decode(decoded, sizeof(decoded), &size,
                                  reinterpret_cast<const uint8_t*>(value), 44) == 0
            && size == 32 && anyByte(decoded, size);
  volatile uint8_t* wipe = decoded;
  for (size_t i = 0; i < sizeof(decoded); ++i) wipe[i] = 0;
  return ok;
}
bool ipv4(const char* value) {
  struct in_addr address;
  return inet_pton(AF_INET, value, &address) == 1;
}
bool cidr(const char* value, size_t length) {
  if (!length || length >= 44) return false;
  char buffer[44] = {};
  memcpy(buffer, value, length);
  char* slash = strchr(buffer, '/');
  if (!slash || slash == buffer || !slash[1]) return false;
  *slash++ = 0;
  if (!ipv4(buffer)) return false;
  for (char* p = slash; *p; ++p) if (*p < '0' || *p > '9') return false;
  char* end = nullptr;
  unsigned long bits = strtoul(slash, &end, 10);
  return end && !*end && bits <= 32;
}
bool allowedNetworks(const char* value) {
  unsigned count = 0;
  const char* start = value;
  do {
    const char* comma = strchr(start, ',');
    const char* end = comma ? comma : start + strlen(start);
    while (start < end && *start == ' ') ++start;
    while (end > start && end[-1] == ' ') --end;
    if (++count > 8 || !cidr(start, end - start)) return false;
    if (!comma) return true;
    start = comma + 1;
  } while (*start);
  return false;
}
bool endpointValid(const char* value) {
  if (ipv4(value)) return true;
  // ASCII DNS labels; IPv6 literals, URLs, ports and shell punctuation rejected.
  size_t label = 0;
  char previous = 0;
  for (const char* p = value; *p; ++p) {
    if (*p == '.') {
      if (!label || label > 63 || previous == '-') return false;
      label = 0;
    } else {
      char c = *p;
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-')) return false;
      if (!label && c == '-') return false;
      ++label;
    }
    previous = *p;
  }
  return label > 0 && label <= 63 && previous != '-';
}
bool namespaceValid(const char* value) {
  if (!value || !value[0] || strlen(value) > 15) return false;
  for (const char* p = value; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
  return true;
}
} // namespace

Config defaults() {
  Config config = {};
  strcpy(config.deviceName, "XIAO-Camera");
  config.wg.port = 51820;
  config.wg.keepalive = 25;
  config.wg.mtu = 1420;
  config.minimumFreeMB = 1024;
  return config;
}

Error validate(const Config& c) {
  static_assert(std::is_trivially_copyable<Config>::value, "NVS payload must remain fixed and bounded");
  if (!text(c.deviceName) || !c.deviceName[0] || !text(c.wifiSSID) || !text(c.wifiPassword)
      || !text(c.wg.address) || !text(c.wg.privateKey) || !text(c.wg.publicKey)
      || !text(c.wg.presharedKey) || !text(c.wg.endpoint) || !text(c.wg.allowedIPs) || !text(c.wg.dns)
      || c.wg.enabled > 1 || c.wg.fullTunnel > 1 || c.autoDelete > 1
      || c.legacyImported > 1 || c.admin.configured > 1 || c.minimumFreeMB > 1048576)
    return Error::InvalidConfig;
  size_t passwordBytes = strlen(c.wifiPassword);
  if ((!c.wifiSSID[0] && (passwordBytes || c.wg.enabled))
      || (passwordBytes && (passwordBytes < 8 || passwordBytes > 63))) return Error::InvalidConfig;
  if (c.wg.fullTunnel && !c.wg.enabled) return Error::InvalidConfig;
  if (!c.wg.port || c.wg.mtu < 576 || c.wg.mtu > 1500
      || !keyValid(c.wg.privateKey, !c.wg.enabled) || !keyValid(c.wg.publicKey, !c.wg.enabled)
      || !keyValid(c.wg.presharedKey, true)
      || ((c.wg.enabled || c.wg.address[0]) && !cidr(c.wg.address, strlen(c.wg.address)))
      || ((c.wg.enabled || c.wg.endpoint[0]) && !endpointValid(c.wg.endpoint))
      || ((c.wg.enabled || c.wg.allowedIPs[0]) && !allowedNetworks(c.wg.allowedIPs))
      || (c.wg.dns[0] && !ipv4(c.wg.dns))) return Error::InvalidConfig;
  if (c.admin.configured && (c.admin.iterations < 10000 || c.admin.iterations > 1000000
      || !anyByte(c.admin.salt, sizeof(c.admin.salt))
      || !anyByte(c.admin.verifier, sizeof(c.admin.verifier)))) return Error::InvalidConfig;
  return Error::Ok;
}

const char* errorName(Error error) {
  switch (error) {
    case Error::Ok: return "ok";
    case Error::InvalidConfig: return "invalid_config";
    case Error::Storage: return "nvs_write_or_read_failed";
    case Error::NoPending: return "no_pending";
    case Error::NoActive: return "no_active";
    case Error::AlreadyTried: return "trial_already_attempted";
    case Error::StaleRevision: return "stale_revision";
    case Error::ProofMissing: return "connectivity_not_validated";
    case Error::Busy: return "configuration_busy";
    case Error::UnsupportedVersion: return "unsupported_config_version";
    case Error::NoLegacy: return "no_legacy_wifi_configuration";
  }
  return "unknown_error";
}
const char* bootStateName(BootState value) {
  switch (value) {
    case BootState::Setup: return "setup_required";
    case BootState::Ready: return "ready";
    case BootState::TrialInterrupted: return "pending_trial_interrupted";
    case BootState::RecoveredBank: return "recovered_previous_bank";
    case BootState::Corrupt: return "configuration_corrupt";
    case BootState::UnsupportedVersion: return "unsupported_config_version";
  }
  return "unknown_state";
}

ConfigStore::Record ConfigStore::makeRecord(const Config& config, uint32_t revision) {
  Record record = {};
  record.magic = MAGIC;
  record.version = CONFIG_VERSION;
  record.bytes = sizeof(Record);
  record.revision = revision;
  // Canonicalize bounded strings so shorter replacements do not retain old
  // secret tails or caller padding inside a new payload.
  Config& c = record.config;
  copyText(c.deviceName, config.deviceName);
  copyText(c.wifiSSID, config.wifiSSID);
  copyText(c.wifiPassword, config.wifiPassword);
  copyText(c.wg.address, config.wg.address);
  copyText(c.wg.privateKey, config.wg.privateKey);
  copyText(c.wg.publicKey, config.wg.publicKey);
  copyText(c.wg.presharedKey, config.wg.presharedKey);
  copyText(c.wg.endpoint, config.wg.endpoint);
  copyText(c.wg.allowedIPs, config.wg.allowedIPs);
  copyText(c.wg.dns, config.wg.dns);
  c.wg.enabled = config.wg.enabled;
  c.wg.fullTunnel = config.wg.fullTunnel;
  c.wg.port = config.wg.port;
  c.wg.keepalive = config.wg.keepalive;
  c.wg.mtu = config.wg.mtu;
  c.admin.configured = config.admin.configured;
  if (c.admin.configured) {
    memcpy(c.admin.salt, config.admin.salt, sizeof(c.admin.salt));
    memcpy(c.admin.verifier, config.admin.verifier, sizeof(c.admin.verifier));
    c.admin.iterations = config.admin.iterations;
  }
  c.minimumFreeMB = config.minimumFreeMB;
  c.autoDelete = config.autoDelete;
  c.legacyImported = config.legacyImported;
  record.crc = crc32(&record, offsetof(Record, crc));
  return record;
}

ConfigStore::Read ConfigStore::readRecord(const char* key, Record& out) {
  static_assert(sizeof(Record) <= MAX_RECORD_BYTES, "Increase NVS budget before growing schema");
  if (!prefs.isKey(key)) return Read::Missing;
  size_t length = prefs.getBytesLength(key);
  if (!length) return Read::Missing;
  if (length > MAX_RECORD_BYTES) return Read::Future; // Preserve unknown larger formats.
  if (length < offsetof(Record, config)) return Read::Corrupt;
  if (length != sizeof(Record)) {
    // Unknown-length envelopes require a bounded heap buffer, never a 2 KB
    // temporary on an HTTP/Arduino task stack. Current schema reads in place.
    std::unique_ptr<uint8_t[]> bytes(new (std::nothrow) uint8_t[length]);
    if (!bytes || prefs.getBytes(key, bytes.get(), length) != length) return Read::Corrupt;
    uint32_t magic;
    uint16_t version;
    memcpy(&magic, bytes.get(), sizeof(magic));
    memcpy(&version, bytes.get() + sizeof(magic), sizeof(version));
    return magic == MAGIC && version != CONFIG_VERSION ? Read::Future : Read::Corrupt;
  }
  if (prefs.getBytes(key, &out, sizeof(out)) != sizeof(out) || out.magic != MAGIC) return Read::Corrupt;
  if (out.version != CONFIG_VERSION) return Read::Future;
  if (out.bytes != sizeof(out) || !out.revision || out.crc != crc32(&out, offsetof(Record, crc))
      || validate(out.config) != Error::Ok) return Read::Corrupt;
  return Read::Valid;
}

bool ConfigStore::writeRecord(const char* key, const Record& record) {
  if (prefs.putBytes(key, &record, sizeof(record)) != sizeof(record)) return false;
  Record check = {};
  return readRecord(key, check) == Read::Valid && check.revision == record.revision
         && check.crc == record.crc;
}

Error ConfigStore::open(const char* nameSpace) {
  close();
  if (!namespaceValid(nameSpace)) return Error::InvalidConfig;
  if (!prefs.begin(nameSpace, false)) return Error::Storage;
  opened = true;
  Record banks[2] = {};
  Read bankStates[2] = {readRecord("active0", banks[0]), readRecord("active1", banks[1])};
  bool selectorPresent = prefs.isKey("selected");
  uint8_t selector = prefs.getUChar("selected", 255);
  if (selectorPresent && selector < 2 && bankStates[selector] == Read::Future) {
    writeBlocked = true;
    state = BootState::UnsupportedVersion;
  } else if (selectorPresent && selector < 2 && bankStates[selector] == Read::Valid) {
    active = banks[selector]; activeBank = selector; hasActive = true; state = BootState::Ready;
  } else if (selectorPresent) {
    // CRC/selector corruption: only use a bank previously written after proof.
    uint8_t other = selector < 2 ? 1 - selector :
      (bankStates[1] == Read::Valid && (bankStates[0] != Read::Valid ||
       banks[1].revision > banks[0].revision) ? 1 : 0);
    if (bankStates[other] == Read::Valid) {
      active = banks[other]; activeBank = other; hasActive = true; state = BootState::RecoveredBank;
    } else state = BootState::Corrupt;
  }
  for (unsigned i = 0; i < 2; ++i) {
    if (bankStates[i] == Read::Valid) maximumRevision = std::max(maximumRevision, banks[i].revision);
    if (bankStates[i] == Read::Future) writeBlocked = true;
  }
  Read pendingState = readRecord("pending", pending);
  if (pendingState == Read::Valid) {
    maximumRevision = std::max(maximumRevision, pending.revision);
    hasPending = !hasActive || pending.revision > active.revision;
  } else if (pendingState == Read::Future) writeBlocked = true;
  else if (pendingState == Read::Corrupt && !hasActive) state = BootState::Corrupt;
  triedRevision = prefs.getUInt("tried", 0);
  maximumRevision = std::max(maximumRevision, triedRevision);
  maximumRevision = std::max(maximumRevision, prefs.getUInt("revision", 0));
  if (hasPending && triedRevision == pending.revision) state = BootState::TrialInterrupted;
  if (writeBlocked) state = BootState::UnsupportedVersion;
  return writeBlocked ? Error::UnsupportedVersion : Error::Ok;
}

void ConfigStore::close() {
  prefs.end();
  active = {}; pending = {};
  maximumRevision = triedRevision = 0;
  activeBank = 0;
  opened = hasActive = hasPending = trialLive = writeBlocked = false;
  state = BootState::Setup;
}

MaskedStatus ConfigStore::status() const {
  MaskedStatus result;
  result.bootState = state;
  result.hasActive = hasActive;
  result.hasPending = hasPending;
  result.activeRevision = hasActive ? active.revision : 0;
  result.pendingRevision = hasPending ? pending.revision : 0;
  result.pendingTried = hasPending && triedRevision == pending.revision;
  if (hasActive) {
    const Config& c = active.config;
    memcpy(result.deviceName, c.deviceName, sizeof(result.deviceName));
    memcpy(result.wifiSSID, c.wifiSSID, sizeof(result.wifiSSID));
    result.wifiPasswordSet = c.wifiPassword[0];
    result.wgPrivateKeySet = c.wg.privateKey[0];
    result.wgPresharedKeySet = c.wg.presharedKey[0];
    result.adminConfigured = c.admin.configured;
    result.wgEnabled = c.wg.enabled;
    result.fullTunnel = c.wg.fullTunnel;
    result.legacyImported = c.legacyImported;
  }
  return result;
}

Error ConfigStore::loadActive(Config& out) const {
  if (!hasActive) return Error::NoActive;
  out = active.config;
  return Error::Ok;
}

Error ConfigStore::stage(const Config& candidate, uint32_t& revision) {
  revision = 0;
  if (!opened) return Error::Storage;
  if (writeBlocked) return Error::UnsupportedVersion;
  if (trialLive) return Error::Busy;
  if (validate(candidate) != Error::Ok) return Error::InvalidConfig;
  if (maximumRevision == UINT32_MAX) return Error::Storage;
  Record next = makeRecord(candidate, maximumRevision + 1);
  // Reserve monotonically before staging, including discarded candidates.
  // This prevents an old confirmation token being reused after a reboot.
  if (prefs.putUInt("revision", next.revision) != sizeof(uint32_t)) return Error::Storage;
  maximumRevision = next.revision;
  if (prefs.getUInt("revision", 0) != next.revision) return Error::Storage;
  if (!writeRecord("pending", next)) return Error::Storage;
  pending = next; hasPending = true; maximumRevision = next.revision;
  revision = next.revision;
  state = hasActive ? BootState::Ready : BootState::Setup;
  return Error::Ok;
}

Error ConfigStore::beginTrial(Config& out, uint32_t& revision) {
  revision = 0;
  if (!opened) return Error::Storage;
  if (writeBlocked) return Error::UnsupportedVersion;
  if (!hasPending) return Error::NoPending;
  if (trialLive) return Error::Busy;
  if (triedRevision == pending.revision) return Error::AlreadyTried;
  // Persist before the caller changes Wi-Fi or routing.
  if (prefs.putUInt("tried", pending.revision) != sizeof(uint32_t)
      || prefs.getUInt("tried", 0) != pending.revision) return Error::Storage;
  triedRevision = pending.revision;
  trialLive = true;
  out = pending.config;
  revision = pending.revision;
  return Error::Ok;
}

Error ConfigStore::confirmTrial(uint32_t revision, const ConnectivityProof& proof) {
  if (!opened) return Error::Storage;
  if (writeBlocked) return Error::UnsupportedVersion;
  if (!hasPending) return Error::NoPending;
  if (!revision || revision != pending.revision) return Error::StaleRevision;
  if (!trialLive || triedRevision != revision) return Error::AlreadyTried;
  const Config& c = pending.config;
  if(proof.authenticatedBleWifi){
    // This proof permits Wi-Fi-only promotion. Admin and WG remain byte-identical.
    if(!hasActive)return Error::ProofMissing;
    Config before=active.config,after=c;
    memset(before.wifiSSID,0,sizeof(before.wifiSSID));memset(after.wifiSSID,0,sizeof(after.wifiSSID));
    memset(before.wifiPassword,0,sizeof(before.wifiPassword));memset(after.wifiPassword,0,sizeof(after.wifiPassword));
    bool same=!memcmp(&before,&after,sizeof(before));clearSecret(&before,sizeof(before));clearSecret(&after,sizeof(after));
    if(!same||!proof.wifiConnected)return Error::ProofMissing;
  }
  if (!proof.authenticatedBleWifi && (!proof.localAccess || (c.wifiSSID[0] && !proof.wifiConnected)
      || (c.wg.enabled && !proof.peerSessionUp) || (c.wg.fullTunnel && !proof.fullTunnelVerified)))
    return Error::ProofMissing;
  uint8_t nextBank = hasActive ? 1 - activeBank : 0;
  if (!writeRecord(bankKey(nextBank), pending)) return Error::Storage;
  // Commit point. A power loss before this write keeps the prior selected bank.
  if (prefs.putUChar("selected", nextBank) != sizeof(uint8_t)
      || prefs.getUChar("selected", 255) != nextBank) return Error::Storage;
  active = pending; activeBank = nextBank; hasActive = true;
  hasPending = trialLive = false;
  state = BootState::Ready;
  // Cleanup is optional after the commit; boot ignores pending == active.
  prefs.remove("pending");
  prefs.remove("tried");
  pending = {}; triedRevision = 0;
  return Error::Ok;
}

Error ConfigStore::updateAdmin(const Config& verified, uint32_t expectedRevision) {
  if(!opened)return Error::Storage;
  if(writeBlocked)return Error::UnsupportedVersion;
  if(!hasActive)return Error::NoActive;
  if(hasPending||trialLive)return Error::Busy;
  if(!expectedRevision||active.revision!=expectedRevision)return Error::StaleRevision;
  Config selected=active.config;selected.admin=verified.admin;
  if(selected.admin.configured!=1||validate(selected)!=Error::Ok){clearSecret(&selected,sizeof(selected));return Error::InvalidConfig;}
  if(maximumRevision==UINT32_MAX){clearSecret(&selected,sizeof(selected));return Error::Storage;}
  std::unique_ptr<Record> next(new(std::nothrow) Record);
  if(!next){clearSecret(&selected,sizeof(selected));return Error::Storage;}
  *next=makeRecord(selected,maximumRevision+1);clearSecret(&selected,sizeof(selected));
  const auto wipe=[&](){volatile uint8_t* p=reinterpret_cast<volatile uint8_t*>(next.get());for(size_t i=0;i<sizeof(Record);++i)p[i]=0;};
  if(prefs.putUInt("revision",next->revision)!=sizeof(uint32_t)){wipe();return Error::Storage;}
  maximumRevision=next->revision;
  const uint8_t target=1-activeBank;
  if(prefs.getUInt("revision",0)!=next->revision||!writeRecord(bankKey(target),*next)
    ||prefs.putUChar("selected",target)!=sizeof(uint8_t)||prefs.getUChar("selected",255)!=target){wipe();return Error::Storage;}
  active=*next;activeBank=target;state=BootState::Ready;wipe();return Error::Ok;
}

Error ConfigStore::discardPending() {
  if (!opened) return Error::Storage;
  if (writeBlocked) return Error::UnsupportedVersion;
  if (prefs.isKey("pending") && !prefs.remove("pending")) return Error::Storage;
  hasPending = trialLive = false; pending = {};
  if (prefs.isKey("tried")) prefs.remove("tried");
  triedRevision = 0;
  state = hasActive ? BootState::Ready : BootState::Setup;
  return Error::Ok;
}

Error ConfigStore::importLegacy(const char* legacyNamespace) {
  if (!opened) return Error::Storage;
  if (writeBlocked) return Error::UnsupportedVersion;
  if (hasActive || hasPending || trialLive) return Error::Busy;
  if (!namespaceValid(legacyNamespace)) return Error::InvalidConfig;
  Preferences legacy;
  if (!legacy.begin(legacyNamespace, true)) return Error::NoLegacy;
  String ssid = legacy.getString("ST_SSID", "");
  String password = legacy.getString("ST_Pass", "");
  legacy.end();
  if (!ssid.length()) return Error::NoLegacy;
  Config config = defaults();
  if (ssid.length() >= sizeof(config.wifiSSID) || password.length() >= sizeof(config.wifiPassword))
    return Error::InvalidConfig;
  memcpy(config.wifiSSID, ssid.c_str(), ssid.length() + 1);
  memcpy(config.wifiPassword, password.c_str(), password.length() + 1);
  config.legacyImported = 1;
  uint32_t revision;
  return stage(config, revision);
}
} // namespace appliance
