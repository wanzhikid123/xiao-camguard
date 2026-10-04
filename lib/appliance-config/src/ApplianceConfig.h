#pragma once
#include <Preferences.h>
#include <stdint.h>

namespace appliance {

constexpr uint16_t CONFIG_VERSION = 1;

// Internal configuration. Never serialize this struct to an HTTP status API.
// NVS is persistent storage, not encrypted or hardware-secure secret storage.
struct Config {
  char deviceName[33];
  char wifiSSID[33];
  char wifiPassword[65];
  struct {
    uint8_t enabled;
    uint8_t fullTunnel;
    char address[44];                 // IPv4/CIDR
    char privateKey[45];
    char publicKey[45];
    char presharedKey[45];
    char endpoint[254];               // hostname or IPv4, without port
    char allowedIPs[256];             // at most eight comma-separated IPv4 CIDRs
    char dns[16];                     // optional IPv4 resolver
    uint16_t port;
    uint16_t keepalive;
    uint16_t mtu;
  } wg;
  struct {
    uint8_t configured;
    uint8_t salt[16];
    uint8_t verifier[32];
    uint32_t iterations;
  } admin;
  uint32_t minimumFreeMB;
  uint8_t autoDelete;
  uint8_t legacyImported;
};

Config defaults();

enum class Error {
  Ok, InvalidConfig, Storage, NoPending, NoActive, AlreadyTried,
  StaleRevision, ProofMissing, Busy, UnsupportedVersion, NoLegacy
};
enum class BootState { Setup, Ready, TrialInterrupted, RecoveredBank, Corrupt, UnsupportedVersion };
const char* errorName(Error error);
const char* bootStateName(BootState state);
Error validate(const Config& config);

struct ConnectivityProof {
  bool localAccess = false;
  bool wifiConnected = false;
  bool peerSessionUp = false;
  bool fullTunnelVerified = false;
  bool authenticatedBleWifi = false;
};

// An explicit allowlist of public fields: it contains no key/password/verifier.
struct MaskedStatus {
  uint16_t cfgVer = CONFIG_VERSION;
  BootState bootState = BootState::Setup;
  uint32_t activeRevision = 0;
  uint32_t pendingRevision = 0;
  bool hasActive = false;
  bool hasPending = false;
  bool pendingTried = false;
  bool wifiPasswordSet = false;
  bool wgPrivateKeySet = false;
  bool wgPresharedKeySet = false;
  bool adminConfigured = false;
  bool wgEnabled = false;
  bool fullTunnel = false;
  bool legacyImported = false;
  char deviceName[33] = {};
  char wifiSSID[33] = {};
};

// One owning task must serialize access to this store. Each Preferences write
// commits through NVS. Promotion writes/readbacks an inactive bank, then writes
// the selector last. Reset during a trial never retries that candidate on boot.
class ConfigStore {
public:
  ~ConfigStore() { close(); }
  ConfigStore() = default;
  ConfigStore(const ConfigStore&) = delete;
  ConfigStore& operator=(const ConfigStore&) = delete;
  Error open(const char* nameSpace = "camcfg");
  void close();
  MaskedStatus status() const;
  Error loadActive(Config& out) const;             // Internal network/auth consumers only.
  Error stage(const Config& candidate, uint32_t& revision);
  Error beginTrial(Config& out, uint32_t& revision);
  Error confirmTrial(uint32_t revision, const ConnectivityProof& proof);
  Error discardPending();
  // Atomic verifier-only update. Requires the selected revision and no trial;
  // does not claim network proof or change any network/capture configuration.
  Error updateAdmin(const Config& verified, uint32_t expectedRevision);
  // Imports only bounded Wi-Fi values into pending. Does not clear legacy NVS
  // or copy its plaintext admin password. New admin enrollment is required.
  Error importLegacy(const char* legacyNamespace = "ESP-CAM_MJPEG");

private:
  struct Record {
    uint32_t magic;
    uint16_t version;
    uint16_t bytes;
    uint32_t revision;
    Config config;
    uint32_t crc;
  };
  enum class Read { Missing, Valid, Corrupt, Future };
  Read readRecord(const char* key, Record& out);
  bool writeRecord(const char* key, const Record& record);
  static Record makeRecord(const Config& config, uint32_t revision);
  Preferences prefs;
  Record active = {};
  Record pending = {};
  BootState state = BootState::Setup;
  uint32_t maximumRevision = 0;
  uint32_t triedRevision = 0;
  uint8_t activeBank = 0;
  bool opened = false;
  bool hasActive = false;
  bool hasPending = false;
  bool trialLive = false;
  bool writeBlocked = false;
};

} // namespace appliance
