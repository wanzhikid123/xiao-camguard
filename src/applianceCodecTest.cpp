#if defined(XIAO_APPLIANCE_CODEC_TEST)
#include "appGlobals.h"
#include "applianceCodec.h"
#include "applianceWeb.h"
#include "applianceEvents.h"
#include "applianceMedia.h"
#include "applianceOta.h"
#include "applianceJobs.h"
#include "applianceTimelapse.h"
#include "cJSON.h"
#include <atomic>
#include "esp_heap_caps.h"
#include "CodecFrame.h"
extern "C" void yuyv2iyuv(uint32_t height,uint32_t width,uint8_t* in,uint8_t* out);
namespace {
std::atomic<bool> running{false},cancel{false},memoryPressure{false};
SemaphoreHandle_t mutex=nullptr;
ApplianceCodecReport result;char source[128]={},temporary[160]={};
bool abort(){return cancel.load()||forceRecord||isCapturing||stopPlayback||applianceStreamsBusy()||applianceEventsArmed()||applianceTimelapseActive()||applianceMaintenance();}
void progress(const ApplianceCodecReport& report){xSemaphoreTake(mutex,portMAX_DELAY);result=report;xSemaphoreGive(mutex);}
void worker(void*){ApplianceCodecReport report;applianceConvertAvi(source,temporary,report,abort,progress);progress(report);running.store(false);vTaskDeleteWithCaps(nullptr);}
esp_err_t reply(httpd_req_t* req,const char* status,const char* text){httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,text);}
esp_err_t checks(httpd_req_t* req){if(!applianceCheckAuth(req,false))return ESP_OK;
  unsigned passed=0;appliance::CodecClock clock;
  if(!appliance::codecClock(0,1,1,clock))++passed;
  if(appliance::codecClock(9000,600000,9000000,clock)&&clock.scale==1&&clock.rate==15&&clock.ticks==9000&&clock.milliseconds==600000)++passed;
  if(appliance::codecClock(7,1000000001,1000000003,clock)&&clock.ticks>UINT32_MAX&&clock.rate==1000000003&&clock.milliseconds==6999)++passed;
  if(!appliance::codecClock(UINT32_MAX,UINT32_MAX,1,clock))++passed;
  if(!appliance::codecBuffersFit(2*1024*1024,400*1024,32000,460800,679936,20480))++passed;
  if(!appliance::codecBuffersFit(256*1024+4*1024-1,1024,1024,1024,1024,1024))++passed;
  if(appliance::codecBuffersFit(256*1024+4*1024,1024,1024,1024,1024,1024))++passed;
  uint8_t input[8*32*2],old[sizeof(input)],expected[8*32*3/2],actual[sizeof(expected)];
  uint32_t random=123456;for(size_t i=0;i<sizeof(input);++i){random=random*1664525+1013904223;input[i]=random>>24;}
  for(size_t i=0;i<sizeof(old);i+=4){old[i]=16+(uint32_t(input[i+1])*219+127)/255;old[i+1]=16+(uint32_t(input[i])*224+127)/255;
    old[i+2]=16+(uint32_t(input[i+3])*219+127)/255;old[i+3]=16+(uint32_t(input[i+2])*224+127)/255;}
  yuyv2iyuv(32,8,old,expected);memset(actual,0,sizeof(actual));bool ok=true;
  for(unsigned row=0;row<32;row+=8)ok=ok&&appliance::codecBlockToI420(input+row*8*2,8*8*2,actual,8,32,row);
  if(ok&&!memcmp(expected,actual,sizeof(actual)))++passed;
  memset(actual,0,sizeof(actual));ok=true;
  for(unsigned row=0;row<32;row+=16)ok=ok&&appliance::codecBlockToI420(input+row*8*2,16*8*2,actual,8,32,row);
  if(ok&&!memcmp(expected,actual,sizeof(actual)))++passed;
  if(!appliance::codecBlockToI420(input,8*8*2,actual,8,32,30))++passed;
  ApplianceCodecReport r;strlcpy(r.error,"psram_unavailable",sizeof(r.error));if(applianceCodecResourceUnavailable(r))++passed;
  strlcpy(r.error,"internal_memory_unavailable",sizeof(r.error));if(applianceCodecResourceUnavailable(r))++passed;
  strlcpy(r.error,"invalid_avi",sizeof(r.error));if(!applianceCodecResourceUnavailable(r))++passed;
  char text[80];snprintf(text,sizeof(text),"{\"passed\":%u,\"total\":13}",passed);return reply(req,passed==13?"200 OK":"500 Internal Server Error",text);
}
esp_err_t handler(httpd_req_t* req){if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  if(strchr(req->uri,'?'))return reply(req,"400 Bad Request","{}");
  if(req->method==HTTP_POST){if(running.load())return reply(req,"409 Conflict","{}");
    char raw[256]={};if(!req->content_len||req->content_len>=sizeof(raw))return reply(req,"400 Bad Request","{}");
    size_t got=0;while(got<req->content_len){int n=httpd_req_recv(req,raw+got,req->content_len-got);if(n<=0)return reply(req,"400 Bad Request","{}");got+=n;}
    cJSON* json=cJSON_Parse(raw);cJSON* path=json?cJSON_GetObjectItemCaseSensitive(json,"path"):nullptr;
    cJSON* mode=json?cJSON_GetObjectItemCaseSensitive(json,"mode"):nullptr;
    bool pressure=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"queue_pressure");
    bool queued=cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"queue")||!strcmp(mode->valuestring,"queue_publish")||!strcmp(mode->valuestring,"queue_large")||pressure);
    bool corrupt=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"corrupt");
    bool part=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"part");
    bool clock=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"clock64");
    unsigned flags=queued?!strcmp(mode->valuestring,"queue_publish")?1:!strcmp(mode->valuestring,"queue_large")?2:0:0;
    bool valid=cJSON_IsObject(json)&&cJSON_GetArraySize(json)==(mode?2:1)&&(!mode||queued||corrupt||part||clock)&&cJSON_IsString(path)&&strlen(path->valuestring)<sizeof(source)
      &&path->valuestring[0]=='/'&&!strstr(path->valuestring,"..")&&!strchr(path->valuestring,'\\')&&strlen(path->valuestring)>14
      &&!strcmp(path->valuestring+strlen(path->valuestring)-4,".avi");
    if(valid)strlcpy(source,path->valuestring,sizeof(source));cJSON_Delete(json);
    if(!valid)return reply(req,"400 Bad Request","{}");
    if(queued){if(!applianceConversionTestSource(source,flags))return reply(req,"409 Conflict","{}");
      memoryPressure.store(pressure);return reply(req,"202 Accepted","{}");}
    if(corrupt)return applianceConversionTestCorrupt(source)?reply(req,"200 OK","{}"):reply(req,"409 Conflict","{}");
    if(part)return applianceConversionTestPart(source)?reply(req,"200 OK","{}"):reply(req,"409 Conflict","{}");
    // Fixture only: a coprime AVI clock keeps the same frames and forces a
    // 64-bit media duration without recording/encoding a ten-minute clip.
    if(clock){ApplianceForeground foreground;if(!foreground.ready())return reply(req,"409 Conflict","{}");
      File file=SD.open(source,"r+");uint32_t values[]={1000000,1000000001,1000000003};
      const uint32_t positions[]={0x20,0x80,0x84};bool ok=bool(file);
      for(unsigned i=0;ok&&i<3;++i)ok=file.seek(positions[i])&&file.write((uint8_t*)&values[i],4)==4;
      if(file){file.flush();file.close();}return reply(req,ok?"200 OK":"400 Bad Request","{}");}
    if(forceRecord||isCapturing||stopPlayback||applianceStreamsBusy()||applianceEventsArmed()||applianceTimelapseActive()||applianceMediaBusy()||applianceMaintenance())return reply(req,"409 Conflict","{}");
    snprintf(temporary,sizeof(temporary),"%.*s_codec_test.mp4",int(strlen(source)-4),source);
    if(SD.exists(temporary))return reply(req,"409 Conflict","{\"error\":\"test_output_exists\"}");
    result={};cancel.store(false);running.store(true);
    if(xTaskCreateWithCaps(worker,"codecProbe",16384,nullptr,0,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){running.store(false);return reply(req,"503 Service Unavailable","{\"error\":\"worker_memory_unavailable\"}");}
    return reply(req,"202 Accepted","{}");
  }
  ApplianceCodecReport r;xSemaphoreTake(mutex,portMAX_DELAY);r=result;xSemaphoreGive(mutex);
  cJSON* json=cJSON_CreateObject();cJSON_AddBoolToObject(json,"running",running.load());cJSON_AddStringToObject(json,"error",r.error);
  cJSON_AddStringToObject(json,"output",temporary);cJSON_AddNumberToObject(json,"frames",r.frames);cJSON_AddNumberToObject(json,"totalFrames",r.totalFrames);
  cJSON_AddNumberToObject(json,"width",r.width);cJSON_AddNumberToObject(json,"height",r.height);cJSON_AddNumberToObject(json,"elapsedMs",r.elapsedMs);
  cJSON_AddNumberToObject(json,"audioSamples",r.audioSamples);cJSON_AddNumberToObject(json,"sourceBytes",double(r.sourceBytes));
  cJSON_AddNumberToObject(json,"outputBytes",double(r.outputBytes));cJSON_AddNumberToObject(json,"peakTemperature",r.peakTemperature);
  const auto memory=applianceCodecMemory();cJSON_AddNumberToObject(json,"initialPsram",memory.initialPsram);
  cJSON_AddNumberToObject(json,"minimumPsram",memory.minimumPsram);cJSON_AddNumberToObject(json,"minimumLargestBlock",memory.minimumLargestBlock);
  cJSON_AddNumberToObject(json,"rawBufferBytes",memory.rawBytes);cJSON_AddNumberToObject(json,"blockBufferBytes",memory.blockBytes);
  char* text=cJSON_PrintUnformatted(json);const auto sent=reply(req,"200 OK",text?text:"{}");free(text);cJSON_Delete(json);return sent;
}
esp_err_t fileHandler(httpd_req_t* req){
  if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  if(running.load()||!temporary[0]||req->content_len||strchr(req->uri,'?'))return reply(req,"409 Conflict","{}");
  if(req->method==HTTP_POST)return reply(req,SD.remove(temporary)?"200 OK":"404 Not Found","{}");
  File f=SD.open(temporary,FILE_READ);if(!f)return reply(req,"404 Not Found","{}");
  httpd_resp_set_type(req,"video/mp4");uint8_t bytes[4096];while(f.available()){
    size_t n=f.read(bytes,sizeof(bytes));if(!n||httpd_resp_send_chunk(req,(char*)bytes,n)!=ESP_OK)return ESP_FAIL;taskYIELD();}
  return httpd_resp_send_chunk(req,nullptr,0);
}
}
bool applianceCodecTestBusy(){return running.load();}
bool applianceCodecTestMemoryPressure(){return memoryPressure.exchange(false);}
void applianceCodecTestCancel(){if(running.load())cancel.store(true);}
bool applianceRegisterCodecTestRoutes(httpd_handle_t server){mutex=xSemaphoreCreateMutex();if(!mutex)return false;
  httpd_uri_t regression={};regression.uri="/api/codecs/checks";regression.method=HTTP_GET;regression.handler=checks;
  if(httpd_register_uri_handler(server,&regression)!=ESP_OK)return false;
  for(httpd_method_t method:{HTTP_GET,HTTP_POST}){httpd_uri_t route={};route.uri="/api/codecs/test";route.method=method;route.handler=handler;
    if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;}
  for(httpd_method_t method:{HTTP_GET,HTTP_POST}){httpd_uri_t route={};route.uri="/api/codecs/file";route.method=method;route.handler=fileHandler;
    if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;}return true;}
#endif
