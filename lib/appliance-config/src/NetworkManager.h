#pragma once
#include "ApplianceConfig.h"

namespace appliance {

// One task owns this manager, its store and the driver. HTTP/UI adapters must
// queue commands to that owner; never call it concurrently or supply invented
// connectivity proof. Driver operations must return promptly, without waits.
struct NetworkReadiness {
  bool wifiConnected = false;
  bool peerSessionUp = false;
  bool fullTunnelVerified = false;
};
class NetworkDriver {
public:
  virtual ~NetworkDriver() = default;
  virtual bool supports(const Config& config) const = 0;
  virtual void apply(const Config& config) = 0;
  virtual void tick(uint32_t now) {}
  virtual NetworkReadiness readiness() const = 0;
  virtual bool rescueAP(bool enabled) = 0;
  virtual void stop() = 0;
};
enum class NetworkState { Stopped, Setup, Connecting, Ready, Rescue, Trial, TrialReady, StorageFault };
enum class NetworkError { None, InvalidConfig, UnsupportedNetwork, Busy, Storage, NotTrial,
                          StaleRevision, ProofMissing, ConnectTimeout, ConfirmationTimeout, Interrupted, AccessPoint };
const char* networkStateName(NetworkState state);
const char* networkErrorName(NetworkError error);
struct NetworkStatus {
  NetworkState state = NetworkState::Stopped;
  NetworkError error = NetworkError::None;
  MaskedStatus config;
  NetworkReadiness readiness;
  bool rescueAP = false;
  uint32_t trialRevision = 0;
};

class NetworkManager {
public:
  explicit NetworkManager(NetworkDriver& driver) : driver(driver) {}
  ~NetworkManager();
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;
  static constexpr uint32_t WIFI_TIMEOUT_MS = 60000;
  static constexpr uint32_t TRIAL_TIMEOUT_MS = 90000;
  static constexpr uint32_t RETRY_MS = 15000;
  static constexpr uint32_t STABLE_MS = 3000;
  Error begin(uint32_t now, const char* nameSpace = "camcfg");
  void stop();
  void tick(uint32_t now);
  NetworkError submit(const Config& candidate, uint32_t now, uint32_t& revision);
  // Invoke only after a real authorized HTTP request reaches the candidate's
  // LAN address. AP access alone does not prove the new Wi-Fi is usable.
  NetworkError confirmLocalAccess(uint32_t revision, uint32_t now);
  NetworkError confirmAuthenticatedBleWifi(uint32_t revision,uint32_t now);
  NetworkError cancelTrial(uint32_t now);
  NetworkStatus status() const;
  Error loadActive(Config& out) const { return store.loadActive(out); }
  Error updateAdmin(const Config& verified, uint32_t expectedRevision);
private:
  NetworkDriver& driver;
  ConfigStore store;
  Config active = {}, trial = {};
  NetworkState state = NetworkState::Stopped;
  NetworkError error = NetworkError::None;
  uint32_t started = 0, retried = 0, stableAt = 0, revision = 0;
  bool ap = false, hasActive = false, trialLive = false, stable = false, writable = false;
  bool wifiStable=false;uint32_t wifiStableAt=0;
  void setAP(bool enabled);
  void connect(uint32_t now);
  void rollback(uint32_t now, NetworkError reason);
  bool ready(const Config& config, const NetworkReadiness& proof) const;
};
} // namespace appliance
