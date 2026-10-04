#pragma once
#if defined(XIAO_APPLIANCE)
#include <Arduino.h>
struct ApplianceCodecReport {
  uint32_t frames=0, totalFrames=0, elapsedMs=0, audioSamples=0;
  uint32_t timeScale=0,timeRate=0,sourceCrc=0,outputCrc=0;
  uint16_t width=0,height=0;
  uint64_t sourceBytes=0,outputBytes=0;
  float peakTemperature=0;
  char error[64]={};
};
using ApplianceCodecAbort=bool (*)();
using ApplianceCodecProgress=void (*)(const ApplianceCodecReport&);
// Owner calls on its low-priority task. Does not publish or remove the source.
bool applianceConvertAvi(const char* source,const char* temporary,ApplianceCodecReport& report,
                         ApplianceCodecAbort abort,ApplianceCodecProgress progress=nullptr);
bool applianceValidateMp4(const char* path,const ApplianceCodecReport& expected,ApplianceCodecAbort abort=nullptr);
bool applianceInspectAvi(const char* path,ApplianceCodecReport& report,ApplianceCodecAbort abort=nullptr);
#if defined(XIAO_APPLIANCE_CODEC_TEST)
bool applianceCodecTestBusy();
void applianceCodecTestCancel();
bool applianceCodecTestPad(const char* path,ApplianceCodecReport& report,ApplianceCodecAbort abort);
#endif
#endif
