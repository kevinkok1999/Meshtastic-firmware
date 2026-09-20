#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: apply_v10.py <saitama_checkout> <overlay_dir>")

root = Path(sys.argv[1]).resolve()
overlay = Path(sys.argv[2]).resolve()

def replace_once(path, old, new):
    p = root / path
    s = p.read_text()
    if old not in s:
        raise SystemExit(f"V10 patch anchor missing in {path}: {old[:180]!r}")
    p.write_text(s.replace(old, new, 1))

dst = root / "src/mesh"
for name in [
    "V10DirectLinkCore.h", "V10DirectLinkCore.cpp",
    "V10WifiLrDirect.h", "V10WifiLrDirect.cpp",
    "V10ExperimentalCaps.h",
]:
    shutil.copy2(overlay / name, dst / name)

replace_once(
    "src/mesh/MeshService.cpp",
    '#include "V9StoreForward.h"\n',
    '#include "V9StoreForward.h"\n'
    '#include "V10DirectLinkCore.h"\n'
    '#include "V10WifiLrDirect.h"\n'
    '#include "V10ExperimentalCaps.h"\n'
)

replace_once(
    "src/mesh/MeshService.cpp",
    "static uint32_t                    v9_lastHybridFallbacks = 0;\n",
    "static uint32_t                    v9_lastHybridFallbacks = 0;\n"
    "static v10::DirectLinkBrain        v10_brain;\n"
    "static v10::ResourceScheduler      v10_scheduler;\n"
    "static v10::WifiLrDirect           v10_wifi;\n"
    "static uint32_t                    v10_lastWifiAckRx = 0;\n"
    "static uint32_t                    v10_lastWifiFallbacks = 0;\n"
)

replace_once(
    "src/mesh/MeshService.cpp",
    "    void clearLastExpectedAck() { _lastExpectedAck = 0; }\n\n",
    """    void clearLastExpectedAck() { _lastExpectedAck = 0; }

    bool resolvePeerPubKey(const uint8_t prefix[4], uint8_t out[32]) const {
        if (!prefix || !out) return false;
        for (int i = 0; i < _peerCount; ++i) {
            if (memcmp(_peers[i].pubKeyPrefix, prefix, 4) == 0) {
                memcpy(out, _peers[i].pubKey, 32);
                return true;
            }
        }
        int idx = -1;
        if (ops::contacts::findByKey(prefix, &idx)) {
            ops::Contact c{};
            if (ops::contacts::get(idx, c)) {
                bool hasKey = false;
                for (int i = 0; i < 32; ++i) if (c.pubKey[i] != 0) { hasKey = true; break; }
                if (hasKey) {
                    memcpy(out, c.pubKey, 32);
                    return true;
                }
            }
        }
        return false;
    }

"""
)

replace_once(
    "src/mesh/MeshService.cpp",
    "    void injectHybridMessage(const uint8_t* pubKey32, const char* name,\n",
    """    void injectV10Message(const uint8_t* pubKey32, const char* name,
                           uint32_t timestamp, const char* text, const char* path) {
        if (!pubKey32 || !text || !text[0]) return;
        if (!hybrid_transport.acceptApplicationMessage(pubKey32, text, true)) {
            OPS_LOG("V10", "Dropped cross-bearer duplicate");
            return;
        }

        RxMessage msg{};
        msg.timestamp = timestamp;
        strncpy(msg.senderName, (name && name[0]) ? name : "V10 peer", 31);
        strncpy(msg.text, text, 159);
        msg.isDirect = true;
        msg.hops = 0;
        msg.rssi = 0;
        msg.snr = 0;
        memcpy(msg.pubKeyPrefix, pubKey32, 4);
        strncpy(msg.pathStr, path ? path : "V10-DIRECT", sizeof(msg.pathStr)-1);
        _enqueueRx(msg);

        for (int i = 0; i < _peerCount; ++i) {
            if (memcmp(_peers[i].pubKeyPrefix, pubKey32, 4) == 0) {
                memcpy(_peers[i].pubKey, pubKey32, 32);
                _peers[i].lastSeen = getRTCClock()->getCurrentTime();
                _peerSerial++;
                return;
            }
        }
    }

    void injectHybridMessage(const uint8_t* pubKey32, const char* name,
"""
)

