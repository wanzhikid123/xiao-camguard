#pragma once
#if defined(XIAO_APPLIANCE)
#include "WifiDriver.h"
#include "WireGuardRoutes.h"
#include "FullTunnelPolicy.h"
#include "PublicRouteProbe.h"
#include <WireGuard-ESP32.h>
#include <atomic>
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

namespace appliance {
struct WireGuardStatus {
  const char* phase = "disabled";
  const char* error = "none";
  bool enabled = false, running = false, peerSessionUp = false;
  bool fullTunnelVerified = false;
  bool fullMode = false, guardReady = false, routeProbeBusy = false;
  uint32_t ipv4Blocked = 0, ipv6Blocked = 0, bootstrapDNSPackets = 0, bootstrapTimePackets = 0;
  uint32_t verificationAgeSeconds = 0;
  int routeProbeHTTP = 0;
  char publicExit[16] = {};
  uint32_t sentBytes = 0, receivedBytes = 0;
  char address[16] = {};
  bool probeBusy = false;
  uint32_t probeSent = 0, probeReceived = 0;
  char probeAddress[16] = {};
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  bool fixtureClockReset=false;
#endif
};
// The NetworkService task owns every driver/lifecycle operation. The only
// cross-task write is the bounded DNS callback's atomic result mailbox.
class WireGuardDriver final : public NetworkDriver {
public:
  explicit WireGuardDriver(const char* apName) : wifi(apName) {}
  bool supports(const Config& config) const override;
  void apply(const Config& config) override;
  void tick(uint32_t now) override;
  NetworkReadiness readiness() const override;
  bool rescueAP(bool enabled) override { return wifi.rescueAP(enabled); }
  void stop() override;
  WireGuardStatus status() const {
    auto copy=observed;
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
    copy.fixtureClockReset=fixtureClockReset;
#endif
    return copy;
  }
  bool probe(uint32_t address);
  void restart(); // Owner-only, keeps Wi-Fi and the selected config intact.
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  void policyFixture(const char* action,const char* hostname);
  PublicRouteResult policyFixtureProbe() const { return fixtureProbe.result(); }
  bool fixtureClockReset=false;
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  bool fullTestStart(uint32_t resolver);
  void fullTestStop();
  void fullTestHold();
#endif
private:
  WifiDriver wifi;
  WireGuard tunnel;
  Config wanted = {};
  WireGuardStatus observed;
  IPv4Route allowed[8], routes[9], address;
  size_t allowedCount = 0, routeCount = 0;
  bool haveConfig = false, attempted = false;
  uint32_t generation = 0, retryAt = 0, lanAddress = 0, resolved = 0, startedAt = 0;
  uint32_t resolvedAt = 0, timeAttemptAt = 0, fullProbeAt = 0, fullProofAt = 0;
  uint32_t fullProbeTX = 0, fullProbeRX = 0;
  bool fullProbeAttempted = false, fullProbeHandled = true;
  PublicRouteProbe fullProbe;
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  PublicRouteProbe fixtureProbe;
  uint32_t fixtureHoldUntil=0,fixtureBadUntil=0,fixtureBootstrapUntil=0;
  bool fixtureWifiHold=false;
  char fixtureEndpoint[254]={};
#endif
  void observeFullTunnel(uint32_t now);
  void configureBootstrapTime();
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  uint32_t fullTestUntil = 0, fullTestHoldUntil = 0;
#endif
  struct DNSJob {
    WireGuardDriver* owner = nullptr;
    uint32_t generation = 0, submitted = 0;
    bool pending = false;
    std::atomic<bool> done {false};
    std::atomic<uint32_t> address {0};
  } dns;
  esp_ping_handle_t ping = nullptr;
  struct Probe {
    uint32_t target = 0;
    std::atomic<uint32_t> sent {0}, received {0};
    std::atomic<bool> done {false};
  } echo;
  static void pingReply(esp_ping_handle_t handle, void* context);
  static void pingEnd(esp_ping_handle_t handle, void* context);
  static void resolvedDNS(const char* name, const ip_addr_t* address, void* context);
  bool startDNS(uint32_t now);
  bool parseNetworks();
  void stopTunnel();
};
} // namespace appliance
#endif
