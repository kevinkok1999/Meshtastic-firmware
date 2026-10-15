#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

enum class XRPayloadClass : uint8_t {
    PUBLIC_CHANNEL = 0,
    PRIVATE_DIRECT_MESSAGE,
    PRIVATE_LOCATION,
    PRIVATE_COURIER,
    DEVICE_CONTROL,
    DIAGNOSTIC,
};

enum class XRTransportKind : uint8_t {
    LORA_MESH = 0,
    WIFI_MQTT,
    LOCAL_WIFI,
    BLE_PROXIMITY,
    COURIER,
};

enum class XRPrivacyRejectReason : uint8_t {
    NONE = 0,
    END_TO_END_ENCRYPTION_REQUIRED,
    PEER_IDENTITY_UNVERIFIED,
    BROKER_IDENTITY_UNVERIFIED,
    TRANSPORT_NOT_ALLOWED,
    THIRD_PARTY_CUSTODY_NOT_AUTHORIZED,
    STORAGE_NOT_DURABLE_ENCRYPTED,
    METADATA_POLICY_BLOCKED,
};

struct XRPrivacyContext {
    XRPayloadClass payloadClass = XRPayloadClass::PRIVATE_DIRECT_MESSAGE;
    XRTransportKind transport = XRTransportKind::LORA_MESH;

    bool meshtasticPkiEncrypted = false;
    bool peerKeyAuthoritative = false;
    bool channelEncrypted = false;

    // For internet transports. An encrypted TCP/TLS tunnel is not sufficient
    // unless the server identity is authenticated as well.
    bool transportTls = false;
    bool brokerIdentityVerified = false;

    // Courier/proximity nodes must not gain plaintext access to third-party
    // messages. They may carry only opaque, destination-protected payloads.
    bool thirdPartyCustody = false;
    bool opaqueToCarrier = false;
    bool custodyAuthorized = false;

    // Persistent private data must use the device's protected storage path.
    bool persisted = false;
    bool protectedStorage = false;

    // Privacy-sensitive traffic should not attach optional position/telemetry
    // unless the user explicitly requested it for this message.
    bool attachesOptionalMetadata = false;
    bool optionalMetadataExplicitlyRequested = false;
};

struct XRPrivacyDecision {
    bool allow = false;
    XRPrivacyRejectReason reason = XRPrivacyRejectReason::NONE;
};

class XRPrivacyPolicy {
  public:
    // Private DMs default to Meshtastic PKI. Channel encryption is retained for
    // public/group channels, but does not replace destination-specific privacy.
    static XRPrivacyDecision evaluate(const XRPrivacyContext &ctx);

    static bool isPrivatePayload(XRPayloadClass payloadClass);
    static bool requiresAuthenticatedInternetTransport(XRTransportKind transport);
};

} // namespace meshoffgrid::xr
