/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 * Local IDF 5 adapter: core-safe lifecycle, explicit Wi-Fi underlay and PSK.
 */
#include "WireGuard-ESP32.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/tcpip.h"
#include "lwip/ip.h"
#include "lwip/netdb.h"
#if defined(XIAO_APPLIANCE_WG_TEARDOWN_TEST)
#include "lwip/tcp.h"
#include <Arduino.h>
#endif
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
extern "C" {
#include "wireguardif.h"
#include "wireguard-platform.h"
#include "wireguard.h"
}

static struct netif wg_netif_struct = {};
static struct netif* wg_netif = nullptr;
static struct netif* previous_default_netif = nullptr;
static uint8_t peer_index = WIREGUARDIF_INVALID_INDEX;
static bool changed_default = false;

#if defined(XIAO_APPLIANCE_WG_TEARDOWN_TEST)
// Hardware-only regression fixture: an active TCP PCB bound to the WG netif
// makes netif_remove synchronously emit a reset through its output callback.
// The normal firmware does not compile this fixture or create this connection.
static void prepareTeardownFixture() {
    struct tcp_pcb* pcb = tcp_new();
    if (!pcb) { Serial.println("WG teardown fixture: allocation failed"); return; }
    ip_addr_t local = IPADDR4_INIT(ip4_addr_get_u32(netif_ip4_addr(wg_netif)));
    ip_addr_t remote = IPADDR4_INIT_BYTES(10, 8, 0, 1);
    if (tcp_bind(pcb, &local, 0) != ERR_OK) {
        tcp_abort(pcb); Serial.println("WG teardown fixture: bind failed"); return;
    }
    tcp_bind_netif(pcb, wg_netif);
    if (tcp_connect(pcb, &remote, 9, nullptr) != ERR_OK) {
        tcp_abort(pcb); Serial.println("WG teardown fixture: connect failed"); return;
    }
    Serial.println("WG teardown fixture: active TCP PCB prepared");
}
#endif

class CoreLock {
public:
    CoreLock() { LOCK_TCPIP_CORE(); }
    ~CoreLock() { UNLOCK_TCPIP_CORE(); }
};

bool WireGuard::beginConfigured(const IPAddress& localIP, const IPAddress& subnet,
                               const char* privateKey, const char* endpoint, const char* publicKey,
                               uint16_t port, const char* presharedKey,
                               const IPAddress& allowedIP, const IPAddress& allowedMask,
                               uint16_t keepalive, bool defaultRoute) {
    if (_is_initialized || !privateKey || !endpoint || !publicKey || !port) return false;
    // DNS can wait; resolve it before acquiring the TCP/IP core lock.
    struct addrinfo hint = {}, *res = nullptr;
    hint.ai_family = AF_INET;
    if (lwip_getaddrinfo(endpoint, nullptr, &hint, &res) || !res) return false;
    ip_addr_t endpointIP = IPADDR4_INIT(0);
    inet_addr_to_ip4addr(ip_2_ip4(&endpointIP), &((struct sockaddr_in*)res->ai_addr)->sin_addr);
    lwip_freeaddrinfo(res);

    uint8_t psk[32] = {};
    size_t pskLength = sizeof(psk);
    bool hasPSK = presharedKey && presharedKey[0];
    if (hasPSK && (!wireguard_base64_decode(presharedKey, psk, &pskLength) || pskLength != 32)) return false;
    esp_netif_t* sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    struct netif* underlay = sta ? (struct netif*)esp_netif_get_netif_impl(sta) : nullptr;
    if (!underlay) return false;

    wireguard_platform_init();
    CoreLock lock;
    if (wg_netif) return false; // This backend intentionally supports one instance.
    struct wireguardif_init_data init = {};
    init.private_key = privateKey;
    init.listen_port = 51820;
    init.bind_netif = underlay;
    ip4_addr_t address, mask, gateway;
    IP4_ADDR(&address, localIP[0], localIP[1], localIP[2], localIP[3]);
    IP4_ADDR(&mask, subnet[0], subnet[1], subnet[2], subnet[3]);
    IP4_ADDR(&gateway, 0, 0, 0, 0);
    wg_netif_struct = {};
    wg_netif = netif_add(&wg_netif_struct, &address, &mask, &gateway, &init, wireguardif_init, ip_input);
    if (!wg_netif) return false;

    struct wireguardif_peer peer;
    wireguardif_peer_init(&peer);
    peer.public_key = publicKey;
    peer.preshared_key = hasPSK ? psk : nullptr;
    peer.endpoint_ip = endpointIP;
    peer.endpoint_port = port;
    peer.keep_alive = keepalive;
    peer.allowed_ip = IPADDR4_INIT(static_cast<uint32_t>(allowedIP));
    peer.allowed_mask = IPADDR4_INIT(static_cast<uint32_t>(allowedMask));
    peer_index = WIREGUARDIF_INVALID_INDEX;
    err_t result = wireguardif_add_peer(wg_netif, &peer, &peer_index);
    memset(psk, 0, sizeof(psk));
    if (result != ERR_OK || peer_index == WIREGUARDIF_INVALID_INDEX ||
        wireguardif_connect(wg_netif, peer_index) != ERR_OK) {
        if (peer_index != WIREGUARDIF_INVALID_INDEX) wireguardif_remove_peer(wg_netif, peer_index);
        netif_remove(wg_netif);
        wireguardif_shutdown(wg_netif);
        wg_netif = nullptr;
        peer_index = WIREGUARDIF_INVALID_INDEX;
        return false;
    }
    netif_set_up(wg_netif);
    previous_default_netif = netif_default;
    changed_default = defaultRoute;
    if (changed_default) netif_set_default(wg_netif);
    _is_initialized = true;
    return true;
}

