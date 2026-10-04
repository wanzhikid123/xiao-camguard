#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
bool applianceTimelapseBegin(httpd_handle_t server);
bool applianceTimelapseActive();
bool applianceTimelapseStart(uint32_t intervalSeconds=900,bool immediate=true);
void applianceTimelapseStop();
bool applianceTimelapseStopAndWait(uint32_t timeoutMs=25000);
void applianceTimelapseStatus(char* output,size_t bytes);
bool applianceTimelapseMediaPath(const char* path);
#endif
