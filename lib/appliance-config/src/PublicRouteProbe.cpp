#if defined(XIAO_APPLIANCE)
#include "PublicRouteProbe.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <IPAddress.h>
#include "lwip/netdb.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace { std::atomic<TaskHandle_t> routeTLSOwner{nullptr}; }
extern "C" void* __real_esp_mbedtls_mem_calloc(size_t count,size_t bytes);
extern "C" void* __wrap_esp_mbedtls_mem_calloc(size_t count,size_t bytes) {
  const TaskHandle_t owner=routeTLSOwner.load();
  // The pinned SDK uses internal-only Mbed TLS allocations. This one TLS
  // worker has no flash writes and can use PSRAM; all other callers retain
  // the SDK allocator. Its ordinary heap-capability free accepts both heaps.
  if(owner&&xTaskGetCurrentTaskHandle()==owner)return heap_caps_calloc(count,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  return __real_esp_mbedtls_mem_calloc(count,bytes);
}
namespace appliance {
namespace {
// Arduino's generic resolver prefers a global STA IPv6 address after router
// autoconfiguration. This product verifies IPv4 only. Resolve an A record but
// preserve the TLS hostname/SNI and certificate bundle; the socket remains
// unbound so the ordinary destination route is still what this job verifies.
class IPv4TLSClient final : public NetworkClientSecure {
public:
  int connect(const char* host,uint16_t port) override {
    addrinfo hints={},*answers=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    if(lwip_getaddrinfo(host,nullptr,&hints,&answers)!=0 || !answers)return 0;
    const IPAddress ip(uint32_t(reinterpret_cast<sockaddr_in*>(answers->ai_addr)->sin_addr.s_addr));
    lwip_freeaddrinfo(answers);
    return NetworkClientSecure::connect(ip,port,host,_CA_cert,_cert,_private_key);
  }
  int connect(const char* host,uint16_t port,int32_t timeout) override {
    _timeout=timeout;return connect(host,port);
  }
};
}
bool PublicRouteProbe::start(uint32_t value) {
  if(running.exchange(true))return false;
  generation.store(value);address.store(0);http.store(0);done.store(false);
  if(xTaskCreatePinnedToCore(worker,"wg_route_check",8192,this,1,nullptr,1)==pdPASS)return true;
  running.store(false);return false;
}
PublicRouteResult PublicRouteProbe::result() const {
  if(!done.load())return {};
  return {true,generation.load(),address.load(),http.load()};
}
void PublicRouteProbe::worker(void* context) {
  auto& probe=*static_cast<PublicRouteProbe*>(context);
  routeTLSOwner.store(xTaskGetCurrentTaskHandle());
  int code=0;uint32_t address=0;
  {
  IPv4TLSClient socket;HTTPClient client;
  socket.useBuiltinCACertBundle();socket.setHandshakeTimeout(6);
  client.setReuse(false);client.setConnectTimeout(4000);client.setTimeout(5000);
  if(client.begin(socket,"https://api.ipify.org/")) {
    code=client.GET();
    if(code==200 && client.getSize()>0 && client.getSize()<=16) {
      IPAddress ip;
      if(ip.fromString(client.getString()) && ip.type()==IPType::IPv4) {
        const uint32_t host=lwip_ntohl(uint32_t(ip));
        if(host && host!=UINT32_MAX && (host>>24)!=127 && (host>>28)<14)address=uint32_t(ip);
      }
    }
    client.end();
  }
  } // Run TLS/client/String destructors before FreeRTOS deletes this stack.
  routeTLSOwner.store(nullptr);
  probe.address.store(address);probe.http.store(code);probe.done.store(true);probe.running.store(false);
  vTaskDelete(nullptr);
}
} // namespace appliance
#endif
