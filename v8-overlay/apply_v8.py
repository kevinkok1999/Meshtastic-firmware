#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: apply_v8.py <saitama_checkout> <overlay_dir>")

root = Path(sys.argv[1]).resolve()
overlay = Path(sys.argv[2]).resolve()

def replace_once(path, old, new):
    p = root / path
    s = p.read_text()
    if old not in s:
        raise SystemExit(f"V8 patch anchor missing in {path}: {old[:120]!r}")
    p.write_text(s.replace(old, new, 1))

dst = root / "src/mesh"
for name in [
    "V8RouteCore.h", "V8RouteCore.cpp",
    "XBeeApiCodec.h", "XBeeApiCodec.cpp",
    "XBeeXr868Link.h", "XBeeXr868Link.cpp",
    "LR2021Adapter.h", "LR2021Adapter.cpp",
]:
    shutil.copy2(overlay / name, dst / name)

replace_once(
    "src/mesh/HybridTransport.h",
    "    bool sendDirect(const uint8_t destPrefix[4], const char* text, uint32_t timestamp);\n",
    "    bool sendDirect(const uint8_t destPrefix[4], const char* text, uint32_t timestamp);\n"
    "    bool canReach(const uint8_t destPrefix[4]);\n"
)

replace_once(
    "src/mesh/HybridTransport.cpp",
    "void HybridTransport::processData(const RawFrame& f) {\n",
    "bool HybridTransport::canReach(const uint8_t destPrefix[4]) {\n"
    "    return _initialized && destPrefix && findPeerByPrefix(destPrefix) != nullptr;\n"
    "}\n\n"
    "void HybridTransport::processData(const RawFrame& f) {\n"
)

replace_once(
    "src/mesh/MeshService.cpp",
    '#include "HybridTransport.h"\n',
    '#include "HybridTransport.h"\n#include "V8RouteCore.h"\n'
)

replace_once(
    "src/mesh/MeshService.cpp",
    "static HybridTransport              hybrid_transport;\n",
    "static HybridTransport              hybrid_transport;\n"
    "static V8RouteCore                 v8_routes;\n"
)

replace_once(
    "src/mesh/MeshService.cpp",
    '    OPS_LOG("Mesh", "MeshCore ready");',
    '    v8_routes.setAvailable(V8Route::LoRa, true, millis());\n'
    '    v8_routes.setReachable(V8Route::LoRa, true, millis());\n'
    '    v8_routes.setAvailable(V8Route::EspNowLR, hybrid_transport.initialized(), millis());\n'
    '    v8_routes.setAvailable(V8Route::XBeeXR868, false, millis());\n'
    '    v8_routes.setAvailable(V8Route::LR2021, false, millis());\n'
    '    v8_routes.setMode(V8RouteMode::AutoBalanced);\n'
    '    OPS_LOG("V8", "Adaptive route core ready; optional XBee/LR2021 adapters compiled separately");\n'
    '    OPS_LOG("Mesh", "MeshCore ready");'
)

old_send = '''bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {
    if (!_initialized) return false;
    if (hybrid_transport.sendDirect(pubKeyPrefix4, text, (uint32_t)rtc_clock.getCurrentTime())) {
        the_mesh.clearLastExpectedAck();
        OPS_LOG("Hybrid", "DM queued over ESP-NOW LR");
        return true;
    }
    return the_mesh.sendDirectMsg(pubKeyPrefix4, text);
}'''

new_send = '''bool MeshService::sendDirect(const uint8_t* pubKeyPrefix4, const char* text) {
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

replace_once("src/mesh/MeshService.cpp", old_send, new_send)

replace_once(
    "src/mesh/MeshService.cpp",
    '        OPS_LOG("Hybrid", "ACK timeout; falling back to MeshCore/LoRa");\n'
    '        the_mesh.sendDirectMsg(fb.destPrefix, fb.text);',
    '        v8_routes.noteFailure(V8Route::EspNowLR, millis());\n'
    '        OPS_LOG("Hybrid", "ACK timeout; V8 falling back to MeshCore/LoRa");\n'
    '        the_mesh.sendDirectMsg(fb.destPrefix, fb.text);'
)

(root / "src/version.h").write_text('''#pragma once
#define OPS_VERSION_MAJOR 8
#define OPS_VERSION_MINOR 0
#define OPS_VERSION_PATCH 0
#define OPS_VERSION_PRE   ""
#define OPS_VERSION_STRING "8.0.0"
''')

print("V8 overlay applied successfully")