replace_once(
    "src/mesh/MeshService.cpp",
    '    void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override {\n'
    '        if (!hybrid_transport.acceptApplicationMessage(from.id.pub_key, text, false)) {',
    '    void onMessageRecv(const ContactInfo& from, mesh::Packet* pkt, uint32_t sender_timestamp, const char* text) override {\n'
    '        v10_wifi.configurePeer(from.id.pub_key);\n'
    '        if (!hybrid_transport.acceptApplicationMessage(from.id.pub_key, text, false)) {'
)

replace_once(
    "src/mesh/MeshService.cpp",
    '    OPS_LOG("V9", "Reach Engine ready; RangeFirst active; bounded deferred-send queue=%u", (unsigned)v9::V9StoreForward::CAPACITY);\n'
    '    OPS_LOG("Mesh", "MeshCore ready");',
    '    uint8_t v10Pub[32], v10Prv[64];\n'
    '    the_mesh.getSelfPubKey(v10Pub);\n'
    '    the_mesh.getSelfPrvKey(v10Prv);\n'
    '    if (v10_wifi.begin(v10Pub, v10Prv))\n'
    '        OPS_LOG("V10", "Wi-Fi LR Direct engine ready; waits for paired peer");\n'
    '    v10_brain.setAvailable(v10::BEARER_LORA, true, millis());\n'
    '    v10_brain.setReachable(v10::BEARER_LORA, true, millis());\n'
    '    v10_brain.setAvailable(v10::BEARER_ESPNOW_LR, hybrid_transport.initialized(), millis());\n'
    '    v10_brain.setAvailable(v10::BEARER_WIFI_LR, true, millis());\n'
    '    v10_brain.setAvailable(v10::BEARER_GFSK, v10::ExperimentalCaps::gfskDirectReleaseEnabled(), millis());\n'
    '    v10_brain.setAvailable(v10::BEARER_BLE_CODED, v10::ExperimentalCaps::bleCodedReleaseEnabled(), millis());\n'
    '    v10_lastWifiAckRx = v10_wifi.stats().ackRx;\n'
    '    v10_lastWifiFallbacks = v10_wifi.stats().fallbacks;\n'
    '    OPS_LOG("V10", "DirectLink Extreme ready: LoRa + ESP-NOW LR + Wi-Fi LR Direct; GFSK/BLE gated pending hardware QA");\n'
    '    OPS_LOG("Mesh", "MeshCore ready");'
)

