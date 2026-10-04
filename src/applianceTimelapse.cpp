#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceTimelapse.h"
#include "applianceWeb.h"
#include "applianceSettings.h"
#include "applianceMedia.h"
#include "applianceEvents.h"
#include "applianceOta.h"
#include "applianceJobs.h"
#include <HttpAuth.h>
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <atomic>
#include <algorithm>
#include <cmath>

namespace {
constexpr size_t PAGE=20,SCAN=4096;
std::atomic<bool> active{false},stopping{false},working{false};
std::atomic<bool> indexReady{false};
SemaphoreHandle_t mutex=nullptr;
StaticTask_t photoTask;StackType_t* photoStack=nullptr;
struct Session {
  char id[56]={},error[40]="none";
  uint32_t interval=900,photos=0,skipped=0,failures=0,consecutive=0,sequence=0;
  uint16_t width=0,height=0;int quality=0;
  uint64_t next=0,started=0,last=0,ended=0;bool synchronized=false;
} session;
class Lock {public:Lock(){xSemaphoreTake(mutex,portMAX_DELAY);}~Lock(){xSemaphoreGive(mutex);}};
uint64_t now(){return uint64_t(esp_timer_get_time())/1000;}
bool label(const char* text){size_t n=strlen(text);if(n<8||n>=sizeof(Session::id))return false;
  for(size_t i=0;i<n;++i)if(!isalnum((uint8_t)text[i])&&text[i]!='_')return false;return !strncmp(text,"TL_",3);}
bool jpegName(const char* text){size_t n=strlen(text);if(n<14||n>48||strcmp(text+n-4,".jpg")||strncmp(text,"IMG_",4))return false;
  for(size_t i=4;i<n-4;++i)if(!isdigit((uint8_t)text[i])&&text[i]!='_')return false;return true;}
struct Item{char name[56];uint64_t bytes;};
static_assert(sizeof(Item)==64,"Stable SD index record size");
bool indexCursor(const char* text){size_t n=strlen(text);if(n<2||n>11||text[0]!='I')return false;
  for(size_t i=1;i<n;++i)if(!isdigit((uint8_t)text[i]))return false;return strtoull(text+1,nullptr,10)<=UINT32_MAX;}
bool appendIndex(const char* path,const char* name){Item record={};strlcpy(record.name,name,sizeof(record.name));
  File f=SD.open(path,SD.exists(path)?"r+":"w");if(!f)return false;
  const uint32_t at=f.size()/sizeof(record)*sizeof(record); // Replace an interrupted tail, never existing records.
  bool ok=f.seek(at)&&f.write((uint8_t*)&record,sizeof(record))==sizeof(record);f.flush();ok=ok&&f.size()==at+sizeof(record);f.close();return ok;}
bool prepareIndex(){const char* path="/timelapse/.sessions.index";if(SD.exists(path))return true;
  ApplianceForeground foreground;if(!foreground.ready())return false;
  if(!SD.exists("/timelapse")&&!SD.mkdir("/timelapse"))return false;
  const char* temporary="/timelapse/.sessions.tmp";SD.remove(temporary);File created=SD.open(temporary,"w");if(!created)return false;created.close();
  File root=SD.open("/timelapse");if(!root)return false;
  for(File f=root.openNextFile();f;f=root.openNextFile()){
    bool saved=!f.isDirectory()||!label(f.name())||appendIndex(temporary,f.name());f.close();if(!saved)return false;vTaskDelay(1);}
  root.close();return SD.rename(temporary,path);}
cJSON* snapshot(const Session& s,bool running){const uint64_t tick=now();cJSON* j=cJSON_CreateObject();
  cJSON_AddBoolToObject(j,"active",running);cJSON_AddStringToObject(j,"session",s.id);
  cJSON_AddStringToObject(j,"error",s.error);cJSON_AddNumberToObject(j,"intervalSeconds",s.interval);
  cJSON_AddNumberToObject(j,"photos",s.photos);cJSON_AddNumberToObject(j,"skipped",s.skipped);
  cJSON_AddNumberToObject(j,"failures",s.failures);cJSON_AddNumberToObject(j,"consecutiveFailures",s.consecutive);
  cJSON_AddNumberToObject(j,"width",s.width);cJSON_AddNumberToObject(j,"height",s.height);cJSON_AddNumberToObject(j,"quality",s.quality);
  cJSON_AddNumberToObject(j,"startedUnix",double(s.started));cJSON_AddNumberToObject(j,"lastPhotoUnix",double(s.last));
  cJSON_AddNumberToObject(j,"endedUnix",double(s.ended));cJSON_AddBoolToObject(j,"timeSynchronized",s.synchronized);
  cJSON_AddNumberToObject(j,"nextInSeconds",running&&s.next>tick?double(s.next-tick)/1000:0);return j;}
bool metadata(const Session& s,bool running){cJSON* j=snapshot(s,running);char* text=cJSON_PrintUnformatted(j);cJSON_Delete(j);
  if(!text)return false;char path[128],temporary[128],backup[128];snprintf(path,sizeof(path),"/timelapse/%s/session.json",s.id);
  snprintf(backup,sizeof(backup),"/timelapse/%s/session.bak",s.id);
  snprintf(temporary,sizeof(temporary),"/timelapse/%s/session.tmp",s.id);
  SD.remove(temporary);File f=SD.open(temporary,FILE_WRITE);size_t n=strlen(text);
  bool ok=f&&f.write((const uint8_t*)text,n)==n;if(f){f.flush();ok=ok&&f.size()==n;f.close();}free(text);
  if(ok){
    // Keep a closed previous snapshot until the new snapshot is published.
    const bool previous=SD.exists(path);
    if(previous){SD.remove(backup);ok=SD.rename(path,backup);}
    if(ok){ok=SD.rename(temporary,path);if(!ok&&previous)SD.rename(backup,path);}
    if(ok)SD.remove(backup);
  }return ok;}
void end(const char* error){Session s;{Lock lock;strlcpy(session.error,error,sizeof(session.error));session.ended=timeSynchronized?time(nullptr):0;s=session;}
  metadata(s,false);stopping.store(false);active.store(false);}
void owner(void*){
  uint64_t retryAt=0;
  for(;;){
    if(!indexReady.load()&&appSetupDone&&SD.cardType()!=CARD_NONE&&!applianceMaintenance())indexReady.store(prepareIndex());
    if(active.load()){
      if(stopping.load()||applianceMaintenance()){end(applianceMaintenance()?"maintenance":"none");retryAt=0;}
      else {Session s;{Lock lock;s=session;}const uint64_t tick=now();
        if(tick>=s.next){
          const uint64_t interval=uint64_t(s.interval)*1000;
          auto advance=[&](bool skip){const uint64_t finished=now();Lock lock;session.next+=interval;while(session.next<=finished){session.next+=interval;++session.skipped;}
            if(skip)++session.skipped;retryAt=0;};
          if(forceRecord||isCapturing||stopPlayback){advance(true);{Lock lock;s=session;}metadata(s,true);}
          else if(applianceCaptureBusy()||applianceStreamsBusy()||applianceMediaBusy()||applianceNetworkBusy()){
            if(!retryAt)retryAt=tick;
            if(tick-retryAt>=30000||tick>=s.next+interval){advance(true);{Lock lock;s=session;}metadata(s,true);}
          }else if(!applianceStorageSpaceAvailable()){end("minimum_free_space");retryAt=0;}
          else {
            working.store(true);char path[128],stamp[24]={};
            if(timeSynchronized){time_t wall=time(nullptr);tm local={};localtime_r(&wall,&local);strftime(stamp,sizeof(stamp),"%Y%m%d_%H%M%S_",&local);}
            {Lock lock;++session.sequence;s=session;}
            snprintf(path,sizeof(path),"/timelapse/%s/IMG_%s%06lu.jpg",s.id,stamp,(unsigned long)s.sequence);
            char index[128];snprintf(index,sizeof(index),"/timelapse/%s/photos.index",s.id);
            // Record the intended name before creating its JPEG. After a power
            // cut, closed photos remain discoverable; absent/failed names are
            // harmless tombstones and pagination continues past them.
            if(!appendIndex(index,strrchr(path,'/')+1)){working.store(false);end("photo_index_write_failed");retryAt=0;continue;}
            bool occupied=false;bool saved=applianceSaveConfiguredPhoto(path,&occupied);
            if(occupied){working.store(false);if(!retryAt)retryAt=tick;
              if(now()-retryAt>=30000||now()>=s.next+interval){advance(true);{Lock lock;s=session;}metadata(s,true);}
              vTaskDelay(pdMS_TO_TICKS(100));continue;}
            {Lock lock;if(saved){++session.photos;session.consecutive=0;session.last=timeSynchronized?time(nullptr):0;}
              else{++session.failures;++session.consecutive;strlcpy(session.error,"photo_capture_or_write_failed",sizeof(session.error));}}
            advance(false);{Lock lock;s=session;}bool written=metadata(s,true);working.store(false);
            if(!written)end("metadata_write_failed");else if(s.consecutive>=5)end("consecutive_photo_failures");
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
esp_err_t reply(httpd_req_t* req,const char* status,const char* text){appliance::HttpAuth::responseHeaders(req);
  httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,text);}
cJSON* input(httpd_req_t* req){char raw[193]={},type[48]={};if(strchr(req->uri,'?')||!req->content_len||req->content_len>=sizeof(raw)
  ||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))return nullptr;
  size_t at=0;while(at<req->content_len){int n=httpd_req_recv(req,raw+at,req->content_len-at);if(n<=0)return nullptr;at+=n;}
  if(memchr(raw,0,at)||strchr(raw,'\\')||strchr(raw,'['))return nullptr;unsigned braces=0;for(size_t i=0;i<at;++i)if(raw[i]=='{')++braces;
  return braces==1?cJSON_ParseWithLengthOpts(raw,at+1,nullptr,true):nullptr;}
esp_err_t control(httpd_req_t* req){if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  if(strchr(req->uri,'?'))return reply(req,"400 Bad Request","{}");
  if(req->method==HTTP_GET){char text[640];applianceTimelapseStatus(text,sizeof(text));return reply(req,"200 OK",text);}
  if(!strcmp(req->uri,"/api/timelapse/stop")){if(req->content_len)return reply(req,"400 Bad Request","{}");applianceTimelapseStop();return reply(req,"202 Accepted","{}");}
  cJSON* j=input(req);cJSON* interval=j?cJSON_GetObjectItemCaseSensitive(j,"intervalSeconds"):nullptr;
  cJSON* immediate=j?cJSON_GetObjectItemCaseSensitive(j,"immediate"):nullptr;
  bool valid=cJSON_IsObject(j)&&cJSON_GetArraySize(j)==2&&cJSON_IsNumber(interval)&&std::isfinite(interval->valuedouble)
    &&interval->valuedouble==interval->valueint&&interval->valueint>=5&&interval->valueint<=604800&&cJSON_IsBool(immediate);
  unsigned seconds=valid?interval->valueint:900;bool first=cJSON_IsTrue(immediate);cJSON_Delete(j);
  if(!valid)return reply(req,"400 Bad Request","{\"error\":\"invalid_timelapse_request\"}");
  if(applianceEventsArmed())return reply(req,"409 Conflict","{\"error\":\"stop_event_mode_first\"}");
  return applianceTimelapseStart(seconds,first)?reply(req,"201 Created","{}"):reply(req,"409 Conflict","{\"error\":\"timelapse_unavailable\"}");
}
bool parseQuery(httpd_req_t* req,char (&id)[56],char (&before)[56],bool sessions){char raw[160]={};size_t n=httpd_req_get_url_query_len(req);
  if(!n)return sessions;if(n>=sizeof(raw)||httpd_req_get_url_query_str(req,raw,sizeof(raw))!=ESP_OK)return false;
  if(sessions){if(strncmp(raw,"before=",7)||(!label(raw+7)&&!indexCursor(raw+7)))return false;strlcpy(before,raw+7,sizeof(before));return true;}
  if(strncmp(raw,"session=",8))return false;char* next=strchr(raw,'&');if(next)*next++=0;
  if(!label(raw+8))return false;strlcpy(id,raw+8,sizeof(id));
  if(next){if(strncmp(next,"before=",7)||(!jpegName(next+7)&&!indexCursor(next+7)))return false;strlcpy(before,next+7,sizeof(before));}return true;}
esp_err_t listing(httpd_req_t* req){if(!applianceCheckAuth(req))return ESP_OK;
  bool sessions=!strncmp(req->uri,"/api/timelapse/sessions",23);char id[56]={},before[56]={};
  if(!parseQuery(req,id,before,sessions))return reply(req,"400 Bad Request","{}");
  char directory[96];snprintf(directory,sizeof(directory),sessions?"/timelapse":"/timelapse/%s",id);
  Item items[PAGE+1]={};size_t count=0,scanned=0;bool truncated=false,more=false;char nextCursor[56]={},indexPath[128];
  snprintf(indexPath,sizeof(indexPath),sessions?"/timelapse/.sessions.index":"/timelapse/%s/photos.index",id);
  File index=SD.open(indexPath,FILE_READ);bool indexed=index&&(!before[0]||indexCursor(before));uint64_t began=now();
  if(indexed){uint32_t position=index.size()/sizeof(Item);if(before[0])position=std::min(position,uint32_t(strtoul(before+1,nullptr,10)));
    while(position&&count<PAGE){if(++scanned>SCAN||now()-began>2000){truncated=true;break;}
      Item entry={};--position;if(!index.seek(position*sizeof(Item))||index.read((uint8_t*)&entry,sizeof(entry))!=sizeof(entry))return reply(req,"500 Internal Server Error","{}");
      if(!memchr(entry.name,0,sizeof(entry.name))||!(sessions?label(entry.name):jpegName(entry.name)))continue;
      char path[160];snprintf(path,sizeof(path),"%s/%s",directory,entry.name);File child=SD.open(path,FILE_READ);
      if(child&&(sessions?child.isDirectory():!child.isDirectory())){entry.bytes=child.size();items[count++]=entry;}
      child.close();if(!(scanned%32))vTaskDelay(1);
    }more=position>0;if(more)snprintf(nextCursor,sizeof(nextCursor),"I%lu",(unsigned long)position);
  }else{if(before[0]&&indexCursor(before))return reply(req,"400 Bad Request","{}");
  File root=SD.open(directory);
  if(root&&root.isDirectory())for(File f=root.openNextFile();f;f=root.openNextFile()){
    if(++scanned>SCAN||now()-began>2000){f.close();truncated=true;break;}
    const char* name=f.name();if((sessions?f.isDirectory()&&label(name):!f.isDirectory()&&jpegName(name))&&(!before[0]||strcmp(name,before)<0)){
      size_t at=0;while(at<count&&strcmp(items[at].name,name)>0)++at;
      if(at<=PAGE){if(count<PAGE+1)++count;for(size_t i=count-1;i>at;--i)items[i]=items[i-1];strlcpy(items[at].name,name,56);items[at].bytes=f.size();}}
    f.close();if(!(scanned%32))vTaskDelay(1);
  }root.close();more=count>PAGE;count=std::min(count,PAGE);if(more&&count)strlcpy(nextCursor,items[count-1].name,sizeof(nextCursor));}
  index.close();
  cJSON* j=cJSON_CreateObject();cJSON* array=cJSON_AddArrayToObject(j,"items");
  for(size_t i=0;i<count;++i){auto* item=cJSON_CreateObject();cJSON_AddItemToArray(array,item);cJSON_AddStringToObject(item,"name",items[i].name);
    if(!sessions){char path[128];snprintf(path,sizeof(path),"/timelapse/%s/%s",id,items[i].name);cJSON_AddStringToObject(item,"path",path);cJSON_AddNumberToObject(item,"bytes",double(items[i].bytes));}
    else{char path[128],raw[1024]={};cJSON* meta=nullptr;
      for(const char* file:{"session.json","session.bak"}){snprintf(path,sizeof(path),"/timelapse/%s/%s",items[i].name,file);File f=SD.open(path);
        if(f&&f.size()<sizeof(raw)){size_t n=f.read((uint8_t*)raw,f.size());meta=cJSON_ParseWithLength(raw,n);}f.close();if(meta)break;}
      bool running;{Lock lock;running=active.load()&&!strcmp(items[i].name,session.id);}
      if(meta){if(!running)cJSON_ReplaceItemInObject(meta,"active",cJSON_CreateBool(false));cJSON_AddItemToObject(item,"metadata",meta);}
      cJSON_AddBoolToObject(item,"running",running);}}
  cJSON_AddBoolToObject(j,"more",more);cJSON_AddBoolToObject(j,"enumerationTruncated",truncated);
  cJSON_AddStringToObject(j,"nextBefore",nextCursor);char* text=cJSON_PrintUnformatted(j);cJSON_Delete(j);
  auto sent=reply(req,"200 OK",text?text:"{}");free(text);return sent;
}
esp_err_t removeSession(httpd_req_t* req){if(!applianceCheckAuth(req,true))return ESP_OK;cJSON* j=input(req);
  cJSON* id=j?cJSON_GetObjectItemCaseSensitive(j,"session"):nullptr;char name[56]={};
  bool valid=cJSON_IsObject(j)&&cJSON_GetArraySize(j)==1&&cJSON_IsString(id)&&label(id->valuestring);if(valid)strlcpy(name,id->valuestring,sizeof(name));cJSON_Delete(j);
  if(!valid)return reply(req,"400 Bad Request","{}");
  ApplianceForeground foreground;if(!foreground.ready())return reply(req,"503 Service Unavailable","{}");
  {Lock lock;if((active.load()&&!strcmp(name,session.id))||working.load()||applianceMediaBusy())return reply(req,"409 Conflict","{\"error\":\"session_in_use\"}");}
  char path[128],directory[96];snprintf(directory,sizeof(directory),"/timelapse/%s",name);File root=SD.open(directory);
  if(!root||!root.isDirectory())return reply(req,"404 Not Found","{}");
  // Validate the complete directory before deleting any entry. Foreign files
  // or nested directories are never recursively removed.
  size_t scanned=0;for(File f=root.openNextFile();f;f=root.openNextFile()){
    bool allowed=!f.isDirectory()&&(jpegName(f.name())||!strcmp(f.name(),"photos.index")||!strcmp(f.name(),"session.json")||!strcmp(f.name(),"session.tmp")||!strcmp(f.name(),"session.bak"));f.close();
    if(!allowed||++scanned>100000){root.close();return reply(req,"409 Conflict","{\"error\":\"unexpected_session_entry\"}");}}
  root.close();bool ok=true;root=SD.open(directory);
  for(File f=root.openNextFile();f;f=root.openNextFile()){snprintf(path,sizeof(path),"%s/%s",directory,f.name());f.close();if(!SD.remove(path)){ok=false;break;}vTaskDelay(1);}
  root.close();ok=ok&&SD.rmdir(directory);return reply(req,ok?"200 OK":"500 Internal Server Error",ok?"{\"deleted\":true}":"{}");
}
#if defined(XIAO_APPLIANCE_CODEC_TEST)
esp_err_t indexTest(httpd_req_t* req){if(!applianceCheckAuth(req,true))return ESP_OK;cJSON* j=input(req);
  cJSON* id=j?cJSON_GetObjectItemCaseSensitive(j,"session"):nullptr;char name[56]={};
  bool valid=cJSON_IsObject(j)&&cJSON_GetArraySize(j)==1&&cJSON_IsString(id)&&label(id->valuestring);if(valid)strlcpy(name,id->valuestring,sizeof(name));cJSON_Delete(j);
  if(!valid)return reply(req,"400 Bad Request","{}");ApplianceForeground foreground;
  if(!foreground.ready()||active.load()||working.load())return reply(req,"409 Conflict","{}");
  char path[128];snprintf(path,sizeof(path),"/timelapse/%s/photos.index",name);File index=SD.open(path,"r+");
  if(!index)return reply(req,"404 Not Found","{}");bool ok=index.seek(index.size()/sizeof(Item)*sizeof(Item));
  Item entry={};strlcpy(entry.name,"IMG_900000000000.jpg",sizeof(entry.name));
  // Only the explicit fixture can insert absent names. This exceeds the old
  // scan limit and proves that an empty page's cursor still reaches real JPEGs.
  for(unsigned i=0;ok&&i<4100;++i){ok=index.write((uint8_t*)&entry,sizeof(entry))==sizeof(entry);if(!(i%32))vTaskDelay(1);}
  index.flush();index.close();return reply(req,ok?"200 OK":"500 Internal Server Error","{}");}
#endif
}
bool applianceTimelapseActive(){return active.load();}
bool applianceTimelapseStart(uint32_t seconds,bool immediate){
  if(!mutex||!indexReady.load()||seconds<5||seconds>604800||active.load()||applianceEventsArmed()||applianceMaintenance()
    ||forceRecord||isCapturing||stopPlayback||applianceCaptureBusy()||!appSetupDone||!applianceStorageSpaceAvailable())return false;
  ApplianceForeground foreground;if(!foreground.ready())return false;Lock lock;
  if(active.load()||applianceEventsArmed())return false;Session next;next.interval=seconds;next.synchronized=timeSynchronized;
  next.started=timeSynchronized?time(nullptr):0;char stamp[24]="unsynced";
  if(timeSynchronized){time_t wall=time(nullptr);tm local={};localtime_r(&wall,&local);strftime(stamp,sizeof(stamp),"%Y%m%d_%H%M%S",&local);}
  snprintf(next.id,sizeof(next.id),"TL_%s_%08lx",stamp,(unsigned long)esp_random());
  appliancePhotoSettings(next.width,next.height,next.quality);next.next=now()+(immediate?0:uint64_t(seconds)*1000);
  char path[96];snprintf(path,sizeof(path),"/timelapse/%s",next.id);
  if(SD.exists(path)||!appendIndex("/timelapse/.sessions.index",next.id)||!SD.mkdir(path))return false;
  if(!metadata(next,true)){SD.rmdir(path);return false;}session=next;stopping.store(false);active.store(true);return true;
}
void applianceTimelapseStop(){stopping.store(true);}
bool applianceTimelapseStopAndWait(uint32_t timeoutMs){applianceTimelapseStop();const uint32_t began=millis();
  while(active.load()&&uint32_t(millis()-began)<timeoutMs)delay(5);return !active.load();}
void applianceTimelapseStatus(char* output,size_t bytes){if(!mutex){strlcpy(output,"{}",bytes);return;}Session s;{Lock lock;s=session;}
  auto* j=snapshot(s,active.load());char* text=cJSON_PrintUnformatted(j);cJSON_Delete(j);strlcpy(output,text?text:"{}",bytes);free(text);}
bool applianceTimelapseMediaPath(const char* path){constexpr size_t prefix=sizeof("/timelapse/")-1;
  if(strncmp(path,"/timelapse/",prefix)||strlen(path)>=128)return false;
  const char* slash=strchr(path+prefix,'/');if(!slash||size_t(slash-path-prefix)>=56)return false;char id[56]={};memcpy(id,path+prefix,slash-path-prefix);
  return label(id)&&jpegName(slash+1);}
bool applianceTimelapseBegin(httpd_handle_t server){mutex=xSemaphoreCreateMutex();if(!mutex)return false;
  const httpd_uri_t routes[]={
    {.uri="/api/timelapse",.method=HTTP_GET,.handler=control,.user_ctx=nullptr},
    {.uri="/api/timelapse/start",.method=HTTP_POST,.handler=control,.user_ctx=nullptr},
    {.uri="/api/timelapse/stop",.method=HTTP_POST,.handler=control,.user_ctx=nullptr},
    {.uri="/api/timelapse/sessions",.method=HTTP_GET,.handler=listing,.user_ctx=nullptr},
    {.uri="/api/timelapse/files",.method=HTTP_GET,.handler=listing,.user_ctx=nullptr},
    {.uri="/api/timelapse/delete",.method=HTTP_POST,.handler=removeSession,.user_ctx=nullptr}};
  for(const auto& route:routes)if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  httpd_uri_t indexRoute={};indexRoute.uri="/api/timelapse/test-index";indexRoute.method=HTTP_POST;indexRoute.handler=indexTest;
  if(httpd_register_uri_handler(server,&indexRoute)!=ESP_OK)return false;
#endif
  // This task uses SD and the camera mailbox; flash/NVS operations remain on
  // internal-stack owners. The pinned SDK supports external static stacks.
  static_assert(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM,"Photo stack requires the pinned SDK");
  photoStack=(StackType_t*)heap_caps_aligned_alloc(16,8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return photoStack&&xTaskCreateStaticPinnedToCore(owner,"intervalPhotos",8192,nullptr,1,photoStack,&photoTask,0);
}
#endif
