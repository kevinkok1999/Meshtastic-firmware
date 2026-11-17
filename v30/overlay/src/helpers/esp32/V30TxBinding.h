#pragma once
#include <stdint.h>
#include "V30RadioAdmissionBridge.h"

namespace v30 {

// DEV-only local identity binding. Only the actual shared bridge's live
// reservation may authorize admission. It never implies recipient delivery.
struct V30TxSettlement {
  uint64_t cookie;
  uint32_t conservativeMs;
  bool success;
};

class V30TxBinding {
public:
  V30TxBinding() = default;
  V30TxBinding(const V30TxBinding&) = delete;
  V30TxBinding& operator=(const V30TxBinding&) = delete;
  bool bind(V30PacketLease lease, uint64_t cookie, const V30PacketLifecycle& life) {
    if (!life.isAllocated(lease) || cookie == 0 || lease_.valid()) return false;
    lease_ = lease;
    cookie_ = cookie;
    return true;
  }

  bool admit(V30PacketLease lease, const V30RadioAdmissionBridge& bridge) {
    if (!lease_.valid() || lease != lease_ || admitted_) return false;
    const uint32_t reservationMs = bridge.reservationMs(lease);
    if (!reservationMs) return false;
    reservationMs_ = reservationMs;
    admitted_ = true;
    return true;
  }

  bool settle(V30PacketLease lease, bool success, uint32_t elapsedMs,
              uint64_t nowUs, V30RadioAdmissionBridge& bridge,
              V30TxSettlement& out) {
    if (!lease_.valid() || lease != lease_ || !admitted_) return false;
    if (!bridge.complete(lease, elapsedMs, nowUs)) return false;
    out = V30TxSettlement{cookie_, elapsedMs > reservationMs_ ? elapsedMs : reservationMs_, success};
    clear();
    return true;
  }

  // Exact-token drop is valid before admission even if the manager already
  // returned the slot to the pool. A reused slot's new token never matches.
  bool drop(V30PacketLease lease, uint64_t& cookie) {
    if (!lease_.valid() || lease != lease_ || admitted_) return false;
    cookie = cookie_;
    clear();
    return true;
  }

private:
  void clear() { lease_ = {}; cookie_ = 0; reservationMs_ = 0; admitted_ = false; }
  V30PacketLease lease_{};
  uint64_t cookie_ = 0;
  uint32_t reservationMs_ = 0;
  bool admitted_ = false;
};

}  // namespace v30