old_send = '''bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {
    if (!_initialized || !pubKeyPrefix4 || !text || !text[0]) return false;

    const uint32_t now = millis();
    const bool espReachable = hybrid_transport.canReach(pubKeyPrefix4);
    v9_reach.setAvailable(V9Route::LoRa, true, now);
    v9_reach.setReachable(V9Route::LoRa, true, now);
    v9_reach.setAvailable(V9Route::EspNowLR, hybrid_transport.initialized(), now);
    v9_reach.setReachable(V9Route::EspNowLR, espReachable, now);

    const V9ReachDecision decision = v9_reach.decide(now);
    OPS_LOG("V9", "Reach decision primary=%s backup=%s fec=%d frag=%u retry=%u",
            decision.hasPrimary ? V9ReachEngine::name(decision.primary) : "none",
            decision.hasBackup ? V9ReachEngine::name(decision.backup) : "none",
            decision.useFec ? 1 : 0,
            (unsigned)decision.fragmentBytes,
            (unsigned)decision.retryRounds);

    auto tryEspNow = [&]() -> bool {
        if (!espReachable) return false;
        if (!hybrid_transport.sendDirect(pubKeyPrefix4, text, (uint32_t)rtc_clock.getCurrentTime())) return false;
        the_mesh.clearLastExpectedAck();
        OPS_LOG("V9", "DM accepted by ESP-NOW LR");
        return true;
    };

    auto tryLoRa = [&]() -> bool {
        if (!the_mesh.sendDirectMsg(pubKeyPrefix4, text)) return false;
        OPS_LOG("V9", "DM accepted by MeshCore/LoRa");
        return true;
    };

    bool accepted = false;
    if (decision.hasPrimary && decision.primary == V9Route::EspNowLR) {
        accepted = tryEspNow();
        if (!accepted) {
            v9_reach.noteFailure(V9Route::EspNowLR, millis());
            accepted = tryLoRa();
        }
    } else {
        accepted = tryLoRa();
        if (!accepted && espReachable) accepted = tryEspNow();
    }

    if (accepted) return true;

    const auto id = v9_ids.next();
    if (decision.allowStoreForward && v9_store.enqueue(id, pubKeyPrefix4, text, now, 10UL * 60UL * 1000UL)) {
        OPS_LOG("V9", "No route accepted message; queued for deferred delivery (%u/%u)",
                (unsigned)v9_store.count(), (unsigned)v9::V9StoreForward::CAPACITY);
        return true;
    }

    OPS_LOG("V9", "No route and deferred-send queue full");
    return false;
}'''

new_send = '''bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {
    if (!_initialized || !pubKeyPrefix4 || !text || !text[0]) return false;

    const uint32_t now = millis();
    uint8_t peerPub[32] = {};
    if (the_mesh.resolvePeerPubKey(pubKeyPrefix4, peerPub)) {
        v10_wifi.configurePeer(peerPub);
    }

    const bool espReachable = hybrid_transport.canReach(pubKeyPrefix4);
    const bool wifiReachable = v10_wifi.canReach(pubKeyPrefix4);

    v10_brain.setAvailable(v10::BEARER_LORA, true, now);
    v10_brain.setReachable(v10::BEARER_LORA, true, now);
    v10_brain.setAvailable(v10::BEARER_ESPNOW_LR, hybrid_transport.initialized(), now);
    v10_brain.setReachable(v10::BEARER_ESPNOW_LR, espReachable, now);
    v10_brain.setAvailable(v10::BEARER_WIFI_LR, v10_wifi.configured(), now);
    v10_brain.setReachable(v10::BEARER_WIFI_LR, wifiReachable, now);

    const v10::Decision decision = v10_brain.decide(now);
    OPS_LOG("V10", "DirectLink primary=%s backup=%s fec=%d frag=%u",
            decision.hasPrimary ? v10::DirectLinkBrain::name(decision.primary) : "none",
            decision.hasBackup ? v10::DirectLinkBrain::name(decision.backup) : "none",
            decision.useFec ? 1 : 0,
            (unsigned)decision.fragmentBytes);

    auto tryWifi = [&]() -> bool {
        if (!wifiReachable) return false;
        if (!v10_wifi.sendDirect(pubKeyPrefix4, text, (uint32_t)rtc_clock.getCurrentTime())) return false;
        the_mesh.clearLastExpectedAck();
        OPS_LOG("V10", "DM accepted by Wi-Fi LR Direct");
        return true;
    };

    auto tryEspNow = [&]() -> bool {
        if (!espReachable) return false;
        if (!hybrid_transport.sendDirect(pubKeyPrefix4, text, (uint32_t)rtc_clock.getCurrentTime())) return false;
        the_mesh.clearLastExpectedAck();
        OPS_LOG("V10", "DM accepted by ESP-NOW LR");
        return true;
    };

    auto tryLoRa = [&]() -> bool {
        if (!the_mesh.sendDirectMsg(pubKeyPrefix4, text)) return false;
        OPS_LOG("V10", "DM accepted by MeshCore/LoRa");
        return true;
    };

    bool accepted = false;
    if (decision.hasPrimary && decision.primary == v10::BEARER_WIFI_LR) {
        accepted = tryWifi();
        if (!accepted) v10_brain.noteFailure(v10::BEARER_WIFI_LR, millis());
    } else if (decision.hasPrimary && decision.primary == v10::BEARER_ESPNOW_LR) {
        accepted = tryEspNow();
        if (!accepted) v10_brain.noteFailure(v10::BEARER_ESPNOW_LR, millis());
    } else {
        accepted = tryLoRa();
    }

    if (!accepted && decision.hasBackup) {
        if (decision.backup == v10::BEARER_WIFI_LR) accepted = tryWifi();
        else if (decision.backup == v10::BEARER_ESPNOW_LR) accepted = tryEspNow();
        else if (decision.backup == v10::BEARER_LORA) accepted = tryLoRa();
    }

    if (!accepted) {
        accepted = tryLoRa();
        if (!accepted) accepted = tryEspNow();
        if (!accepted) accepted = tryWifi();
    }

    if (accepted) return true;

    const auto id = v9_ids.next();
    if (v9_store.enqueue(id, pubKeyPrefix4, text, now, 10UL * 60UL * 1000UL)) {
        OPS_LOG("V10", "No immediate direct route; queued bounded deferred delivery (%u/%u)",
                (unsigned)v9_store.count(), (unsigned)v9::V9StoreForward::CAPACITY);
        return true;
    }

    OPS_LOG("V10", "No route and deferred queue full");
    return false;
}'''

