#include <helpers/StaticPoolPacketManager.h>
#include "V30PoolObserverAdapter.h"
#include <cassert>
#include <vector>

#if defined(MESH_OFFGRIDNL_V30_POOL_OBSERVER) && MESH_OFFGRIDNL_V30_POOL_OBSERVER
using v30::V30PoolObserverAdapter;
#endif

static void defaultBehavior() {
  StaticPoolPacketManager manager(2);
  auto* a = manager.allocNew(); auto* b = manager.allocNew();
  assert(a && b && a != b && !manager.allocNew());
  manager.queueOutbound(a, 2, 20);
  manager.queueOutbound(b, 1, 10);
  assert(!manager.getNextOutbound(9));
  assert(manager.getNextOutbound(10) == b);
  manager.free(b);
  assert(manager.removeOutboundByIdx(0) == a);
  manager.queueOutbound(a, 0, 11);
  assert(manager.getNextOutbound(11) == a);
  manager.free(a);
  assert(manager.getFreeCount() == 2);
}

#if defined(MESH_OFFGRIDNL_V30_POOL_OBSERVER) && MESH_OFFGRIDNL_V30_POOL_OBSERVER
static void actualLifecycle() {
  V30PoolObserverAdapter observer;
  StaticPoolPacketManager manager(2);
  assert(manager.setV30Observer(&observer));
  assert(!manager.setV30Observer(&observer));
  auto* a = manager.allocNew(); auto* b = manager.allocNew();
  auto lease = observer.lifecycle().current(a);
  assert(lease.valid() && observer.lifecycle().isAllocated(lease));
  assert(observer.lifecycle().current(b).valid());
  manager.queueOutbound(a, 1, 10);
  assert(observer.lifecycle().isQueued(lease));
  assert(manager.removeOutboundByIdx(0) == a);
  assert(observer.lifecycle().isAllocated(lease));
  manager.queueOutbound(a, 1, 10);
  assert(manager.getNextOutbound(10) == a);
  assert(observer.lifecycle().isDispatching(lease));
  manager.free(a);
  assert(!observer.lifecycle().current(a).valid());
  auto* reused = manager.allocNew();
  assert(reused == a);
  auto fresh = observer.lifecycle().current(reused);
  assert(fresh.valid() && fresh != lease);
  assert(observer.lifecycle().isAllocated(fresh));
  manager.queueInbound(reused, 15);
  assert(observer.lifecycle().isInboundQueued(fresh));
  assert(manager.getNextInbound(15) == reused);
  assert(observer.lifecycle().isAllocated(fresh));
  manager.queueOutbound(reused, 0, 15);
  assert(manager.getNextOutbound(15) == reused);
  manager.free(reused);
  manager.free(b);
  assert(manager.getFreeCount() == 2 && !observer.faulted());
}

static void attachMustBeIdle() {
  V30PoolObserverAdapter observer;
  StaticPoolPacketManager manager(1);
  auto* packet = manager.allocNew();
  assert(!manager.setV30Observer(&observer));
  manager.free(packet);
  assert(manager.setV30Observer(&observer));
}

struct Probe : V30PoolObserver {
  std::vector<V30PoolEvent> events;
  void onV30PoolEvent(V30PoolEvent event, const mesh::Packet*) override {
    events.push_back(event);
  }
};

static void actualRejection() {
  // In the correct ownership model a pool of one cannot supply two unique
  // outbound packets. Deliberately duplicate the enqueue to execute the real
  // failure branch without substituting a fake PacketQueue implementation.
  Probe probe;
  StaticPoolPacketManager manager(1);
  assert(manager.setV30Observer(&probe));
  auto* packet = manager.allocNew();
  manager.queueOutbound(packet, 0, 10);
  manager.queueOutbound(packet, 0, 10);
  assert((probe.events == std::vector<V30PoolEvent>{V30PoolEvent::Allocated,
      V30PoolEvent::OutboundQueued, V30PoolEvent::OutboundRejected,
      V30PoolEvent::Released}));

  V30PoolObserverAdapter observer;
  StaticPoolPacketManager second(1);
  assert(second.setV30Observer(&observer));
  auto* p = second.allocNew();
  second.queueOutbound(p, 0, 10);
  second.queueOutbound(p, 0, 10);
  assert(observer.faulted()); // never treat duplicate queue/free as a new message
}

static void releaseFailure() {
  V30PoolObserverAdapter observer;
  StaticPoolPacketManager manager(1);
  assert(manager.setV30Observer(&observer));
  auto* packet = manager.allocNew();
  manager.free(packet);
  assert(!observer.faulted());
  manager.free(packet); // real unused.add() failure, should latch observer fault
  assert(observer.faulted());
}
#endif

int main() {
  defaultBehavior();
#if defined(MESH_OFFGRIDNL_V30_POOL_OBSERVER) && MESH_OFFGRIDNL_V30_POOL_OBSERVER
  actualLifecycle();
  attachMustBeIdle();
  actualRejection();
  releaseFailure();
#endif
}
