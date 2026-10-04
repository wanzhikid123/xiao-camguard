#pragma once
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
#include <stdint.h>
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
namespace appliance {
// Explicit hardware-only spike. Never persists a mode or config, and does not
// establish the product's full-tunnel boot/bootstrap contract.
struct FullTunnelTestStatus {
  bool active = false, tunnelPresent = false, outboundDone = false;
  uint32_t guardedInterfaces = 0, ipv4Blocked = 0, ipv6Blocked = 0;
  uint32_t forwardedEndpoint = 0, forwardedLocal = 0, routedTunnel = 0;
  uint32_t packetChecks = 0, packetFailures = 0;
  uint32_t outboundAddress = 0;
  uint32_t dnsServers[3] = {};
  int outboundHTTP = 0;
};
bool beginFullTunnelTest(void* tunnel, uint32_t endpoint, uint16_t port, uint32_t resolver);
void endFullTunnelTest();
bool probeFullTunnelTest();
void fullTunnelTestDetach();
void fullTunnelTestAttach(void* tunnel, uint32_t endpoint);
bool fullTunnelTestRoute(const ip4_addr_t* source, const ip4_addr_t* destination, netif*& route);
FullTunnelTestStatus fullTunnelTestStatus();
} // namespace appliance
#endif
