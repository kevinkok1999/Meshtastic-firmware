#pragma once
#if defined(MESH_OFFGRIDNL_V30_POOL_OBSERVER) && MESH_OFFGRIDNL_V30_POOL_OBSERVER
#include <helpers/StaticPoolPacketManager.h>
#include "V30RadioAdmissionBridge.h"
namespace v30 {
class V30PoolObserverAdapter final : public V30PoolObserver {
public:
  void onV30PoolEvent(V30PoolEvent, const mesh::Packet*) override;
  bool faulted() const { return faulted_; }
  const V30PacketLifecycle& lifecycle() const { return life_; }
private:
  V30PacketLifecycle life_;
  bool faulted_ = false;
};
}
#endif
