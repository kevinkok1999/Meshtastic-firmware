#pragma once

#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstdint>

namespace meshoffgrid::v6 {

enum class MessagePrivacyDecision : uint8_t {
    Allow = 0,
    RequireVerifiedDirectEncryption,
};

class V6MessageGuard
{
  public:
    // Evaluate after Meshtastic has encoded/encrypted the packet, while the
    // decoded copy is still available. This single gate therefore protects
    // both MQTT and every RF transport.
    static MessagePrivacyDecision evaluateOutgoing(const meshtastic_MeshPacket &encrypted,
                                                   const meshtastic_MeshPacket &decoded);
};

} // namespace meshoffgrid::v6
