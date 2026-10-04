#if defined(XIAO_APPLIANCE)
#include "FullTunnelPolicy.h"
#include <IPAddress.h>
#include <atomic>
#include <ctype.h>
#include <string.h>
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/dns.h"
#include "lwip/ip6_addr.h"
#include "lwip/pbuf.h"
#include "lwip/prot/ip4.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"

extern "C" err_t __real_etharp_output(netif*, pbuf*, const ip4_addr_t*);
extern "C" err_t __real_ethip6_output(netif*, pbuf*, const ip6_addr_t*);
extern "C" err_t __wrap_etharp_output(netif*, pbuf*, const ip4_addr_t*);
extern "C" err_t __wrap_ethip6_output(netif*, pbuf*, const ip6_addr_t*);
namespace {
class CoreLock {
public: CoreLock() { LOCK_TCPIP_CORE(); } ~CoreLock() { UNLOCK_TCPIP_CORE(); }
};
std::atomic<bool> requested {false};
struct Policy {
  bool configured = false, ready = false, haveUnderlayDNS = false;
  netif* tunnel = nullptr;
  netif* underlay = nullptr;
  uint32_t endpoint = 0, resolver = 0, dnsExpires = 0, timeExpires = 0;
  uint32_t budgetEpoch = 0;
  uint16_t port = 0, dnsBudget = 16, timeBudget = 8;
  char endpointName[254] = {};
  ip_addr_t underlayDNS[DNS_MAX_SERVERS];
  appliance::FullTunnelPolicyStatus observed;
} policy;
constexpr uint32_t TIME_SERVER = PP_HTONL(0xa29fc801U); // Cloudflare 162.159.200.1
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
bool fixtureSpy=false;
uint32_t fixtureRealCalls=0;
appliance::FullPolicyFixtureStatus fixtureResults;
#endif
bool lease(uint32_t expires) { return expires && int32_t(sys_now()-expires) < 0; }
bool local(uint32_t address, const netif* interface) {
  return interface && netif_is_up(interface) && netif_is_link_up(interface)
    && netif_ip4_addr(interface)->addr
    && (address & netif_ip4_netmask(interface)->addr)
      == (netif_ip4_addr(interface)->addr & netif_ip4_netmask(interface)->addr);
}
bool unicast(uint32_t address) {
  uint32_t host = lwip_ntohl(address);
  return host && host != UINT32_MAX && (host >> 24) != 127 && (host >> 28) < 14;
}
uint16_t big16(const uint8_t* bytes) { return uint16_t(bytes[0]) << 8 | bytes[1]; }
void selectDNS() {
  ip_addr_t selected = IPADDR4_INIT(policy.resolver), none = IPADDR4_INIT(0);
  if (lease(policy.dnsExpires) && policy.haveUnderlayDNS) selected = policy.underlayDNS[0];
  if (!ip_addr_cmp(dns_getserver(0), &selected)) dns_setserver(0, &selected);
  for (size_t i = 1; i < DNS_MAX_SERVERS; ++i)
    if (!ip_addr_cmp(dns_getserver(i), &none)) dns_setserver(i, &none);
}
bool validInterfaces() {
  uint32_t count = 0;
  for (netif* interface = netif_list; interface; interface = interface->next) {
    if (interface == policy.tunnel) continue;
    // The SDK's lo/127.0.0.1 interface never reaches a physical device. It has
    // its own loopback callbacks and must not be counted as an unguarded NIC.
    if (interface->name[0]=='l' && interface->name[1]=='o'
      && (lwip_ntohl(netif_ip4_addr(interface)->addr)>>24)==127
      && !(interface->flags & (NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET))) continue;
    if (interface->output != __wrap_etharp_output || interface->output_ip6 != __wrap_ethip6_output) return false;
    ++count;
  }
  policy.observed.guardedInterfaces = count;
  return policy.underlay && count != 0;
}
// A bounded lease permits only a standard A query for the configured endpoint,
// not other DNS requests to the same resolver. The first question is uncompressed
// in lwIP; recursively resolved CNAME answers require no additional exception.
bool endpointQuestion(pbuf* packet, uint16_t offset) {
  uint8_t bytes[284];
  const uint16_t available = packet->tot_len-offset;
  if (available < 17 || available > sizeof(bytes)
    || pbuf_copy_partial(packet,bytes,available,offset)!=available) return false;
  if ((big16(bytes+2)&0xf800) || big16(bytes+4)!=1 || big16(bytes+6) || big16(bytes+8)) return false;
  size_t at=12, name=0;
  while(at<available && bytes[at]) {
    const uint8_t length=bytes[at++];
    if(!length || length>63 || at+length>=available) return false;
    if(name) { if(policy.endpointName[name++]!='.') return false; }
    for(size_t i=0;i<length;++i)
      if(!policy.endpointName[name] || tolower(bytes[at++])!=tolower(uint8_t(policy.endpointName[name++]))) return false;
  }
  return at+5==available && !policy.endpointName[name]
    && big16(bytes+at+1)==1 && big16(bytes+at+3)==1;
}
bool allow4(netif* interface,pbuf* packet,const ip4_addr_t* destination) {
  if(!destination || !packet || packet->tot_len<20) return false;
  uint8_t header[20];
  if(pbuf_copy_partial(packet,header,sizeof(header),0)!=sizeof(header) || header[0]>>4!=4) return false;
  const uint16_t offset=(header[0]&15)*4;
  uint32_t target;memcpy(&target,header+16,4);
  if(offset<20 || offset>60 || packet->tot_len<offset || big16(header+2)!=packet->tot_len
    || target!=destination->addr || (big16(header+6)&(IP_MF|IP_OFFMASK))) return false;
  uint8_t protocol=header[9];uint16_t sourcePort=0,port=0;
  if(protocol==6 || protocol==17) {
    uint8_t transport[8];const size_t size=protocol==17?8:4;
    if(pbuf_copy_partial(packet,transport,size,offset)!=size) return false;
    sourcePort=big16(transport);port=big16(transport+2);
    if(protocol==17 && big16(transport+4)!=packet->tot_len-offset) return false;
  }
  if(port==53) {
    const ip_addr_t* dns=policy.haveUnderlayDNS?&policy.underlayDNS[0]:nullptr;
    if(policy.ready && protocol==17 && lease(policy.dnsExpires) && policy.dnsBudget && dns && IP_IS_V4(dns)
      && target==ip_2_ip4(dns)->addr && interface==policy.underlay && endpointQuestion(packet,offset+8)) {
      --policy.dnsBudget;++policy.observed.bootstrapDNSPackets;return true;
    }
    return false;
  }
  if(policy.ready && target==policy.endpoint && protocol==17 && port==policy.port && interface==policy.underlay) {
    ++policy.observed.endpointPackets;return true;
  }
  if(target==TIME_SERVER && protocol==17 && port==123 && interface==policy.underlay
    && lease(policy.timeExpires) && policy.timeBudget && packet->tot_len-offset==56) {
    uint8_t first=0;
    if(pbuf_copy_partial(packet,&first,1,offset+8)==1 && (first&7)==3) {
      --policy.timeBudget;++policy.observed.bootstrapTimePackets;return true;
    }
  }
  uint32_t host=lwip_ntohl(target);
  if(host==UINT32_MAX) return protocol==17 && sourcePort==68 && port==67;
  if((host&0xffffff00U)==0xe0000000U || local(target,interface)) {
    ++policy.observed.localPackets;return true;
  }
  return false;
}
bool allow6(pbuf* packet,const ip6_addr_t* destination) {
  uint8_t header[44];
  const bool valid=packet && destination && packet->tot_len>=sizeof(header)
    && pbuf_copy_partial(packet,header,sizeof(header),0)==sizeof(header)
    && header[0]>>4==6 && big16(header+4)+40==packet->tot_len
    && !memcmp(header+24,destination->addr,16);
  return valid && (ip6_addr_islinklocal(destination)
    || (ip6_addr_ismulticast(destination) && ip6_addr_multicast_scope(destination)==2))
    && (header[6]==6 || header[6]==17 || header[6]==58)
    && !((header[6]==6 || header[6]==17) && big16(header+42)==53);
}
} // namespace
extern "C" err_t __wrap_etharp_output(netif* interface,pbuf* packet,const ip4_addr_t* destination) {
  if(requested.load() && !allow4(interface,packet,destination)) { ++policy.observed.ipv4Blocked;return ERR_RTE; }
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  if(fixtureSpy) { ++fixtureRealCalls;return ERR_OK; }
#endif
  return __real_etharp_output(interface,packet,destination);
}
extern "C" err_t __wrap_ethip6_output(netif* interface,pbuf* packet,const ip6_addr_t* destination) {
  if(requested.load() && !allow6(packet,destination)) { ++policy.observed.ipv6Blocked;return ERR_RTE; }
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
  if(fixtureSpy) { ++fixtureRealCalls;return ERR_OK; }
#endif
  return __real_ethip6_output(interface,packet,destination);
}
namespace appliance {
void requestFullTunnelPolicy() { requested.store(true); }
bool fullTunnelRequested() { return requested.load(); }
bool configureFullTunnelPolicy(const char* endpoint,uint16_t port,uint32_t resolver) {
  if(!endpoint || strlen(endpoint)>=sizeof(policy.endpointName) || !port || port==53 || !unicast(resolver)) return false;
  CoreLock lock;
  if(!policy.configured) {
    policy={};policy.configured=true;policy.budgetEpoch=sys_now();
    for(size_t i=0;i<DNS_MAX_SERVERS;++i) policy.underlayDNS[i]=*dns_getserver(i);
    policy.haveUnderlayDNS=IP_IS_V4(&policy.underlayDNS[0]) && unicast(ip_2_ip4(&policy.underlayDNS[0])->addr);
  }
  policy.port=port;policy.resolver=resolver;strlcpy(policy.endpointName,endpoint,sizeof(policy.endpointName));
  policy.dnsExpires=policy.timeExpires=0;policy.endpoint=0;policy.tunnel=nullptr;
  esp_netif_t* sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  policy.underlay=sta?static_cast<netif*>(esp_netif_get_netif_impl(sta)):nullptr;
  policy.ready=validInterfaces();selectDNS();return policy.ready;
}
void endFullTunnelPolicy() {
  if(!requested.load()) return;
  CoreLock lock;
  if(policy.configured)
    for(size_t i=0;i<DNS_MAX_SERVERS;++i) dns_setserver(i,&policy.underlayDNS[i]);
  policy={};requested.store(false);
}
bool updateFullTunnelUnderlay() {
  if(!requested.load()) return true;
  CoreLock lock;
  esp_netif_t* sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  policy.underlay=sta?static_cast<netif*>(esp_netif_get_netif_impl(sta)):nullptr;
  if(!policy.underlay || local(policy.resolver,policy.underlay)) { policy.ready=false;return false; }
  // DHCP writes all global slots. Capture the new underlay before restoring the
  // selected resolver; wrappers already deny unrestricted router DNS meanwhile.
  const ip_addr_t* current=dns_getserver(0);
  if(IP_IS_V4(current) && unicast(ip_2_ip4(current)->addr)
    && (ip_2_ip4(current)->addr!=policy.resolver || !policy.haveUnderlayDNS)) {
    for(size_t i=0;i<DNS_MAX_SERVERS;++i)policy.underlayDNS[i]=*dns_getserver(i);
    policy.haveUnderlayDNS=true;
  }
  policy.ready=validInterfaces();selectDNS();return policy.ready;
}
void maintainFullTunnelPolicy(uint32_t now) {
  if(!requested.load()) return;
  CoreLock lock;
  if(uint32_t(now-policy.budgetEpoch)>=300000) {
    policy.budgetEpoch=now;policy.dnsBudget=16;policy.timeBudget=8;
  }
  const bool finishingDNSLease=policy.dnsExpires && !lease(policy.dnsExpires);
  if(!lease(policy.dnsExpires)) policy.dnsExpires=0;
  if(!lease(policy.timeExpires)) policy.timeExpires=0;
  // Capture a same-address DHCP renewal before removing its resolver updates.
  const ip_addr_t* current=dns_getserver(0);
  if(!finishingDNSLease && !lease(policy.dnsExpires) && IP_IS_V4(current) && unicast(ip_2_ip4(current)->addr)
    && ip_2_ip4(current)->addr!=policy.resolver) {
    for(size_t i=0;i<DNS_MAX_SERVERS;++i)policy.underlayDNS[i]=*dns_getserver(i);
    policy.haveUnderlayDNS=true;
  }
  selectDNS();policy.ready=validInterfaces() && !local(policy.resolver,policy.underlay);
}
bool beginEndpointDNSLease(uint32_t now) {
  CoreLock lock;
  if(!policy.ready || !policy.haveUnderlayDNS || !policy.dnsBudget) return false;
  policy.dnsExpires=now+15000;selectDNS();return true;
}
void finishEndpointDNSLease() { CoreLock lock;policy.dnsExpires=0;if(requested.load())selectDNS(); }
bool beginTimeLease(uint32_t now) {
  CoreLock lock;if(!policy.ready || !policy.timeBudget)return false;
  policy.timeExpires=now+45000;return true;
}
void finishTimeLease() { CoreLock lock;policy.timeExpires=0; }
void setFullTunnelEndpoint(uint32_t endpoint) { CoreLock lock;policy.endpoint=endpoint; }
void attachFullTunnelRoute(void* interface,uint32_t endpoint) { policy.tunnel=static_cast<netif*>(interface);policy.endpoint=endpoint; }
void detachFullTunnelRoute() { policy.tunnel=nullptr; }
bool fullTunnelRoute(const ip4_addr_t* source,const ip4_addr_t* destination,netif*& route) {
  if(!requested.load() || !destination) return false;
  uint32_t target=destination->addr,host=lwip_ntohl(target);
  if(target==policy.endpoint || (target==TIME_SERVER && lease(policy.timeExpires))) { route=policy.underlay;return true; }
  if(lease(policy.dnsExpires) && policy.haveUnderlayDNS && IP_IS_V4(&policy.underlayDNS[0])
    && target==ip_2_ip4(&policy.underlayDNS[0])->addr) { route=policy.underlay;return true; }
  if((host>>24)==127 || (host>>28)==14 || host==UINT32_MAX) return false;
  for(netif* interface=netif_list;interface;interface=interface->next)
    if(interface!=policy.tunnel && local(target,interface)) { route=interface;return true; }
  if(source && source->addr && (!policy.tunnel || source->addr!=netif_ip4_addr(policy.tunnel)->addr)) {
    route=policy.underlay;return true;
  }
  route=policy.tunnel;if(route)++policy.observed.routedPackets;return true;
}
FullTunnelPolicyStatus fullTunnelPolicyStatus() {
  CoreLock lock;auto result=policy.observed;result.requested=requested.load();result.ready=policy.ready;
  result.tunnelPresent=bool(policy.tunnel);result.endpointDNSLease=lease(policy.dnsExpires);result.timeLease=lease(policy.timeExpires);
  static_assert(DNS_MAX_SERVERS==3,"Pinned policy DNS slot count changed");
  for(size_t i=0;i<DNS_MAX_SERVERS;++i) { const ip_addr_t* value=dns_getserver(i);result.dnsServers[i]=IP_IS_V4(value)?ip_2_ip4(value)->addr:0; }
  return result;
}
#if defined(XIAO_APPLIANCE_FULL_POLICY_TEST)
bool exerciseFullTunnelPolicy() {
  CoreLock lock;
  if(!requested.load() || !policy.ready || !policy.tunnel || !policy.underlay) return false;
  const Policy saved=policy;fixtureResults={};fixtureSpy=true;
  auto check=[&](bool okay) { ++fixtureResults.checks;if(!okay)++fixtureResults.failures; };
  auto packet4=[&](uint32_t target,uint8_t protocol,uint16_t port,bool expected,
      const uint8_t* payload=nullptr,uint16_t length=0,uint16_t fragment=0,bool chain=false,uint16_t source=53000) {
    uint8_t bytes[330]={0x45};const uint16_t total=28+length;
    bytes[2]=total>>8;bytes[3]=total;bytes[6]=fragment>>8;bytes[7]=fragment;bytes[8]=64;bytes[9]=protocol;
    memcpy(bytes+12,&netif_ip4_addr(policy.underlay)->addr,4);memcpy(bytes+16,&target,4);
    bytes[20]=source>>8;bytes[21]=source;bytes[22]=port>>8;bytes[23]=port;
    bytes[24]=(8+length)>>8;bytes[25]=8+length;if(length)memcpy(bytes+28,payload,length);
    pbuf* packet=pbuf_alloc(PBUF_RAW,chain?21:total,PBUF_RAM);
    if(packet && chain) { pbuf* tail=pbuf_alloc(PBUF_RAW,total-21,PBUF_RAM);
      if(tail)pbuf_cat(packet,tail);else { pbuf_free(packet);packet=nullptr; } }
    if(!packet) { check(false);return; }
    pbuf_take(packet,bytes,total);const ip4_addr_t destination={target};const uint32_t before=fixtureRealCalls;
    const err_t result=policy.underlay->output(policy.underlay,packet,&destination);
    check(expected ? result==ERR_OK && fixtureRealCalls==before+1 : result==ERR_RTE && fixtureRealCalls==before);
    pbuf_free(packet);
  };
  const uint32_t lan=netif_ip4_gw(policy.underlay)->addr,wan=uint32_t(IPAddress(8,8,8,8));
  packet4(lan,6,80,true);packet4(lan,17,53,false);packet4(lan,6,53,false);
  packet4(wan,6,443,false);packet4(wan,17,443,false);packet4(wan,17,53,false);
  packet4(policy.endpoint,17,policy.port,true);packet4(policy.endpoint,6,policy.port,false);
  packet4(policy.endpoint,17,policy.port==65535?1:policy.port+1,false);
  packet4(policy.endpoint,17,policy.port,false,nullptr,0,IP_MF);
  packet4(policy.endpoint,17,policy.port,false,nullptr,0,1);
  packet4(policy.endpoint,17,policy.port,true,nullptr,0,0,true);
  packet4(UINT32_MAX,17,67,true,nullptr,0,0,false,68);packet4(UINT32_MAX,17,443,false);
  // Requested protection must also deny WAN before policy readiness.
  policy.ready=false;packet4(wan,6,443,false);packet4(lan,6,80,true);
  packet4(policy.endpoint,17,policy.port,false);policy.ready=true;
  for(const char* text:{"2606:4700:4700::1111","fd00::1","fe80::1","ff02::1","ff05::1"}) {
    ip6_addr_t destination;bool parsed=ip6addr_aton(text,&destination);
    pbuf* packet=pbuf_alloc(PBUF_RAW,48,PBUF_RAM);
    if(!parsed || !packet) { if(packet)pbuf_free(packet);check(false);continue; }
    for(uint8_t protocol:{uint8_t(58),uint8_t(17),uint8_t(0)}) {
      uint8_t bytes[48]={0x60,0,0,0,0,8,0,64};bytes[6]=protocol;bytes[40]=135;bytes[43]=53;
      memcpy(bytes+24,destination.addr,16);pbuf_take(packet,bytes,sizeof(bytes));
      const uint32_t before=fixtureRealCalls;const err_t result=policy.underlay->output_ip6(policy.underlay,packet,&destination);
      const bool expected=protocol==58 && (!strcmp(text,"fe80::1") || !strcmp(text,"ff02::1"));
      check(expected ? result==ERR_OK && fixtureRealCalls==before+1 : result==ERR_RTE && fixtureRealCalls==before);
    }
    pbuf_free(packet);
  }
  // Exact endpoint A question, negative names/types/protocols, packet budget,
  // expiry and chained buffers exercise the production bootstrap parser.
  strlcpy(policy.endpointName,"endpoint.example",sizeof(policy.endpointName));
  ip_addr_t resolver=IPADDR4_INIT(lan);policy.underlayDNS[0]=resolver;policy.haveUnderlayDNS=true;
  uint8_t question[]={0x01,0x02,0x01,0,0,1,0,0,0,0,0,0,8,'e','n','d','p','o','i','n','t',7,'e','x','a','m','p','l','e',0,0,1,0,1};
  policy.dnsExpires=sys_now()+15000;policy.dnsBudget=16;
  packet4(lan,17,53,true,question,sizeof(question),0,true);
  question[13]='x';packet4(lan,17,53,false,question,sizeof(question));question[13]='e';
  question[31]=28;packet4(lan,17,53,false,question,sizeof(question));question[31]=1;
  packet4(lan,6,53,false,question,sizeof(question));packet4(wan,17,53,false,question,sizeof(question));
  for(int i=0;i<15;++i)packet4(lan,17,53,true,question,sizeof(question));
  packet4(lan,17,53,false,question,sizeof(question));policy.dnsBudget=16;
  policy.dnsExpires=sys_now()-1;packet4(lan,17,53,false,question,sizeof(question));
  uint8_t timePacket[48]={0x23};policy.timeExpires=sys_now()+45000;policy.timeBudget=8;
  packet4(TIME_SERVER,17,123,true,timePacket,48,0,true);
  timePacket[0]=0x24;packet4(TIME_SERVER,17,123,false,timePacket,48);timePacket[0]=0x23;
  packet4(wan,17,123,false,timePacket,48);packet4(TIME_SERVER,17,123,false,timePacket,47);
  for(int i=0;i<7;++i)packet4(TIME_SERVER,17,123,true,timePacket,48);
  packet4(TIME_SERVER,17,123,false,timePacket,48);policy.timeBudget=8;
  policy.timeExpires=sys_now()-1;packet4(TIME_SERVER,17,123,false,timePacket,48);
  policy.dnsExpires=policy.timeExpires=0;
  ip4_addr_t destination={wan};netif* route=nullptr;
  check(fullTunnelRoute(nullptr,&destination,route) && route==policy.tunnel);
  const ip4_addr_t source=*netif_ip4_addr(policy.underlay);
  check(fullTunnelRoute(&source,&destination,route) && route==policy.underlay);
  destination.addr=policy.endpoint;check(fullTunnelRoute(nullptr,&destination,route) && route==policy.underlay);
  destination.addr=lan;check(fullTunnelRoute(nullptr,&destination,route) && route==policy.underlay);
  policy.tunnel=nullptr;destination.addr=wan;check(fullTunnelRoute(nullptr,&destination,route) && !route);
  packet4(wan,6,443,false);policy.tunnel=saved.tunnel;
  netif unexpected={};unexpected.next=netif_list;netif* oldList=netif_list;netif_list=&unexpected;
  check(!validInterfaces());netif_list=oldList;check(validInterfaces());
  fixtureSpy=false;const auto observed=policy.observed;policy=saved;policy.observed=observed;
  selectDNS();return fixtureResults.failures==0;
}
FullPolicyFixtureStatus fullPolicyFixtureStatus() { CoreLock lock;return fixtureResults; }
#endif
} // namespace appliance
#endif