replace_once("src/mesh/MeshService.cpp", old_send, new_send)

replace_once(
    "src/mesh/MeshService.cpp",
    "    hybrid_transport.tick();\n"
    "    HybridRxMessage hmsg;\n",
    "    hybrid_transport.tick();\n"
    "    v10_scheduler.tick(millis());\n"
    "    v10_wifi.tick();\n"
    "    v10::WifiRxMessage wmsg;\n"
    "    int v10WifiDrain = 0;\n"
    "    while (v10WifiDrain++ < 4 && v10_wifi.dequeue(wmsg))\n"
    "        the_mesh.injectV10Message(wmsg.senderPubKey, \"V10 peer\", wmsg.timestamp, wmsg.text, \"WIFI-LR\");\n"
    "    v10::WifiFallback wfb;\n"
    "    if (v10_wifi.pollFallback(wfb)) {\n"
    "        v10_brain.noteFailure(v10::BEARER_WIFI_LR, millis());\n"
    "        OPS_LOG(\"V10\", \"Wi-Fi LR ACK timeout; falling back to LoRa\");\n"
    "        the_mesh.sendDirectMsg(wfb.destPrefix, wfb.text);\n"
    "    }\n"
    "    const v10::WifiStats& wstats = v10_wifi.stats();\n"
    "    if (wstats.ackRx != v10_lastWifiAckRx) {\n"
    "        v10_brain.noteSuccess(v10::BEARER_WIFI_LR, 250, millis());\n"
    "        v10_lastWifiAckRx = wstats.ackRx;\n"
    "    }\n"
    "    if (wstats.fallbacks != v10_lastWifiFallbacks) {\n"
    "        v10_brain.noteFailure(v10::BEARER_WIFI_LR, millis());\n"
    "        v10_lastWifiFallbacks = wstats.fallbacks;\n"
    "    }\n"
    "    HybridRxMessage hmsg;\n"
)

(root / "src/version.h").write_text('''#pragma once
#define OPS_VERSION_MAJOR 10
#define OPS_VERSION_MINOR 0
#define OPS_VERSION_PATCH 0
#define OPS_VERSION_PRE   ""
#define OPS_VERSION_STRING "10.0.0"
''')

print("V10 overlay applied successfully")
