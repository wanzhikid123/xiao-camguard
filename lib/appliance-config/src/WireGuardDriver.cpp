#if defined(XIAO_APPLIANCE)
#include "WireGuardDriver.h"
#include "AdminAuth.h"
#include <WiFi.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/dns.h"
#include "lwip/tcpip.h"
#include "FullTunnelTest.h"
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
#include "esp_sntp.h"
#include <sys/time.h>
#endif

namespace appliance {
bool WireGuardDriver::supports(const Config& config) const {
  IPv4Route route;
  if (!config.wg.enabled) return !config.wg.fullTunnel;
  if (!parseIPv4Route(config.wg.address, route) || !route.mask) return false;
  if (!config.wg.fullTunnel) return true;
  IPAddress resolver;
  if (!config.wg.dns[0] || !resolver.fromString(config.wg.dns) || resolver.type()!=IPType::IPv4
    || config.wg.port==53 || config.wg.mtu>1420) return false;
  const uint32_t host=lwip_ntohl(uint32_t(resolver));
  if(!host || host==UINT32_MAX || (host>>24)==127 || (host>>28)>=14 || uint32_t(resolver)==route.address) return false;
  char networks[sizeof(config.wg.allowedIPs)];strlcpy(networks,config.wg.allowedIPs,sizeof(networks));
  char* saved=nullptr;
  for(char* item=strtok_r(networks,",",&saved);item;item=strtok_r(nullptr,",",&saved)) {
    while(*item==' ')++item;
    char* end=item+strlen(item);while(end>item && end[-1]==' ')*--end=0;
    if(parseIPv4Route(item,route) && !route.mask)return true;
  }
  return false;
}
void WireGuardDriver::stopTunnel() {
  if (ping) esp_ping_stop(ping);
  // The first apply precedes Wi-Fi/lwIP initialization. Lock the TCP/IP core
  // only when there is an interface to remove; clear routing before removal.
  if (tunnel.is_initialized()) {
    clearWireGuardRoutes();
    tunnel.end();
  }
  observed.running = observed.peerSessionUp = observed.fullTunnelVerified = false;
  observed.publicExit[0]=0;observed.verificationAgeSeconds=0;observed.routeProbeBusy=false;
  fullProofAt=0;fullProbeAttempted=false;fullProbeHandled=true;
  observed.sentBytes = observed.receivedBytes = 0;
}
void WireGuardDriver::apply(const Config& config) {
  const bool sameWifi = haveConfig && !strcmp(config.wifiSSID, wanted.wifiSSID)
    && !strcmp(config.wifiPassword, wanted.wifiPassword);
  const bool sameWG = haveConfig && !memcmp(&config.wg, &wanted.wg, sizeof(config.wg));
  if (!sameWifi || !sameWG) {
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
    if(!haveConfig && config.wg.fullTunnel) {
      esp_sntp_stop();timeval epoch={};settimeofday(&epoch,nullptr);fixtureClockReset=true;
    }
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
    fullTestStop(); // Preserve the guard when the manager reapplies the same config.
#endif
    if(config.wg.fullTunnel)requestFullTunnelPolicy();
    stopTunnel(); ++generation; resolved = 0; attempted = false;
    clearSecret(&wanted, sizeof(wanted));
    memcpy(wanted.wifiSSID, config.wifiSSID, sizeof(wanted.wifiSSID));
    memcpy(wanted.wifiPassword, config.wifiPassword, sizeof(wanted.wifiPassword));
    wanted.wg = config.wg; haveConfig = true;
    observed = {}; observed.enabled = wanted.wg.enabled;
    observed.fullMode = wanted.wg.fullTunnel;
    observed.phase = wanted.wg.enabled ? "waiting_wifi" : "disabled";
    if (wanted.wg.enabled && !parseNetworks()) observed.error = "invalid_routes";
    if(wanted.wg.fullTunnel) {
      wifi.prepare();IPAddress resolver;resolver.fromString(wanted.wg.dns);
      if(!configureFullTunnelPolicy(wanted.wg.endpoint,wanted.wg.port,uint32_t(resolver)))observed.error="full_tunnel_guard_unavailable";
    } else endFullTunnelPolicy();
    resolvedAt=timeAttemptAt=0;
  }
  // A WG-only change or a peer retry keeps a working Wi-Fi/camera/LAN session.
  if (!sameWifi || !wifi.readiness().wifiConnected) wifi.apply(config);
}
bool WireGuardDriver::parseNetworks() {
  allowedCount = routeCount = 0;
  if (!parseIPv4Route(wanted.wg.address, address) || !address.mask) return false;
  IPAddress ip(address.address); strlcpy(observed.address, ip.toString().c_str(), sizeof(observed.address));
  routes[routeCount++] = address;
  char networks[sizeof(wanted.wg.allowedIPs)]; strlcpy(networks, wanted.wg.allowedIPs, sizeof(networks));
  char* save = nullptr;
  for (char* item = strtok_r(networks, ",", &save); item; item = strtok_r(nullptr, ",", &save)) {
    while (*item == ' ') ++item;
    char* end = item + strlen(item); while (end > item && end[-1] == ' ') *--end = 0;
    if (allowedCount == 8 || !parseIPv4Route(item, allowed[allowedCount])) return false;
    if (allowed[allowedCount].mask) routes[routeCount++] = allowed[allowedCount];
    ++allowedCount;
  }
  return allowedCount != 0;
}
void WireGuardDriver::resolvedDNS(const char*, const ip_addr_t* address, void* context) {
  auto& job = *static_cast<DNSJob*>(context);
  job.address.store(address && IP_IS_V4(address) ? ip_2_ip4(address)->addr : 0);
  job.done.store(true);
}
bool WireGuardDriver::startDNS(uint32_t now) {
  if (dns.pending) return false; // Never reuse a callback context still held by lwIP.
  IPAddress numeric;
  if(wanted.wg.fullTunnel && !numeric.fromString(wanted.wg.endpoint) && !observed.peerSessionUp
    && !beginEndpointDNSLease(now)) { observed.error="endpoint_bootstrap_dns_unavailable";return false; }
  dns.owner = this; dns.generation = generation; dns.submitted = now;
  dns.pending = true; dns.done.store(false); dns.address.store(0);
  ip_addr_t address;
  LOCK_TCPIP_CORE();
  const err_t outcome = dns_gethostbyname_addrtype(wanted.wg.endpoint, &address, resolvedDNS, &dns, LWIP_DNS_ADDRTYPE_IPV4);
  UNLOCK_TCPIP_CORE();
  if (outcome != ERR_INPROGRESS) {
    dns.address.store(outcome == ERR_OK && IP_IS_V4(&address) ? ip_2_ip4(&address)->addr : 0);
    dns.done.store(true);
  }
  return true;
}
void WireGuardDriver::tick(uint32_t now) {
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  if(fixtureHoldUntil) {
    if(wanted.wg.fullTunnel)maintainFullTunnelPolicy(now);
    if(int32_t(now-fixtureHoldUntil)<0)return;
    fixtureHoldUntil=0;
    if(fixtureWifiHold) { fixtureWifiHold=false;wifi.apply(wanted); }
  }
  if((fixtureBadUntil && int32_t(now-fixtureBadUntil)>=0)
    || (fixtureBootstrapUntil && int32_t(now-fixtureBootstrapUntil)>=0))policyFixture("restore",nullptr);
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  if (fullTestUntil && int32_t(now-fullTestUntil) >= 0) fullTestStop();
  if (fullTestHoldUntil && int32_t(now-fullTestHoldUntil) < 0) return;
  fullTestHoldUntil = 0;
#endif
  if(wanted.wg.fullTunnel) {
    maintainFullTunnelPolicy(now);
    const auto guard=fullTunnelPolicyStatus();
    observed.guardReady=guard.ready;observed.ipv4Blocked=guard.ipv4Blocked;observed.ipv6Blocked=guard.ipv6Blocked;
    observed.bootstrapDNSPackets=guard.bootstrapDNSPackets;observed.bootstrapTimePackets=guard.bootstrapTimePackets;
  }
  if (ping && echo.done.load()) {
    observed.probeSent = echo.sent.load(); observed.probeReceived = echo.received.load();
    esp_ping_delete_session(ping); ping = nullptr; observed.probeBusy = false;
  }
  const bool wifiReady = wifi.readiness().wifiConnected;
  const uint32_t currentLAN = wifiReady ? uint32_t(WiFi.localIP()) : 0;
  if (currentLAN != lanAddress) {
    stopTunnel(); ++generation; resolved = 0; attempted = false; lanAddress = currentLAN;
    if(wifiReady && wanted.wg.fullTunnel && !updateFullTunnelUnderlay())observed.error="full_tunnel_guard_unavailable";
  }
  if (!wanted.wg.enabled) { observed.phase = "disabled"; return; }
  if (!wifiReady) { observed.phase = "waiting_wifi"; observed.fullTunnelVerified=false;return; }
  if(wanted.wg.fullTunnel && !fullTunnelPolicyStatus().ready) {
    observed.phase="error";observed.error="full_tunnel_guard_unavailable";observed.fullTunnelVerified=false;return;
  }
  if (time(nullptr) < 1704067200) {
    if(wanted.wg.fullTunnel && (!timeAttemptAt || uint32_t(now-timeAttemptAt)>=60000) && beginTimeLease(now)) {
      timeAttemptAt=now;configureBootstrapTime();
    }
    observed.phase = "waiting_time"; return;
  }
  if(wanted.wg.fullTunnel)finishTimeLease();
  if (!strcmp(observed.error, "invalid_routes")) { observed.phase = "error"; return; }
  if(dns.pending && dns.done.load()) {
    const uint32_t answer=dns.generation==generation?dns.address.load():0;
    dns.pending=false;
    if(wanted.wg.fullTunnel)finishEndpointDNSLease();
    if(dns.generation==generation) {
      resolvedAt=now;
      if(answer) {
        if(resolved && resolved!=answer && tunnel.is_initialized()) { stopTunnel();++generation;attempted=false; }
        resolved=answer;
      } else if(!tunnel.is_initialized()) { observed.error="endpoint_dns_failed";attempted=true;retryAt=now; }
    }
  }
  if (tunnel.is_initialized()) {
    observed.running = true; observed.peerSessionUp = tunnel.peerSessionUp();
    observed.phase = observed.peerSessionUp ? "peer_session_up" : "handshaking";
    observed.error = observed.peerSessionUp || uint32_t(now-startedAt) < 30000 ? "none" : "peer_session_unavailable";
    tunnel.trafficBytes(observed.sentBytes, observed.receivedBytes);
    if(wanted.wg.fullTunnel)observeFullTunnel(now);
    if(!dns.pending && uint32_t(now-resolvedAt)>=(observed.peerSessionUp?300000U:60000U))startDNS(now);
    return;
  }
  if (attempted && uint32_t(now - retryAt) < 15000) { observed.phase = "retry_wait"; return; }
  if (!resolved) {
    observed.phase = "resolving_endpoint";
    startDNS(now);
    if (dns.pending && uint32_t(now - dns.submitted) >= 10000) observed.error = "endpoint_dns_timeout";
    return;
  }
  char endpoint[16]; IPAddress remote(resolved); strlcpy(endpoint, remote.toString().c_str(), sizeof(endpoint));
  if(wanted.wg.fullTunnel)setFullTunnelEndpoint(resolved);
  observed.phase = "starting"; attempted = true; retryAt = now;
  // Numeric endpoint avoids waiting for DNS in the backend lifecycle call.
  bool okay = tunnel.beginConfigured(IPAddress(address.address), IPAddress(address.mask), wanted.wg.privateKey,
    endpoint, wanted.wg.publicKey, wanted.wg.port, wanted.wg.presharedKey,
    IPAddress(allowed[0].address), IPAddress(allowed[0].mask), wanted.wg.keepalive, false);
  for (size_t i = 1; okay && i < allowedCount; ++i)
    okay = tunnel.addAllowedIP(wanted.wg.publicKey, IPAddress(allowed[i].address), IPAddress(allowed[i].mask));
  okay = okay && tunnel.setMTU(wanted.wg.mtu)
    && installWireGuardRoutes(tunnel.interfaceHandle(), resolved, routes, routeCount);
  if (!okay) { stopTunnel(); observed.error = "backend_start_failed"; observed.phase = "retry_wait"; }
  else { observed.error = "none"; observed.running = true; observed.phase = "handshaking"; startedAt = now; }
}
NetworkReadiness WireGuardDriver::readiness() const {
  return {wifi.readiness().wifiConnected, observed.peerSessionUp, observed.fullTunnelVerified};
}
void WireGuardDriver::configureBootstrapTime() {
  char timezone[128];const char* current=getenv("TZ");strlcpy(timezone,current?current:"UTC0",sizeof(timezone));
  configTzTime(timezone,"162.159.200.1");
}
void WireGuardDriver::observeFullTunnel(uint32_t now) {
  const auto guard=fullTunnelPolicyStatus();
  observed.guardReady=guard.ready;observed.ipv4Blocked=guard.ipv4Blocked;observed.ipv6Blocked=guard.ipv6Blocked;
  observed.bootstrapDNSPackets=guard.bootstrapDNSPackets;observed.bootstrapTimePackets=guard.bootstrapTimePackets;
  if(!fullProbeHandled) {
    const auto result=fullProbe.result();
    if(result.done) {
      fullProbeHandled=true;
      observed.routeProbeHTTP=result.http;
      if(result.generation==generation && result.http==200 && result.address && observed.peerSessionUp
        && guard.ready && guard.tunnelPresent && observed.sentBytes>fullProbeTX && observed.receivedBytes>fullProbeRX) {
        fullProofAt=now;IPAddress exit(result.address);strlcpy(observed.publicExit,exit.toString().c_str(),sizeof(observed.publicExit));
      } else fullProofAt=0;
    }
  }
  observed.fullTunnelVerified=observed.peerSessionUp && guard.ready && guard.tunnelPresent && fullProofAt && uint32_t(now-fullProofAt)<180000;
  observed.verificationAgeSeconds=fullProofAt?uint32_t(now-fullProofAt)/1000:0;
  if(observed.peerSessionUp && !fullProbe.busy() && (!fullProbeAttempted
    || uint32_t(now-fullProbeAt)>=(fullProofAt?60000U:15000U))) {
    if(fullProbe.start(generation)) {
      fullProbeAt=now;fullProbeAttempted=true;fullProbeHandled=false;fullProbeTX=observed.sentBytes;fullProbeRX=observed.receivedBytes;
    }
  }
  observed.routeProbeBusy=fullProbe.busy();
  if(observed.peerSessionUp && !observed.fullTunnelVerified) {
    observed.phase="verifying_full_tunnel";
    observed.error=fullProbeAttempted && fullProbeHandled?"full_tunnel_public_probe_failed":"none";
  } else if(observed.fullTunnelVerified)observed.phase="full_tunnel_verified";
}
void WireGuardDriver::pingReply(esp_ping_handle_t handle, void* context) {
  auto& probe = *static_cast<Probe*>(context);
  ip_addr_t source;
  if (esp_ping_get_profile(handle, ESP_PING_PROF_IPADDR, &source, sizeof(source)) == ESP_OK
    && IP_IS_V4(&source) && ip_2_ip4(&source)->addr == probe.target) probe.received.fetch_add(1);
}
void WireGuardDriver::pingEnd(esp_ping_handle_t handle, void* context) {
  auto& probe = *static_cast<Probe*>(context); uint32_t sent = 0;
  esp_ping_get_profile(handle, ESP_PING_PROF_REQUEST, &sent, sizeof(sent)); probe.sent.store(sent);
  probe.done.store(true);
}
bool WireGuardDriver::probe(uint32_t target) {
  if (ping || !observed.peerSessionUp || !wireguardDestination(target)) return false;
  echo.target = target; echo.sent.store(0); echo.received.store(0); echo.done.store(false);
  esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
  config.count = 3; config.interval_ms = 300; config.timeout_ms = 600; config.task_stack_size = 3072;
  config.target_addr = IPADDR4_INIT(target);
  // interface=0 exercises destination routing, rather than forcing the tunnel.
  const esp_ping_callbacks_t callbacks = {.cb_args=&echo, .on_ping_success=pingReply,
    .on_ping_timeout=nullptr, .on_ping_end=pingEnd};
  if (esp_ping_new_session(&config, &callbacks, &ping) != ESP_OK) { ping = nullptr; return false; }
  if (esp_ping_start(ping) != ESP_OK) { esp_ping_delete_session(ping); ping = nullptr; return false; }
  observed.probeBusy = true; observed.probeSent = observed.probeReceived = 0;
  IPAddress ip(target); strlcpy(observed.probeAddress, ip.toString().c_str(), sizeof(observed.probeAddress));
  return true;
}
void WireGuardDriver::stop() {
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  fullTestStop();
#endif
  stopTunnel(); endFullTunnelPolicy();wifi.stop(); ++generation;
  clearSecret(&wanted, sizeof(wanted)); observed = {}; haveConfig = false;
  lanAddress = resolved = 0; attempted = false;
}
void WireGuardDriver::restart() {
  stopTunnel(); ++generation; resolved = 0; attempted = false;
  observed.error = "none";
  observed.phase = wanted.wg.enabled ? "waiting_wifi" : "disabled";
}
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
void WireGuardDriver::policyFixture(const char* action,const char* hostname) {
  if(!wanted.wg.fullTunnel)return;
  if(!strcmp(action,"packets")) { exerciseFullTunnelPolicy();return; }
  if(!strcmp(action,"restore")) {
    fixtureHoldUntil=fixtureBadUntil=fixtureBootstrapUntil=0;
    if(fixtureEndpoint[0]) { strlcpy(wanted.wg.endpoint,fixtureEndpoint,sizeof(wanted.wg.endpoint));fixtureEndpoint[0]=0; }
    if(fixtureWifiHold) { fixtureWifiHold=false;wifi.apply(wanted); }
  }
  stopTunnel();++generation;attempted=false;resolved=0;
  if(!strcmp(action,"hold") || !strcmp(action,"wifi")) {
    fixtureWifiHold=!strcmp(action,"wifi");fixtureHoldUntil=millis()+30000;
    if(fixtureWifiHold)WiFi.disconnect(false,false);
    fixtureProbe.start(generation);return;
  }
  if(!strcmp(action,"bad_endpoint")) {
    resolved=uint32_t(IPAddress(203,0,113,1));fixtureBadUntil=millis()+25000;return;
  }
  if(!strcmp(action,"bootstrap") && hostname && hostname[0]) {
    if(!fixtureEndpoint[0])strlcpy(fixtureEndpoint,wanted.wg.endpoint,sizeof(fixtureEndpoint));
    strlcpy(wanted.wg.endpoint,hostname,sizeof(wanted.wg.endpoint));
    LOCK_TCPIP_CORE();dns_clear_cache();UNLOCK_TCPIP_CORE();
    esp_sntp_stop();timeval epoch={};settimeofday(&epoch,nullptr);timeAttemptAt=0;fixtureClockReset=true;
    fixtureBootstrapUntil=millis()+65000;
  }
  IPAddress resolver;resolver.fromString(wanted.wg.dns);
  configureFullTunnelPolicy(wanted.wg.endpoint,wanted.wg.port,uint32_t(resolver));
}
#endif
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
bool WireGuardDriver::fullTestStart(uint32_t resolver) {
  bool hasDefault = false;
  for (size_t i = 0; i < allowedCount; ++i) hasDefault |= !allowed[i].mask;
  if (!observed.peerSessionUp || !hasDefault || wanted.wg.mtu > 1420
    || !beginFullTunnelTest(tunnel.interfaceHandle(), resolved, wanted.wg.port, resolver)) return false;
  fullTestUntil = millis() + 180000; fullTestHoldUntil = 0; return true;
}
void WireGuardDriver::fullTestStop() {
  if (fullTestUntil) endFullTunnelTest();
  fullTestUntil = fullTestHoldUntil = 0;
}
void WireGuardDriver::fullTestHold() {
  if (!fullTestUntil) return;
  stopTunnel(); fullTestHoldUntil = millis() + 45000;
  probeFullTunnelTest();
}
#endif
} // namespace appliance
#endif
