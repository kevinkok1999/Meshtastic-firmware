#include "V6MessageGuard.h"

#if defined(MESHOFFGRID_ENABLE_V6)

#include "V6ModeController.h"
#include "mesh/MeshTypes.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

namespace meshoffgrid::v6 {

MessagePrivacyDecision V6MessageGuard::evaluateOutgoing(const meshtastic_MeshPacket &encrypted,
                                                        const meshtastic_MeshPacket &decoded)
{
    const auto profile = V6ModeController::privacyProfile();
    if (profile == PrivacyProfile::Balanced)
        return MessagePrivacyDecision::Allow;

    // Channel/group broadcasts keep their normal Meshtastic channel crypto.
    // V6's stricter direct-message rule applies only to user text unicasts.
    if (isBroadcast(encrypted.to) || decoded.which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
        decoded.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP) {
        return MessagePrivacyDecision::Allow;
    }

    // In Private/Maximum, a direct chat must have been converted to
    // destination-specific Meshtastic PKI encryption. A channel-key fallback
    // is intentionally refused instead of silently weakening privacy.
    if (!encrypted.pki_encrypted)
        return MessagePrivacyDecision::RequireVerifiedDirectEncryption;

    return MessagePrivacyDecision::Allow;
}

} // namespace meshoffgrid::v6

#endif
