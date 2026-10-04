#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
#include "esp_camera.h"
bool applianceRegisterEventRoutes(httpd_handle_t server);
bool applianceEventsArmed();
// Capture task owns the sensor, detector, buffers and AVI lifecycle.
void applianceEventsFrame(camera_fb_t* frame);
void applianceEventsTick();
void applianceEventsStatus(char* output, size_t bytes);
// Audio owner serializes WAV writes with capture-owner tail decisions.
bool applianceAudioTailPrepare();
void applianceAudioTailRelease();
void applianceAudioTailBegin();
bool applianceAudioTailFinish(bool keep);
bool applianceAudioRecordHealthy();
bool appliancePrepareEventIndex();
#endif
