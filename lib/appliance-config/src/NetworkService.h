#pragma once
#include "HttpAuth.h"
#include "WifiDriver.h"
#if defined(XIAO_APPLIANCE)
#include "WireGuardDriver.h"
#endif

namespace appliance {
// Lifetime owner for the product's config and Wi-Fi manager. HTTP exchanges
// bounded commands/snapshots under a mutex; never mutates NVS or Wi-Fi itself.
class NetworkService {
public:
  NetworkService(HttpAuth& auth, const char* apName) : auth(auth), driver(apName), manager(driver) {}
  bool begin(void (*connected)() = nullptr);
  bool registerEndpoints(httpd_handle_t server);
  esp_err_t bootstrap(httpd_req_t* request);
  NetworkStatus status();
  bool busy();
  // Only the BLE owner calls this after authenticated encrypted pairing.
  bool submitAuthenticatedBleWifi(const char* ssid,const char* password);
  bool requestFactoryReset();
#if defined(XIAO_APPLIANCE)
  WireGuardStatus wireguardStatus();
#endif
private:
  enum class Command { None, Enroll, Wifi, Confirm, Cancel, FactoryReset, WGImport, WGDisable, WGClear, WGRestart, WGProbe, WGMode
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
    , WGPolicyFixture
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
    , WGFullStart, WGFullStop, WGFullHold
#endif
  };
  enum class JobState { Idle, Hashing, Applying, Connecting, AwaitingConfirmation, Active, Failed };
  HttpAuth& auth;
#if defined(XIAO_APPLIANCE)
  WireGuardDriver driver;
  WireGuardStatus wgSnapshot;
  char importText[4097] = {};
  size_t importBytes = 0;
  const char* importError = "none";
  unsigned importedIPv6 = 0;
  bool importedDefault = false;
  bool fullModeCandidate = false;
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  char fixtureAction[16]={},fixtureHostname[254]={};
  static esp_err_t policyFixture(httpd_req_t* request);
#endif
#else
  WifiDriver driver;
#endif
  NetworkManager manager;
  SemaphoreHandle_t mutex = nullptr, initialized = nullptr;
  TaskHandle_t task = nullptr;
  void (*connected)() = nullptr;
  NetworkStatus snapshot;
  char nonce[65] = {}, ticket[65] = {}, password[129] = {};
  Config candidate = {};
  Command command = Command::None;
  JobState jobState = JobState::Idle;
  uint32_t submitted = 0, expectedRevision = 0, localAddress = 0, applyNotBefore = 0;
  bool setupJob = false, processing = false, accountBusy = false;
  bool bleWifiJob=false;
  NetworkError jobError = NetworkError::None;
  static void worker(void* context);
  static esp_err_t submit(httpd_req_t* request);
  static esp_err_t result(httpd_req_t* request);
  static esp_err_t confirm(httpd_req_t* request);
  static esp_err_t cancel(httpd_req_t* request);
  static esp_err_t networkStatus(httpd_req_t* request);
  static esp_err_t account(httpd_req_t* request);
  static Error saveAdmin(void* context,const Config& verified,uint32_t revision);
#if defined(XIAO_APPLIANCE)
  static esp_err_t wireguard(httpd_req_t* request);
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  static esp_err_t fullTest(httpd_req_t* request);
  bool fullTestActive = false;
#endif
#endif
  bool setupAuthorized(httpd_req_t* request, bool initial);
  bool testBusy() const {
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
    return fullTestActive;
#else
    return false;
#endif
  }
  esp_err_t sendStatus(httpd_req_t* request, bool job);
};
} // namespace appliance
