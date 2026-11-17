#pragma once
#include <atomic>
#include <cstdint>
namespace meshtastic::nanomap {
// One UI owner issues monotonically changing request tokens. A worker only
// publishes a result if its token still identifies the current viewport.
class MapGeneration {
 public:
  uint32_t next() {
    uint32_t id=counter_.fetch_add(1)+1;
    if(!id)id=counter_.fetch_add(1)+1;
    return id;
  }
  bool isCurrent(uint32_t id) const {return id&&counter_.load()==id;}
 private:
  std::atomic<uint32_t> counter_{0};
};
}
