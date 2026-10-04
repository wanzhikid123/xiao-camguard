#pragma once
#if defined(XIAO_APPLIANCE)
#include <stdint.h>
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
namespace appliance {
struct FullTunnelPolicyStatus {
  bool requested = false, ready = false, tunnelPresent = false;
  bool endpointDNSLease = false, timeLease = false;
  uint32_t guardedInterfaces = 0, ipv4Blocked = 0, ipv6Blocked = 0;
  uint32_t endpointPackets = 0, localPackets = 0, routedPackets = 0;
  uint32_t bootstrapDNSPackets = 0, bootstrapTimePackets = 0;
  uint32_t dnsServers[3] = {};
};
// Requested is published before radio startup. Global physical-output wrappers
// then fail closed even before the TCP/IP owner has initialized the policy.
void requestFullTunnelPolicy();
bool configureFullTunnelPolicy(const char* endpoint, uint16_t port, uint32_t resolver);
void endFullTunnelPolicy();
bool fullTunnelRequested();
bool updateFullTunnelUnderlay();
void maintainFullTunnelPolicy(uint32_t now);
bool beginEndpointDNSLease(uint32_t now);
void finishEndpointDNSLease();
bool beginTimeLease(uint32_t now);
void finishTimeLease();
void setFullTunnelEndpoint(uint32_t endpoint);
// Called with the TCP/IP core held from WireGuardRoutes.
void attachFullTunnelRoute(void* interface, uint32_t endpoint);
void detachFullTunnelRoute();
bool fullTunnelRoute(const ip4_addr_t* source, const ip4_addr_t* destination, netif*& route);
FullTunnelPolicyStatus fullTunnelPolicyStatus();
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
struct FullPolicyFixtureStatus { uint32_t checks=0, failures=0; };
// Owner-only injection, using real output callbacks with a radio-output spy.
bool exerciseFullTunnelPolicy();
FullPolicyFixtureStatus fullPolicyFixtureStatus();
#endif
} // namespace appliance
#endif
