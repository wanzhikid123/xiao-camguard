#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
// The network service owns config/Wi-Fi; camera and HTTP use snapshots/commands.
bool appliancePrepare();
bool applianceBeginNetwork();
bool applianceRegisterRoutes(httpd_handle_t server);
bool applianceCheckAuth(httpd_req_t* request, bool mutation = false);
bool applianceStreamToken(httpd_req_t* request, char (&token)[65]);
bool applianceStreamAuthorized(const char* token);
bool applianceSessionValid(const char* token);
void applianceClearToken(char (&token)[65]);
bool applianceNetworkBusy();
bool applianceStreamsBusy();
bool applianceBleWifi(const char* ssid,const char* password);
void applianceBleNetworkStatus(char* wifi,size_t wifiBytes,char* ssid,size_t ssidBytes,char* wg,size_t wgBytes);
bool applianceRequestRecording(bool start);
bool applianceRequestFactoryReset();
#endif
