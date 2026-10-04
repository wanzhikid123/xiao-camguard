#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
bool applianceBleBegin(httpd_handle_t server);
void applianceBleStatus(char* output,size_t bytes);
#endif
