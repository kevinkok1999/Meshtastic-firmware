#include "V30PoolObserverAdapter.h"
#if defined(MESH_OFFGRIDNL_V30_POOL_OBSERVER) && MESH_OFFGRIDNL_V30_POOL_OBSERVER
namespace v30 {

// Pure observation, no queue mutation, radio send, airtime reserve or receipt.
// Once inconsistent ownership is observed, later events cannot erase evidence.
void V30PoolObserverAdapter::onV30PoolEvent(V30PoolEvent event,
                                           const mesh::Packet* packet) {
  if (faulted_) return;
  if (event == V30PoolEvent::Allocated) {
    if (!life_.allocate(packet).valid()) faulted_ = true;
    return;
  }
  const auto lease = life_.current(packet);
  bool accepted = false;
  switch (event) {
    case V30PoolEvent::OutboundQueued: accepted = life_.markQueued(lease); break;
    case V30PoolEvent::Dispatching: accepted = life_.markDispatching(lease); break;
    case V30PoolEvent::Detached: accepted = life_.detach(lease); break;
    case V30PoolEvent::InboundQueued: accepted = life_.markInboundQueued(lease); break;
    case V30PoolEvent::InboundDequeued: accepted = life_.markInboundDequeued(lease); break;
    case V30PoolEvent::OutboundRejected:
    case V30PoolEvent::InboundRejected:
      // Rejection callback precedes actual return to pool; valid only if
      // sender still owned that allocated packet, not an already queued alias.
      accepted = life_.isAllocated(lease); break;
    case V30PoolEvent::Released:
      accepted = (life_.isAllocated(lease) || life_.isDispatching(lease)) && life_.release(lease);
      break;
    case V30PoolEvent::ReleaseFailed:
    case V30PoolEvent::Allocated:
      break;
  }
  if (!accepted) faulted_ = true;
}
}
#endif
