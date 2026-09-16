#include "XRFusionTransport.h"

namespace meshoffgrid::xr {

bool XRFusionTransport::isBulkClass(XRPayloadClass payloadClass)
{
    switch (payloadClass) {
    case XRPayloadClass::MAP_CACHE:
    case XRPayloadClass::UPDATE_PACKAGE:
    case XRPayloadClass::DIAGNOSTIC_EXPORT:
        return true;
    default:
        return false;
    }
}

bool XRFusionTransport::mustUseMeshtasticDataplane(XRPayloadClass payloadClass)
{
    return payloadClass == XRPayloadClass::STOCK_PUBLIC_TEXT || payloadClass == XRPayloadClass::STOCK_DIRECT_TEXT;
}

bool XRFusionTransport::internetMeshtasticReady(const XRTransportState &state)
{
    return state.wifiInternetAvailable && state.mqttConfigured && state.mqttConnected;
}

XRTransport XRFusionTransport::bestAuthenticatedProximityTransport(const XRTransportState &state)
{
    if (!state.peerAuthenticatedForProximity || !state.peerSupportsFusion)
        return XRTransport::NONE;
    if (state.wifiLocalAvailable)
        return XRTransport::WIFI_LOCAL;
    if (state.bleAvailable)
        return XRTransport::BLE_PROXIMITY;
    if (state.usbAvailable)
        return XRTransport::USB_LOCAL;
    return XRTransport::NONE;
}

XRTransportDecision XRFusionTransport::choose(const XRTransferRequest &request, const XRTransportState &state) const
{
    XRTransportDecision decision{};

    if (mustUseMeshtasticDataplane(request.payloadClass)) {
        if (state.loraAvailable && !request.normalMeshtasticDeliveryFailed) {
            decision.transport = XRTransport::LORA_MESHTASTIC;
            decision.useNormalMeshtasticPacket = true;
            decision.reason = "normal Meshtastic LoRa path";
            return decision;
        }

        if (request.allowInternetFallback && internetMeshtasticReady(state)) {
            const bool direct = request.payloadClass == XRPayloadClass::STOCK_DIRECT_TEXT;
            if (!direct || state.peerLikelyReachableViaMqtt) {
                decision.transport = XRTransport::WIFI_MQTT_MESHTASTIC;
                decision.useNormalMeshtasticPacket = true;
                decision.internetFallbackUsed = true;
                decision.reason = "same Meshtastic message via Wi-Fi/MQTT fallback";
                return decision;
            }
        }

        if (state.loraAvailable) {
            decision.transport = XRTransport::NONE;
            decision.defer = request.mayDefer;
            decision.useNormalMeshtasticPacket = true;
            decision.reason = "LoRa delivery failed; wait for next delivery opportunity";
            return decision;
        }

        decision.transport = XRTransport::NONE;
        decision.defer = request.mayDefer;
        decision.useNormalMeshtasticPacket = true;
        decision.reason = "no Meshtastic transport currently available";
        return decision;
    }

    XRTransport proximity = bestAuthenticatedProximityTransport(state);
    if (request.containsThirdPartyPrivateData && !state.peerAuthorizedForThirdPartyCustody) {
        proximity = XRTransport::NONE;
        decision.privacyBlockedProximity = true;
    }

    if (isBulkClass(request.payloadClass)) {
        if (proximity != XRTransport::NONE) {
            decision.transport = proximity;
            decision.useNormalMeshtasticPacket = false;
            decision.requiresExplicitUserApproval = request.payloadClass == XRPayloadClass::UPDATE_PACKAGE;
            decision.reason = "bulk object moved to authenticated proximity transport";
            return decision;
        }
        decision.transport = XRTransport::NONE;
        decision.defer = request.mayDefer;
        decision.useNormalMeshtasticPacket = false;
        decision.requiresExplicitUserApproval = request.payloadClass == XRPayloadClass::UPDATE_PACKAGE;
        decision.reason = decision.privacyBlockedProximity ? "proximity blocked by privacy policy"
                                                           : "bulk object waiting for proximity transport";
        return decision;
    }

    if (request.payloadClass == XRPayloadClass::COURIER_OBJECT && proximity != XRTransport::NONE && request.payloadBytes > 96) {
        decision.transport = proximity;
        decision.useNormalMeshtasticPacket = false;
        decision.reason = "courier payload accelerated by authorized proximity transport";
        return decision;
    }

    if (state.loraAvailable) {
        decision.transport = XRTransport::LORA_MESHTASTIC;
        decision.useNormalMeshtasticPacket = true;
        decision.reason = decision.privacyBlockedProximity ? "private data kept on Meshtastic path"
                                                           : "small resilient control object over Meshtastic";
        return decision;
    }

    if (proximity != XRTransport::NONE) {
        decision.transport = proximity;
        decision.useNormalMeshtasticPacket = false;
        decision.reason = "LoRa unavailable; authenticated proximity fallback";
        return decision;
    }

    decision.transport = XRTransport::NONE;
    decision.defer = request.mayDefer;
    decision.useNormalMeshtasticPacket = false;
    decision.reason = decision.privacyBlockedProximity ? "no privacy-authorized transport available"
                                                       : "no eligible transport available";
    return decision;
}

} // namespace meshoffgrid::xr
