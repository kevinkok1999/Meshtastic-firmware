#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

enum class XRTransport : uint8_t {
    NONE = 0,
    LORA_MESHTASTIC,
    WIFI_MQTT_MESHTASTIC,
    BLE_PROXIMITY,
    WIFI_LOCAL,
    USB_LOCAL,
};

enum class XRPayloadClass : uint8_t {
    STOCK_PUBLIC_TEXT = 0,
    STOCK_DIRECT_TEXT,
    XR_CONTROL,
    COURIER_OBJECT,
    RECEIPT,
    WAYPOINT_BUNDLE,
    MAP_CACHE,
    UPDATE_MANIFEST,
    UPDATE_PACKAGE,
    DIAGNOSTIC_EXPORT,
};

enum class XRUrgency : uint8_t {
    BACKGROUND = 0,
    NORMAL,
    HIGH,
};

struct XRTransportState {
    bool loraAvailable = true;
    bool bleAvailable = false;
    bool wifiLocalAvailable = false;
    bool usbAvailable = false;
    bool wifiInternetAvailable = false;
    bool mqttConfigured = false;
    bool mqttConnected = false;
    bool peerLikelyReachableViaMqtt = false;
    bool peerAuthenticatedForProximity = false;
    bool peerSupportsFusion = false;
    bool peerAuthorizedForThirdPartyCustody = false;
    uint8_t batteryPercent = 100;
    uint8_t channelHealth = 100;
    uint32_t proximityMtuBytes = 0;
};

struct XRTransferRequest {
    XRPayloadClass payloadClass = XRPayloadClass::XR_CONTROL;
    XRUrgency urgency = XRUrgency::NORMAL;
    uint32_t payloadBytes = 0;
    bool stockMeshtasticCompatibilityRequired = true;
    bool mayDefer = true;
    bool containsThirdPartyPrivateData = false;
    bool allowInternetFallback = true;
    bool normalMeshtasticDeliveryFailed = false;
    bool sameConversationRequired = true;
};

struct XRTransportDecision {
    XRTransport transport = XRTransport::NONE;
    bool defer = false;
    bool useNormalMeshtasticPacket = true;
    bool requiresExplicitUserApproval = false;
    bool privacyBlockedProximity = false;
    bool internetFallbackUsed = false;
    const char *reason = "no transport";
};

class XRFusionTransport {
  public:
    XRTransportDecision choose(const XRTransferRequest &request, const XRTransportState &state) const;
    static bool isBulkClass(XRPayloadClass payloadClass);
    static bool mustUseMeshtasticDataplane(XRPayloadClass payloadClass);

  private:
    static XRTransport bestAuthenticatedProximityTransport(const XRTransportState &state);
    static bool internetMeshtasticReady(const XRTransportState &state);
};

} // namespace meshoffgrid::xr
