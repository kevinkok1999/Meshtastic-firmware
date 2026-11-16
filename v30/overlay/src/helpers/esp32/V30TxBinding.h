#pragma once
#include <stdint.h>

namespace v30 {

// DEV-only identity contract. No radio, clock, persistence or regulatory policy
// is implemented here. The caller must reserve airtime before admit().
struct V30TxSettlement {
  uint64_t cookie;
  uint32_t conservativeMs;
  bool success;
};

class V30TxBinding {
public:
  bool bind(const void* packet, uint64_t cookie) {
    if (!packet || cookie == 0 || packet_) return false;
    packet_ = packet;
    cookie_ = cookie;
    return true;
  }

  bool admit(const void* packet, uint32_t reservationMs, bool reserved) {
    if (packet != packet_ || !packet_ || admitted_ || !reserved || reservationMs == 0)
      return false;
    reservationMs_ = reservationMs;
    admitted_ = true;
    return true;
  }

  bool settle(const void* packet, bool success, uint32_t elapsedMs,
              V30TxSettlement& out) {
    if (!packet_ || packet != packet_ || !admitted_) return false;
    out = V30TxSettlement{cookie_, elapsedMs > reservationMs_ ? elapsedMs : reservationMs_, success};
    clear();
    return true;
  }

  bool drop(const void* packet, uint64_t& cookie) {
    if (!packet_ || packet != packet_ || admitted_) return false;
    cookie = cookie_;
    clear();
    return true;
  }

private:
  void clear() { packet_ = nullptr; cookie_ = 0; reservationMs_ = 0; admitted_ = false; }
  const void* packet_ = nullptr;
  uint64_t cookie_ = 0;
  uint32_t reservationMs_ = 0;
  bool admitted_ = false;
};

}  // namespace v30
