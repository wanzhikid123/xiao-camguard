#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceJobs.h"
#include "applianceCodec.h"
#include "applianceWeb.h"
#include "applianceMedia.h"
#include "applianceSettings.h"
#include "applianceEvents.h"
#include "applianceTimelapse.h"
#include "applianceOta.h"
#include <Preferences.h>
#include <HttpAuth.h>
#include <atomic>
#include "cJSON.h"
#include "esp_heap_caps.h"

#if defined(XIAO_APPLIANCE_AUTH_TEST) || defined(XIAO_APPLIANCE_EVENT_TEST) || defined(XIAO_APPLIANCE_OTA_ROLLBACK_TEST) \
  || defined(XIAO_APPLIANCE_SD_ABSENT_TEST) || defined(XIAO_APPLIANCE_WG_TEARDOWN_TEST) \
  || defined(XIAO_APPLIANCE_FULL_TUNNEL_TEST) || defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
#define APPLIANCE_NO_HISTORY_CONVERSION
#endif

namespace {
SemaphoreHandle_t mutex=nullptr;
SemaphoreHandle_t foregroundMutex=nullptr;
StaticTask_t codecTask;StackType_t* codecStack=nullptr;
std::atomic<bool> running{false},enabled{true};
std::atomic<bool> persistDisable{false};
std::atomic<unsigned> leases{0};std::atomic<uint32_t> activity{0};
Preferences prefs;ApplianceCodecReport result;char source[128]={};const char* state="waiting_idle";
uint32_t completed=0,failed=0,skipped=0,consecutiveFailures=0;char cursor[128]={};
#if defined(XIAO_APPLIANCE_CODEC_TEST)
char fixtureSource[128]={};
unsigned fixtureFlags=0;
#endif
class Lock{public:Lock(){xSemaphoreTake(mutex,portMAX_DELAY);}~Lock(){xSemaphoreGive(mutex);}};
bool busy(){return leases.load()
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  ||applianceCodecTestBusy()
#endif
  ||forceRecord||isCapturing||stopPlayback||doPlayback||doKeepFrame||applianceCaptureBusy()
  ||applianceStreamsBusy()||applianceMediaBusy()||applianceEventsArmed()||applianceTimelapseActive()||applianceMaintenance()
  ||applianceNetworkBusy()||!appSetupDone||startupFailure[0];}
bool abort(){return !enabled.load()||busy()||readInternalTemp()>80||ESP.getFreeHeap()<16384;}
void progress(const ApplianceCodecReport& r){Lock lock;result=r;}
void saveDisabled(void*){{Lock lock;if(persistDisable.exchange(false)&&prefs.putBool("enabled",false)!=1)
    LOG_ERR("Unable to persist conversion failure protection");}vTaskDelete(nullptr);}
bool oldest(char (&out)[128]){
#if defined(APPLIANCE_NO_HISTORY_CONVERSION)
  out[0]=0;return false; // Other fixtures must not touch historical recordings.
#endif
  char after[128];
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  char selected[128];
#endif
  {Lock lock;strlcpy(after,cursor,sizeof(after));
#if defined(XIAO_APPLIANCE_CODEC_TEST)
    strlcpy(selected,fixtureSource,sizeof(selected));
#endif
  }out[0]=0;File root=SD.open("/");if(!root)return false;
  // Scan finalized, strictly validated media only. Yield and cancel per entry; never
  // enter /data, timelapse, the current AVI, WAV or settings files.
  for(File directory=root.openNextFile();directory;directory=root.openNextFile()){
    const char* name=directory.name();bool dated=directory.isDirectory()&&strlen(name)==8;
    for(unsigned i=0;dated&&i<8;++i)dated=isdigit((uint8_t)name[i]);
    if(dated)for(File f=directory.openNextFile();f;f=directory.openNextFile()){
      char path[128];snprintf(path,sizeof(path),"/%s/%s",name,f.name());
      if(!f.isDirectory()&&applianceMediaPath(path)&&strlen(path)>4&&!strcmp(path+strlen(path)-4,".avi")
#if defined(XIAO_APPLIANCE_CODEC_TEST)
        &&selected[0]&&!strcmp(path,selected)
#endif
        &&(!after[0]||strcmp(path,after)>0)&&(!out[0]||strcmp(path,out)<0))strlcpy(out,path,sizeof(out));
      f.close();if(abort()){directory.close();root.close();return false;}vTaskDelay(1);
    }
    directory.close();if(abort()){root.close();return false;}
  }root.close();return out[0];}
struct Journal{uint32_t magic=0x4d503431,version=2;ApplianceCodecReport report;};
bool cleanupInterrupted(){File root=SD.open("/");if(!root)return false;
  for(File directory=root.openNextFile();directory;directory=root.openNextFile()){
    const char* name=directory.name();bool dated=directory.isDirectory()&&strlen(name)==8;
    for(unsigned i=0;dated&&i<8;++i)dated=isdigit((uint8_t)name[i]);
    if(dated)for(File f=directory.openNextFile();f;f=directory.openNextFile()){
      char path[144],media[128];snprintf(path,sizeof(path),"/%s/%s",name,f.name());
      const size_t n=strlen(path);bool temporary=!f.isDirectory()&&n>9&&!strcmp(path+n-9,".mp4.part");
      if(temporary){snprintf(media,sizeof(media),"%.*s.avi",int(n-9),path);temporary=applianceMediaPath(media);}
      // User deletion can leave a completed transaction record behind. Only
      // remove our exact journal format when both associated media are absent.
      if(!temporary&&!f.isDirectory()&&n>12&&!strcmp(path+n-12,".mp4.journal")&&f.size()==sizeof(Journal)){
        uint32_t header[2]={};char published[128];snprintf(media,sizeof(media),"%.*s.avi",int(n-12),path);
        snprintf(published,sizeof(published),"%.*s",int(n-8),path);
        temporary=applianceMediaPath(media)&&f.read((uint8_t*)header,sizeof(header))==sizeof(header)
          &&header[0]==0x4d503431&&header[1]==2&&!SD.exists(media)&&!SD.exists(published);
      }
      f.close();if(temporary&&!SD.remove(path)){directory.close();root.close();return false;}
      if(busy()){directory.close();root.close();return false;}vTaskDelay(1);
    }directory.close();if(busy()){root.close();return false;}
  }root.close();return true;}
bool writeJournal(const char* path,const ApplianceCodecReport& report){Journal j;j.report=report;
  SD.remove(path);File f=SD.open(path,FILE_WRITE);bool ok=f&&f.write((uint8_t*)&j,sizeof(j))==sizeof(j);
  if(f){f.flush();ok=ok&&f.size()==sizeof(j);f.close();}return ok;}
bool recover(const char* avi,const char* mp4,const char* journal,ApplianceCodecReport& report){Journal j;File f=SD.open(journal,FILE_READ);
  bool valid=f&&f.size()==sizeof(j)&&f.read((uint8_t*)&j,sizeof(j))==sizeof(j)&&j.magic==0x4d503431&&j.version==2;f.close();
  ApplianceCodecReport current;
  if(!valid||!applianceValidateMp4(mp4,j.report,abort)||!applianceInspectAvi(avi,current,abort)||current.sourceCrc!=j.report.sourceCrc
    ||current.sourceBytes!=j.report.sourceBytes||abort())return false;
  if(!SD.remove(avi))return false;report=j.report;SD.remove(journal);return true;}
void owner(void*){
  bool cleaned=false;
  for(;;){
    if(!cleaned&&appSetupDone&&SD.cardType()!=CARD_NONE&&!busy())cleaned=cleanupInterrupted();
    if(!enabled.load()||busy()||SD.cardType()==CARD_NONE){activity.store(millis());{Lock lock;state=enabled.load()?"paused":"disabled";}vTaskDelay(pdMS_TO_TICKS(250));continue;}
    if(uint32_t(millis()-activity.load())<30000){vTaskDelay(pdMS_TO_TICKS(250));continue;}
    if(ESP.getFreeHeap()<32768||ESP.getFreePsram()<512*1024||readInternalTemp()>80||!applianceStorageSpaceAvailable()){
      {Lock lock;state="paused";}activity.store(millis());vTaskDelay(pdMS_TO_TICKS(250));continue;}
    char next[128]={};if(!oldest(next)){{Lock lock;cursor[0]=0;}activity.store(millis());vTaskDelay(pdMS_TO_TICKS(250));continue;}
    {Lock lock;if(busy())continue;running.store(true);strlcpy(source,next,sizeof(source));result={};state="converting";}
    const uint32_t cpu=getCpuFrequencyMhz();setCpuFrequencyMhz(240);
    char mp4[128],temporary[144],journal[144];snprintf(mp4,sizeof(mp4),"%.*s.mp4",int(strlen(next)-4),next);
    snprintf(temporary,sizeof(temporary),"%s.part",mp4);snprintf(journal,sizeof(journal),"%s.journal",mp4);
    ApplianceCodecReport report;bool converted=false;const char* outcome="failed";
    if(SD.exists(mp4)){
      converted=recover(next,mp4,journal,report);outcome=converted?"completed":"destination_exists";
    }else{
      // This name is reserved for this converter and derived from a validated
      // AVI path. It is invisible to listings and never treated as media.
      SD.remove(temporary);
      converted=applianceStorageSpaceAvailable()&&applianceConvertAvi(next,temporary,report,abort,progress);
#if defined(XIAO_APPLIANCE_CODEC_TEST)
      unsigned flags;{Lock lock;flags=fixtureFlags;}
      if(converted&&(flags&2))converted=applianceCodecTestPad(temporary,report,abort);
#endif
      if(converted&&!abort()){
        converted=writeJournal(journal,report)&&!SD.exists(mp4)&&SD.rename(temporary,mp4);
        if(converted){bool valid=applianceValidateMp4(mp4,report,abort);
          if(abort())strlcpy(report.error,"aborted",sizeof(report.error));
          if(!valid){SD.remove(mp4);SD.remove(journal);}
#if defined(XIAO_APPLIANCE_CODEC_TEST)
          if(valid&&(flags&1)){enabled.store(false);strlcpy(report.error,"aborted",sizeof(report.error));converted=false;}
          else
#endif
          converted=valid&&!abort()&&SD.remove(next);if(converted)SD.remove(journal);}
      }else converted=false;
      if(!converted)SD.remove(temporary);
      outcome=converted?"completed":!strcmp(report.error,"aborted")?"paused":!strcmp(report.error,"unsupported_source_resolution")?"unsupported":"failed";
    }
    setCpuFrequencyMhz(cpu);
    bool protect=false;
    {Lock lock;result=report;state=outcome;if(converted){++completed;consecutiveFailures=0;}else if(!strcmp(outcome,"unsupported")||!strcmp(outcome,"destination_exists"))++skipped;
      else if(strcmp(outcome,"paused")){++failed;if(++consecutiveFailures>=3){enabled.store(false);persistDisable.store(true);protect=true;}}
      running.store(false);}
    // Flash commits require an internal stack. The SD-only converter retains
    // its PSRAM stack; a short-lived internal owner persists the protection.
    if(protect&&xTaskCreatePinnedToCore(saveDisabled,"saveCodecGuard",4096,nullptr,1,nullptr,0)!=pdPASS)
      LOG_ERR("Conversion disabled in RAM; failure protection could not be persisted");
    if(strcmp(outcome,"paused")){Lock lock;strlcpy(cursor,next,sizeof(cursor));}activity.store(millis());
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
esp_err_t settings(httpd_req_t* req){if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  appliance::HttpAuth::responseHeaders(req);httpd_resp_set_type(req,"application/json");
  if(strchr(req->uri,'?')){httpd_resp_set_status(req,"400 Bad Request");return httpd_resp_sendstr(req,"{}");}
  if(req->method==HTTP_POST){char raw[48]={},type[32]={};size_t at=0;
    if(!req->content_len||req->content_len>=sizeof(raw)||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json")){
      httpd_resp_set_status(req,"400 Bad Request");return httpd_resp_sendstr(req,"{}");}
    while(at<req->content_len){int n=httpd_req_recv(req,raw+at,req->content_len-at);if(n<=0)return ESP_FAIL;at+=n;}
    cJSON* json=!memchr(raw,0,at)&&!strchr(raw,'\\')&&!strchr(raw,'[')?cJSON_ParseWithLengthOpts(raw,at+1,nullptr,true):nullptr;
    cJSON* field=json?cJSON_GetObjectItemCaseSensitive(json,"enabled"):nullptr;
    bool valid=cJSON_IsObject(json)&&cJSON_GetArraySize(json)==1&&cJSON_IsBool(field),on=cJSON_IsTrue(field);cJSON_Delete(json);
    if(!valid){
      httpd_resp_set_status(req,"400 Bad Request");return httpd_resp_sendstr(req,"{}");}
    {Lock lock;if(prefs.putBool("enabled",on)!=1){httpd_resp_set_status(req,"500 Internal Server Error");return httpd_resp_sendstr(req,"{}");}
      persistDisable.store(false);enabled.store(on);consecutiveFailures=0;activity.store(millis());}
  }
  char text[640];applianceConversionStatus(text,sizeof(text));return httpd_resp_sendstr(req,text);
}
}
ApplianceForeground::ApplianceForeground(){
  if(foregroundMutex){if(xSemaphoreTakeRecursive(foregroundMutex,pdMS_TO_TICKS(25000))!=pdTRUE)return;serialized=true;}
  if(mutex){Lock lock;leases.fetch_add(1);activity.store(millis());}else leases.fetch_add(1);
  leased=true;
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  applianceCodecTestCancel();
#endif
  const uint32_t started=millis();while(applianceConversionBusy()&&uint32_t(millis()-started)<25000)delay(5);available=!applianceConversionBusy();
}
ApplianceForeground::~ApplianceForeground(){if(leased){activity.store(millis());leases.fetch_sub(1);}if(serialized)xSemaphoreGiveRecursive(foregroundMutex);}
bool applianceConversionBusy(){return running.load()
#if defined(XIAO_APPLIANCE_CODEC_TEST)
  ||applianceCodecTestBusy()
#endif
  ;}
#if defined(XIAO_APPLIANCE_CODEC_TEST)
bool applianceConversionTestSource(const char* path,unsigned flags){if(!path||!applianceMediaPath(path)||strlen(path)<4||strcmp(path+strlen(path)-4,".avi"))return false;
  ApplianceForeground foreground;if(!foreground.ready())return false;Lock lock;
  strlcpy(fixtureSource,path,sizeof(fixtureSource));fixtureFlags=flags;cursor[0]=0;enabled.store(true);consecutiveFailures=0;persistDisable.store(false);return true;}
bool applianceConversionTestCorrupt(const char* path){ApplianceForeground foreground;if(!foreground.ready())return false;
  {Lock lock;if(strcmp(path,fixtureSource))return false;enabled.store(false);}
  char mp4[128];snprintf(mp4,sizeof(mp4),"%.*s.mp4",int(strlen(path)-4),path);File f=SD.open(mp4,"r+");uint8_t byte=0;
  if(!f||f.size()<128||!f.seek(f.size()-1)||f.read(&byte,1)!=1)return false;byte^=1;
  const bool ok=f.seek(f.size()-1)&&f.write(&byte,1)==1;f.flush();f.close();return ok;}
bool applianceConversionTestPart(const char* path){ApplianceForeground foreground;if(!foreground.ready()||!applianceMediaPath(path)||!SD.exists(path))return false;
  {Lock lock;enabled.store(false);strlcpy(fixtureSource,path,sizeof(fixtureSource));}
  char part[144];snprintf(part,sizeof(part),"%.*s.mp4.part",int(strlen(path)-4),path);if(SD.exists(part))return false;
  File f=SD.open(part,FILE_WRITE);const uint8_t bytes[]={0,0,0,8,'f','r','e','e'};
  const bool ok=f&&f.write(bytes,sizeof(bytes))==sizeof(bytes);f.flush();f.close();return ok;}
#endif
void applianceConversionStatus(char* output,size_t bytes){if(!mutex){strlcpy(output,"{}",bytes);return;}Lock lock;
  snprintf(output,bytes,"{\"enabled\":%s,\"running\":%s,\"state\":\"%s\",\"source\":\"%s\",\"frames\":%lu,\"totalFrames\":%lu,\"elapsedMs\":%lu,\"completed\":%lu,\"failed\":%lu,\"skipped\":%lu,\"error\":\"%s\"}",
    enabled.load()?"true":"false",running.load()?"true":"false",state,source,(unsigned long)result.frames,(unsigned long)result.totalFrames,
    (unsigned long)result.elapsedMs,(unsigned long)completed,(unsigned long)failed,(unsigned long)skipped,result.error);
}
bool applianceJobsBegin(httpd_handle_t server){mutex=xSemaphoreCreateMutex();if(!mutex||!prefs.begin("camjobs",false))return false;
  foregroundMutex=xSemaphoreCreateRecursiveMutex();if(!foregroundMutex)return false;
  enabled.store(prefs.getBool("enabled",true));activity.store(millis());
#if defined(XIAO_APPLIANCE_CODEC_TEST) || defined(APPLIANCE_NO_HISTORY_CONVERSION)
  enabled.store(false); // The fixture never converts or deletes historical AVI.
#endif
  for(httpd_method_t method:{HTTP_GET,HTTP_POST}){httpd_uri_t route={};route.uri="/api/settings/transcode";route.method=method;route.handler=settings;
    if(httpd_register_uri_handler(server,&route)!=ESP_OK)return false;}
  // The pinned SDK explicitly allows external static task stacks. This owner
  // writes SD only; NVS/OTA operations stay on internal-stack foreground tasks.
  static_assert(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM,"Codec stack placement requires the pinned SDK");
  codecStack=(StackType_t*)heap_caps_aligned_alloc(16,16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return codecStack&&xTaskCreateStaticPinnedToCore(owner,"idleConverter",16384,nullptr,0,codecStack,&codecTask,1);
}
#endif
