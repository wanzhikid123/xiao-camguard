#pragma once
#if defined(XIAO_APPLIANCE)
#include "esp_http_server.h"
bool applianceSettingsBegin();
bool applianceRegisterSettingsRoutes(httpd_handle_t server);
bool applianceSettingsCaptureStart();
// Called exclusively by the existing capture task before acquiring a frame.
bool applianceSettingsCaptureTick();
bool applianceConfiguredPhoto(bool* occupied=nullptr);
bool applianceCodecCameraSuspend();
bool applianceCodecCameraResume();
bool applianceCaptureIdle();
bool applianceCameraSensorSleeping();
uint32_t applianceLastWakeFrameMs();
bool applianceCaptureBusy();
bool applianceAudioNeeded();
bool applianceAudioInputRunning();
bool applianceStorageSpaceAvailable();
void applianceCaptureWake();
void applianceCpuUsage(float (&percent)[2], bool& available);
void appliancePhotoSettings(uint16_t& width,uint16_t& height,int& quality);
#endif
