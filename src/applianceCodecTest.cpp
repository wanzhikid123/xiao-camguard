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
namespace {
std::atomic<bool> running{false},cancel{false};
SemaphoreHandle_t mutex=nullptr;
ApplianceCodecReport result;char source[128]={},temporary[160]={};
bool abort(){return cancel.load()||forceRecord||isCapturing||stopPlayback||applianceStreamsBusy()||applianceEventsArmed()||applianceTimelapseActive()||applianceMaintenance();}
void progress(const ApplianceCodecReport& report){xSemaphoreTake(mutex,portMAX_DELAY);result=report;xSemaphoreGive(mutex);}
void worker(void*){ApplianceCodecReport report;applianceConvertAvi(source,temporary,report,abort,progress);progress(report);running.store(false);vTaskDeleteWithCaps(nullptr);}
esp_err_t reply(httpd_req_t* req,const char* status,const char* text){httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,text);}
esp_err_t handler(httpd_req_t* req){if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  if(strchr(req->uri,'?'))return reply(req,"400 Bad Request","{}");
  if(req->method==HTTP_POST){if(running.load())return reply(req,"409 Conflict","{}");
    char raw[256]={};if(!req->content_len||req->content_len>=sizeof(raw))return reply(req,"400 Bad Request","{}");
    size_t got=0;while(got<req->content_len){int n=httpd_req_recv(req,raw+got,req->content_len-got);if(n<=0)return reply(req,"400 Bad Request","{}");got+=n;}
    cJSON* json=cJSON_Parse(raw);cJSON* path=json?cJSON_GetObjectItemCaseSensitive(json,"path"):nullptr;
    cJSON* mode=json?cJSON_GetObjectItemCaseSensitive(json,"mode"):nullptr;
    bool queued=cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"queue")||!strcmp(mode->valuestring,"queue_publish")||!strcmp(mode->valuestring,"queue_large"));
    bool corrupt=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"corrupt");
    bool part=cJSON_IsString(mode)&&!strcmp(mode->valuestring,"part");
    unsigned flags=queued?!strcmp(mode->valuestring,"queue_publish")?1:!strcmp(mode->valuestring,"queue_large")?2:0:0;
    bool valid=cJSON_IsObject(json)&&cJSON_GetArraySize(json)==(mode?2:1)&&(!mode||queued||corrupt||part)&&cJSON_IsString(path)&&strlen(path->valuestring)<sizeof(source)
      &&path->valuestring[0]=='/'&&!strstr(path->valuestring,"..")&&!strchr(path->valuestring,'\\')&&strlen(path->valuestring)>14
      &&!strcmp(path->valuestring+strlen(path->valuestring)-4,".avi");
    if(valid)strlcpy(source,path->valuestring,sizeof(source));cJSON_Delete(json);
    if(!valid)return reply(req,"400 Bad Request","{}");
    if(queued)return applianceConversionTestSource(source,flags)?reply(req,"202 Accepted","{}"):reply(req,"409 Conflict","{}");
    if(corrupt)return applianceConversionTestCorrupt(source)?reply(req,"200 OK","{}"):reply(req,"409 Conflict","{}");
    if(part)return applianceConversionTestPart(source)?reply(req,"200 OK","{}"):reply(req,"409 Conflict","{}");
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
void applianceCodecTestCancel(){if(running.load())cancel.store(true);}
bool applianceRegisterCodecTestRoutes(httpd_handle_t server){mutex=xSemaphoreCreateMutex();if(!mutex)return false;
  for(httpd_method_t method:{HTTP_GET,HTTP_POST}){httpd_uri_t route={};route.uri="/api/codecs/test";route.method=method;route.handler=handler;
    if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;}
  for(httpd_method_t method:{HTTP_GET,HTTP_POST}){httpd_uri_t route={};route.uri="/api/codecs/file";route.method=method;route.handler=fileHandler;
    if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;}return true;}
#endif
