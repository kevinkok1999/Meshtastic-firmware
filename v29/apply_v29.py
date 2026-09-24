#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys

def fail(msg: str) -> None:
    raise SystemExit("V29 patch failed: " + msg)

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected 1 anchor, found {count}")
    return text.replace(old, new, 1)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v29.py <V28-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent

    pio_path = root / "platformio.ini"
    mesh_h_path = root / "src/MyMesh.h"
    mesh_cpp_path = root / "src/MyMesh.cpp"
    main_path = root / "src/main.cpp"

    for p in (pio_path, mesh_h_path, mesh_cpp_path, main_path):
        if not p.exists():
            fail("missing " + str(p))

    # V29 is strictly additive on top of the validated V28 baseline.
    pio = pio_path.read_text()
    tdeck_start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    tdeck_end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", tdeck_start)
    if tdeck_start < 0 or tdeck_end < 0:
        fail("T-Deck env missing")
    block = pio[tdeck_start:tdeck_end]
    if "MESH_OFFGRIDNL_V28=1" not in block:
        fail("V28 baseline missing from T-Deck env")

    if "MESH_OFFGRIDNL_V29=1" not in block:
        block = replace_once(
            block,
            "  -D V28_BROWSER_CHAT_SHELL=1\n",
            "  -D V28_BROWSER_CHAT_SHELL=1\n"
            "  -D MESH_OFFGRIDNL_V29=1\n"
            "  -D V29_OFFLINE_CORE=1\n"
            "  -D V29_EMERGENCY_192H_TARGET=1\n"
            "  -D V29_MEMORY_TARGET_PERCENT=80\n"
            "  -D V29_SIMPLE_EMERGENCY_UX=1\n",
            "V29 T-Deck flags",
        )
        pio = pio[:tdeck_start] + block + pio[tdeck_end:]
        pio_path.write_text(pio)

    # Copy the V29 emergency fabric. It has no HTTP/MQTT dependency.
    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # Narrow MyMesh surface: raw V29 emergency envelope TX only.
    mesh_h = mesh_h_path.read_text()
    h_anchor = """#if defined(MESH_OFFGRIDNL_V28)
  bool v28CalcSharedSecretAny(const uint8_t peerPub[32], uint8_t out[32]);
#endif
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
"""
    h_new = """#if defined(MESH_OFFGRIDNL_V28)
  bool v28CalcSharedSecretAny(const uint8_t peerPub[32], uint8_t out[32]);
#endif
#if defined(MESH_OFFGRIDNL_V29)
  bool v29SendEmergencyRaw(const uint8_t* data, size_t len);
#endif
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
"""
    if "v29SendEmergencyRaw" not in mesh_h:
        mesh_h = replace_once(mesh_h, h_anchor, h_new, "V29 MyMesh header hook")
        mesh_h_path.write_text(mesh_h)

    mesh_cpp = mesh_cpp_path.read_text()
    include_anchor = """#if defined(MESH_OFFGRIDNL_V11)
#include "helpers/esp32/V11GlobalBridge.h"
#endif
"""
    include_new = include_anchor + """#if defined(MESH_OFFGRIDNL_V29)
#include "helpers/esp32/V29EmergencyFabric.h"
#endif
"""
    if "V29EmergencyFabric.h" not in mesh_cpp:
        mesh_cpp = replace_once(mesh_cpp, include_anchor, include_new, "V29 MyMesh include")

    helper_anchor = """bool MyMesh::v27GetChannelByIndex(uint8_t idx, ChannelDetails& out) {
"""
    helper_code = """#if defined(MESH_OFFGRIDNL_V29)
bool MyMesh::v29SendEmergencyRaw(const uint8_t* data, size_t len) {
  if (!data || len == 0 || len > MAX_PACKET_PAYLOAD) return false;
  mesh::Packet* pkt = createRawData(data, len);
  if (!pkt) return false;
  sendFlood(pkt, 0, floodPathHashSize());
  return true;
}
#endif

""" + helper_anchor
    if "bool MyMesh::v29SendEmergencyRaw" not in mesh_cpp:
        mesh_cpp = replace_once(mesh_cpp, helper_anchor, helper_code,
                                "V29 raw emergency sender")

    raw_anchor = """void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
"""
    raw_new = """void MyMesh::onRawDataRecv(mesh::Packet *packet) {
#if defined(MESH_OFFGRIDNL_V29)
  if (packet && v29_emergency_fabric.onRawFrame(packet->payload, packet->payload_len)) {
    return;
  }
#endif
  if (packet->payload_len + 4 > sizeof(out_frame)) {
"""
    if "v29_emergency_fabric.onRawFrame" not in mesh_cpp:
        mesh_cpp = replace_once(mesh_cpp, raw_anchor, raw_new,
                                "V29 raw emergency receive hook")

    mesh_cpp_path.write_text(mesh_cpp)

    # Lifecycle: initialize after normal storage/Wi-Fi config has initialized,
    # but V29 itself never requires Wi-Fi or Internet.
    main_cpp = main_path.read_text()
    main_include_anchor = """    #if defined(MESH_OFFGRIDNL_V11)
      #include "helpers/esp32/V11GlobalBridge.h"
    #endif
"""
    main_include_new = main_include_anchor + """    #if defined(MESH_OFFGRIDNL_V29)
      #include "helpers/esp32/V29EmergencyFabric.h"
    #endif
"""
    if "V29EmergencyFabric.h" not in main_cpp:
        main_cpp = replace_once(main_cpp, main_include_anchor, main_include_new,
                                "V29 main include")

    begin_anchor = """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)
  v11_global_bridge.begin(&the_mesh);
#endif
"""
    begin_new = begin_anchor + """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)
  v29_emergency_fabric.begin(&the_mesh);
#endif
"""
    if "v29_emergency_fabric.begin(&the_mesh)" not in main_cpp:
        main_cpp = replace_once(main_cpp, begin_anchor, begin_new, "V29 main begin")

    loop_anchor = """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)
#ifdef DISPLAY_CLASS
  STALL_SCOPE("v11-global", v11_global_bridge.loop());
#else
  v11_global_bridge.loop();
#endif
#endif
"""
    loop_new = loop_anchor + """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)
#ifdef DISPLAY_CLASS
  STALL_SCOPE("v29-emergency", v29_emergency_fabric.loop());
#else
  v29_emergency_fabric.loop();
#endif
#endif
"""
    if "v29_emergency_fabric.loop()" not in main_cpp:
        main_cpp = replace_once(main_cpp, loop_anchor, loop_new, "V29 main loop")

    main_path.write_text(main_cpp)

    joined = "\n".join((
        pio_path.read_text(),
        mesh_h_path.read_text(),
        mesh_cpp_path.read_text(),
        main_path.read_text(),
        (root / "src/helpers/esp32/V29EmergencyFabric.h").read_text(),
        (root / "src/helpers/esp32/V29EmergencyFabric.cpp").read_text(),
    ))

    for marker in (
        "MESH_OFFGRIDNL_V29=1",
        "V29_OFFLINE_CORE=1",
        "V29_MEMORY_TARGET_PERCENT=80",
        "v29SendEmergencyRaw",
        "v29_emergency_fabric.onRawFrame",
        "v29_emergency_fabric.begin(&the_mesh)",
        "v29_emergency_fabric.loop()",
        "MOG29-DIRECT-V1",
        "v29q0.bin",
        "v29q1.bin",
    ):
        if marker not in joined:
            fail("missing V29 marker " + marker)

    # The V29 emergency core must stay genuinely off-grid.
    fabric = (root / "src/helpers/esp32/V29EmergencyFabric.cpp").read_text()
    for forbidden in ("HTTPClient", "WiFiClientSecure", "PubSubClient", "V28_RELAY_URL"):
        if forbidden in fabric:
            fail("offline core unexpectedly depends on " + forbidden)

    print("V29 applied: offline emergency fabric + 80/20 memory governor + V28/P1 compatibility preserved")

if __name__ == "__main__":
    main()
