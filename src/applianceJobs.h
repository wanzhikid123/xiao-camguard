#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
// A foreground lease first cancels conversion, then waits for its codec/SD
// resources to be released. Keep the lease through the complete operation.
class ApplianceForeground {
public:
  ApplianceForeground();
  ~ApplianceForeground();
  bool ready() const {return available;}
  ApplianceForeground(const ApplianceForeground&)=delete;
  ApplianceForeground& operator=(const ApplianceForeground&)=delete;
private: bool available=false,leased=false,serialized=false;
};
bool applianceJobsBegin(httpd_handle_t server);
bool applianceConversionBusy();
void applianceConversionStatus(char* output,size_t bytes);
#if defined(XIAO_APPLIANCE_CODEC_TEST)
bool applianceConversionTestSource(const char* path,unsigned flags=0);
bool applianceConversionTestCorrupt(const char* path);
bool applianceConversionTestPart(const char* path);
#endif
#endif
