#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: apply_v9.py <saitama_checkout> <overlay_dir>")

root = Path(sys.argv[1]).resolve()
overlay = Path(sys.argv[2]).resolve()

def replace_once(path, old, new):
    p = root / path
    s = p.read_text()
    if old not in s:
        raise SystemExit(f"V9 patch anchor missing in {path}: {old[:140]!r}")
    p.write_text(s.replace(old, new, 1))

dst = root / "src/mesh"
for name in [
    "V9ReachEngine.h", "V9ReachEngine.cpp",
    "V9DeliveryCore.h", "V9DeliveryCore.cpp",
    "V9StoreForward.h", "V9StoreForward.cpp",
]:
    shutil.copy2(overlay / name, dst / name)

replace_once(
    "src/mesh/MeshService.cpp",
    '#include "V8RouteCore.h"\n',
    '#include "V8RouteCore.h"\n#include "V9ReachEngine.h"\n#include "V9DeliveryCore.h"\n#include "V9StoreForward.h"\n'
)

replace_once(
    "src/mesh/MeshService.cpp",
    "static V8RouteCore                 v8_routes;\n",
    "static V8RouteCore                 v8_routes;\n"
    "static V9ReachEngine               v9_reach;\n"
    "static v9::MessageIdGenerator      v9_ids;\n"
    "static v9::V9StoreForward          v9_store;\n"
    "static uint32_t                    v9_lastHybridAckRx = 0;\n"
    "static uint32_t                    v9_lastHybridFallbacks = 0;\n"
)

replace_once(
    "src/mesh/MeshService.cpp",
    '    OPS_LOG("V8", "Adaptive route core ready; optional XBee/LR2021 adapters compiled separately");\n'
    '    OPS_LOG("Mesh", "MeshCore ready");',
    '    v9_reach.setAvailable(V9Route::LoRa, true, millis());\n'
    '    v9_reach.setReachable(V9Route::LoRa, true, millis());\n'
    '    v9_reach.setAvailable(V9Route::EspNowLR, hybrid_transport.initialized(), millis());\n'
    '    v9_reach.setAvailable(V9Route::XBeeXR868, false, millis());\n'
    '    v9_reach.setAvailable(V9Route::LR2021, false, millis());\n'
    '    v9_reach.setMode(V9Mode::RangeFirst);\n'
    '    uint8_t v9Prefix[4] = {};\n'
    '    the_mesh.getSelfPubKeyPrefix(v9Prefix);\n'
    '    uint64_t v9NodePart = 0;\n'
    '    memcpy(&v9NodePart, v9Prefix, sizeof(v9Prefix));\n'
    '    uint64_t v9Session = ((uint64_t)esp_random() << 32) | esp_random();\n'
    '    v9_ids.seed(v9NodePart, v9Session);\n'
    '    v9_lastHybridAckRx = hybrid_transport.stats().ackRx;\n'
    '    v9_lastHybridFallbacks = hybrid_transport.stats().fallbacks;\n'
    '    OPS_LOG("V9", "Reach Engine ready; RangeFirst active; bounded deferred-send queue=%u", (unsigned)v9::V9StoreForward::CAPACITY);\n'
    '    OPS_LOG("Mesh", "MeshCore ready");'
)

old_send = '''bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {
    if (!_initialized || !pubKeyPrefix4 || !text || !text[0]) return false;

    const uint32_t now = millis();
    const bool espReachable = hybrid_transport.canReach(pubKeyPrefix4);
    v8_routes.setReachable(V8Route::EspNowLR, espReachable, now);
    v8_routes.setReachable(V8Route::LoRa, true, now);

    const V8Route route = v8_routes.choose(now);
    OPS_LOG("V8", "DM route selected: %s", V8RouteCore::name(route));

    if (route == V8Route::EspNowLR && espReachable) {
        if (hybrid_transport.sendDirect(pubKeyPrefix4, text, (uint32_t)rtc_clock.getCurrentTime())) {
            the_mesh.clearLastExpectedAck();
            OPS_LOG("Hybrid", "V8 DM queued over ESP-NOW LR");
            return true;
        }
        v8_routes.noteFailure(V8Route::EspNowLR, millis());
        OPS_LOG("V8", "ESP-NOW enqueue failed; immediate LoRa fallback");
    }

    return the_mesh.sendDirectMsg(pubKeyPrefix4, text);
}'''

new_send = '''bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {
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

replace_once("src/mesh/MeshService.cpp", old_send, new_send)

old_tick = '''    HybridFallback fb;
    if (hybrid_transport.pollFallback(fb)) {
        v8_routes.noteFailure(V8Route::EspNowLR, millis());
        OPS_LOG("Hybrid", "ACK timeout; V8 falling back to MeshCore/LoRa");
        the_mesh.sendDirectMsg(fb.destPrefix, fb.text);
    }
    _tickFhss();'''

new_tick = '''    HybridFallback fb;
    if (hybrid_transport.pollFallback(fb)) {
        v8_routes.noteFailure(V8Route::EspNowLR, millis());
        v9_reach.noteFailure(V9Route::EspNowLR, millis());
        OPS_LOG("Hybrid", "ACK timeout; V9 falling back to MeshCore/LoRa");
        if (!the_mesh.sendDirectMsg(fb.destPrefix, fb.text)) {
            const auto id = v9_ids.next();
            if (v9_store.enqueue(id, fb.destPrefix, fb.text, millis(), 10UL * 60UL * 1000UL))
                OPS_LOG("V9", "Fallback route busy; message queued for deferred delivery");
        }
    }

    const HybridStats& v9Hs = hybrid_transport.stats();
    if (v9Hs.ackRx != v9_lastHybridAckRx) {
        v9_reach.noteSuccess(V9Route::EspNowLR, 250, millis());
        v9_lastHybridAckRx = v9Hs.ackRx;
    }
    if (v9Hs.fallbacks != v9_lastHybridFallbacks) {
        v9_reach.noteFailure(V9Route::EspNowLR, millis());
        v9_lastHybridFallbacks = v9Hs.fallbacks;
    }

    v9_store.purgeExpired(millis());
    const int v9Due = v9_store.nextDue(millis());
    if (v9Due >= 0) {
        v9::StoreItem* queued = v9_store.item((size_t)v9Due);
        if (queued) {
            bool queuedAccepted = false;
            const bool queuedEspReachable = hybrid_transport.canReach(queued->destination);
            if (queuedEspReachable) {
                queuedAccepted = hybrid_transport.sendDirect(
                    queued->destination, queued->text, (uint32_t)rtc_clock.getCurrentTime());
                if (queuedAccepted) the_mesh.clearLastExpectedAck();
            }
            if (!queuedAccepted) queuedAccepted = the_mesh.sendDirectMsg(queued->destination, queued->text);
            if (queuedAccepted) {
                OPS_LOG("V9", "Deferred message re-queued successfully");
                v9_store.markAccepted((size_t)v9Due);
            } else {
                v9_store.markFailed((size_t)v9Due, millis());
            }
        }
    }
    _tickFhss();'''

replace_once("src/mesh/MeshService.cpp", old_tick, new_tick)

(root / "src/version.h").write_text('''#pragma once
#define OPS_VERSION_MAJOR 9
#define OPS_VERSION_MINOR 0
#define OPS_VERSION_PATCH 0
#define OPS_VERSION_PRE   ""
#define OPS_VERSION_STRING "9.0.0"
''')

print("V9 overlay applied successfully")
