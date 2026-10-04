#if defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST)
#include "FullTunnelTest.h"
#include <IPAddress.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <atomic>
#include <string.h>
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/dns.h"
#include "lwip/ip4.h"
#include "lwip/ip6_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/prot/ip4.h"

namespace {
class CoreLock {
public: CoreLock() { LOCK_TCPIP_CORE(); } ~CoreLock() { UNLOCK_TCPIP_CORE(); }
};
struct Guard {
  netif* interface = nullptr;
  netif_output_fn ipv4 = nullptr;
  netif_output_ip6_fn ipv6 = nullptr;
};
struct State {
  bool active = false;
  netif* tunnel = nullptr;
  netif* underlay = nullptr;
  uint32_t endpoint = 0, resolver = 0;
  uint16_t port = 0;
  Guard guards[3];
  ip_addr_t savedDNS[DNS_MAX_SERVERS];
  appliance::FullTunnelTestStatus status;
} state;
std::atomic<bool> outboundBusy {false}, outboundDone {false};
std::atomic<int> outboundHTTP {0};
std::atomic<uint32_t> outboundAddress {0};
bool local(uint32_t address, const netif* interface) {
  return interface && netif_is_up(interface) && netif_is_link_up(interface)
    && netif_ip4_addr(interface)->addr
    && (address & netif_ip4_netmask(interface)->addr)
      == (netif_ip4_addr(interface)->addr & netif_ip4_netmask(interface)->addr);
}
Guard* guard(netif* interface) {
  for (auto& entry : state.guards) if (entry.interface == interface) return &entry;
  return nullptr;
}
uint16_t big16(const uint8_t* value) { return uint16_t(value[0]) << 8 | value[1]; }
// Inspect the complete IP packet, even for a pbuf chain. DNS is checked before
// the LAN exception: a DHCP/router resolver must not silently bypass WG.
bool allow4(netif* interface, pbuf* packet, const ip4_addr_t* destination) {
  if (!destination || !packet || packet->tot_len < 20) return false;
  uint8_t header[60];
  if (pbuf_copy_partial(packet, header, 20, 0) != 20 || header[0] >> 4 != 4) return false;
  const uint16_t size = (header[0] & 15) * 4;
  if (size < 20 || size > sizeof(header) || packet->tot_len < size
    || big16(header + 2) != packet->tot_len) return false;
  uint32_t destinationInHeader; memcpy(&destinationInHeader, header + 16, 4);
  if (destinationInHeader != destination->addr) return false;
  const uint16_t fragment = big16(header + 6);
  const uint8_t protocol = header[9];
  uint16_t sourcePort = 0, destinationPort = 0;
  if ((protocol == 6 || protocol == 17) && !(fragment & IP_OFFMASK)) {
    uint8_t ports[4];
    if (pbuf_copy_partial(packet, ports, sizeof(ports), size) != sizeof(ports)) return false;
    sourcePort = big16(ports); destinationPort = big16(ports + 2);
  }
  // Fragmented DNS cannot hide its destination port from the guard. Tunnel
  // transport is deliberately unfragmented in this bounded 1420-MTU spike.
  if (fragment & (IP_MF | IP_OFFMASK)) return false;
  if (destinationPort == 53) return false;
  if (destination->addr == state.endpoint && protocol == 17 && destinationPort == state.port) {
    ++state.status.forwardedEndpoint; return interface == state.underlay;
  }
  const uint32_t host = lwip_ntohl(destination->addr);
  // DHCP before/after reconnect and multicast link management stay local.
  if (host == UINT32_MAX) return protocol == 17 && sourcePort == 68 && destinationPort == 67;
  if ((host & 0xffffff00U) == 0xe0000000U) { ++state.status.forwardedLocal; return true; }
  if (local(destination->addr, interface)) { ++state.status.forwardedLocal; return true; }
  return false;
}
err_t output4(netif* interface, pbuf* packet, const ip4_addr_t* destination) {
  Guard* entry = guard(interface);
  if (!entry || !entry->ipv4) return ERR_RTE;
  if (state.active && !allow4(interface, packet, destination)) {
    ++state.status.ipv4Blocked; return ERR_RTE;
  }
  return entry->ipv4(interface, packet, destination);
}
err_t output6(netif* interface, pbuf* packet, const ip6_addr_t* destination) {
  Guard* entry = guard(interface);
  if (!entry || !entry->ipv6) return ERR_RTE;
  // IPv6 WG is outside v1. Only link-local unicast/multicast needed for local
  // link control can leave physically. No global/ULA application fallback.
  uint8_t header[44];
  const bool basic = packet && destination && packet->tot_len >= sizeof(header)
    && pbuf_copy_partial(packet, header, sizeof(header), 0) == sizeof(header)
    && header[0] >> 4 == 6 && big16(header + 4) + 40 == packet->tot_len
    && !memcmp(header + 24, destination->addr, 16);
  const bool localLink = basic && (ip6_addr_islinklocal(destination)
    || (ip6_addr_ismulticast(destination) && ip6_addr_multicast_scope(destination) == 2));
  const bool knownProtocol = basic && (header[6] == 6 || header[6] == 17 || header[6] == 58);
  const bool dns = basic && (header[6] == 6 || header[6] == 17) && big16(header + 42) == 53;
  if (state.active && (!localLink || !knownProtocol || dns)) {
    ++state.status.ipv6Blocked; return ERR_RTE;
  }
  return entry->ipv6(interface, packet, destination);
}
void restore() {
  state.active = false;
  for (auto& entry : state.guards) {
    if (!entry.interface) continue;
    // The spike does not stop/destroy the Wi-Fi interfaces.
    if (entry.interface->output == output4) entry.interface->output = entry.ipv4;
    if (entry.interface->output_ip6 == output6) entry.interface->output_ip6 = entry.ipv6;
  }
  for (size_t i = 0; i < DNS_MAX_SERVERS; ++i) dns_setserver(i, &state.savedDNS[i]);
  state.tunnel = nullptr; state.status.active = state.status.tunnelPresent = false;
}
void outbound(void*) {
  int code = 0; uint32_t address = 0;
  {
  NetworkClientSecure socket; HTTPClient client;
  socket.useBuiltinCACertBundle(); socket.setHandshakeTimeout(6);
  client.setReuse(false); client.setConnectTimeout(4000); client.setTimeout(5000);
  if (client.begin(socket, "https://api.ipify.org/")) {
    code = client.GET();
    // Bounded public IPv4 body only; no redirects, credentials, or identifiers.
    if (code == 200 && client.getSize() > 0 && client.getSize() <= 16) {
      IPAddress ip;
      if (ip.fromString(client.getString()) && ip.type() == IPType::IPv4) address = uint32_t(ip);
    }
    client.end();
  }
  }
  outboundAddress.store(address); outboundHTTP.store(code);
  outboundDone.store(true); outboundBusy.store(false);
  vTaskDelete(nullptr);
}
// Exercise the actual installed output function on the real underlay with an
// original-output spy. Rejected packets must never reach that spy. The packets
// cannot reach the radio; then real unbound outbound tests run separately.
uint32_t spyCalls = 0;
err_t spy4(netif*, pbuf*, const ip4_addr_t*) { ++spyCalls; return ERR_OK; }
err_t spy6(netif*, pbuf*, const ip6_addr_t*) { ++spyCalls; return ERR_OK; }
void packetChecks() {
  Guard* entry = guard(state.underlay); if (!entry) return;
  const auto original4 = entry->ipv4; const auto original6 = entry->ipv6;
  entry->ipv4 = spy4; entry->ipv6 = spy6;
  auto check4 = [&](uint32_t target, uint8_t protocol, uint16_t port, bool expected, uint16_t fragment = 0, bool chained = false) {
    uint8_t bytes[28] = {0x45,0,0,28};
    bytes[6] = fragment >> 8; bytes[7] = fragment; bytes[8] = 64; bytes[9] = protocol;
    memcpy(bytes + 12, &netif_ip4_addr(state.underlay)->addr, 4); memcpy(bytes + 16, &target, 4);
    bytes[20] = 0xcf; bytes[21] = 0x08; bytes[22] = port >> 8; bytes[23] = port;
    pbuf* packet = pbuf_alloc(PBUF_RAW, chained ? 21 : sizeof(bytes), PBUF_RAM);
    if (packet && chained) {
      pbuf* tail = pbuf_alloc(PBUF_RAW, sizeof(bytes)-21, PBUF_RAM);
      if (!tail) { pbuf_free(packet); packet = nullptr; }
      else pbuf_cat(packet, tail);
    }
    ++state.status.packetChecks;
    if (!packet) { ++state.status.packetFailures; return; }
    pbuf_take(packet, bytes, sizeof(bytes)); const uint32_t before = spyCalls;
    const ip4_addr_t destination {target};
    const err_t result = state.underlay->output(state.underlay, packet, &destination);
    if (expected ? result != ERR_OK || spyCalls != before+1 : result != ERR_RTE || spyCalls != before)
      ++state.status.packetFailures;
    pbuf_free(packet);
  };
  const uint32_t lan = netif_ip4_gw(state.underlay)->addr;
  IPAddress publicIP(8,8,8,8); const uint32_t internet = uint32_t(publicIP);
  check4(lan, 6, 80, true);
  check4(lan, 17, 53, false);
  check4(lan, 6, 53, false);
  check4(internet, 6, 80, false);
  check4(internet, 17, 443, false);
  check4(internet, 17, 53, false);
  check4(state.endpoint, 17, state.port, true);
  check4(state.endpoint, 6, state.port, false);
  check4(state.endpoint, 17, state.port == 65535 ? 1 : state.port + 1, false);
  check4(state.endpoint, 17, state.port, false, IP_MF);
  check4(state.endpoint, 17, state.port, false, 1);
  check4(internet, 17, 53, false, 0, true);
  check4(state.endpoint, 17, state.port, true, 0, true);
  for (const char* text : {"2606:4700:4700::1111", "fd00::1", "fe80::1", "ff02::1", "ff05::1"}) {
    ip6_addr_t destination; const bool parsed = ip6addr_aton(text, &destination);
    pbuf* packet = pbuf_alloc(PBUF_RAW, 48, PBUF_RAM); ++state.status.packetChecks;
    if (!parsed || !packet) { if (packet) pbuf_free(packet); ++state.status.packetFailures; continue; }
    const bool expected = !strcmp(text, "fe80::1") || !strcmp(text, "ff02::1");
    uint8_t bytes[48] = {0x60,0,0,0,0,8,58,64}; bytes[40] = 135;
    memcpy(bytes + 24, destination.addr, 16); pbuf_take(packet, bytes, sizeof(bytes));
    const uint32_t before = spyCalls;
    const err_t result = state.underlay->output_ip6(state.underlay, packet, &destination);
    if (expected ? result != ERR_OK || spyCalls != before+1 : result != ERR_RTE || spyCalls != before)
      ++state.status.packetFailures;
    pbuf_free(packet);
  }
  entry->ipv4 = original4; entry->ipv6 = original6;
}
} // namespace
namespace appliance {
bool beginFullTunnelTest(void* tunnel, uint32_t endpoint, uint16_t port, uint32_t resolver) {
  if (!tunnel || !endpoint || !port || !resolver || outboundBusy.load()) return false;
  esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  auto* underlay = sta ? static_cast<netif*>(esp_netif_get_netif_impl(sta)) : nullptr;
  if (!underlay) return false;
  CoreLock lock; if (state.active) return false;
  // A physical LAN resolver is deliberately unsupported by this spike.
  if (local(resolver, underlay)) return false;
  state = {}; state.tunnel = static_cast<netif*>(tunnel); state.underlay = underlay;
  state.endpoint = endpoint; state.port = port; state.resolver = resolver;
  for (size_t i = 0; i < DNS_MAX_SERVERS; ++i) state.savedDNS[i] = *dns_getserver(i);
  for (netif* interface = netif_list; interface; interface = interface->next) {
    if (interface == state.tunnel) continue;
    if (state.status.guardedInterfaces == 3 || !interface->output || !interface->output_ip6) { restore(); return false; }
    Guard& entry = state.guards[state.status.guardedInterfaces++];
    entry = {interface, interface->output, interface->output_ip6};
    interface->output = output4; interface->output_ip6 = output6;
  }
  state.active = state.status.active = state.status.tunnelPresent = true;
  ip_addr_t selected = IPADDR4_INIT(resolver), none = IPADDR4_INIT(0);
  dns_setserver(0, &selected);
  for (size_t i = 1; i < DNS_MAX_SERVERS; ++i) dns_setserver(i, &none);
  packetChecks();
  if (!probeFullTunnelTest()) { restore(); return false; }
  return true;
}
bool probeFullTunnelTest() {
  if (outboundBusy.exchange(true)) return false;
  outboundDone.store(false); outboundHTTP.store(0); outboundAddress.store(0);
  if (xTaskCreatePinnedToCore(outbound, "wg_full_probe", 8192, nullptr, 1, nullptr, 1) == pdPASS) return true;
  outboundBusy.store(false); return false;
}
void endFullTunnelTest() { CoreLock lock; if (state.active) restore(); }
// Called only from WireGuardRoutes while its TCP/IP core lock is held.
void fullTunnelTestDetach() { state.tunnel = nullptr; state.status.tunnelPresent = false; }
void fullTunnelTestAttach(void* interface, uint32_t endpoint) {
  if (state.active) { state.tunnel = static_cast<netif*>(interface); state.endpoint = endpoint; state.status.tunnelPresent = bool(interface); }
}
bool fullTunnelTestRoute(const ip4_addr_t* source, const ip4_addr_t* destination, netif*& route) {
  if (!state.active || !destination) return false;
  const uint32_t target = destination->addr, host = lwip_ntohl(target);
  if (target == state.endpoint) { route = state.underlay; return true; }
  if ((host >> 24) == 127 || (host >> 28) == 14 || host == UINT32_MAX) return false;
  for (const auto& entry : state.guards) if (local(target, entry.interface)) { route = entry.interface; return true; }
  if (source && source->addr && (!state.tunnel || source->addr != netif_ip4_addr(state.tunnel)->addr)) {
    route = state.underlay; return true; // Actual output guard rejects a bound-WAN bypass.
  }
  route = state.tunnel;
  if (route) ++state.status.routedTunnel;
  // NULL deliberately falls through lwIP routing when WG is absent. Every
  // physical output remains guarded, so fallback cannot emit ordinary WAN.
  return true;
}
FullTunnelTestStatus fullTunnelTestStatus() {
  CoreLock lock; auto status = state.status;
  status.outboundDone = outboundDone.load(); status.outboundHTTP = outboundHTTP.load();
  status.outboundAddress = outboundAddress.load();
  static_assert(DNS_MAX_SERVERS == 3, "Pinned fixture expects three DNS slots");
  for (size_t i = 0; i < DNS_MAX_SERVERS; ++i) {
    const ip_addr_t* resolver = dns_getserver(i);
    status.dnsServers[i] = IP_IS_V4(resolver) ? ip_2_ip4(resolver)->addr : 0;
  }
  return status;
}
} // namespace appliance
#endif
