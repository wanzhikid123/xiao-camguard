#pragma once
#include "ApplianceConfig.h"
#include <stddef.h>

namespace appliance {
enum class ImportError { Ok, TooLarge, InvalidText, Section, Duplicate, UnsupportedField,
                         MissingField, InvalidValue, UnsupportedListenPort, TooManyNetworks };
struct ImportSummary {
  unsigned errorLine = 0;
  unsigned ipv4Networks = 0;
  unsigned excludedIPv6Networks = 0;
  unsigned excludedIPv6Addresses = 0;
  bool fullTunnelRequested = false;
};
const char* importErrorName(ImportError error);
// Atomic single-peer import. On error, target is unchanged. Wi-Fi, admin and
// application fields are retained. IPv6 is explicitly counted/excluded; hooks
// and unknown fields are rejected. Imported default routes do not enable WG-B
// before the network manager has implemented/validated that mode.
ImportError importWireGuard(const char* input, size_t length, Config& target, ImportSummary& summary);
} // namespace appliance
