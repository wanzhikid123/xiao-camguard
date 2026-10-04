#pragma once
#if defined(XIAO_APPLIANCE)
#include <atomic>
#include <stdint.h>
namespace appliance {
struct PublicRouteResult { bool done=false;uint32_t generation=0,address=0;int http=0; };
// Lifetime is the network driver. One bounded, low-priority unbound TLS request;
// only an atomic mailbox crosses tasks. No credentials or arbitrary destination.
class PublicRouteProbe {
public:
  bool start(uint32_t generation);
  PublicRouteResult result() const;
  bool busy() const { return running.load(); }
private:
  std::atomic<bool> running {false}, done {false};
  std::atomic<uint32_t> generation {0}, address {0};
  std::atomic<int> http {0};
  static void worker(void* context);
};
} // namespace appliance
#endif
