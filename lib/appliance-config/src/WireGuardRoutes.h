#pragma once
#if defined(XIAO_APPLIANCE)
#include <stdint.h>
#include <stddef.h>
namespace appliance {
struct IPv4Route { uint32_t address = 0, mask = 0; };
bool parseIPv4Route(const char* cidr, IPv4Route& out);
// Values are IPv4 network byte order. These functions lock the TCP/IP core.
bool installWireGuardRoutes(void* interface, uint32_t endpoint, const IPv4Route* routes, size_t count);
void clearWireGuardRoutes();
bool wireguardDestination(uint32_t address);
} // namespace appliance
#endif
