#pragma once
#include "V30AirtimeGovernor.h"
#include <stddef.h>
#include <stdint.h>
namespace v30 {
// Pool slot address plus fresh allocation generation; never serialize this
// local token or derive it from an untrusted radio frame.
struct V30PacketLease {
  const void* pointer;
  uint64_t generation;
  V30PacketLease(const void* p=nullptr, uint64_t g=0): pointer(p), generation(g) {}
  bool valid() const { return pointer && generation; }
  bool operator==(const V30PacketLease& rhs) const { return pointer==rhs.pointer && generation==rhs.generation; }
  bool operator!=(const V30PacketLease& rhs) const { return !(*this==rhs); }
};
class V30PacketLifecycle {
public:
  static constexpr size_t kCapacity = 16;  // pinned T-Deck StaticPool size
  V30PacketLifecycle() = default;
  V30PacketLifecycle(const V30PacketLifecycle&) = delete;
  V30PacketLifecycle& operator=(const V30PacketLifecycle&) = delete;
  V30PacketLease allocate(const void*);
  bool markQueued(V30PacketLease);
  bool markDispatching(V30PacketLease);
  bool markInboundQueued(V30PacketLease);
  bool markInboundDequeued(V30PacketLease);
  bool detach(V30PacketLease);
  bool release(V30PacketLease);
  bool isAllocated(V30PacketLease) const;
  bool isQueued(V30PacketLease) const;
  bool isInboundQueued(V30PacketLease) const;
  bool isDispatching(V30PacketLease) const;
  V30PacketLease current(const void*) const;
private:
  enum class State : uint8_t { Empty, Allocated, Queued, InboundQueued, Dispatching };
  struct Slot {
    V30PacketLease lease{};
    State state = State::Empty;
  };
  Slot slots_[kCapacity]{};
  uint64_t generation_ = 0;
  bool transition(V30PacketLease, State, State);
  bool hasState(V30PacketLease, State) const;
};
// Caller must obtain RF conformity/ERP/emissions evidence and check actual
// active radio settings before supplying Qualified8694. Nothing sets this
// value in firmware; unqualified/default hardware remains dormant.
enum class V30QualifiedProfile : uint8_t { Unqualified, Qualified8694 };
class V30RadioAdmissionBridge {
public:
  explicit V30RadioAdmissionBridge(const V30PacketLifecycle& life): life_(life) {}
  V30RadioAdmissionBridge(const V30RadioAdmissionBridge&) = delete;
  V30RadioAdmissionBridge& operator=(const V30RadioAdmissionBridge&) = delete;
  // Dispatcher estimates are milliseconds. Governor timestamps/charges are
  // microseconds. One shared bridge is required for ALL physical TX paths.
  bool admit(V30PacketLease, uint32_t estimatedMs, uint64_t nowUs, V30QualifiedProfile);
  bool complete(V30PacketLease, uint32_t elapsedMs, uint64_t nowUs);
  bool faulted() const { return faulted_ || governor_.faulted(); }
  uint64_t usedUs(uint64_t nowUs) const { return governor_.usedUs(nowUs); }
  uint32_t reservationMs(V30PacketLease) const;
private:
  const V30PacketLifecycle& life_;
  V30AirtimeGovernor governor_;
  V30PacketLease pending_{};
  uint32_t reservationMs_ = 0;
  uint64_t admittedAtUs_ = 0;
  bool faulted_ = false;
};
}
