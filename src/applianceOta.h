#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
bool applianceRegisterOtaRoutes(httpd_handle_t server);
bool applianceMaintenance();
bool applianceBeginOtaHealth();
void applianceOtaHttpReady();
#endif
