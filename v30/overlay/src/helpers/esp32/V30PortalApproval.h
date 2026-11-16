#pragma once
#include "V30PortalRequest.h"
#include <atomic>
namespace v30 {
// request(), take() and clear() belong to the portal service loop. The native
// UI may only inspect pending() and approve() the exact observed request ID.
class PortalApproval {
public:
 bool request(PortalRequest::Action a, uint32_t now) {
    if (a < PortalRequest::Safe || a > PortalRequest::Meeting ||
        state_.load() != 0 || generation_ >= 0x0fffffffU) return false;
    created_ = now; ++generation_;
    state_.store((generation_ << 4) | static_cast<uint32_t>(a));
    return true;
 }
 uint32_t pending() const {
    uint32_t s = state_.load(); return (s & 8u) ? 0 : s;
 }
 static PortalRequest::Action action(uint32_t id) {
    if (!id) return PortalRequest::Deny;
    return static_cast<PortalRequest::Action>(id & 7u);
 }
 bool approve(uint32_t id) {
    if (!id || (id & 8u)) return false;
    return state_.compare_exchange_strong(id,id | 8u);
 }
 PortalRequest::Action take(uint32_t now) {
    uint32_t s=state_.load();
    if (s==0) return PortalRequest::Deny;
    if ((uint32_t)(now-created_) >= 30000u) {
      state_.store(0); return PortalRequest::Deny;
    }
    if (!(s & 8u)) return PortalRequest::Deny;
    state_.store(0); return action(s);
 }
 void clear() { state_.store(0); }
private:
 std::atomic<uint32_t> state_{0};
 uint32_t generation_=0, created_=0;
};
}
