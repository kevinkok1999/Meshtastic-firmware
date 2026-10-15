#include "XRPrivacyPolicy.h"

namespace meshoffgrid::xr {

bool XRPrivacyPolicy::isPrivatePayload(XRPayloadClass payloadClass)
{
    switch (payloadClass) {
    case XRPayloadClass::PRIVATE_DIRECT_MESSAGE:
    case XRPayloadClass::PRIVATE_LOCATION:
    case XRPayloadClass::PRIVATE_COURIER:
    case XRPayloadClass::DEVICE_CONTROL:
        return true;
    default:
        return false;
    }
}

bool XRPrivacyPolicy::requiresAuthenticatedInternetTransport(XRTransportKind transport)
{
    return transport == XRTransportKind::WIFI_MQTT || transport == XRTransportKind::LOCAL_WIFI;
}

XRPrivacyDecision XRPrivacyPolicy::evaluate(const XRPrivacyContext &ctx)
{
    XRPrivacyDecision out{};

    if (isPrivatePayload(ctx.payloadClass)) {
        // Destination-specific private traffic must remain end-to-end protected.
        // For ordinary direct messages that means Meshtastic PKI with a proven
        // peer key. A channel key alone is intentionally not accepted here.
        if (!ctx.meshtasticPkiEncrypted) {
            out.reason = XRPrivacyRejectReason::END_TO_END_ENCRYPTION_REQUIRED;
            return out;
        }
        if (!ctx.peerKeyAuthoritative) {
            out.reason = XRPrivacyRejectReason::PEER_IDENTITY_UNVERIFIED;
            return out;
        }
    } else if (ctx.payloadClass == XRPayloadClass::PUBLIC_CHANNEL && !ctx.channelEncrypted) {
        // Preserve upstream semantics: a deliberately plaintext channel is a
        // valid user choice. Do not silently claim privacy for it.
    }

    if (requiresAuthenticatedInternetTransport(ctx.transport)) {
        if (!ctx.transportTls || !ctx.brokerIdentityVerified) {
            out.reason = XRPrivacyRejectReason::BROKER_IDENTITY_UNVERIFIED;
            return out;
        }
    }

    if (ctx.thirdPartyCustody) {
        if (!ctx.opaqueToCarrier) {
            out.reason = XRPrivacyRejectReason::END_TO_END_ENCRYPTION_REQUIRED;
            return out;
        }
        if (!ctx.custodyAuthorized) {
            out.reason = XRPrivacyRejectReason::THIRD_PARTY_CUSTODY_NOT_AUTHORIZED;
            return out;
        }
    }

    if (ctx.persisted && isPrivatePayload(ctx.payloadClass) && !ctx.protectedStorage) {
        out.reason = XRPrivacyRejectReason::STORAGE_NOT_DURABLE_ENCRYPTED;
        return out;
    }

    if (ctx.attachesOptionalMetadata && !ctx.optionalMetadataExplicitlyRequested) {
        out.reason = XRPrivacyRejectReason::METADATA_POLICY_BLOCKED;
        return out;
    }

    out.allow = true;
    out.reason = XRPrivacyRejectReason::NONE;
    return out;
}

} // namespace meshoffgrid::xr