bool WireGuard::begin(const IPAddress& localIP, const IPAddress& subnet, const IPAddress& gateway,
                      const char* privateKey, const char* endpoint, const char* publicKey, uint16_t port) {
    (void)gateway;
    return beginConfigured(localIP, subnet, privateKey, endpoint, publicKey, port, nullptr,
                           IPAddress(0,0,0,0), IPAddress(0,0,0,0),
                           WIREGUARDIF_KEEPALIVE_DEFAULT, true);
}

bool WireGuard::begin(const IPAddress& localIP, const char* privateKey, const char* endpoint,
                      const char* publicKey, uint16_t port) {
    return begin(localIP, IPAddress(255,255,255,255), IPAddress(0,0,0,0),
                 privateKey, endpoint, publicKey, port);
}

bool WireGuard::peerSessionUp() const {
    CoreLock lock;
    if (!_is_initialized || !wg_netif) return false;
    return wireguardif_peer_is_up(wg_netif, peer_index, nullptr, nullptr) == ERR_OK;
}

void WireGuard::trafficBytes(uint32_t& sent, uint32_t& received) const {
    CoreLock lock;
    sent = received = 0;
    if (_is_initialized && wg_netif && wg_netif->state) {
        auto* device = (struct wireguard_device*)wg_netif->state;
        sent = device->plaintext_tx_bytes;
        received = device->plaintext_rx_bytes;
    }
}

void WireGuard::end() {
    CoreLock lock;
    if (!_is_initialized || !wg_netif) return;
#if defined(XIAO_APPLIANCE_WG_TEARDOWN_TEST)
    prepareTeardownFixture();
#endif
    if (changed_default && netif_default == wg_netif) netif_set_default(previous_default_netif);
    changed_default = false;
    previous_default_netif = nullptr;
    wireguardif_disconnect(wg_netif, peer_index);
    wireguardif_remove_peer(wg_netif, peer_index);
    peer_index = WIREGUARDIF_INVALID_INDEX;
    // lwIP can synchronously output TCP resets while removing local addresses.
    // Keep the device alive until those callbacks finish; peers are already
    // removed, so reset output returns ERR_RTE instead of using freed state.
    netif_remove(wg_netif);
    wireguardif_shutdown(wg_netif);
    wg_netif = nullptr;
    _is_initialized = false;
}

bool WireGuard::addAllowedIP(const char* publicKey, const IPAddress& address, const IPAddress& mask) {
    CoreLock lock;
    if (!_is_initialized || !wg_netif || !publicKey) return false;
    struct wireguardif_peer peer;
    wireguardif_peer_init(&peer);
    peer.public_key = publicKey;
    peer.allowed_ip = IPADDR4_INIT(static_cast<uint32_t>(address));
    peer.allowed_mask = IPADDR4_INIT(static_cast<uint32_t>(mask));
    uint8_t index = WIREGUARDIF_INVALID_INDEX;
    // The existing backend adds a CIDR to an already-known public-key peer;
    // it does not reset that peer's endpoint, keys or handshake state.
    return wireguardif_add_peer(wg_netif, &peer, &index) == ERR_OK && index == peer_index;
}
bool WireGuard::setMTU(uint16_t mtu) {
    CoreLock lock;
    if (!_is_initialized || !wg_netif || mtu < 576 || mtu > 1500) return false;
    wg_netif->mtu = mtu; return true;
}
void* WireGuard::interfaceHandle() const { return _is_initialized ? wg_netif : nullptr; }
