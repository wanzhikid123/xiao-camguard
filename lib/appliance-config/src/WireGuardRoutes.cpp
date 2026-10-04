#if defined(XIAO_APPLIANCE)
#include "WireGuardRoutes.h"
#include <IPAddress.h>
#include <string.h>
#include <stdlib.h>
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/tcpip.h"
#include "FullTunnelTest.h"
#include "FullTunnelPolicy.h"

namespace {
struct Policy {
  netif* tunnel = nullptr;
  netif* underlay = nullptr;
  uint32_t endpoint = 0;
  appliance::IPv4Route routes[9];
  size_t count = 0;
} policy;
class CoreLock {
public: CoreLock() { LOCK_TCPIP_CORE(); } ~CoreLock() { UNLOCK_TCPIP_CORE(); }
};
bool onNetwork(uint32_t destination, const netif* interface) {
  return interface && netif_is_up(interface) && netif_is_link_up(interface)
    && !ip4_addr_isany_val(*netif_ip4_addr(interface))
    && (destination & netif_ip4_netmask(interface)->addr)
       == (netif_ip4_addr(interface)->addr & netif_ip4_netmask(interface)->addr);
}
} // namespace
extern "C" netif* __real_ip4_route_src_hook(const ip4_addr_t*, const ip4_addr_t*);
extern "C" netif* __wrap_ip4_route_src_hook(const ip4_addr_t* source, const ip4_addr_t* destination) {
  // Called with TCP/IP core serialized by lwIP. Policy updates use the same
  // lock, so no removed interface or partially-written CIDR can be observed.
  netif* strictRoute = nullptr;
  if (appliance::fullTunnelRoute(source, destination, strictRoute)) return strictRoute;
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  netif* fullRoute = nullptr;
  if (appliance::fullTunnelTestRoute(source, destination, fullRoute)) return fullRoute;
#endif
  if (!policy.tunnel || !destination) return __real_ip4_route_src_hook(source, destination);
  const uint32_t dest = destination->addr, host = lwip_ntohl(dest);
  if (dest == policy.endpoint) return policy.underlay;
  if ((host >> 24) == 127 || (host >> 28) == 14 || host == UINT32_MAX)
    return __real_ip4_route_src_hook(source, destination);
  for (netif* interface = netif_list; interface; interface = interface->next)
    if (interface != policy.tunnel && onNetwork(dest, interface))
      return __real_ip4_route_src_hook(source, destination); // Keep physical LAN/AP routes.
  if (source && !ip4_addr_isany(source)) {
    netif* bound = __real_ip4_route_src_hook(source, destination);
    if (bound) return bound; // Includes replies from the actual WireGuard address.
  }
  for (size_t i = 0; i < policy.count; ++i)
    if ((dest & policy.routes[i].mask) == (policy.routes[i].address & policy.routes[i].mask))
      return policy.tunnel;
  return __real_ip4_route_src_hook(source, destination); // Ordinary WAN retains its underlay.
}
namespace appliance {
bool parseIPv4Route(const char* cidr, IPv4Route& out) {
  if (!cidr || !cidr[0] || strlen(cidr) > 43) return false;
  char text[44]; strlcpy(text, cidr, sizeof(text));
  char* slash = strchr(text, '/'); if (!slash) return false; *slash++ = 0;
  char* end = nullptr; unsigned long bits = strtoul(slash, &end, 10);
  if (!slash[0] || !end || *end || bits > 32) return false;
  IPAddress address; if (!address.fromString(text) || address.type() != IPType::IPv4) return false;
  out.address = uint32_t(address);
  out.mask = bits ? lwip_htonl(UINT32_MAX << (32 - bits)) : 0;
  return true;
}
bool installWireGuardRoutes(void* interface, uint32_t endpoint, const IPv4Route* routes, size_t count) {
  if (!interface || !routes || !count || count > 9 || !endpoint) return false;
  esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  auto* underlay = sta ? static_cast<netif*>(esp_netif_get_netif_impl(sta)) : nullptr;
  if (!underlay) return false;
  CoreLock lock;
  policy = {}; policy.tunnel = static_cast<netif*>(interface); policy.underlay = underlay;
  policy.endpoint = endpoint;
  for (size_t i = 0; i < count; ++i) {
    if (!routes[i].mask) { policy = {}; return false; } // WG-A never installs a default route.
    policy.routes[i] = routes[i];
  }
  policy.count = count;
  attachFullTunnelRoute(interface,endpoint);
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  fullTunnelTestAttach(interface, endpoint);
#endif
  return true;
}
void clearWireGuardRoutes() {
  CoreLock lock; policy = {};
  detachFullTunnelRoute();
#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
  fullTunnelTestDetach();
#endif
}
bool wireguardDestination(uint32_t address) {
  CoreLock lock;
  if (!policy.tunnel || address == policy.endpoint || address == netif_ip4_addr(policy.tunnel)->addr) return false;
  for (netif* interface = netif_list; interface; interface = interface->next)
    if (interface != policy.tunnel && onNetwork(address, interface)) return false;
  for (size_t i = 0; i < policy.count; ++i)
    if ((address & policy.routes[i].mask) == (policy.routes[i].address & policy.routes[i].mask)) return true;
  return false;
}
} // namespace appliance
#endif
