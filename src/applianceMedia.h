#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
bool applianceRegisterMediaRoutes(httpd_handle_t server);
bool applianceMediaBusy();
bool appliancePhotoBusy();
bool applianceMediaPath(const char* path);
bool applianceSaveConfiguredPhoto(const char* path,bool* occupied=nullptr);
bool applianceSaveManualPhoto(char* path,size_t bytes);
#endif
