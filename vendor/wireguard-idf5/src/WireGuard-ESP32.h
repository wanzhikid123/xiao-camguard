/*
 * WireGuard implementation for ESP32 Arduino by Kenta Ida (fuga@fugafuga.org)
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once
#include <IPAddress.h>

class WireGuard
{
private:
    bool _is_initialized = false;
public:
    bool begin(const IPAddress& localIP, const IPAddress& Subnet, const IPAddress& Gateway, const char* privateKey, const char* remotePeerAddress, const char* remotePeerPublicKey, uint16_t remotePeerPort);
    bool begin(const IPAddress& localIP, const char* privateKey, const char* remotePeerAddress, const char* remotePeerPublicKey, uint16_t remotePeerPort);
    bool beginConfigured(const IPAddress& localIP, const IPAddress& subnet,
                         const char* privateKey, const char* endpoint, const char* publicKey,
                         uint16_t port, const char* presharedKey,
                         const IPAddress& allowedIP, const IPAddress& allowedMask,
                         uint16_t keepalive, bool defaultRoute);
    bool peerSessionUp() const;
    void trafficBytes(uint32_t& sent, uint32_t& received) const;
    bool addAllowedIP(const char* publicKey, const IPAddress& address, const IPAddress& mask);
    bool setMTU(uint16_t mtu);
    // Owning adapter only; install/remove its route policy under TCP/IP core
    // lock, and clear the policy before end() removes this interface.
    void* interfaceHandle() const;
    void end();
    bool is_initialized() const { return this->_is_initialized; }
};
