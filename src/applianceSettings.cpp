#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceJpeg.h"
#include "applianceSettings.h"
#include "applianceWeb.h"
#include "applianceEvents.h"
#include "applianceOta.h"
#include "applianceTimelapse.h"
#include "applianceJobs.h"
#include "applianceMedia.h"
#include <HttpAuth.h>
#include <Preferences.h>
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include <atomic>
#include <cmath>
#include <algorithm>

namespace {
struct Parameter { const char* name; int32_t minimum, maximum, initial; };
const Parameter parameters[] = {
  {"videoResolution",FRAMESIZE_QVGA,FRAMESIZE_UXGA,FRAMESIZE_VGA},
  {"photoResolution",FRAMESIZE_QVGA,FRAMESIZE_QXGA,FRAMESIZE_QXGA},
  {"fps",1,30,15},{"videoQuality",5,63,10},{"photoQuality",5,63,8},
  {"brightness",-2,2,1},{"contrast",-2,2,0},{"saturation",-2,2,0},
  {"sharpness",-3,3,0},{"denoise",0,8,0},{"effect",0,6,0},
  {"autoWhiteBalance",0,1,1},{"whiteBalanceGain",0,1,1},{"whiteBalance",0,4,0},
  {"autoExposure",0,1,1},{"exposureLevel",-2,2,0},{"exposure",0,1200,300},
  {"autoGain",0,1,1},{"gain",0,30,0},{"gainCeiling",0,6,2},
  {"mirror",0,1,0},{"flip",0,1,1},{"badPixelCorrection",0,1,1},
  {"whitePixelCorrection",0,1,1},{"gamma",0,1,1},{"lensCorrection",0,1,1},
  {"micGain",0,7,5},{"cpuMHz",80,240,240},{"powerSave",0,1,1},
  {"idleSeconds",30,1800,120},{"minimumFreeMB",1,1048576,1024},
};
constexpr size_t COUNT = sizeof(parameters)/sizeof(parameters[0]);
static_assert(COUNT<=64,"Settings request duplicate mask must cover every field");
enum Index { VIDEO,PHOTO,FPS_SETTING,VQUALITY,PQUALITY,BRIGHTNESS,CONTRAST,SATURATION,
  SHARPNESS,DENOISE,EFFECT,AWB,AWB_GAIN,WB,AEC,AE_LEVEL,EXPOSURE,AGC,GAIN,GAIN_CEILING,
  MIRROR,FLIP,BPC,WPC,GAMMA,LENC,MIC,CPU,POWER,IDLE,MIN_FREE };
struct Values { int32_t value[COUNT]; };
struct Record { uint32_t magic; uint16_t version, bytes; uint32_t revision; Values values; uint32_t crc; };
constexpr uint32_t MAGIC = 0x43415031;
Preferences preferences;
SemaphoreHandle_t mutex = nullptr, done = nullptr;
Values active = {}, candidate = {};
enum class Command { None, Apply, Photo, CodecSuspend, CodecResume };
Command command = Command::None;
bool working = false, succeeded = false, writable = false;
uint8_t bank = 0;
uint32_t revision = 0;
const char* lastError = "none";
std::atomic<bool> idle{false}, changing{false}, wakeRequested{false};
std::atomic<bool> sensorSleeping{false};
std::atomic<uint32_t> lastWakeFrameMs{0};
uint32_t lastActivity = 0;
class Lock { public: Lock(){xSemaphoreTake(mutex,portMAX_DELAY);} ~Lock(){xSemaphoreGive(mutex);} };
class Change {
  bool previous;
public:
  Change():previous(changing.exchange(true)){}
  ~Change(){changing.store(previous);}
};
Values defaults() { Values result = {}; for(size_t i=0;i<COUNT;++i)result.value[i]=parameters[i].initial; return result; }
bool supportedResolution(int value, bool photo) {
  const int options[] = {FRAMESIZE_QVGA,FRAMESIZE_VGA,FRAMESIZE_SVGA,FRAMESIZE_XGA,
    FRAMESIZE_HD,FRAMESIZE_SXGA,FRAMESIZE_UXGA,FRAMESIZE_QXGA};
  for(int option:options) if(option==value && (photo || option<=FRAMESIZE_UXGA)) return true;
  return false;
}
bool valid(const Values& values) {
  for(size_t i=0;i<COUNT;++i)if(values.value[i]<parameters[i].minimum||values.value[i]>parameters[i].maximum)return false;
  return supportedResolution(values.value[VIDEO],false)&&supportedResolution(values.value[PHOTO],true)
    && (values.value[CPU]==80||values.value[CPU]==160||values.value[CPU]==240);
}
uint32_t crc32(const void* data,size_t bytes) {
  uint32_t crc=0xffffffff; const auto* p=static_cast<const uint8_t*>(data);
  while(bytes--){crc^=*p++;for(unsigned i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u&(0u-(crc&1u)));} return ~crc;
}
bool read(uint8_t target,Record& record) {
  const char* key=target?"bank1":"bank0";
  return preferences.getBytesLength(key)==sizeof(record)
    &&preferences.getBytes(key,&record,sizeof(record))==sizeof(record)
    &&record.magic==MAGIC&&record.version==1&&record.bytes==sizeof(record)&&record.revision
    &&record.crc==crc32(&record,offsetof(Record,crc))&&valid(record.values);
}
bool futureRecord(uint8_t target) {
  const char* key=target?"bank1":"bank0";const size_t bytes=preferences.getBytesLength(key);
  if(bytes>sizeof(Record))return true;
  Record record={};return bytes==sizeof(record)&&preferences.getBytes(key,&record,sizeof(record))==sizeof(record)
    &&record.magic==MAGIC&&record.version!=1;
}
bool save(const Values& values) {
  if(!writable||revision==UINT32_MAX)return false;
  Record record={MAGIC,1,sizeof(Record),revision+1,values,0};
  record.crc=crc32(&record,offsetof(Record,crc)); const uint8_t target=1-bank;
  const char* key=target?"bank1":"bank0"; Record checked={};
  if(preferences.putBytes(key,&record,sizeof(record))!=sizeof(record)||!read(target,checked)
    ||memcmp(&record,&checked,sizeof(record)))return false;
  if(preferences.putUChar("selected",target)!=1||preferences.getUChar("selected",255)!=target)return false;
  bank=target;revision=record.revision;return true;
}
bool recording(){return forceRecord||isCapturing||stopPlayback||applianceEventsArmed();}
bool streaming(){return applianceStreamsBusy();}
bool applySensor(const Values& v) {
  sensor_t* s=esp_camera_sensor_get();if(!s)return false;
  // One task owns every sensor change. Record/HTTP workers never reconfigure it.
  const int32_t* p=v.value; bool ok=true;
  auto check=[&](int result){if(result)ok=false;};
  check(s->set_framesize(s,static_cast<framesize_t>(p[VIDEO])));
  check(s->set_quality(s,p[VQUALITY]));
  check(s->set_brightness(s,p[BRIGHTNESS]));check(s->set_contrast(s,p[CONTRAST]));
  check(s->set_saturation(s,p[SATURATION]));check(s->set_sharpness(s,p[SHARPNESS]));
  check(s->set_denoise(s,p[DENOISE]));check(s->set_special_effect(s,p[EFFECT]));
  check(s->set_whitebal(s,p[AWB]));check(s->set_awb_gain(s,p[AWB_GAIN]));check(s->set_wb_mode(s,p[WB]));
  check(s->set_exposure_ctrl(s,p[AEC]));check(s->set_ae_level(s,p[AE_LEVEL]));
  if(!p[AEC])check(s->set_aec_value(s,p[EXPOSURE]));
  check(s->set_gain_ctrl(s,p[AGC]));if(!p[AGC])check(s->set_agc_gain(s,p[GAIN]));
  check(s->set_gainceiling(s,static_cast<gainceiling_t>(p[GAIN_CEILING])));
  check(s->set_hmirror(s,p[MIRROR]));check(s->set_vflip(s,p[FLIP]));
  check(s->set_bpc(s,p[BPC]));check(s->set_wpc(s,p[WPC]));check(s->set_raw_gma(s,p[GAMMA]));check(s->set_lenc(s,p[LENC]));
  if(ok){ fsizePtr=p[VIDEO];setFPS(p[FPS_SETTING]);micGain=p[MIC];
    char value[8];snprintf(value,sizeof(value),"%d",p[VIDEO]);updateConfigVect("framesize",value);
    snprintf(value,sizeof(value),"%d",p[FPS_SETTING]);updateConfigVect("fps",value);
  }
  return ok;
}
bool sensorStandby(bool sleeping) {
  sensor_t* sensor=esp_camera_sensor_get();
  // OV3660 SYSTEM_CTROL0 bit 6 is software power down. Only touch that bit;
  // retain reset/clock bits and never apply this register to another sensor.
  constexpr int control=0x3008, mask=0x40;
  const int target=sleeping?mask:0;
  if(!sensor||sensor->id.PID!=OV3660_PID||!sensor->set_reg||!sensor->get_reg
    ||sensor->set_reg(sensor,control,mask,target)!=0
    ||sensor->get_reg(sensor,control,mask)!=target)return false;
  sensorSleeping.store(sleeping);return true;
}
bool freshFrameAfter(int64_t wakeAt) {
  // Discard frames queued before standby and a frame whose DMA started before
  // sleep. The driver's timestamp records frame start, not HTTP delivery time.
  for(unsigned i=0;i<8&&esp_timer_get_time()-wakeAt<6000000;++i) {
    camera_fb_t* frame=esp_camera_fb_get();if(!frame)return false;
    const int64_t started=int64_t(frame->timestamp.tv_sec)*1000000+frame->timestamp.tv_usec;
    const bool fresh=started>=wakeAt&&frame->len>4;
    esp_camera_fb_return(frame);
    if(fresh){lastWakeFrameMs.store(uint32_t(started/1000));return true;}
  }
  return false;
}
bool resume(const Values& values) {
  Change change;
  const bool waking=idle.load();
  const bool retained=sensorSleeping.load();
  // The OV3660 keeps its driver/buffers while its sensor is powered down.
  // Other sensors retain the old full teardown/startup-clock fallback.
  if(!setCpuFrequencyMhz(waking&&!retained?240:values.value[CPU]))return false;
  WiFi.setSleep(false);
  if(waking) {
    if(retained) {
      const int64_t wakeAt=esp_timer_get_time();
      if(!sensorStandby(false)||!freshFrameAfter(wakeAt)) {
        sensorStandby(true);setCpuFrequencyMhz(values.value[CPU]);return false;
      }
    } else if(!prepCam()||!applySensor(values)) {
      esp_camera_deinit();setCpuFrequencyMhz(values.value[CPU]);return false;
    }
    idle.store(false);LOG_INF("Camera resumed from idle standby");
  }
  return setCpuFrequencyMhz(values.value[CPU]);
}
bool capturePhoto(const Values& values) {
  if(!resume(values))return false;
  sensor_t* sensor=esp_camera_sensor_get();if(!sensor)return false;
  const int target=recording()?values.value[VIDEO]:values.value[PHOTO];
  if(recording()&&values.value[PHOTO]!=values.value[VIDEO])return false;
  const int quality=recording()?values.value[VQUALITY]:values.value[PQUALITY];
  bool ok=sensor->set_framesize(sensor,static_cast<framesize_t>(target))==0&&sensor->set_quality(sensor,quality)==0;
  alertBufferSize=0;
  // Discard queued frames from the previous sensor dimensions/quality.
  bool kept=false;
  for(unsigned i=0;ok&&i<APPLIANCE_CAMERA_BUFFERS+6;++i) {
    camera_fb_t* frame=esp_camera_fb_get();if(!frame){ok=false;break;}
    uint16_t width=0,height=0;
    if(i>=APPLIANCE_CAMERA_BUFFERS&&frame->len>4&&frame->len<maxAlertBuffSize&&applianceJpegSize(frame->buf,frame->len,width,height)
      &&width==frameData[target].frameWidth&&height==frameData[target].frameHeight){keepFrame(frame);kept=true;}
    esp_camera_fb_return(frame);
    if(kept)break;
  }
  const bool restored=sensor->set_framesize(sensor,static_cast<framesize_t>(values.value[VIDEO]))==0
    &&sensor->set_quality(sensor,values.value[VQUALITY])==0;
  // Drain the still-photo pipeline before normal video frames are published.
  for(unsigned i=0;restored&&i<3;++i){camera_fb_t* frame=esp_camera_fb_get();if(frame)esp_camera_fb_return(frame);}
  return ok&&kept&&restored&&alertBufferSize;
}
esp_err_t json(httpd_req_t* req,const char* status,const char* body) {
  appliance::HttpAuth::responseHeaders(req);httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");
  return httpd_resp_sendstr(req,body);
}
void addValues(cJSON* root,const char* key,const Values& values) {
  cJSON* object=cJSON_AddObjectToObject(root,key);
  for(size_t i=0;i<COUNT;++i)cJSON_AddNumberToObject(object,parameters[i].name,values.value[i]);
}
esp_err_t settings(httpd_req_t* req) {
  if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  if(strchr(req->uri,'?'))return json(req,"400 Bad Request","{\"error\":\"query_not_allowed\"}");
  if(req->method==HTTP_GET) {
    Values selected;uint32_t rev;bool pending;const char* error;
    {Lock lock;selected=active;rev=revision;pending=working||command!=Command::None;error=lastError;}
    cJSON* root=cJSON_CreateObject();addValues(root,"values",selected);addValues(root,"defaults",defaults());
    cJSON_AddNumberToObject(root,"revision",rev);cJSON_AddBoolToObject(root,"busy",pending);
    cJSON_AddBoolToObject(root,"persistent",writable);cJSON_AddStringToObject(root,"error",error);
    cJSON_AddStringToObject(root,"sensor",camModel);
    for(bool photo:{false,true}) {
      cJSON* array=cJSON_AddArrayToObject(root,photo?"photoResolutions":"videoResolutions");
      for(int i=0;i<=FRAMESIZE_QXGA;++i)if(supportedResolution(i,photo)) {
        cJSON* item=cJSON_CreateObject();cJSON_AddItemToArray(array,item);
        cJSON_AddNumberToObject(item,"value",i);cJSON_AddNumberToObject(item,"width",frameData[i].frameWidth);
        cJSON_AddNumberToObject(item,"height",frameData[i].frameHeight);
      }
    }
    char* output=cJSON_PrintUnformatted(root);esp_err_t result=output?json(req,"200 OK",output):json(req,"503 Service Unavailable","{\"error\":\"memory_unavailable\"}");
    free(output);cJSON_Delete(root);return result;
  }
  if(recording())return json(req,"409 Conflict","{\"error\":\"recording_in_progress\"}");
  if(applianceTimelapseActive())return json(req,"409 Conflict","{\"error\":\"stop_timelapse_before_settings\"}");
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  char type[48]={},raw[1401]={};
  if(!req->content_len||req->content_len>1400||httpd_req_get_hdr_value_len(req,"Content-Type")>=sizeof(type)
    ||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
    return json(req,"400 Bad Request","{\"error\":\"invalid_settings_request\"}");
  size_t received=0;uint32_t started=millis();
  while(received<req->content_len) {
    if(millis()-started>5000)return json(req,"408 Request Timeout","{\"error\":\"request_timeout\"}");
    int count=httpd_req_recv(req,raw+received,req->content_len-received);
    if(count<=0)return json(req,"400 Bad Request","{\"error\":\"invalid_settings_request\"}");received+=count;
  }
  unsigned braces=0; for(size_t i=0;i<received;++i)if(raw[i]=='{')++braces;
  // Numeric fields in a single values object; bound recursion before cJSON.
  cJSON* root=!memchr(raw,0,received)&&!strchr(raw,'\\')&&!strchr(raw,'[')&&braces==2
    ?cJSON_ParseWithLengthOpts(raw,received+1,nullptr,true):nullptr;
  cJSON* values=root?cJSON_GetObjectItemCaseSensitive(root,"values"):nullptr;
  bool acceptable=cJSON_IsObject(root)&&cJSON_GetArraySize(root)==1&&cJSON_IsObject(values)&&cJSON_GetArraySize(values)>0;
  Values next;{Lock lock;next=active;} uint64_t seen=0;
  for(cJSON* item=acceptable?values->child:nullptr;item;item=item->next) {
    size_t index=0;while(index<COUNT&&strcmp(item->string,parameters[index].name))++index;
    if(index==COUNT||!cJSON_IsNumber(item)||!std::isfinite(item->valuedouble)||floor(item->valuedouble)!=item->valuedouble
      ||item->valuedouble<parameters[index].minimum||item->valuedouble>parameters[index].maximum||(seen&(1ULL<<index))){acceptable=false;break;}
    next.value[index]=static_cast<int32_t>(item->valuedouble);seen|=1ULL<<index;
  }
  cJSON_Delete(root);
  if(!acceptable||!valid(next))return json(req,"400 Bad Request","{\"error\":\"invalid_settings\"}");
  if(!appSetupDone||startupFailure[0])return json(req,"503 Service Unavailable","{\"error\":\"camera_unavailable\"}");
  {Lock lock;if(command!=Command::None||working||changing.load())return json(req,"409 Conflict","{\"error\":\"capture_busy\"}");
    candidate=next;command=Command::Apply;changing.store(true);xSemaphoreTake(done,0);}
  if(xSemaphoreTake(done,pdMS_TO_TICKS(20000))!=pdTRUE)return json(req,"504 Gateway Timeout","{\"error\":\"capture_timeout\"}");
  bool success;const char* error;{Lock lock;success=succeeded;error=lastError;}
  char response[128];snprintf(response,sizeof(response),success?"{\"saved\":true}":"{\"error\":\"%s\"}",error);
  return json(req,success?"200 OK":"500 Internal Server Error",response);
}
} // namespace

bool applianceSettingsBegin() {
  mutex=xSemaphoreCreateMutex();done=xSemaphoreCreateBinary();if(!mutex||!done)return false;
  active=defaults();writable=preferences.begin("camcapture",false);
  if(writable&&preferences.isKey("selected")) {
    bank=preferences.getUChar("selected",255);Record record={};
    if(bank<2&&futureRecord(bank)){writable=false;lastError="settings_storage_unsupported";}
    else if(bank<2&&read(bank,record)){active=record.values;revision=record.revision;}
    else if(bank<2&&read(1-bank,record)){bank=1-bank;active=record.values;revision=record.revision;lastError="previous_settings_recovered";}
    else {writable=false;lastError="settings_storage_invalid";bank=0;}
  } else if(!writable)lastError="settings_storage_unavailable";
  if(writable&&(futureRecord(0)||futureRecord(1))){writable=false;lastError="settings_storage_unsupported";}
  lastActivity=millis();return true;
}
bool applianceRegisterSettingsRoutes(httpd_handle_t server) {
  const httpd_uri_t routes[]={
    {.uri="/api/settings/capture",.method=HTTP_GET,.handler=settings,.user_ctx=nullptr},
    {.uri="/api/settings/capture",.method=HTTP_POST,.handler=settings,.user_ctx=nullptr},
  };
  for(const auto& route:routes)if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;return true;
}
bool applianceSettingsCaptureStart() {
  Values values;{Lock lock;values=active;}
  return setCpuFrequencyMhz(values.value[CPU])&&applySensor(values);
}
bool applianceSettingsCaptureTick() {
  if(startupFailure[0])return false;
  if(applianceMaintenance()||applianceConversionBusy()){Lock lock;
    if(command!=Command::CodecResume&&(command!=Command::CodecSuspend||applianceMaintenance()))return false;}
  Command work;Values next,current;
  {Lock lock;work=command;current=active;next=candidate;if(work!=Command::None){working=true;command=Command::None;}}
  if(work!=Command::None) {
    bool result=false;const char* error="none";
    if(recording()&&work==Command::Apply)error="recording_in_progress";
    else if(work==Command::Apply) {
      changing.store(true);
      if(!resume(current)||!setCpuFrequencyMhz(next.value[CPU])||!applySensor(next))error="sensor_settings_failed";
      else {
        bool fresh=false;
        for(unsigned i=0;i<APPLIANCE_CAMERA_BUFFERS+6;++i){camera_fb_t* frame=esp_camera_fb_get();if(!frame)break;uint16_t width=0,height=0;
          fresh=i>=APPLIANCE_CAMERA_BUFFERS&&applianceJpegSize(frame->buf,frame->len,width,height)
            &&width==frameData[next.value[VIDEO]].frameWidth&&height==frameData[next.value[VIDEO]].frameHeight;
          esp_camera_fb_return(frame);if(fresh)break;}
        if(!fresh)error="sensor_frames_not_ready";else{Lock lock;if(!save(next))error="settings_storage_failed";else result=true;}
      }
      if(!result){setCpuFrequencyMhz(current.value[CPU]);applySensor(current);}
    } else if(work==Command::CodecSuspend){
      changing.store(true);idle.store(true);wakeRequested.store(false);
      const uint32_t began=millis();while(applianceAudioInputRunning()&&uint32_t(millis()-began)<1500)delay(5);
      result=!applianceAudioInputRunning()&&(!esp_camera_sensor_get()||esp_camera_deinit()==ESP_OK);
      if(result)sensorSleeping.store(false);else{error="camera_suspend_failed";resume(current);}
    } else if(work==Command::CodecResume){result=resume(current);if(!result)error="camera_resume_failed";
    } else {result=capturePhoto(current);if(!result)error="photo_capture_failed";}
    {Lock lock;if(result&&work==Command::Apply)active=next;succeeded=result;lastError=error;working=false;}
    changing.store(false);lastActivity=millis();xSemaphoreGive(done);
  }
  if(applianceMaintenance()||applianceConversionBusy())return false;
  const bool needed=wakeRequested.exchange(false)||recording()||doKeepFrame||streaming();
  if(needed){lastActivity=millis();if(idle.load()&&!resume(current)){
    // Failed wake cannot leave a recording request blocking normal recovery,
    // or allow an old kept frame to satisfy the current still request.
    forceRecord=false;doKeepFrame=false;alertBufferSize=0;
    LOG_WRN("Camera standby wake failed");return false;
  }}
  if(!needed&&!idle.load()&&current.value[POWER]&&millis()-lastActivity>=uint32_t(current.value[IDLE])*1000) {
    changing.store(true);
    // Stop the audio owner and wait for its actual RX handle. OV3660 software
    // power down avoids repeated camera/GDMA teardown after audio recording.
    idle.store(true);
    const uint32_t stopping=millis();
    while(applianceAudioInputRunning()&&uint32_t(millis()-stopping)<1500)delay(5);
    sensor_t* sensor=esp_camera_sensor_get();
    const bool retained=sensor&&sensor->id.PID==OV3660_PID;
    const bool stopped=!applianceAudioInputRunning()
      &&(retained?sensorStandby(true):esp_camera_deinit()==ESP_OK);
    if(stopped) {
      WiFi.setSleep(true);LOG_INF("Idle standby: %s, Wi-Fi modem sleep enabled",retained?"sensor powered down":"camera stopped");
    } else {
      if(retained)sensorStandby(false);
      idle.store(false);LOG_WRN("Idle standby could not stop capture resources");
    }
    changing.store(false);
  }
  return !idle.load();
}
bool applianceConfiguredPhoto(bool* occupied) {
  if(occupied)*occupied=false;
  if(!mutex)return false;
  {Lock lock;if(command!=Command::None||working||changing.load()){if(occupied)*occupied=true;return false;}
    if(recording()&&active.value[PHOTO]!=active.value[VIDEO]){if(occupied)*occupied=true;return false;}
    command=Command::Photo;changing.store(true);xSemaphoreTake(done,0);}
  if(xSemaphoreTake(done,pdMS_TO_TICKS(20000))!=pdTRUE)return false;
  Lock lock;return succeeded;
}
namespace {
bool codecCamera(Command request){if(!mutex)return false;
  {Lock lock;if(command!=Command::None||working||changing.load()||(request==Command::CodecSuspend&&recording()))return false;
    command=request;changing.store(true);xSemaphoreTake(done,0);}
  if(xSemaphoreTake(done,pdMS_TO_TICKS(20000))!=pdTRUE){Lock lock;
    if(command==request){command=Command::None;changing.store(false);}return false;}
  Lock lock;return succeeded;}
}
bool applianceCodecCameraSuspend(){return codecCamera(Command::CodecSuspend);}
bool applianceCodecCameraResume(){return codecCamera(Command::CodecResume);}
bool applianceCaptureIdle(){return idle.load();}
bool applianceCameraSensorSleeping(){return sensorSleeping.load();}
uint32_t applianceLastWakeFrameMs(){return lastWakeFrameMs.load();}
bool applianceCaptureBusy(){return changing.load()||appliancePhotoBusy();}
bool applianceAudioNeeded(){
  if(!mutex)return false;
  Lock lock;return active.value[MIC]!=0&&(!idle.load()||recording());
}
void applianceCaptureWake(){wakeRequested.store(true);}
bool applianceStorageSpaceAvailable() {
  if(SD.cardType()==CARD_NONE)return false;
  Values values;{Lock lock;values=active;}
  const uint64_t total=SD.totalBytes(),used=SD.usedBytes();
  return total>=used&&(total-used)>uint64_t(values.value[MIN_FREE])*1024*1024;
}
void appliancePhotoSettings(uint16_t& width,uint16_t& height,int& quality){Lock lock;
  width=frameData[active.value[PHOTO]].frameWidth;height=frameData[active.value[PHOTO]].frameHeight;quality=active.value[PQUALITY];}
void applianceCpuUsage(float (&percent)[2],bool& available) {
  static uint32_t previous[2]={};static int64_t at=0;static float measured[2]={};static bool ready=false;
  const int64_t now=esp_timer_get_time();
  if(!at||now-at>=2000000) {
    for(int core=0;core<2;++core){TaskStatus_t info={};vTaskGetInfo(xTaskGetIdleTaskHandleForCore(core),&info,pdFALSE,eInvalid);
      const uint32_t counter=info.ulRunTimeCounter;
      if(at&&now-at<UINT32_MAX){double fraction=double(uint32_t(counter-previous[core]))/double(now-at);
        measured[core]=float(100.0*(1.0-std::clamp(fraction,0.0,1.0)));}previous[core]=counter;}
    ready=at!=0;at=now;
  }
  percent[0]=measured[0];percent[1]=measured[1];available=ready;
}
#endif
