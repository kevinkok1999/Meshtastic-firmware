#!/usr/bin/env python3
from __future__ import annotations
import pathlib
import shutil
import sys

WADAMESH_SHA = "6fe6b9f06332708b6ed0cc78958017d0dcc9e35d"
MESHCORE_SHA = "edd7ed47f42acf6c64c9fcac551050fdbd531a09"

def fail(message: str) -> None:
    raise SystemExit("V30 patch failed: " + message)

def once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v30.py <V11-through-V29 WadaMesh checkout>")
    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent
    required = [root / "platformio.ini", root / "src/MyMesh.h", root / "src/MyMesh.cpp", root / "src/main.cpp"]
    for path in required:
        if not path.exists(): fail("missing " + str(path))
    pio_path, mesh_h_path, mesh_cpp_path = required[0], required[1], required[2]
    pio = pio_path.read_text(encoding="utf-8")
    start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start < 0 or end < 0: fail("T-Deck environment missing")
    block = pio[start:end]
    if "MESH_OFFGRIDNL_V29=1" not in block: fail("V29 baseline must be applied first")
    if "MESH_OFFGRIDNL_V30=1" not in block:
        block = once(block, "  -D MESH_OFFGRIDNL_V29=1\n", "  -D MESH_OFFGRIDNL_V29=1\n  -D MESH_OFFGRIDNL_V30=1\n", "V30 build flag")
        pio_path.write_text(pio[:start] + block + pio[end:], encoding="utf-8")
    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_file():
            dst = root / src.relative_to(overlay)
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
    mesh_cpp = mesh_cpp_path.read_text(encoding="utf-8")
    include = '#include "helpers/esp32/V29EmergencyFabric.h"\n'
    if "V30EmergencyTransport.h" not in mesh_cpp:
        mesh_cpp = once(mesh_cpp, include, include + '#include "helpers/esp32/V30EmergencyTransport.h"\n', "V30 transport include")
    old = """bool MyMesh::v29SendEmergencyRaw(const uint8_t* data, size_t len) {\n  if (!data || len == 0 || len > MAX_PACKET_PAYLOAD) return false;\n  mesh::Packet* pkt = createRawData(data, len);\n  if (!pkt) return false;\n  sendFlood(pkt, 0, floodPathHashSize());\n  return true;\n}\n"""
    new = """bool MyMesh::v29SendEmergencyRaw(const uint8_t* data, size_t len) {\n#if defined(MESH_OFFGRIDNL_V30)\n  static uint32_t next_attempt = 0;\n  const uint64_t cookie = ++next_attempt;\n  const auto admission = v30TryEnqueueEmergencyRaw(data, len, cookie, 2);\n  if (admission != TxAdmission::Accepted) return false;\n  v30::V30RawFrame frame{};\n  if (!v30::v30TakeEmergencyRaw(frame)) return false;\n  mesh::Packet* pkt = createRawData(frame.data, frame.len);\n  if (!pkt) return false;\n  // Emergency RAW is direct/zero-hop only. Enqueue is not TX completion; the\n  // dispatcher completion hook must call onV30TxResult(cookie, ...).\n  sendZeroHop(pkt, 0);\n  return true;\n#else\n  if (!data || len == 0 || len > MAX_PACKET_PAYLOAD) return false;\n  mesh::Packet* pkt = createRawData(data, len);\n  if (!pkt) return false;\n  sendFlood(pkt, 0, floodPathHashSize());\n  return true;\n#endif\n}\n"""
    if "v30TryEnqueueEmergencyRaw" not in mesh_cpp:
        mesh_cpp = once(mesh_cpp, old, new, "V30 emergency sender")
    mesh_cpp_path.write_text(mesh_cpp, encoding="utf-8")
    # The receive hook is additive. V30 magic is terminally owned; unrelated
    # native RAW still reaches the existing V29/serial handler.
    raw_anchor = "void MyMesh::onRawDataRecv(mesh::Packet *packet) {\n"
    raw_new = """void MyMesh::onRawDataRecv(mesh::Packet *packet) {\n#if defined(MESH_OFFGRIDNL_V30)\n  if (packet) {\n    const auto decision = v30::v30ParseRaw(packet->payload, packet->payload_len,\n                                            packet->isRouteDirect(), nullptr, nullptr);\n    if (decision != v30::RawDecision::Unrelated) return;\n  }\n#endif\n"""
    if "v30::v30ParseRaw" not in mesh_cpp:
        mesh_cpp = once(mesh_cpp, raw_anchor, raw_new, "V30 raw parser gate")
    mesh_cpp_path.write_text(mesh_cpp, encoding="utf-8")
    print("V30 overlay applied: WadaMesh=" + WADAMESH_SHA + " MeshCore=" + MESHCORE_SHA)

if __name__ == "__main__": main()
