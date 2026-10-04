#pragma once
#include "NetworkManager.h"
#include <IPAddress.h>

namespace appliance {
// Wi-Fi-only underlay for the manager's first integration gate. It explicitly
// refuses WG/full-tunnel trials; a verified tunnel driver must replace it.
class WifiDriver : public NetworkDriver {
public:
  explicit WifiDriver(const char* apName);
  bool supports(const Config& config) const override;
  void apply(const Config& config) override;
  void prepare() { beginRadio(); }
  NetworkReadiness readiness() const override;
  bool rescueAP(bool enabled) override;
  void stop() override;
private:
  char apName[33] = {}, ssid[33] = {};
  bool begun = false;
  void beginRadio();
};
} // namespace appliance
