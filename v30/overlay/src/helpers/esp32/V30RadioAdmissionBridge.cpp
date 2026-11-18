#include "V30RadioAdmissionBridge.h"

namespace v30 {

V30PacketLease V30PacketLifecycle::allocate(const void* pointer) {
  if (!pointer || current(pointer).valid() || generation_ == UINT64_MAX) return {};
  for (auto& slot: slots_) {
    if (slot.state != State::Empty) continue;
    slot.lease = V30PacketLease(pointer, ++generation_);
    slot.state = State::Allocated;
    return slot.lease;
  }
  return {};
}

V30PacketLease V30PacketLifecycle::current(const void* pointer) const {
  if (!pointer) return {};
  for (const auto& slot: slots_)
    if (slot.state != State::Empty && slot.lease.pointer == pointer)
      return slot.lease;
  return {};
}

bool V30PacketLifecycle::transition(V30PacketLease lease, State from, State to) {
  if (!lease.valid()) return false;
  for (auto& slot: slots_) {
    if (slot.lease != lease || slot.state != from) continue;
    slot.state = to;
    return true;
  }
  return false;
}

bool V30PacketLifecycle::hasState(V30PacketLease lease, State state) const {
  if (!lease.valid()) return false;
  for (const auto& slot: slots_)
    if (slot.lease == lease && slot.state == state) return true;
  return false;
}

bool V30PacketLifecycle::markQueued(V30PacketLease lease) {
  return transition(lease, State::Allocated, State::Queued);
}
bool V30PacketLifecycle::markDispatching(V30PacketLease lease) {
  return transition(lease, State::Queued, State::Dispatching);
}
bool V30PacketLifecycle::markInboundQueued(V30PacketLease lease) {
  return transition(lease, State::Allocated, State::InboundQueued);
}
bool V30PacketLifecycle::markInboundDequeued(V30PacketLease lease) {
  return transition(lease, State::InboundQueued, State::Allocated);
}
bool V30PacketLifecycle::detach(V30PacketLease lease) {
  return transition(lease, State::Queued, State::Allocated);
}
bool V30PacketLifecycle::isAllocated(V30PacketLease lease) const {
  return hasState(lease, State::Allocated);
}
bool V30PacketLifecycle::isQueued(V30PacketLease lease) const {
  return hasState(lease, State::Queued);
}
bool V30PacketLifecycle::isInboundQueued(V30PacketLease lease) const {
  return hasState(lease, State::InboundQueued);
}
bool V30PacketLifecycle::isDispatching(V30PacketLease lease) const {
  return hasState(lease, State::Dispatching);
}
bool V30PacketLifecycle::release(V30PacketLease lease) {
  if (!lease.valid()) return false;
  for (auto& slot: slots_) {
    if (slot.lease != lease || slot.state == State::Empty) continue;
    slot = Slot{};
    return true;
  }
  return false;
}

namespace {
constexpr uint8_t kQualifiedBand = 1;
constexpr uint64_t millisecondsToUs(uint32_t ms) { return uint64_t(ms) * 1000; }
}

bool V30RadioAdmissionBridge::admit(V30PacketLease lease, uint32_t estimatedMs,
                                   uint64_t nowUs, V30QualifiedProfile profile) {
  if (pending_.valid() && !life_.isDispatching(pending_)) faulted_ = true;
  if (faulted() || pending_.valid() || !life_.isDispatching(lease) ||
      profile != V30QualifiedProfile::Qualified8694 || estimatedMs == 0)
    return false;
  if (!governor_.mayTransmit(nowUs, millisecondsToUs(estimatedMs), kQualifiedBand))
    return false;
  pending_ = lease;
  reservationMs_ = estimatedMs;
  admittedAtUs_ = nowUs;
  return true;
}

bool V30RadioAdmissionBridge::complete(V30PacketLease lease, uint32_t elapsedMs,
                                      uint64_t nowUs) {
  if (faulted() || !pending_.valid() || pending_ != lease || !life_.isDispatching(lease)) {
    // Never retire an unresolved reservation on a stale/forged completion.
    faulted_ = true;
    return false;
  }
  const uint64_t measuredUs = millisecondsToUs(elapsedMs);
  // Use both callback-reported duration and monotonic elapsed interval. A
  // late callback can overcharge conservatively, never undercharge.
  const uint64_t intervalUs = nowUs >= admittedAtUs_ ? nowUs - admittedAtUs_ : 0;
  governor_.recordAttempt(nowUs, intervalUs > measuredUs ? intervalUs : measuredUs,
                          kQualifiedBand);
  if (governor_.faulted()) faulted_ = true;
  pending_ = {};
  reservationMs_ = 0;
  admittedAtUs_ = 0;
  return !faulted();
}

uint32_t V30RadioAdmissionBridge::reservationMs(V30PacketLease lease) const {
  if (faulted() || pending_ != lease || !life_.isDispatching(lease)) return 0;
  return reservationMs_;
}

} // namespace v30
