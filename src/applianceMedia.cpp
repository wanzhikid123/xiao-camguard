#if defined(XIAO_APPLIANCE)
#include "appGlobals.h"
#include "applianceWeb.h"
#include "applianceOta.h"
#include "applianceMedia.h"
#include "applianceSettings.h"
#include "applianceEvents.h"
#include "applianceTimelapse.h"
#include "applianceJobs.h"
#include <AdminAuth.h>
#include <HttpAuth.h>
#include "cJSON.h"
#include "esp_random.h"
#include <atomic>

namespace {
constexpr size_t PATH_BYTES = 128, PAGE_LIMIT = 20, SCAN_LIMIT = 4096;
SemaphoreHandle_t mediaMutex = nullptr;
SemaphoreHandle_t photoMutex = nullptr;
bool transferring = false;
std::atomic<bool> savingPhoto{false};
char transferPath[PATH_BYTES] = {};
struct Transfer { httpd_req_t* request; char path[PATH_BYTES], token[65],range[64]; bool inlinePhoto; };
struct Item { char name[104]; uint64_t bytes; };
class Lock {
public:
  Lock() { xSemaphoreTake(mediaMutex, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mediaMutex); }
};
esp_err_t json(httpd_req_t* req, const char* status, const char* text) {
  appliance::HttpAuth::responseHeaders(req);
  httpd_resp_set_status(req, status); httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, text);
}
bool recording() { return forceRecord || isCapturing || stopPlayback || applianceEventsArmed(); }
bool digits(const char* text, size_t length) {
  for (size_t i = 0; i < length; ++i) if (text[i] < '0' || text[i] > '9') return false;
  return true;
}
bool date(const char* text) {
  if (strlen(text) != 8 || !digits(text, 8)) return false;
  unsigned month = (text[4]-'0')*10 + text[5]-'0', day = (text[6]-'0')*10 + text[7]-'0';
  return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
bool safeLabel(const char* text,size_t bytes) {
  if(!bytes||bytes>80||text[0]==' '||text[bytes-1]==' ')return false;
  for(size_t i=0;i<bytes;) {
    const auto c=static_cast<uint8_t>(text[i++]);
    if(c<128){if(!isalnum(c)&&c!='_'&&c!='-'&&c!=' ')return false;continue;}
    unsigned extra;uint32_t point,minimum;
    if(c>=0xc2&&c<=0xdf){extra=1;point=c&31;minimum=0x80;}
    else if(c>=0xe0&&c<=0xef){extra=2;point=c&15;minimum=0x800;}
    else if(c>=0xf0&&c<=0xf4){extra=3;point=c&7;minimum=0x10000;}
    else return false;
    if(i+extra>bytes)return false;
    while(extra--){uint8_t next=text[i++];if((next&0xc0)!=0x80)return false;point=(point<<6)|(next&63);}
    if(point<minimum||point<0xa0||point>0x10ffff||(point>=0xd800&&point<=0xdfff)
      ||(point>=0x2000&&point<=0x206f)||point==0xfeff||(point&0xffff)>=0xfffe)return false;
  }
  return true;
}
bool mediaName(const char* name, const char* folder) {
  const size_t length = strlen(name);
  if (length < 20 || length >= sizeof(Item::name) || memcmp(name, folder, 8)
      || name[8] != '_' || !digits(name + 9, 6) || name[15] != '_') return false;
  const char* dot = strchr(name, '.');
  if (!dot || (strcmp(dot, ".avi") && strcmp(dot, ".jpg") && strcmp(dot, ".mp4"))) return false;
  return safeLabel(name+16,dot-(name+16));
}
bool mediaPath(const char* path) {
  const size_t length = strlen(path);
  if (length >= PATH_BYTES || length < 30 || path[0] != '/' || path[9] != '/') return false;
  char folder[9] = {}; memcpy(folder, path + 1, 8);
  return date(folder) && mediaName(path + 10, folder);
}
bool decode(const char* input, char* output, size_t bytes) {
  size_t count = 0;
  for (size_t i = 0; input[i]; ++i) {
    unsigned char c = input[i];
    if (c == '%') {
      if (!input[i+1] || !input[i+2]) return false;
      auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
      };
      int high = nibble(input[++i]), low = nibble(input[++i]);
      if (high < 0 || low < 0) return false;
      c = (high << 4) | low;
    }
    if (c < 32 || c == 127 || count + 1 >= bytes) return false;
    output[count++] = c;
  }
  output[count] = 0; return true;
}
bool query(httpd_req_t* req, char* value, size_t bytes) {
  char raw[384] = {};
  if (httpd_req_get_url_query_len(req) >= sizeof(raw)
      || httpd_req_get_url_query_str(req, raw, sizeof(raw)) != ESP_OK
      || strncmp(raw, "path=", 5) || strchr(raw, '&')) return false;
  return decode(raw + 5, value, bytes) && (mediaPath(value)||applianceTimelapseMediaPath(value));
}
bool emptyMutation(httpd_req_t* req) {
  if (!applianceCheckAuth(req, true)) return false;
  if (req->content_len || strchr(req->uri, '?')) {
    json(req, "400 Bad Request", "{\"error\":\"empty_body_required\"}"); return false;
  }
  return true;
}
esp_err_t photo(httpd_req_t* req) {
  if (!emptyMutation(req)) return ESP_OK;
  if (!appSetupDone || startupFailure[0] || SD.cardType() == CARD_NONE)
    return json(req, "503 Service Unavailable", "{\"error\":\"camera_or_sd_unavailable\"}");
  if (applianceCaptureBusy()) return json(req, "409 Conflict", "{\"error\":\"capture_busy\"}");
  if (!applianceStorageSpaceAvailable()) return json(req, "507 Insufficient Storage", "{\"error\":\"minimum_free_space\"}");
  char folder[FILE_NAME_LEN], stamp[FILE_NAME_LEN], path[PATH_BYTES];
  dateFormat(stamp, sizeof(stamp), false);
  memcpy(folder, stamp, 9); folder[9] = 0; // One timestamp across midnight.
  if (!SD.exists(folder) && !SD.mkdir(folder)) {
    alertBufferSize = 0; return json(req, "507 Insufficient Storage", "{\"error\":\"photo_directory_failed\"}");
  }
  bool available = false;
  for (unsigned attempt = 0; attempt < 8; ++attempt) {
    snprintf(path, sizeof(path), "%s_%06lx_photo.jpg", stamp, (unsigned long)(esp_random() & 0xffffff));
    if (!SD.exists(path)) { available = true; break; }
  }
  bool success=available&&applianceSaveConfiguredPhoto(path);
  File file=success?SD.open(path,FILE_READ):File();const size_t size=file?file.size():0;file.close();
  if (!success) {
    if (available && SD.exists(path)) SD.remove(path); // Only this request's new file.
    return json(req, "507 Insufficient Storage", "{\"error\":\"photo_write_failed\"}");
  }
  char response[256];
  snprintf(response, sizeof(response), "{\"saved\":true,\"path\":\"%s\",\"bytes\":%lu,\"timeSynchronized\":%s}",
    path, (unsigned long)size, timeSynchronized ? "true" : "false");
  return json(req, "201 Created", response);
}
esp_err_t list(httpd_req_t* req) {
  if (!applianceCheckAuth(req)) return ESP_OK;
  if (SD.cardType() == CARD_NONE) return json(req, "503 Service Unavailable", "{\"error\":\"sd_unavailable\"}");
  const bool dates = !strncmp(req->uri, "/api/media/dates", 16);
  char raw[256] = {}, folder[9] = {}, before[104] = {};
  const size_t queryBytes = httpd_req_get_url_query_len(req);
  bool valid = queryBytes < sizeof(raw);
  if (queryBytes) valid = valid && httpd_req_get_url_query_str(req, raw, sizeof(raw)) == ESP_OK;
  // Exact allowlist and order; no ambiguous duplicate/unknown query fields.
  if (dates) {
    if (queryBytes) valid = valid && !strncmp(raw, "before=", 7) && !strchr(raw, '&')
      && decode(raw + 7, before, sizeof(before)) && date(before);
  } else {
    valid = valid && !strncmp(raw, "date=", 5);
    char* next = strchr(raw, '&'); if (next) *next++ = 0;
    valid = valid && decode(raw + 5, folder, sizeof(folder)) && date(folder);
    if (next) valid = valid && !strncmp(next, "before=", 7) && !strchr(next, '&')
      && decode(next + 7, before, sizeof(before)) && mediaName(before, folder);
  }
  if (!valid) return json(req, "400 Bad Request", "{\"error\":\"invalid_media_query\"}");
  auto* items = static_cast<Item*>(calloc(PAGE_LIMIT + 1, sizeof(Item)));
  if (!items) return json(req, "503 Service Unavailable", "{\"error\":\"media_memory\"}");
  char directory[12]; snprintf(directory, sizeof(directory), dates ? "/" : "/%s", folder);
  File root = SD.open(directory);
  size_t count = 0, scanned = 0; bool truncated = false;
  const uint32_t started = millis();
  if (root && root.isDirectory()) {
    File entry = root.openNextFile();
    while (entry) {
      if (++scanned > SCAN_LIMIT || millis() - started > 2000) { truncated = true; entry.close(); break; }
      const char* name = entry.name();
      const bool acceptable = dates ? entry.isDirectory() && date(name) : !entry.isDirectory() && mediaName(name, folder);
      if (acceptable && (!before[0] || strcmp(name, before) < 0)) {
        size_t position = 0;
        while (position < count && strcmp(items[position].name, name) > 0) ++position;
        if (position <= PAGE_LIMIT) {
          if (count < PAGE_LIMIT + 1) ++count;
          for (size_t i = count - 1; i > position; --i) items[i] = items[i-1];
          strlcpy(items[position].name, name, sizeof(items[position].name)); items[position].bytes = entry.size();
        }
      }
      entry.close(); entry = root.openNextFile();
      if (!(scanned % 32)) taskYIELD();
    }
  }
  root.close();
  const bool more = count > PAGE_LIMIT; count = std::min(count, PAGE_LIMIT);
  cJSON* response = cJSON_CreateObject(); cJSON* array = cJSON_AddArrayToObject(response, "items");
  for (size_t i = 0; i < count && array; ++i) {
    cJSON* item = cJSON_CreateObject(); cJSON_AddItemToArray(array, item);
    cJSON_AddStringToObject(item, dates ? "date" : "name", items[i].name);
    if (!dates) {
      char path[PATH_BYTES]; snprintf(path, sizeof(path), "/%s/%s", folder, items[i].name);
      cJSON_AddStringToObject(item, "path", path); cJSON_AddNumberToObject(item, "bytes", double(items[i].bytes));
      cJSON_AddStringToObject(item, "kind", strstr(items[i].name, ".jpg") ? "photo" : "video");
    }
  }
  cJSON_AddBoolToObject(response, "more", more); cJSON_AddBoolToObject(response, "enumerationTruncated", truncated);
  cJSON_AddStringToObject(response, "nextBefore", more && count ? items[count-1].name : "");
  free(items);
  char* text = array ? cJSON_PrintUnformatted(response) : nullptr;
  esp_err_t sent = text ? json(req, "200 OK", text) : json(req, "503 Service Unavailable", "{\"error\":\"media_memory\"}");
  free(text); cJSON_Delete(response); return sent;
}
void finishTransfer(Transfer* transfer) {
  httpd_req_async_handler_complete(transfer->request);
  appliance::clearSecret(transfer, sizeof(*transfer)); free(transfer);
  { Lock lock; transferring = false; transferPath[0] = 0; }
  vTaskDelete(nullptr);
}
void sendFile(void* context) {
  auto* transfer = static_cast<Transfer*>(context); auto* req = transfer->request;
  File file = SD.open(transfer->path, FILE_READ);
  if (!file || file.isDirectory()) {
    json(req, "404 Not Found", "{\"error\":\"media_not_found\"}"); file.close(); finishTransfer(transfer); return;
  }
  // The async request inherits the authorization response headers. Adding
  // them again would exhaust the SDK's response-header slots.
  const bool photo = strstr(transfer->path, ".jpg");
  const bool mp4=strstr(transfer->path,".mp4");
  httpd_resp_set_type(req, photo ? "image/jpeg" : mp4?"video/mp4":"video/x-msvideo");
  char disposition[384],encoded[320]={};size_t at=0;
  const char* name=strrchr(transfer->path,'/')+1;
  for(const auto* p=reinterpret_cast<const uint8_t*>(name);*p&&at+3<sizeof(encoded);++p) {
    if(isalnum(*p)||*p=='_'||*p=='-'||*p=='.')encoded[at++]=*p;
    else {snprintf(encoded+at,sizeof(encoded)-at,"%%%02X",*p);at+=3;}
  }
  snprintf(disposition,sizeof(disposition),"%s; filename=\"capture.%s\"; filename*=UTF-8''%s",
    transfer->inlinePhoto&&(photo||mp4)?"inline":"attachment",photo?"jpg":mp4?"mp4":"avi",encoded);
  httpd_resp_set_hdr(req, "Content-Disposition", disposition);
  uint64_t first=0,last=file.size()?file.size()-1:0;bool partial=transfer->range[0],valid=file.size()>0&&file.size()<=INT32_MAX;
  if(partial){char* value=transfer->range;char* dash=strchr(value,'-');char* end=nullptr;
    valid=valid&&!strncmp(value,"bytes=",6)&&dash&&!strchr(value,',')&&!strchr(dash+1,'-');
    if(valid){*dash=0;if(value[6]){for(char* p=value+6;*p;++p)if(!isdigit((uint8_t)*p))valid=false;
        first=strtoull(value+6,&end,10);valid=valid&&end&&!*end;
        if(dash[1]){for(char* p=dash+1;*p;++p)if(!isdigit((uint8_t)*p))valid=false;last=strtoull(dash+1,&end,10);valid=valid&&end&&!*end;}}
      else{for(char* p=dash+1;*p;++p)if(!isdigit((uint8_t)*p))valid=false;uint64_t suffix=strtoull(dash+1,&end,10);
        valid=valid&&suffix&&end&&!*end;first=suffix>=file.size()?0:file.size()-suffix;}
      last=std::min<uint64_t>(last,file.size()-1);valid=valid&&first<file.size()&&first<=last;}}
  char contentRange[96];httpd_resp_set_hdr(req,"Accept-Ranges","bytes");
  if(!valid){snprintf(contentRange,sizeof(contentRange),"bytes */%llu",(unsigned long long)file.size());httpd_resp_set_hdr(req,"Content-Range",contentRange);
    httpd_resp_set_status(req,"416 Range Not Satisfiable");httpd_resp_send(req,nullptr,0);file.close();finishTransfer(transfer);return;}
  if(partial){snprintf(contentRange,sizeof(contentRange),"bytes %llu-%llu/%llu",(unsigned long long)first,(unsigned long long)last,(unsigned long long)file.size());
    httpd_resp_set_hdr(req,"Content-Range",contentRange);httpd_resp_set_status(req,"206 Partial Content");}
  uint32_t remaining=last-first+1;
  // SDK 5.5.5 sends a fixed-length header when body is null; stream the
  // declared bytes through its socket API. No conflicting chunked encoding.
  bool success=file.seek(first)&&httpd_resp_send(req,nullptr,remaining)==ESP_OK;
  uint8_t buffer[4096];const uint32_t start = millis();
  while (success&&remaining) {
    // WG transfers are slower than LAN. A real 47 MB AVI hit the former ten
    // minute cap; retain a finite thirty minute limit and session revocation.
    if (millis() - start > 1800000 || !applianceSessionValid(transfer->token)) { success = false; break; }
    const size_t count = file.read(buffer, std::min<uint32_t>(sizeof(buffer),remaining));
    if(!count){success=false;break;}size_t at=0;
    while(at<count){int n=httpd_socket_send(req->handle,httpd_req_to_sockfd(req),(char*)buffer+at,count-at,0);if(n<=0){success=false;break;}at+=n;}
    remaining-=count;
    taskYIELD();
  }
  file.close();
  if (!success) httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
  finishTransfer(transfer);
}
esp_err_t download(httpd_req_t* req) {
  if (!applianceCheckAuth(req)) return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{\"error\":\"converter_stopping\"}");
  if (applianceMaintenance()) return json(req, "409 Conflict", "{\"error\":\"maintenance_in_progress\"}");
  char path[PATH_BYTES] = {};
  if (!query(req, path, sizeof(path))) return json(req, "400 Bad Request", "{\"error\":\"invalid_media_path\"}");
  if (SD.cardType() == CARD_NONE) return json(req, "503 Service Unavailable", "{\"error\":\"sd_unavailable\"}");
  if (recording() && strstr(path, ".avi")) return json(req, "409 Conflict", "{\"error\":\"recording_in_progress\"}");
  auto* transfer = static_cast<Transfer*>(calloc(1, sizeof(Transfer)));
  if (!transfer) return json(req, "503 Service Unavailable", "{\"error\":\"media_memory\"}");
  {
    Lock lock;
    if (transferring) { free(transfer); return json(req, "409 Conflict", "{\"error\":\"media_transfer_busy\"}"); }
    transferring = true; strlcpy(transferPath, path, sizeof(transferPath));
  }
  strlcpy(transfer->path, path, sizeof(transfer->path));
  if(httpd_req_get_hdr_value_len(req,"Range")>=sizeof(transfer->range)){
    {Lock lock;transferring=false;transferPath[0]=0;}free(transfer);return json(req,"400 Bad Request","{}");}
  httpd_req_get_hdr_value_str(req,"Range",transfer->range,sizeof(transfer->range));
  transfer->inlinePhoto = !strncmp(req->uri, "/api/media/file?", 16);
  applianceStreamToken(req, transfer->token);
  if (httpd_req_async_handler_begin(req, &transfer->request) != ESP_OK) {
    { Lock lock; transferring = false; transferPath[0] = 0; }
    appliance::clearSecret(transfer, sizeof(*transfer)); free(transfer);
    return json(req, "503 Service Unavailable", "{\"error\":\"media_worker_unavailable\"}");
  }
  if (xTaskCreatePinnedToCore(sendFile, "mediaDownload", 8192, transfer, 3, nullptr, 0) != pdPASS) {
    json(transfer->request, "503 Service Unavailable", "{\"error\":\"media_worker_unavailable\"}");
    httpd_req_async_handler_complete(transfer->request);
    { Lock lock; transferring = false; transferPath[0] = 0; }
    appliance::clearSecret(transfer, sizeof(*transfer)); free(transfer);
  }
  return ESP_OK;
}
esp_err_t remove(httpd_req_t* req) {
  if (!applianceCheckAuth(req, true)) return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  char type[64] = {}, raw[385] = {}, path[PATH_BYTES] = {};
  const size_t typeLength = httpd_req_get_hdr_value_len(req, "Content-Type");
  if (strchr(req->uri, '?') || !req->content_len || req->content_len > 384 || typeLength >= sizeof(type)
    || httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK
    || strcmp(type, "application/json")) return json(req, "400 Bad Request", "{\"error\":\"invalid_media_request\"}");
  size_t received = 0;
  while (received < req->content_len) {
    const int count = httpd_req_recv(req, raw + received, req->content_len - received);
    if (count <= 0) return json(req, "400 Bad Request", "{\"error\":\"invalid_media_request\"}");
    received += count;
  }
  // This single-field schema accepts plain ASCII paths. Reject escape/nesting
  // before parsing, including NUL truncation and ambiguous path spellings.
  bool valid = !memchr(raw, 0, received) && !strchr(raw, '\\') && !strchr(raw, '[');
  unsigned braces = 0; for (size_t i = 0; i < received; ++i) if (raw[i] == '{') ++braces;
  valid = valid && braces == 1;
  cJSON* root = valid ? cJSON_ParseWithLengthOpts(raw, received+1, nullptr, true) : nullptr;
  cJSON* value = root ? cJSON_GetObjectItemCaseSensitive(root, "path") : nullptr;
  valid = cJSON_IsObject(root) && cJSON_GetArraySize(root) == 1 && cJSON_IsString(value)
    && value->valuestring && strlen(value->valuestring) < sizeof(path) && mediaPath(value->valuestring);
  if (valid) strlcpy(path, value->valuestring, sizeof(path));
  cJSON_Delete(root);
  if (!valid) return json(req, "400 Bad Request", "{\"error\":\"invalid_media_path\"}");
  if (SD.cardType() == CARD_NONE) return json(req, "503 Service Unavailable", "{\"error\":\"sd_unavailable\"}");
  if (recording()) return json(req, "409 Conflict", "{\"error\":\"recording_in_progress\"}");
  Lock lock;
  if (transferring && !strcmp(path, transferPath)) return json(req, "409 Conflict", "{\"error\":\"media_transfer_busy\"}");
  File file = SD.open(path, FILE_READ); const bool exists = file && !file.isDirectory(); file.close();
  if (!exists) return json(req, "404 Not Found", "{\"error\":\"media_not_found\"}");
  return SD.remove(path) ? json(req, "200 OK", "{\"deleted\":true}")
    : json(req, "500 Internal Server Error", "{\"error\":\"media_delete_failed\"}");
}
esp_err_t renameMedia(httpd_req_t* req) {
  if(!applianceCheckAuth(req,true))return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  char type[48]={},raw[513]={},path[PATH_BYTES]={},next[PATH_BYTES]={};
  if(strchr(req->uri,'?')||!req->content_len||req->content_len>512||httpd_req_get_hdr_value_len(req,"Content-Type")>=sizeof(type)
    ||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
    return json(req,"400 Bad Request","{\"error\":\"invalid_media_request\"}");
  size_t received=0;const uint32_t started=millis();
  while(received<req->content_len) {
    if(millis()-started>5000)return json(req,"408 Request Timeout","{\"error\":\"request_timeout\"}");
    int count=httpd_req_recv(req,raw+received,req->content_len-received);
    if(count<=0)return json(req,"400 Bad Request","{\"error\":\"invalid_media_request\"}");received+=count;
  }
  unsigned braces=0;for(size_t i=0;i<received;++i)if(raw[i]=='{')++braces;
  cJSON* root=!memchr(raw,0,received)&&!strchr(raw,'\\')&&!strchr(raw,'[')&&braces==1
    ?cJSON_ParseWithLengthOpts(raw,received+1,nullptr,true):nullptr;
  cJSON* source=root?cJSON_GetObjectItemCaseSensitive(root,"path"):nullptr;
  cJSON* label=root?cJSON_GetObjectItemCaseSensitive(root,"name"):nullptr;
  bool valid=cJSON_IsObject(root)&&cJSON_GetArraySize(root)==2&&cJSON_IsString(source)&&cJSON_IsString(label)
    &&mediaPath(source->valuestring)&&strlen(label->valuestring)<=64&&safeLabel(label->valuestring,strlen(label->valuestring));
  if(valid) {
    strlcpy(path,source->valuestring,sizeof(path));memcpy(next,path,26);
    snprintf(next+26,sizeof(next)-26,"%s%s",label->valuestring,strrchr(path,'.'));
    valid=mediaPath(next);
  }
  cJSON_Delete(root);
  if(!valid)return json(req,"400 Bad Request","{\"error\":\"invalid_media_name\"}");
  if(SD.cardType()==CARD_NONE)return json(req,"503 Service Unavailable","{\"error\":\"sd_unavailable\"}");
  if(recording())return json(req,"409 Conflict","{\"error\":\"recording_in_progress\"}");
  Lock lock;
  if(transferring&&(!strcmp(path,transferPath)||!strcmp(next,transferPath)))return json(req,"409 Conflict","{\"error\":\"media_transfer_busy\"}");
  File file=SD.open(path,FILE_READ);const bool exists=file&&!file.isDirectory();file.close();
  if(!exists)return json(req,"404 Not Found","{\"error\":\"media_not_found\"}");
  if(strcmp(path,next)&&SD.exists(next))return json(req,"409 Conflict","{\"error\":\"media_name_exists\"}");
  if(strcmp(path,next)&&!SD.rename(path,next))return json(req,"500 Internal Server Error","{\"error\":\"media_rename_failed\"}");
  // The allowlist excludes JSON quoting/control characters.
  char response[192];snprintf(response,sizeof(response),"{\"renamed\":true,\"path\":\"%s\"}",next);
  return json(req,"200 OK",response);
}
esp_err_t removeBatch(httpd_req_t* req) {
  if(!applianceCheckAuth(req,true))return ESP_OK;
  ApplianceForeground foreground;if(!foreground.ready())return json(req,"503 Service Unavailable","{}");
  char type[32]={};
  if(strchr(req->uri,'?')||!req->content_len||req->content_len>3200
    ||httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
    return json(req,"400 Bad Request","{\"error\":\"invalid_media_request\"}");
  char* raw=static_cast<char*>(calloc(req->content_len+1,1));
  if(!raw)return json(req,"503 Service Unavailable","{\"error\":\"media_memory\"}");
  size_t received=0;const uint32_t started=millis();
  while(received<req->content_len&&millis()-started<5000) {
    int count=httpd_req_recv(req,raw+received,req->content_len-received);if(count<=0)break;received+=count;
  }
  unsigned braces=0,arrays=0;
  for(size_t i=0;i<received;++i){if(raw[i]=='{')++braces;if(raw[i]=='[')++arrays;}
  cJSON* root=received==req->content_len&&!memchr(raw,0,received)&&!strchr(raw,'\\')&&braces==1&&arrays==1
    ?cJSON_ParseWithLengthOpts(raw,received+1,nullptr,true):nullptr;
  free(raw);
  cJSON* paths=root?cJSON_GetObjectItemCaseSensitive(root,"paths"):nullptr;
  const int count=cJSON_GetArraySize(paths);
  bool valid=cJSON_IsObject(root)&&cJSON_GetArraySize(root)==1&&cJSON_IsArray(paths)&&count>=1&&count<=20;
  for(int i=0;valid&&i<count;++i) {
    const cJSON* value=cJSON_GetArrayItem(paths,i);
    valid=cJSON_IsString(value)&&value->valuestring&&mediaPath(value->valuestring);
    for(int j=0;valid&&j<i;++j)valid=strcmp(value->valuestring,cJSON_GetArrayItem(paths,j)->valuestring)!=0;
  }
  const char* failure=nullptr;const char* status="200 OK";unsigned removed=0;
  if(!valid){failure="invalid_media_path";status="400 Bad Request";}
  else if(SD.cardType()==CARD_NONE){failure="sd_unavailable";status="503 Service Unavailable";}
  else if(recording()){failure="recording_in_progress";status="409 Conflict";}
  else {
    Lock lock;
    if(transferring){failure="media_transfer_busy";status="409 Conflict";}
    // Validate every member's existence before the first irreversible operation.
    for(int i=0;!failure&&i<count;++i) {
      File file=SD.open(cJSON_GetArrayItem(paths,i)->valuestring,FILE_READ);
      const bool exists=file&&!file.isDirectory();file.close();
      if(!exists){failure="media_not_found";status="404 Not Found";}
    }
    for(int i=0;!failure&&i<count;++i) {
      if(!SD.remove(cJSON_GetArrayItem(paths,i)->valuestring)){failure="media_delete_failed";status="500 Internal Server Error";}
      else ++removed;
    }
  }
  cJSON_Delete(root);char response[160];
  snprintf(response,sizeof(response),"{\"deleted\":%u,\"error\":\"%s\"}",removed,failure?failure:"none");
  return json(req,status,response);
}
} // namespace
bool applianceMediaBusy() { if (!mediaMutex) return false; Lock lock; return transferring; }
bool appliancePhotoBusy(){return savingPhoto.load();}
bool applianceMediaPath(const char* path){return mediaPath(path);}
bool applianceSaveManualPhoto(char* output,size_t bytes){
  if(!appSetupDone||startupFailure[0]||applianceCaptureBusy()||!applianceStorageSpaceAvailable()||applianceMaintenance())return false;
  char folder[FILE_NAME_LEN],stamp[FILE_NAME_LEN],path[128];dateFormat(stamp,sizeof(stamp),false);
  memcpy(folder,stamp,9);folder[9]=0;if(!SD.exists(folder)&&!SD.mkdir(folder))return false;
  for(unsigned i=0;i<8;++i){snprintf(path,sizeof(path),"%s_%06lx_photo.jpg",stamp,(unsigned long)(esp_random()&0xffffff));
    if(!SD.exists(path)){if(!applianceSaveConfiguredPhoto(path))return false;strlcpy(output,path,bytes);return true;}}
  return false;
}
bool applianceSaveConfiguredPhoto(const char* path,bool* occupied){
  if(occupied)*occupied=false;
  if(!photoMutex||!(mediaPath(path)||applianceTimelapseMediaPath(path))||SD.exists(path))return false;
  ApplianceForeground foreground;if(!foreground.ready()||xSemaphoreTake(photoMutex,pdMS_TO_TICKS(25000))!=pdTRUE){if(occupied)*occupied=true;return false;}
  if(occupied&&(forceRecord||isCapturing||stopPlayback||applianceStreamsBusy()||applianceMediaBusy()||applianceNetworkBusy())){
    *occupied=true;xSemaphoreGive(photoMutex);return false;}
  savingPhoto.store(true);
  bool success=applianceConfiguredPhoto(occupied);const size_t size=alertBufferSize;File f;
  success=success&&size>=4&&alertBuffer[0]==255&&alertBuffer[1]==216&&alertBuffer[size-2]==255&&alertBuffer[size-1]==217;
  if(success){f=SD.open(path,FILE_WRITE);success=bool(f);}
  uint8_t bytes[1024];for(size_t at=0;success&&at<size;){size_t n=std::min(sizeof(bytes),size-at);memcpy(bytes,alertBuffer+at,n);
    success=f.write(bytes,n)==n;at+=n;}
  if(f){f.flush();success=success&&f.size()==size;f.close();}alertBufferSize=0;
  if(!success)SD.remove(path);savingPhoto.store(false);xSemaphoreGive(photoMutex);return success;
}
bool applianceRegisterMediaRoutes(httpd_handle_t server) {
  if (!mediaMutex) mediaMutex = xSemaphoreCreateMutex(); if (!mediaMutex) return false;
  if(!photoMutex)photoMutex=xSemaphoreCreateMutex();if(!photoMutex)return false;
  const httpd_uri_t routes[] = {
    {.uri="/api/camera/photo", .method=HTTP_POST, .handler=photo, .user_ctx=nullptr},
    {.uri="/api/media/dates", .method=HTTP_GET, .handler=list, .user_ctx=nullptr},
    {.uri="/api/media", .method=HTTP_GET, .handler=list, .user_ctx=nullptr},
    {.uri="/api/media/file", .method=HTTP_GET, .handler=download, .user_ctx=nullptr},
    {.uri="/api/media/download", .method=HTTP_GET, .handler=download, .user_ctx=nullptr},
    {.uri="/api/media/delete", .method=HTTP_POST, .handler=remove, .user_ctx=nullptr},
    {.uri="/api/media/rename", .method=HTTP_POST, .handler=renameMedia, .user_ctx=nullptr},
    {.uri="/api/media/delete-batch", .method=HTTP_POST, .handler=removeBatch, .user_ctx=nullptr},
  };
  for (const auto& route : routes) if (httpd_register_uri_handler(server, &route) != ESP_OK) return false;
  return true;
}
#endif
