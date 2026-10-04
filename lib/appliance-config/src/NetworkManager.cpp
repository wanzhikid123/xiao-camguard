#include "NetworkManager.h"
#include "AdminAuth.h"

namespace appliance {
const char* networkStateName(NetworkState state) {
  switch (state) {
    case NetworkState::Stopped: return "stopped";
    case NetworkState::Setup: return "setup";
    case NetworkState::Connecting: return "connecting";
    case NetworkState::Ready: return "ready";
    case NetworkState::Rescue: return "rescue";
    case NetworkState::Trial: return "trial";
    case NetworkState::TrialReady: return "awaiting_local_confirmation";
    case NetworkState::StorageFault: return "storage_fault";
  }
  return "unknown";
}
const char* networkErrorName(NetworkError error) {
  switch (error) {
    case NetworkError::None: return "none";
    case NetworkError::InvalidConfig: return "invalid_config";
    case NetworkError::UnsupportedNetwork: return "unsupported_network";
    case NetworkError::Busy: return "busy";
    case NetworkError::Storage: return "storage";
    case NetworkError::NotTrial: return "not_trial";
    case NetworkError::StaleRevision: return "stale_revision";
    case NetworkError::ProofMissing: return "proof_missing";
    case NetworkError::ConnectTimeout: return "connect_timeout";
    case NetworkError::ConfirmationTimeout: return "confirmation_timeout";
    case NetworkError::Interrupted: return "interrupted";
    case NetworkError::AccessPoint: return "access_point_failed";
  }
  return "unknown";
}
NetworkManager::~NetworkManager() { clearSecret(&active, sizeof(active)); clearSecret(&trial, sizeof(trial)); }
void NetworkManager::setAP(bool enabled) {
  if (ap != enabled) {
    if (driver.rescueAP(enabled)) ap = enabled;
    else error = NetworkError::AccessPoint;
  }
}
void NetworkManager::connect(uint32_t now) {
  stable = false;
  started = retried = now;
  driver.apply(active);
  if (!hasActive || !active.wifiSSID[0]) {
    state = NetworkState::Setup;
    setAP(true);
  } else state = NetworkState::Connecting;
}
Error NetworkManager::begin(uint32_t now, const char* nameSpace) {
  stop();
  Error result = store.open(nameSpace);
  writable = result == Error::Ok;
  hasActive = store.loadActive(active) == Error::Ok;
  if (!hasActive) active = defaults();
  const bool interrupted = store.status().pendingTried;
  // Never retry an unconfirmed candidate after a reset. Even an untried
  // pending record needs a new explicit submission, not a surprise boot apply.
  if (writable && store.status().hasPending && store.discardPending() != Error::Ok) {
    writable = false; result = Error::Storage;
  }
  error = !writable ? NetworkError::Storage : interrupted ? NetworkError::Interrupted : NetworkError::None;
  connect(now);
  if (!writable) { state = NetworkState::StorageFault; setAP(true); }
  return result;
}
void NetworkManager::stop() {
  if (state != NetworkState::Stopped) { setAP(false); driver.stop(); }
  store.close();
  clearSecret(&active, sizeof(active)); clearSecret(&trial, sizeof(trial));
  state = NetworkState::Stopped; error = NetworkError::None;
  revision = 0; ap = hasActive = trialLive = stable = writable = false;
}
bool NetworkManager::ready(const Config& config, const NetworkReadiness& proof) const {
  return (!config.wifiSSID[0] || proof.wifiConnected)
    && (!config.wg.enabled || proof.peerSessionUp)
    && (!config.wg.fullTunnel || proof.fullTunnelVerified);
}
NetworkError NetworkManager::submit(const Config& candidate, uint32_t now, uint32_t& outRevision) {
  outRevision = 0;
  if (!writable) return NetworkError::Storage;
  if (trialLive) return NetworkError::Busy;
  if (validate(candidate) != Error::Ok || !candidate.wifiSSID[0]) return NetworkError::InvalidConfig;
  if (!driver.supports(candidate)) return NetworkError::UnsupportedNetwork;
  if (store.stage(candidate, outRevision) != Error::Ok) return NetworkError::Storage;
  if (store.beginTrial(trial, revision) != Error::Ok) {
    outRevision = 0; return NetworkError::Storage;
  }
  // The tried marker has been committed before any underlay/route mutation.
  trialLive = true; stable = wifiStable = false; started = retried = now;
  state = NetworkState::Trial; error = NetworkError::None;
  setAP(true);
  driver.apply(trial);
  return NetworkError::None;
}
void NetworkManager::rollback(uint32_t now, NetworkError reason) {
  const bool discarded = store.discardPending() == Error::Ok;
  clearSecret(&trial, sizeof(trial)); trialLive = false; revision = 0;
  error = discarded ? reason : NetworkError::Storage;
  if (!discarded) writable = false;
  setAP(true);
  connect(now);
  if (!writable) state = NetworkState::StorageFault;
}
NetworkError NetworkManager::cancelTrial(uint32_t now) {
  if (!trialLive) return NetworkError::NotTrial;
  rollback(now, NetworkError::None);
  return writable ? NetworkError::None : NetworkError::Storage;
}
void NetworkManager::tick(uint32_t now) {
  if (state == NetworkState::Stopped) return;
  driver.tick(now);
  if (state == NetworkState::Setup || state == NetworkState::StorageFault) {
    if (!ap && now - retried >= RETRY_MS) { setAP(true); retried = now; }
    return;
  }
  const NetworkReadiness proof = driver.readiness();
  if(proof.wifiConnected){if(!wifiStable){wifiStable=true;wifiStableAt=now;}}else wifiStable=false;
  const Config& wanted = trialLive ? trial : active;
  if (ready(wanted, proof)) {
    if (!stable) { stable = true; stableAt = now; }
    if (now - stableAt >= STABLE_MS) {
      state = trialLive ? NetworkState::TrialReady : NetworkState::Ready;
      if (!trialLive) setAP(false);
    }
  } else {
    stable = false;
    if (state == NetworkState::Ready) {
      connect(now); // Start a fresh 60-second loss/reconnect window.
      return;
    }
    if (trialLive) state = NetworkState::Trial;
    if (now - started >= WIFI_TIMEOUT_MS) {
      if (trialLive) { rollback(now, NetworkError::ConnectTimeout); return; }
      if (!proof.wifiConnected) { state = NetworkState::Rescue; setAP(true); }
    }
    // A broken active peer is repaired through the working LAN. It must not
    // keep an unnecessary open AP alive after the Wi-Fi underlay recovers.
    if (!trialLive && proof.wifiConnected && ap) {
      setAP(false);
      if (state == NetworkState::Rescue) state = NetworkState::Connecting;
    }
    if (now - retried >= RETRY_MS) { driver.apply(wanted); retried = now; }
  }
  if (trialLive && now - started >= TRIAL_TIMEOUT_MS)
    rollback(now, NetworkError::ConfirmationTimeout);
}
NetworkError NetworkManager::confirmLocalAccess(uint32_t expected, uint32_t now) {
  if (!trialLive) return NetworkError::NotTrial;
  if (!expected || expected != revision) return NetworkError::StaleRevision;
  if (now - started >= TRIAL_TIMEOUT_MS) {
    rollback(now, NetworkError::ConfirmationTimeout);
    return NetworkError::ConfirmationTimeout;
  }
  const NetworkReadiness proof = driver.readiness();
  if (state != NetworkState::TrialReady || !stable || now - stableAt < STABLE_MS || !ready(trial, proof))
    return NetworkError::ProofMissing;
  if (store.confirmTrial(revision, {true, proof.wifiConnected, proof.peerSessionUp, proof.fullTunnelVerified}) != Error::Ok)
    return NetworkError::Storage;
  active = trial; clearSecret(&trial, sizeof(trial));
  hasActive = true; trialLive = false; revision = 0;
  state = NetworkState::Ready; error = NetworkError::None;
  setAP(false);
  return NetworkError::None;
}
NetworkStatus NetworkManager::status() const {
  return {state, error, store.status(), driver.readiness(), ap, revision};
}
NetworkError NetworkManager::confirmAuthenticatedBleWifi(uint32_t expected,uint32_t now){
  if(!trialLive)return NetworkError::NotTrial;if(!expected||expected!=revision)return NetworkError::StaleRevision;
  if(now-started>=TRIAL_TIMEOUT_MS)return NetworkError::ConfirmationTimeout;
  const auto proof=driver.readiness();
  if(!proof.wifiConnected||!wifiStable||now-wifiStableAt<STABLE_MS)return NetworkError::ProofMissing;
  if(store.confirmTrial(revision,{false,true,false,false,true})!=Error::Ok)return NetworkError::Storage;
  active=trial;clearSecret(&trial,sizeof(trial));hasActive=true;trialLive=false;revision=0;
  state=ready(active,proof)?NetworkState::Ready:NetworkState::Connecting;error=NetworkError::None;setAP(false);return NetworkError::None;
}
Error NetworkManager::updateAdmin(const Config& verified,uint32_t expectedRevision) {
  if(!writable)return Error::Storage;
  if(trialLive)return Error::Busy;
  const Error result=store.updateAdmin(verified,expectedRevision);
  if(result==Error::Ok)active.admin=verified.admin;
  return result;
}
} // namespace appliance
