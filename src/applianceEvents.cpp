#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceEvents.h"
#include "applianceSettings.h"
#include "applianceMedia.h"
#include "applianceWeb.h"
#include "applianceOta.h"
#include "applianceTimelapse.h"
#include "applianceJobs.h"
#include <EventMotion.h>
#include <HttpAuth.h>
#include "jpeg_decoder.h"
#include "esp_random.h"
#include "cJSON.h"
#include <atomic>
#if defined(XIAO_APPLIANCE_EVENT_TEST)
#include <lwip/sockets.h>
#endif
extern SemaphoreHandle_t aviMutex;

namespace {
constexpr size_t PIXELS=80*60, TAIL_BYTES=512*1024, TAIL_FRAMES=32;
constexpr uint32_t SAMPLE_MS=250, QUIET_MS=1000, SETTLE_MS=2000, MAX_CLIP_MS=300000;
constexpr const char* TEMP="/event-current.avi";
std::atomic<bool> requested{false}, armed{false}, tailStatus{false};
std::atomic<unsigned> sensitivity{5}, score{0}, saved{0}, decodeMs{0};
std::atomic<const char*> error{"none"};
uint8_t *rgb=nullptr, *gray=nullptr, *previous=nullptr, *tail=nullptr;
struct Held { size_t offset, bytes; } held[TAIL_FRAMES];
size_t tailBytes=0, heldCount=0;
uint32_t sampled=0, settledAt=0, quietAt=0, started=0, spaceAt=0;
unsigned positives=0, failures=0;
bool background=false, motion=false, holding=false;
File file;
uint16_t frames=0;
char stamp[FILE_NAME_LEN]={};
uint32_t eventId=0;
#if defined(XIAO_APPLIANCE_EVENT_TEST)
// RAM-only scene substitutions after actual VGA JPEG decode. No sensor,
// account, camera configuration or existing media is changed by this fixture.
std::atomic<unsigned> testScene{0};
uint32_t testStep=0;
void testGray(uint8_t* output) {
  const unsigned scene=testScene.load();if(!scene)return;
  const uint32_t step=++testStep;
  for(size_t i=0;i<PIXELS;++i) {
    const unsigned x=i%80,y=i/80;
    output[i]=scene==2?(x>=20&&x<60&&y>=15&&y<45?(step&1?180:40):100)
      :scene==3?(step&1?100:130):scene==4?100+((i*13+step*7)%9)-4:100;
  }
}
esp_err_t fixture(httpd_req_t* req);
#endif

esp_err_t json(httpd_req_t* req,const char* status,const char* body) {
  appliance::HttpAuth::responseHeaders(req);httpd_resp_set_status(req,status);
  httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,body);
}
bool write(const void* data,size_t bytes) {
  const uint8_t* p=static_cast<const uint8_t*>(data);
  while(bytes) {size_t n=std::min(bytes,size_t(8192));if(file.write(p,n)!=n)return false;p+=n;bytes-=n;}
  return true;
}
bool append(const uint8_t* jpeg,size_t bytes) {
  const uint32_t padded=(bytes+3)&~3u;
  const uint8_t zero[3]={};
  if(!write(dcBuf,4)||!write(&padded,4)||!write(jpeg,bytes)||!write(zero,padded-bytes))return false;
  buildAviIdx(padded);++frames;return true;
}
void resetTail() {heldCount=tailBytes=0;holding=false;tailStatus.store(false);}
void discardFile() {
  applianceAudioTailFinish(false);finishAudioRecord(false);
  if(file)file.close();SD.remove(TEMP);SD.remove(WAVTEMP);
  isCapturing=stopPlayback=false;resetTail();
}
bool close(uint32_t endAt) {
  // finishAudioRecord atomically stops writes and discards the candidate tail;
  // do not resume the audio writer between discarding and finalization.
  bool healthy=applianceAudioRecordHealthy();finishAudioRecord(true);
  const uint32_t duration=std::max(uint32_t(1),uint32_t(endAt-started));
  bool ok=file&&frames&&healthy;
  const bool sound=ok&&haveWavFile();
  if(sound) {size_t bytes;do {bytes=writeWavFile(iSDbuffer,RAMSIZE);if(bytes&&!write(iSDbuffer,bytes))ok=false;}while(bytes);}
  if(ok) {
    finalizeAviIndex(frames);size_t bytes;
    do {bytes=writeAviIndex(iSDbuffer,RAMSIZE);if(bytes&&!write(iSDbuffer,bytes))ok=false;}while(bytes);
    xSemaphoreTake(aviMutex,portMAX_DELAY);
    buildAviHdr(15,FRAMESIZE_VGA,frames,false,duration);
    if(!file.seek(0,SeekSet)||!write(aviHeader,AVI_HEADER_LEN))ok=false;
    xSemaphoreGive(aviMutex);
    file.flush();
  }
  if(file)file.close();
  char path[FILE_NAME_LEN];
  snprintf(path,sizeof(path),"%s_%06lx_event_VGA_S.avi",stamp,(unsigned long)eventId);
  if(ok)ok=!SD.exists(path)&&SD.rename(TEMP,path);
  if(ok){saved.fetch_add(1);LOG_INF("Saved 480p event: %u frames, %lu ms",frames,(unsigned long)duration);}
  else {SD.remove(TEMP);error.store("event_write_failed");}
  SD.remove(WAVTEMP);isCapturing=stopPlayback=false;resetTail();return ok;
}
bool open() {
  if(!applianceStorageSpaceAvailable()){error.store("minimum_free_space");return false;}
  dateFormat(stamp,sizeof(stamp),false);char folder[10]={};memcpy(folder,stamp,9);
  if(!SD.exists(folder)&&!SD.mkdir(folder)){error.store("event_directory_failed");return false;}
  bool unique=false;
  for(unsigned i=0;i<8&&!unique;++i){eventId=esp_random()&0xffffff;char path[FILE_NAME_LEN];
    snprintf(path,sizeof(path),"%s_%06lx_event_VGA_S.avi",stamp,(unsigned long)eventId);unique=!SD.exists(path);}
  if(!unique){error.store("event_name_collision");return false;}
  file=SD.open(TEMP,FILE_WRITE);uint8_t blank[AVI_HEADER_LEN]={};
  if(!file||!write(blank,sizeof(blank))){discardFile();error.store("event_write_failed");return false;}
  if(!appliancePrepareEventIndex()){discardFile();error.store("event_memory_failed");return false;}
  frames=0;resetTail();started=millis();startAudioRecord();
  isCapturing=stopPlayback=true;return true;
}
void release() {
  free(rgb);free(gray);free(previous);free(tail);rgb=gray=previous=tail=nullptr;
  applianceAudioTailRelease();background=motion=false;positives=failures=0;score.store(0);
}
void stop() {
  if(isCapturing)close(holding?quietAt:millis());
  // Sensor changes and buffer release happen exclusively in the capture owner.
  // Keep mode guards active until the stored resolution/FPS is restored;
  // otherwise HTTP could observe disarmed while the sensor is still VGA.
  requested.store(false);release();
  if(!applianceSettingsCaptureStart())error.store("sensor_restore_failed");
  applianceCaptureWake();
  armed.store(false);
}
bool detect(camera_fb_t* frame) {
  const uint32_t begin=millis();
  esp_jpeg_image_cfg_t config={};esp_jpeg_image_output_t image={};
  config.indata=frame->buf;config.indata_size=frame->len;config.outbuf=rgb;config.outbuf_size=PIXELS*3;
  config.out_format=JPEG_IMAGE_FORMAT_RGB888;config.out_scale=JPEG_IMAGE_SCALE_1_8;
  // get_image_info reports source dimensions; decode reports scaled dimensions.
  if(esp_jpeg_get_image_info(&config,&image)!=ESP_OK||image.width!=640||image.height!=480||image.output_len>PIXELS*3
    ||esp_jpeg_decode(&config,&image)!=ESP_OK) {
    decodeMs.store(millis()-begin);if(++failures>=8){error.store("event_decode_failed");requested.store(false);}return false;
  }
  failures=0;
  for(size_t i=0;i<PIXELS;++i)gray[i]=(77u*rgb[3*i]+150u*rgb[3*i+1]+29u*rgb[3*i+2])>>8;
#if defined(XIAO_APPLIANCE_EVENT_TEST)
  testGray(gray);
#endif
  const unsigned changed=background?appliance::eventChangedPermille(gray,previous,PIXELS):0;
  memcpy(previous,gray,PIXELS);background=true;score.store(changed);decodeMs.store(millis()-begin);
  if(uint32_t(millis()-settledAt)<SETTLE_MS){positives=0;motion=false;return true;}
  motion=changed>=appliance::eventMotionThreshold(sensitivity.load());
  positives=motion?positives+1:0;return true;
}
esp_err_t control(httpd_req_t* req) {
  if(!applianceCheckAuth(req,true))return ESP_OK;
  if(strchr(req->uri,'?'))return json(req,"400 Bad Request","{\"error\":\"query_not_allowed\"}");
  const bool start=!strcmp(req->uri,"/api/events/start");
  if(start&&applianceTimelapseActive())return json(req,"409 Conflict","{\"error\":\"stop_timelapse_first\"}");
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  if(start&&applianceTimelapseActive())return json(req,"409 Conflict","{\"error\":\"stop_timelapse_first\"}");
  if(!start) {
    if(req->content_len)return json(req,"400 Bad Request","{\"error\":\"empty_body_required\"}");
    requested.store(false);return json(req,"202 Accepted","{\"eventRequested\":false}");
  }
  unsigned level=5;
  if(req->content_len) {
    char type[32]={},raw[65]={};
    if(req->content_len>64||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
      return json(req,"400 Bad Request","{\"error\":\"invalid_event_request\"}");
    size_t got=0;while(got<req->content_len){int n=httpd_req_recv(req,raw+got,req->content_len-got);if(n<=0)break;got+=n;}
    cJSON* root=got==req->content_len&&!memchr(raw,0,got)&&!strchr(raw,'\\')&&!strchr(raw,'[')
      ?cJSON_ParseWithLengthOpts(raw,got+1,nullptr,true):nullptr;
    cJSON* value=root?cJSON_GetObjectItemCaseSensitive(root,"sensitivity"):nullptr;
    bool valid=cJSON_IsObject(root)&&cJSON_GetArraySize(root)==1&&cJSON_IsNumber(value)
      &&value->valuedouble==value->valueint&&value->valueint>=1&&value->valueint<=10;
    if(valid)level=value->valueint;cJSON_Delete(root);
    if(!valid)return json(req,"400 Bad Request","{\"error\":\"invalid_event_request\"}");
  }
  if(requested.load()||armed.load()||forceRecord||isCapturing||stopPlayback||doPlayback||applianceCaptureBusy()||applianceMediaBusy())
    return json(req,"409 Conflict","{\"error\":\"device_busy\"}");
  if(!appSetupDone||startupFailure[0]||SD.cardType()==CARD_NONE||!micGain)
    return json(req,"503 Service Unavailable","{\"error\":\"event_unavailable\"}");
  if(!applianceStorageSpaceAvailable())return json(req,"507 Insufficient Storage","{\"error\":\"minimum_free_space\"}");
  sensitivity.store(level);error.store("none");requested.store(true);applianceCaptureWake();
  return json(req,"202 Accepted","{\"eventRequested\":true}");
}
#if defined(XIAO_APPLIANCE_EVENT_TEST)
esp_err_t fixture(httpd_req_t* req) {
  if(!applianceCheckAuth(req,true))return ESP_OK;
  sockaddr_storage local={};socklen_t length=sizeof(local);uint32_t localIPv4=0;
  if(!getsockname(httpd_req_to_sockfd(req),reinterpret_cast<sockaddr*>(&local),&length)) {
    if(local.ss_family==AF_INET)localIPv4=reinterpret_cast<sockaddr_in*>(&local)->sin_addr.s_addr;
#if CONFIG_LWIP_IPV6
    else if(local.ss_family==AF_INET6) {
      auto* ipv6=reinterpret_cast<sockaddr_in6*>(&local);
      if(IN6_IS_ADDR_V4MAPPED(ipv6->sin6_addr.un.u32_addr))memcpy(&localIPv4,ipv6->sin6_addr.s6_addr+12,4);
    }
#endif
  }
  if(!localIPv4||WiFi.status()!=WL_CONNECTED||localIPv4!=static_cast<uint32_t>(WiFi.localIP()))
    return json(req,"403 Forbidden","{\"error\":\"lan_required\"}");
  char body[65]={};
  if(strchr(req->uri,'?')||!req->content_len||req->content_len>64)return json(req,"400 Bad Request","{}");
  size_t got=0;while(got<req->content_len){int n=httpd_req_recv(req,body+got,req->content_len-got);if(n<=0)break;got+=n;}
  cJSON* root=got==req->content_len&&!memchr(body,0,got)?cJSON_ParseWithLengthOpts(body,got+1,nullptr,true):nullptr;
  cJSON* scene=root?cJSON_GetObjectItemCaseSensitive(root,"scene"):nullptr;unsigned value=99;
  if(cJSON_IsObject(root)&&cJSON_GetArraySize(root)==1&&cJSON_IsString(scene)) {
    if(!strcmp(scene->valuestring,"selftest")) {
      uint8_t* samples=static_cast<uint8_t*>(ps_malloc(PIXELS*2));
      if(!samples){cJSON_Delete(root);return json(req,"503 Service Unavailable","{}");}
      uint8_t *a=samples,*b=samples+PIXELS;unsigned passed=0;
      memset(a,100,PIXELS);memset(b,100,PIXELS);
      if(appliance::eventChangedPermille(a,b,PIXELS)==0)++passed;
      memset(a,130,PIXELS);if(appliance::eventChangedPermille(a,b,PIXELS)==0)++passed;
      for(size_t i=0;i<PIXELS;++i)a[i]=100+(i*7%9)-4;
      if(appliance::eventChangedPermille(a,b,PIXELS)==0)++passed;
      memset(a,100,PIXELS);memset(a,180,PIXELS/50);
      if(appliance::eventChangedPermille(a,b,PIXELS)<appliance::eventMotionThreshold(5))++passed;
      memset(a,100,PIXELS);memset(a,180,PIXELS/10);
      if(appliance::eventChangedPermille(a,b,PIXELS)>=appliance::eventMotionThreshold(5))++passed;
      if(appliance::eventMotionThreshold(1)>appliance::eventMotionThreshold(5)
        &&appliance::eventMotionThreshold(5)>appliance::eventMotionThreshold(10))++passed;
      free(samples);cJSON_Delete(root);char response[64];
      snprintf(response,sizeof(response),"{\"passed\":%u,\"total\":6}",passed);return json(req,"200 OK",response);
    }
    const char* names[]={"actual","static","motion","lighting","noise"};
    for(unsigned i=0;i<5;++i)if(!strcmp(scene->valuestring,names[i]))value=i;
  }
  cJSON_Delete(root);
  if(value==99)return json(req,"400 Bad Request","{}");
  testScene.store(value);return json(req,"200 OK","{\"testSceneSet\":true}");
}
#endif
}
bool applianceEventsArmed(){return requested.load()||armed.load();}
void applianceEventsTick() {
  if(armed.load()&&!requested.load()){stop();return;}
  if(!requested.load()||armed.load())return;
  rgb=static_cast<uint8_t*>(ps_malloc(PIXELS*3));previous=static_cast<uint8_t*>(ps_malloc(PIXELS));
  gray=static_cast<uint8_t*>(ps_malloc(PIXELS));
  tail=static_cast<uint8_t*>(ps_malloc(TAIL_BYTES));
  sensor_t* sensor=esp_camera_sensor_get();
  if(!rgb||!gray||!previous||!tail||!applianceAudioTailPrepare()||!sensor||sensor->set_framesize(sensor,FRAMESIZE_VGA)) {
    error.store("event_prepare_failed");requested.store(false);release();applianceSettingsCaptureStart();return;
  }
  fsizePtr=FRAMESIZE_VGA;setFPS(15);armed.store(true);settledAt=millis();sampled=spaceAt=0;
  for(unsigned i=0;i<4;++i){camera_fb_t* frame=esp_camera_fb_get();if(frame)esp_camera_fb_return(frame);}
}
void applianceEventsFrame(camera_fb_t* frame) {
  if(!armed.load())return;
  uint32_t now=millis();
  if(SD.cardType()==CARD_NONE||(now-spaceAt>=5000&&!applianceStorageSpaceAvailable())) {
    spaceAt=now;error.store("minimum_free_space");requested.store(false);return;
  }
  if(now-spaceAt>=5000)spaceAt=now;
  bool checked=false;
  if(now-sampled>=SAMPLE_MS){sampled=now;checked=detect(frame);}
  if(!requested.load()){stop();return;}
  if(!isCapturing) {
    if(!checked||positives<2)return;
    if(!open()){requested.store(false);stop();return;}
  }
  // Opening/decoding can advance the clock. A stale pre-open timestamp can
  // precede started, so unsigned elapsed time would instantly hit the limit.
  now=millis();
  if(checked&&!motion&&!holding) {holding=true;quietAt=now;tailStatus.store(true);applianceAudioTailBegin();}
  if(holding) {
    if(checked&&motion) {
      if(!applianceAudioTailFinish(true)){error.store("event_audio_failed");discardFile();requested.store(false);stop();return;}
      for(size_t i=0;i<heldCount;++i)if(!append(tail+held[i].offset,held[i].bytes)){
        error.store("event_write_failed");discardFile();requested.store(false);stop();return;}
      resetTail();
    } else if(now-quietAt>=QUIET_MS) {if(!close(quietAt)){requested.store(false);stop();}return;}
    else {
      if(heldCount>=TAIL_FRAMES||frame->len>TAIL_BYTES-tailBytes) {
        error.store("event_tail_limit");close(quietAt);return;
      }
      held[heldCount++]={tailBytes,frame->len};memcpy(tail+tailBytes,frame->buf,frame->len);tailBytes+=frame->len;return;
    }
  }
  if(!append(frame->buf,frame->len)||!applianceAudioRecordHealthy()) {
    error.store("event_write_failed");discardFile();requested.store(false);stop();return;
  }
  if(now-started>=MAX_CLIP_MS||frames>=4000) {if(!close(millis())){requested.store(false);stop();}}
}
void applianceEventsStatus(char* output,size_t bytes) {
  const bool active=armed.load(), pending=requested.load();
  snprintf(output,bytes,"{\"requested\":%s,\"armed\":%s,\"quietPending\":%s,\"sensitivity\":%u,\"changedPermille\":%u,\"savedClips\":%u,\"decodeMs\":%u,\"error\":\"%s\"}",
    pending?"true":"false",active?"true":"false",tailStatus.load()?"true":"false",sensitivity.load(),score.load(),saved.load(),decodeMs.load(),error.load());
}
bool applianceRegisterEventRoutes(httpd_handle_t server) {
  const httpd_uri_t routes[]={
    {.uri="/api/events/start",.method=HTTP_POST,.handler=control,.user_ctx=nullptr},
    {.uri="/api/events/stop",.method=HTTP_POST,.handler=control,.user_ctx=nullptr},
#if defined(XIAO_APPLIANCE_EVENT_TEST)
    {.uri="/api/events/test",.method=HTTP_POST,.handler=fixture,.user_ctx=nullptr},
#endif
  };
  for(const auto& route:routes)if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;return true;
}
#endif
