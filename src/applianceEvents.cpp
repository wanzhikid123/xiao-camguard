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
#include <EventTimestamp.h>
#include <HttpAuth.h>
#include "jpeg_decoder.h"
#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "cJSON.h"
#include <atomic>
#if defined(XIAO_APPLIANCE_EVENT_TEST)
#include <lwip/sockets.h>
#endif
extern SemaphoreHandle_t aviMutex;

namespace {
constexpr size_t PIXELS=80*60, TAIL_BYTES=512*1024, TAIL_FRAMES=32;
constexpr uint32_t SAMPLE_MS=250, QUIET_MS=1000, SETTLE_MS=2000;
constexpr const char* TEMP="/event-current.avi";
constexpr const char* INDEX_TEMP="/event-current.index";
std::atomic<bool> requested{false}, armed{false}, tailStatus{false};
std::atomic<unsigned> sensitivity{5}, score{0}, saved{0}, decodeMs{0};
std::atomic<const char*> error{"none"};
uint8_t *rgb=nullptr, *gray=nullptr, *previous=nullptr, *tail=nullptr;
struct Held { size_t offset, bytes; char clock[20]; } held[TAIL_FRAMES];
size_t tailBytes=0, heldCount=0;
uint32_t sampled=0, settledAt=0, quietAt=0, started=0, spaceAt=0;
uint32_t durationMs=0;
std::atomic<uint32_t> sessionEvents{0};
unsigned positives=0, failures=0;
bool background=false, motion=false, holding=false;
File file,indexFile;
uint32_t frames=0;
char stamp[FILE_NAME_LEN]={};
uint32_t eventId=0;
struct TimestampJpeg {
  static constexpr size_t RAW_BYTES=640*480*2, JPEG_BYTES=RAW_BYTES+65536;
  uint8_t *raw=nullptr,*encoded=nullptr;
  jpeg_dec_handle_t decoder=nullptr;
  jpeg_enc_handle_t encoder=nullptr;
  void release() {
    if(encoder){jpeg_enc_close(encoder);encoder=nullptr;}
    if(decoder){jpeg_dec_close(decoder);decoder=nullptr;}
    free(raw);free(encoded);raw=encoded=nullptr;
  }
  bool begin() {
    raw=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,RAW_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    encoded=static_cast<uint8_t*>(heap_caps_aligned_alloc(16,JPEG_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    jpeg_dec_config_t decode={};decode.output_type=JPEG_PIXEL_FORMAT_CbYCrY;
    jpeg_enc_config_t encode=DEFAULT_JPEG_ENC_CONFIG();
    encode.width=640;encode.height=480;encode.src_type=JPEG_PIXEL_FORMAT_CbYCrY;
    encode.subsampling=JPEG_SUBSAMPLE_422;encode.quality=90;
    return raw&&encoded&&jpeg_dec_open(&decode,&decoder)==JPEG_ERR_OK
      &&jpeg_enc_open(&encode,&encoder)==JPEG_ERR_OK;
  }
  bool render(const uint8_t*& jpeg,size_t& bytes,const char* clock) {
    jpeg_dec_io_t io={};io.inbuf=const_cast<uint8_t*>(jpeg);io.inbuf_len=bytes;io.outbuf=raw;
    jpeg_dec_header_info_t dimensions={};int rawBytes=0,encodedBytes=0;
    if(!decoder||!encoder||jpeg_dec_parse_header(decoder,&io,&dimensions)!=JPEG_ERR_OK
      ||dimensions.width!=640||dimensions.height!=480
      ||jpeg_dec_get_outbuf_len(decoder,&rawBytes)!=JPEG_ERR_OK||rawBytes!=RAW_BYTES
      ||jpeg_dec_process(decoder,&io)!=JPEG_ERR_OK
      ||!appliance::drawEventClock(raw,640,480,clock)
      ||jpeg_enc_process(encoder,raw,RAW_BYTES,encoded,JPEG_BYTES,&encodedBytes)!=JPEG_ERR_OK
      ||encodedBytes<4||encodedBytes>JPEG_BYTES)return false;
    jpeg=encoded;bytes=encodedBytes;return true;
  }
} timestampJpeg;
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
bool writeTo(File& output,const void* data,size_t bytes) {
  const uint8_t* p=static_cast<const uint8_t*>(data);
  while(bytes) {size_t n=std::min(bytes,size_t(8192));if(output.write(p,n)!=n)return false;p+=n;bytes-=n;}
  return true;
}
bool write(const void* data,size_t bytes){return writeTo(file,data,bytes);}
bool index(const uint8_t* kind,uint32_t bytes,uint32_t offset) {
  uint32_t entry[4]={0,kind==dcBuf?0x10u:0u,offset,bytes};memcpy(entry,kind,4);
  return writeTo(indexFile,entry,sizeof(entry));
}
bool append(const uint8_t* jpeg,size_t bytes,const char* clock) {
  uint8_t marker[appliance::EVENT_MARKER_BYTES];
  if(bytes<4||jpeg[0]!=255||jpeg[1]!=216||!appliance::eventTimestampMarker(marker,clock))return false;
  // Paint before writing the MJPEG chunk. Held frames retain their capture
  // clock, and discarded quiet frames never pay the full JPEG encode cost.
  if(!timestampJpeg.render(jpeg,bytes,clock))return false;
  const uint32_t payload=bytes+sizeof(marker),padded=(payload+3)&~3u;
  if(uint64_t(file.position())+padded+8+uint64_t(frames+2)*16>UINT32_MAX)return false;
  const uint32_t offset=file.position()-306;
  const uint8_t zero[3]={};
  if(!write(dcBuf,4)||!write(&padded,4)||!write(jpeg,2)||!write(marker,sizeof(marker))
    ||!write(jpeg+2,bytes-2)||!write(zero,padded-payload)||!index(dcBuf,padded,offset))return false;
  ++frames;return true;
}
void resetTail() {heldCount=tailBytes=0;holding=false;tailStatus.store(false);}
void discardFile() {
  applianceAudioTailFinish(false);finishAudioRecord(false);
  if(file)file.close();if(indexFile)indexFile.close();SD.remove(TEMP);SD.remove(INDEX_TEMP);SD.remove(WAVTEMP);
  isCapturing=stopPlayback=false;resetTail();
}
bool close() {
  // finishAudioRecord atomically stops writes and discards the candidate tail;
  // do not resume the audio writer between discarding and finalization.
  bool healthy=applianceAudioRecordHealthy();finishAudioRecord(true);
  const uint32_t duration=std::max(uint32_t(1),durationMs);
  bool ok=file&&frames&&healthy;
  File wav=ok?SD.open(WAVTEMP,FILE_READ):File();
  const uint32_t audioBytes=wav&&wav.size()>WAV_HDR_LEN?wav.size()-WAV_HDR_LEN:0;
  if(ok&&audioBytes) {
    const uint32_t offset=file.position()-306;
    ok=uint64_t(file.position())+8+audioBytes+uint64_t(frames+1)*16+8<=UINT32_MAX
      &&wav.seek(WAV_HDR_LEN)&&write(wbBuf,4)&&write(&audioBytes,4);
    while(ok&&wav.available()){const size_t bytes=wav.read(iSDbuffer,RAMSIZE);ok=bytes&&write(iSDbuffer,bytes);}
    ok=ok&&index(wbBuf,audioBytes,offset);
  }else ok=false;
  wav.close();
  if(ok) {
    const uint32_t movieBytes=file.position()-306,indexBytes=(frames+1)*16;
    indexFile.flush();indexFile.close();indexFile=SD.open(INDEX_TEMP,FILE_READ);
    ok=indexFile&&indexFile.size()==indexBytes&&write("idx1",4)&&write(&indexBytes,4);
    while(ok&&indexFile.available()){const size_t bytes=indexFile.read(iSDbuffer,RAMSIZE);ok=bytes&&write(iSDbuffer,bytes);}
    indexFile.close();
    uint8_t header[AVI_HEADER_LEN];
    xSemaphoreTake(aviMutex,portMAX_DELAY);
    memcpy(header,aviHeader,sizeof(header));
    xSemaphoreGive(aviMutex);
    auto put=[&](size_t at,uint32_t value){memcpy(header+at,&value,4);};
    // SD's write-only handle may still report size()==0 until flush/close.
    // position() includes the complete movie and index before seeking the header.
    put(4,file.position()-8);put(0x20,uint64_t(duration)*1000/frames);put(0x30,frames);put(0x38,2);
    put(0x40,640);put(0x44,480);put(0x80,duration);put(0x84,frames*1000);put(0x8c,frames);
    put(0xa8,640);put(0xac,480);put(0xf4,1);put(0xf8,SAMPLE_RATE);put(0x100,audioBytes/2);
    put(0x104,SAMPLE_RATE*2);put(0x11c,SAMPLE_RATE);put(0x120,SAMPLE_RATE*2);put(0x12e,movieBytes);
    if(!file.seek(0,SeekSet)||!write(header,sizeof(header)))ok=false;
    file.flush();
  }
  if(file)file.close();
  if(indexFile)indexFile.close();
  char path[FILE_NAME_LEN];
  snprintf(path,sizeof(path),"%s_%06lx_event_VGA_S.avi",stamp,(unsigned long)eventId);
  if(ok)ok=!SD.exists(path)&&SD.rename(TEMP,path);
  if(ok){saved.fetch_add(1);LOG_INF("Saved 480p event session: %lu frames, %lu ms, %lu events",(unsigned long)frames,(unsigned long)duration,(unsigned long)sessionEvents.load());}
  else {SD.remove(TEMP);error.store("event_write_failed");}
  SD.remove(INDEX_TEMP);SD.remove(WAVTEMP);isCapturing=stopPlayback=false;resetTail();return ok;
}
bool open() {
  if(!applianceStorageSpaceAvailable()){error.store("minimum_free_space");return false;}
  dateFormat(stamp,sizeof(stamp),false);char folder[10]={};memcpy(folder,stamp,9);
  if(!SD.exists(folder)&&!SD.mkdir(folder)){error.store("event_directory_failed");return false;}
  bool unique=false;
  for(unsigned i=0;i<8&&!unique;++i){eventId=esp_random()&0xffffff;char path[FILE_NAME_LEN];
    snprintf(path,sizeof(path),"%s_%06lx_event_VGA_S.avi",stamp,(unsigned long)eventId);unique=!SD.exists(path);}
  if(!unique){error.store("event_name_collision");return false;}
  SD.remove(TEMP);SD.remove(INDEX_TEMP);SD.remove(WAVTEMP);
  file=SD.open(TEMP,FILE_WRITE);indexFile=SD.open(INDEX_TEMP,FILE_WRITE);uint8_t blank[AVI_HEADER_LEN]={};
  if(!file||!indexFile||!write(blank,sizeof(blank))){discardFile();error.store("event_write_failed");return false;}
  frames=durationMs=0;resetTail();startAudioRecord();return true;
}
void finishSegment(uint32_t endAt) {
  applianceAudioRecordPause(true);applianceAudioTailFinish(false);
  durationMs+=std::max(uint32_t(1),uint32_t(endAt-started));
  isCapturing=stopPlayback=false;resetTail();
  setFPS(15); // Keep idle motion sampling responsive.
}
void release() {
  timestampJpeg.release();
  free(rgb);free(gray);free(previous);free(tail);rgb=gray=previous=tail=nullptr;
  applianceAudioTailRelease();background=motion=false;positives=failures=0;score.store(0);
}
void stop() {
  if(isCapturing)finishSegment(holding?quietAt:millis());
  if(file)close();
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
    if(!strcmp(scene->valuestring,"runtime")) {
      cJSON_Delete(root);char response[192];
      snprintf(response,sizeof(response),"{\"freeHeap\":%u,\"minimumFreeHeap\":%u,\"largestInternalBlock\":%u,\"freePsram\":%u,\"captureStackFree\":%u}",
        unsigned(ESP.getFreeHeap()),unsigned(ESP.getMinFreeHeap()),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
        unsigned(ESP.getFreePsram()),unsigned(uxTaskGetStackHighWaterMark(captureHandle)));
      return json(req,"200 OK",response);
    }
    if(!strcmp(scene->valuestring,"timestamp_selftest")) {
      uint8_t marker[appliance::EVENT_MARKER_BYTES],jpeg[2+appliance::EVENT_MARKER_BYTES+2]={255,216};char parsed[20];
      unsigned passed=0;const char* clock="2026-10-08 14:27:39";
      if(appliance::eventTimestampMarker(marker,clock))++passed;
      memcpy(jpeg+2,marker,sizeof(marker));jpeg[sizeof(jpeg)-2]=255;jpeg[sizeof(jpeg)-1]=217;
      if(appliance::readEventTimestamp(jpeg,sizeof(jpeg),parsed)&&!strcmp(clock,parsed))++passed;
      if(!appliance::readEventTimestamp(jpeg,10,parsed))++passed;
      jpeg[4]=255;if(!appliance::readEventTimestamp(jpeg,sizeof(jpeg),parsed))++passed;jpeg[4]=0;
      jpeg[14]='x';if(!appliance::readEventTimestamp(jpeg,sizeof(jpeg),parsed))++passed;
      uint8_t* pixels=static_cast<uint8_t*>(ps_malloc(320*32*2));
      if(pixels){memset(pixels,100,320*32*2);
        if(appliance::drawEventClock(pixels,320,32,clock))++passed;
        if(pixels[0]==100&&pixels[(8*320+82)*2+1]==0&&pixels[(10*320+86)*2+1]==255)++passed;
        if(pixels[(10*320+86)*2]==128)++passed;
        if(!appliance::drawEventClock(pixels,160,32,clock))++passed;
        if(!appliance::drawEventClock(pixels,320,16,clock))++passed;free(pixels);
      }
      cJSON_Delete(root);char response[64];snprintf(response,sizeof(response),"{\"passed\":%u,\"total\":10}",passed);
      return json(req,"200 OK",response);
    }
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
  if(!rgb||!gray||!previous||!tail||!applianceAudioTailPrepare()||!timestampJpeg.begin()||!sensor||sensor->set_framesize(sensor,FRAMESIZE_VGA)) {
    error.store("event_prepare_failed");requested.store(false);release();applianceSettingsCaptureStart();return;
  }
  fsizePtr=FRAMESIZE_VGA;setFPS(15);armed.store(true);settledAt=millis();sampled=spaceAt=0;
  sessionEvents=0;
  for(unsigned i=0;i<4;++i){camera_fb_t* frame=esp_camera_fb_get();if(frame)esp_camera_fb_return(frame);}
}
void applianceEventsFrame(camera_fb_t* frame) {
  if(!armed.load())return;
  uint32_t now=millis();
  // Snapshot before detection or flushing held JPEGs can delay this frame.
  const time_t captured=time(nullptr);
  if(SD.cardType()==CARD_NONE||(now-spaceAt>=5000&&!applianceStorageSpaceAvailable())) {
    spaceAt=now;error.store("minimum_free_space");requested.store(false);return;
  }
  if(now-spaceAt>=5000)spaceAt=now;
  bool checked=false;
  if(now-sampled>=SAMPLE_MS){sampled=now;checked=detect(frame);}
  if(!requested.load()){stop();return;}
  if(!isCapturing) {
    if(!checked||positives<2)return;
    if(!file&&!open()){requested.store(false);stop();return;}
    // Painting JPEGs is approximately a 5 FPS workload on S3. Use the same
    // cadence for held quiet frames, avoiding a 15 FPS burst on resumption.
    setFPS(5);started=millis();++sessionEvents;applianceAudioRecordPause(false);isCapturing=stopPlayback=true;
  }
  // Opening/decoding can advance the clock. A stale pre-open timestamp can
  // precede started, so unsigned elapsed time would instantly hit the limit.
  now=millis();
  if(checked&&!motion&&!holding) {holding=true;quietAt=now;tailStatus.store(true);applianceAudioTailBegin();}
  if(holding) {
    if(checked&&motion) {
      if(!applianceAudioTailFinish(true)){error.store("event_audio_failed");discardFile();requested.store(false);stop();return;}
      for(size_t i=0;i<heldCount;++i)if(!append(tail+held[i].offset,held[i].bytes,held[i].clock)){
        error.store("event_write_failed");discardFile();requested.store(false);stop();return;}
      resetTail();
    } else if(now-quietAt>=QUIET_MS) {finishSegment(quietAt);return;}
    else {
      if(heldCount>=TAIL_FRAMES||frame->len>TAIL_BYTES-tailBytes) {
        error.store("event_tail_limit");finishSegment(quietAt);return;
      }
      Held& entry=held[heldCount++];entry.offset=tailBytes;entry.bytes=frame->len;
      struct tm local;localtime_r(&captured,&local);strftime(entry.clock,sizeof(entry.clock),"%Y-%m-%d %H:%M:%S",&local);
      memcpy(tail+tailBytes,frame->buf,frame->len);tailBytes+=frame->len;return;
    }
  }
  char clock[20];struct tm local;localtime_r(&captured,&local);strftime(clock,sizeof(clock),"%Y-%m-%d %H:%M:%S",&local);
  if(!append(frame->buf,frame->len,clock)||!applianceAudioRecordHealthy()) {
    error.store("event_write_failed");discardFile();requested.store(false);stop();return;
  }
  // AVI 1.0 has 32-bit size/timing fields. End this session at its format
  // boundary instead of silently producing a second video.
  if(frames>=UINT32_MAX/1000||uint64_t(durationMs)+uint32_t(now-started)>=UINT32_MAX-1000
    ||uint64_t(file.position())+uint64_t(frames)*16+uint64_t(durationMs+uint32_t(now-started))*32+1048576>=UINT32_MAX) {
    error.store("event_file_limit");requested.store(false);stop();
  }
}
void applianceEventsStatus(char* output,size_t bytes) {
  const bool active=armed.load(), pending=requested.load();
  snprintf(output,bytes,"{\"requested\":%s,\"armed\":%s,\"quietPending\":%s,\"sensitivity\":%u,\"changedPermille\":%u,\"savedClips\":%u,\"sessionEvents\":%lu,\"decodeMs\":%u,\"error\":\"%s\"}",
    pending?"true":"false",active?"true":"false",tailStatus.load()?"true":"false",sensitivity.load(),score.load(),saved.load(),(unsigned long)sessionEvents.load(),decodeMs.load(),error.load());
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
