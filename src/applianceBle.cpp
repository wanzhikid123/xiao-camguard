#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceBle.h"
#include "applianceWeb.h"
#include "applianceMedia.h"
#include "applianceSettings.h"
#include "applianceTimelapse.h"
#include "applianceEvents.h"
#include "applianceJobs.h"
#include "applianceOta.h"
#include <Preferences.h>
#include <HttpAuth.h>
#include <atomic>
#include "cJSON.h"
extern "C" {
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_hs_adv.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
void ble_store_config_init(void);
}
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
// Match IDF's supported EXTERNAL NimBLE allocation strategy without rebuilding
// or changing the pinned Arduino SDK. Controller/DMA and host task stacks keep
// their SDK allocation rules. Only the host's documented allocator is wrapped.
extern "C" void* __wrap_nimble_mem_malloc(size_t bytes){
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
extern "C" void* __wrap_nimble_mem_calloc(size_t count,size_t bytes){
  return heap_caps_calloc(count,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
extern "C" void __wrap_nimble_mem_free(void* memory){heap_caps_free(memory);}
// Arduino 3.3.12 otherwise releases BLE controller RAM before setup(). Native
// NimBLE needs the same retained-memory declaration as Arduino BLE libraries.
extern "C" bool bleInUse(void){
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  return false;
#else
  return true;
#endif
}

namespace {
constexpr unsigned COUNT=22;
ble_uuid128_t uuids[COUNT+1];ble_gatt_chr_def characteristics[COUNT+1]={};ble_gatt_svc_def services[2]={};
uint16_t handles[COUNT]={};uint8_t addressType=0;
std::atomic<uint16_t> connection{BLE_HS_CONN_HANDLE_NONE};std::atomic<uint32_t> pin{0};
std::atomic<bool> ready{false},rotating{false};
SemaphoreHandle_t mutex=nullptr;QueueHandle_t queue=nullptr;Preferences prefs;
char name[24]={},ssid[33]={},password[64]={};
char (*values)[96]=nullptr,(*pendingValues)[96]=nullptr;
bool hasSSID=false,hasPassword=false;uint32_t stagedAt=0,interval=900;
uint16_t stagingConnection=BLE_HS_CONN_HANDLE_NONE;
struct Command{unsigned id;uint16_t connection;char value[65];};
class Lock{public:Lock(){xSemaphoreTake(mutex,portMAX_DELAY);}~Lock(){xSemaphoreGive(mutex);}};
bool secure(uint16_t handle){ble_gap_conn_desc d={};return !rotating.load()&&handle!=BLE_HS_CONN_HANDLE_NONE
  &&ble_gap_conn_find(handle,&d)==0&&d.sec_state.encrypted&&d.sec_state.authenticated&&d.sec_state.bonded&&d.sec_state.key_size==16;}
void clearStaged(){memset(ssid,0,sizeof(ssid));memset(password,0,sizeof(password));hasSSID=hasPassword=false;stagingConnection=BLE_HS_CONN_HANDLE_NONE;}
void result(const char* text){{Lock lock;strlcpy(values[19],text,sizeof(values[19]));}if(ready.load())ble_gatts_chr_updated(handles[19]);}
void advertise();
int gap(ble_gap_event* event,void*){
  switch(event->type){
    case BLE_GAP_EVENT_CONNECT:if(!event->connect.status){connection.store(event->connect.conn_handle);}else advertise();break;
    case BLE_GAP_EVENT_DISCONNECT:{connection.store(BLE_HS_CONN_HANDLE_NONE);{Lock lock;clearStaged();}advertise();break;}
    case BLE_GAP_EVENT_ADV_COMPLETE:advertise();break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:{
      if(event->passkey.params.action!=BLE_SM_IOACT_DISP)return BLE_HS_EAUTHEN;
      ble_sm_io io={};io.action=BLE_SM_IOACT_DISP;io.passkey=pin.load();return ble_sm_inject_io(event->passkey.conn_handle,&io);}
    case BLE_GAP_EVENT_REPEAT_PAIRING:return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ENC_CHANGE:if(event->enc_change.status||!secure(event->enc_change.conn_handle))
      ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_AUTH_FAIL);break;
  }return 0;
}
void advertise(){if(!ready.load()||connection.load()!=BLE_HS_CONN_HANDLE_NONE)return;
  ble_hs_adv_fields fields={};fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128=&uuids[0];fields.num_uuids128=1;fields.uuids128_is_complete=1;
  if(ble_gap_adv_set_fields(&fields))return;
  fields={};fields.name=(uint8_t*)name;fields.name_len=strlen(name);fields.name_is_complete=1;
  if(ble_gap_adv_rsp_set_fields(&fields))return;
  ble_gap_adv_params p={};p.conn_mode=BLE_GAP_CONN_MODE_UND;p.disc_mode=BLE_GAP_DISC_MODE_GEN;
  p.itvl_min=320;p.itvl_max=480;ble_gap_adv_start(addressType,nullptr,BLE_HS_FOREVER,&p,gap,nullptr);
}
void synced(){if(ble_hs_id_infer_auto(0,&addressType))return;ready.store(true);advertise();}
void host(void*){nimble_port_run();nimble_port_freertos_deinit();}
int access(uint16_t handle,uint16_t,ble_gatt_access_ctxt* context,void* argument){
  const unsigned id=uintptr_t(argument);
  if(context->op==BLE_GATT_ACCESS_OP_READ_CHR){char text[96];{Lock lock;strlcpy(text,values[id-1],sizeof(text));}
    return os_mbuf_append(context->om,text,strlen(text))?BLE_ATT_ERR_INSUFFICIENT_RES:0;}
  if(context->op!=BLE_GATT_ACCESS_OP_WRITE_CHR||!secure(handle))return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
  uint16_t n=0;Command command={id,handle,{}};
  if(ble_hs_mbuf_to_flat(context->om,command.value,sizeof(command.value)-1,&n)||memchr(command.value,0,n))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  command.value[n]=0;
  for(unsigned i=0;i<n;++i)if((uint8_t)command.value[i]<32||(uint8_t)command.value[i]==127)return BLE_ATT_ERR_UNLIKELY;
  // Callbacks copy bounded input only. The worker checks security again before
  // submitting mutations. Password has no READ/NOTIFY property.
  bool valid=id==3?(n>0&&n<=32):id==4?n<=63:id==5?!strcmp(command.value,"APPLY"):
    id==13?!strcmp(command.value,"PHOTO"):id==14?(!strcmp(command.value,"START")||!strcmp(command.value,"STOP")):
    id==15?!strcmp(command.value,"REBOOT"):id==16?!strcmp(command.value,"FACTORY_RESET_CONFIRM"):
    id==18?n>0&&n<=6:id==19?(!strcmp(command.value,"START")||!strcmp(command.value,"START_DELAY")||!strcmp(command.value,"STOP")):false;
  if(!valid)return BLE_ATT_ERR_UNLIKELY;
  if(xQueueSend(queue,&command,0)!=pdTRUE){memset(&command,0,sizeof(command));return BLE_ATT_ERR_INSUFFICIENT_RES;}
  memset(&command,0,sizeof(command));return 0;
}
bool rotate(){rotating.store(true);uint32_t next=esp_random()%1000000;
  if(prefs.putUInt("pin",next)!=sizeof(uint32_t)){rotating.store(false);return false;}
  pin.store(next);uint16_t handle=connection.load();if(handle!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(handle,BLE_ERR_REM_USER_CONN_TERM);
  bool ok=ble_store_clear()==0;{Lock lock;clearStaged();}rotating.store(false);return ok;
}
void refresh(){char wifi[64],currentSSID[33],wg[64];applianceBleNetworkStatus(wifi,sizeof(wifi),currentSSID,sizeof(currentSSID),wg,sizeof(wg));
  auto* next=pendingValues;memset(next,0,COUNT*96);
  strlcpy(next[0],startupFailure[0]?"ERROR":!appSetupDone?"BOOTING":isCapturing?"RECORDING":WiFi.status()==WL_CONNECTED?"READY":"WIFI_CONNECTING",96);
  strlcpy(next[1],wifi,96);strlcpy(next[2],currentSSID,96);
  strlcpy(next[5],WiFi.localIP().toString().c_str(),96);strlcpy(next[6],"xiao-camera.local",96);snprintf(next[7],96,"%d",WiFi.RSSI());
  const float temperature=readInternalTemp();snprintf(next[8],96,isfinite(temperature)&&temperature!=NULL_TEMP?"%.1f":"UNAVAILABLE",temperature);
  strlcpy(next[9],SD.cardType()!=CARD_NONE?"READY":"UNAVAILABLE",96);
  snprintf(next[10],96,"%llu",(unsigned long long)((SD.totalBytes()-SD.usedBytes())/1024/1024));
  strlcpy(next[11],startupFailure[0]?"ERROR":isCapturing||forceRecord?"RECORDING":applianceCaptureBusy()?"PHOTO":"IDLE",96);
  strlcpy(next[16],wg,96);snprintf(next[17],96,"%lu",(unsigned long)interval);
  char status[640];
  for(unsigned i=0;i<2;++i){if(i)applianceTimelapseStatus(status,sizeof(status));else applianceConversionStatus(status,sizeof(status));
    cJSON* j=cJSON_Parse(status);const char* running=i?"active":"running";
    auto* flag=j?cJSON_GetObjectItemCaseSensitive(j,running):nullptr;auto* count=j?cJSON_GetObjectItemCaseSensitive(j,i?"photos":"frames"):nullptr;
    snprintf(next[20+i],96,"%s;%s=%lu",cJSON_IsTrue(flag)?"RUNNING":"IDLE",i?"photos":"frames",(unsigned long)(cJSON_IsNumber(count)?count->valuedouble:0));cJSON_Delete(j);}
  for(unsigned i=0;i<COUNT;++i){if(i==3||i==4||i==12||i==13||i==14||i==15||i==18||i==19)continue;
    bool changed;{Lock lock;changed=strcmp(values[i],next[i]);strlcpy(values[i],next[i],96);}if(changed&&ready.load())ble_gatts_chr_updated(handles[i]);}
}
void owner(void*){uint32_t refreshed=0;char serial[32]={};size_t serialBytes=0;
  for(;;){Command command={};if(xQueueReceive(queue,&command,pdMS_TO_TICKS(100))==pdTRUE){
    bool ok=false;
    if(command.id==100){ApplianceForeground foreground;ok=foreground.ready()&&rotate();result(ok?"PIN_ROTATED":"PIN_ROTATE_FAILED");}
    else if(secure(command.connection)&&!applianceMaintenance()){
      if(command.id==3||command.id==4){Lock lock;if(stagingConnection!=command.connection)clearStaged();stagingConnection=command.connection;stagedAt=millis();
        if(command.id==3){strlcpy(ssid,command.value,sizeof(ssid));hasSSID=true;}else{strlcpy(password,command.value,sizeof(password));hasPassword=true;}ok=true;}
      else if(command.id==5){char target[33]={},secret[64]={};bool staged;
        {Lock lock;staged=hasSSID&&hasPassword&&stagingConnection==command.connection&&uint32_t(millis()-stagedAt)<300000;
          if(staged){memcpy(target,ssid,sizeof(target));memcpy(secret,password,sizeof(secret));}clearStaged();}
        ApplianceForeground foreground;ok=staged&&foreground.ready()&&applianceBleWifi(target,secret);memset(target,0,sizeof(target));memset(secret,0,sizeof(secret));}
      else if(command.id==13){char path[128];ok=applianceSaveManualPhoto(path,sizeof(path));}
      else if(command.id==14)ok=applianceRequestRecording(!strcmp(command.value,"START"));
      else if(command.id==15){bool stopped=applianceTimelapseStopAndWait();ApplianceForeground foreground;ok=stopped&&foreground.ready()&&!forceRecord&&!isCapturing&&!stopPlayback&&!applianceEventsArmed()&&!applianceMediaBusy();
        if(ok){applianceTimelapseStop();result("REBOOT_REQUESTED");doRestart("BLE management");}}
      else if(command.id==16){bool stopped=applianceTimelapseStopAndWait();ApplianceForeground foreground;ok=stopped&&foreground.ready()&&!forceRecord&&!isCapturing&&!stopPlayback&&!applianceEventsArmed()&&!applianceMediaBusy()&&!applianceNetworkBusy();
        if(ok){applianceTimelapseStop();ok=ble_store_clear()==0&&applianceRequestFactoryReset();}}
      else if(command.id==18){char* end=nullptr;unsigned long value=strtoul(command.value,&end,10);ok=end&&!*end&&value>=5&&value<=604800;
        if(ok)interval=value;}
      else if(command.id==19){if(!strcmp(command.value,"STOP")){applianceTimelapseStop();ok=true;}
        else ok=applianceTimelapseStart(interval,strcmp(command.value,"START_DELAY")!=0);}
      result(ok?command.id==5?"WIFI_APPLY_REQUESTED":"ACCEPTED":applianceTimelapseActive()?"STOP_TIMELAPSE_FIRST":applianceEventsArmed()?"STOP_EVENT_MODE_FIRST":"REJECTED");
    }else result("AUTHENTICATION_REQUIRED");
    memset(&command,0,sizeof(command));
  }
  if(uint32_t(millis()-refreshed)>=2000){refresh();refreshed=millis();Lock lock;if(hasSSID&&uint32_t(millis()-stagedAt)>=300000)clearStaged();}
  // USB is a local recovery channel. PIN is printed only on an explicit local
  // command, never during boot and never in the status or SD log.
  while(Serial.available()){int c=Serial.read();if(c=='\r')continue;if(c=='\n'){
    serial[serialBytes]=0;if(!strcmp(serial,"BLE PIN"))Serial.printf("BLE PIN: %06lu\n",(unsigned long)pin.load());
    else if(!strcmp(serial,"BLE PIN ROTATE")){ApplianceForeground foreground;if(foreground.ready()&&rotate())Serial.printf("BLE PIN: %06lu\n",(unsigned long)pin.load());}
    memset(serial,0,sizeof(serial));serialBytes=0;
  }else if(serialBytes+1<sizeof(serial))serial[serialBytes++]=char(c);else serialBytes=0;}
  }
}
esp_err_t settings(httpd_req_t* req){if(!applianceCheckAuth(req,req->method==HTTP_POST))return ESP_OK;
  appliance::HttpAuth::responseHeaders(req);httpd_resp_set_type(req,"application/json");
  if(strchr(req->uri,'?')||req->content_len){httpd_resp_set_status(req,"400 Bad Request");return httpd_resp_sendstr(req,"{}");}
  if(req->method==HTTP_POST){Command command={100,BLE_HS_CONN_HANDLE_NONE,{}};
    if(xQueueSend(queue,&command,0)!=pdTRUE){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"{}");}
    httpd_resp_set_status(req,"202 Accepted");return httpd_resp_sendstr(req,"{\"pending\":true}");}
  char text[256];snprintf(text,sizeof(text),"{\"ready\":%s,\"name\":\"%s\",\"pin\":\"%06lu\",\"connected\":%s,\"authenticated\":%s}",
    ready.load()?"true":"false",name,(unsigned long)pin.load(),connection.load()!=BLE_HS_CONN_HANDLE_NONE?"true":"false",secure(connection.load())?"true":"false");
  return httpd_resp_sendstr(req,text);
}
}
void applianceBleStatus(char* output,size_t bytes){snprintf(output,bytes,"{\"ready\":%s,\"connected\":%s,\"authenticated\":%s}",
  ready.load()?"true":"false",connection.load()!=BLE_HS_CONN_HANDLE_NONE?"true":"false",secure(connection.load())?"true":"false");}
bool applianceBleBegin(httpd_handle_t server){
#if defined(XIAO_APPLIANCE_AUTH_TEST)
  return true;
#else
  mutex=xSemaphoreCreateMutex();queue=xQueueCreate(4,sizeof(Command));if(!mutex||!queue||!prefs.begin("camble",false))return false;
  values=(char (*)[96])heap_caps_calloc(COUNT,96,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  pendingValues=(char (*)[96])heap_caps_calloc(COUNT,96,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!values||!pendingValues)return false;
  uint32_t stored=prefs.getUInt("pin",UINT32_MAX);if(stored>999999){stored=esp_random()%1000000;if(prefs.putUInt("pin",stored)!=sizeof(uint32_t))return false;}pin.store(stored);
  uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);snprintf(name,sizeof(name),"XIAO-Camera-%02X%02X",mac[4],mac[5]);
  for(unsigned i=0;i<=COUNT;++i){uuids[i].u.type=BLE_UUID_TYPE_128;
    const uint8_t bytes[]={0x00,0x00,0x00,0xe1,0x23,0x5d,0xb1,0xa7,0x9f,0x4b,0x3a,0x8c,uint8_t(i),0x00,0x10,0x7a};memcpy(uuids[i].value,bytes,16);}
  for(unsigned i=0;i<COUNT;++i){unsigned id=i+1;auto& c=characteristics[i];c.uuid=&uuids[id].u;c.access_cb=access;c.arg=(void*)uintptr_t(id);c.val_handle=&handles[i];
    bool write=id==3||id==4||id==5||id==13||id==14||id==15||id==16||id==18||id==19;
    bool read=id!=4&&id!=5&&id!=13&&id!=14&&id!=15&&id!=16&&id!=19;
    c.flags=(read?BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY:0)|(write?BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN:0);c.min_key_size=write?16:0;}
  services[0].type=BLE_GATT_SVC_TYPE_PRIMARY;services[0].uuid=&uuids[0].u;services[0].characteristics=characteristics;
  LOG_INF("Native BLE initialization; heap %u",ESP.getFreeHeap());
  // One manager connection; advertising stops while connected. Two controller
  // activities cover advertising and the single peripheral connection.
  esp_bt_controller_config_t controller=BT_CONTROLLER_INIT_CONFIG_DEFAULT();controller.ble_max_act=2;
  esp_err_t started=esp_bt_controller_init(&controller);
  if(started==ESP_OK)started=esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if(started==ESP_OK)started=esp_nimble_init();
  if(started!=ESP_OK){LOG_ERR("Native BLE initialization error %d",started);return false;}
  LOG_INF("Native BLE controller ready; heap %u",ESP.getFreeHeap());
  ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set(name);
  ble_hs_cfg.sync_cb=synced;ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_DISP_ONLY;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_mitm=1;
  ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_sc_only=1;ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist=ble_hs_cfg.sm_our_key_dist;ble_store_config_init();
  int counted=ble_gatts_count_cfg(services),added=counted?0:ble_gatts_add_svcs(services);
  if(counted||added){LOG_ERR("Native BLE GATT error count %d add %d",counted,added);return false;}
  result("READY");nimble_port_freertos_init(host);
  for(httpd_method_t method:{HTTP_GET,HTTP_POST}){httpd_uri_t route={};route.uri="/api/settings/ble";route.method=method;route.handler=settings;
    esp_err_t registered=httpd_register_uri_handler(server,&route);if(registered!=ESP_OK){LOG_ERR("Native BLE HTTP error %d",registered);return false;}}
  bool created=xTaskCreatePinnedToCore(owner,"bleCommands",6144,nullptr,1,nullptr,0)==pdPASS;
  if(!created)LOG_ERR("Native BLE worker allocation failed");return created;
#endif
}
#endif
