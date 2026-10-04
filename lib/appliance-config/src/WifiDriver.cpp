#include "WifiDriver.h"
#include <WiFi.h>
#include <string.h>

namespace appliance {
WifiDriver::WifiDriver(const char* name) { strlcpy(apName, name, sizeof(apName)); }
bool WifiDriver::supports(const Config& config) const { return !config.wg.enabled && !config.wg.fullTunnel; }
void WifiDriver::beginRadio() {
  if (begun) return;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  begun = true;
}
void WifiDriver::apply(const Config& config) {
  beginRadio();
  WiFi.disconnect(false, false);
  strlcpy(ssid, config.wifiSSID, sizeof(ssid));
  if (ssid[0]) WiFi.begin(config.wifiSSID, config.wifiPassword);
}
NetworkReadiness WifiDriver::readiness() const {
  return {begun && ssid[0] && WiFi.status() == WL_CONNECTED && uint32_t(WiFi.localIP()) && WiFi.SSID() == ssid, false, false};
}
bool WifiDriver::rescueAP(bool enabled) {
  beginRadio();
  if (enabled) {
    return WiFi.mode(WIFI_AP_STA)
      && WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0))
      && WiFi.softAP(apName) && WiFi.AP.SSID() == apName;
  }
  // false merely clears the SSID in this SDK; true actually ends AP mode.
  return WiFi.softAPdisconnect(true) && !(WiFi.getMode() & WIFI_AP);
}
void WifiDriver::stop() {
  if (begun) WiFi.disconnect(true, true); // WiFi.persistent(false): clear RAM credentials.
  ssid[0] = 0; begun = false;
}
} // namespace appliance
