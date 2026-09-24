#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys

def fail(msg: str) -> None:
    raise SystemExit("V27 patch failed: " + msg)

def section(source: str, heading: str, next_heading: str, old: str, new: str, label: str) -> str:
    start = source.find(heading)
    if start < 0:
        fail(label + ": section missing")
    end = source.find(next_heading, start + len(heading))
    if end < 0:
        end = len(source)
    chunk = source[start:end]
    count = chunk.count(old)
    if count != 1:
        fail(f"{label}: expected 1 anchor, found {count}")
    chunk = chunk.replace(old, new, 1)
    return source[:start] + chunk + source[end:]

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v27.py <V26-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio_path = root / "platformio.ini"
    main_path = root / "src/main.cpp"
    mesh_path = root / "src/MyMesh.cpp"
    mesh_h_path = root / "src/MyMesh.h"
    bridge_h = root / "src/helpers/esp32/V11GlobalBridge.h"
    bridge_cpp = root / "src/helpers/esp32/V11GlobalBridge.cpp"

    for p in (pio_path, main_path, mesh_path, mesh_h_path, bridge_h, bridge_cpp):
        if not p.exists():
            fail("missing " + str(p))

    pio = pio_path.read_text()
    for marker in ("MESH_OFFGRIDNL_V19=1", "MESH_OFFGRIDNL_V26=1"):
        if marker not in pio:
            fail("required baseline missing " + marker)

    # V27 is T-Deck-only and additive. These flags gate privacy/global work
    # without changing the radio waveform used by P1 Pro V8.
    pio = section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V26=1\n",
        "  -D MESH_OFFGRIDNL_V26=1\n"
        "  -D MESH_OFFGRIDNL_V27=1\n"
        "  -D V27_PRIVACY_PRO=1\n"
        "  -D V27_P1_V8_COMPAT=1\n"
        "  -D V27_ZERO_CONFIG=1\n",
        "V27 T-Deck flags",
    )
    pio_path.write_text(pio)

    # Fail closed if the known P1-V8-compatible RF profile drifted before V27.
    tdeck_start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    tdeck_end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", tdeck_start)
    tdeck = pio[tdeck_start:tdeck_end]
    for marker in (
        "LORA_FREQ=869.618",
        "LORA_BW=62.5",
        "LORA_SF=8",
        "LORA_TX_POWER=22",
        "MAX_LORA_TX_POWER=22",
        "SX126X_RX_BOOSTED_GAIN=1",
        "SX126X_DIO2_AS_RF_SWITCH=true",
    ):
        if marker not in tdeck:
            fail("P1 Pro V8 compatibility marker missing " + marker)

    mesh_h_text = mesh_h_path.read_text()
    if "#define LORA_CR 5" not in mesh_h_text:
        fail("P1 Pro V8 compatibility marker missing #define LORA_CR 5")

    # Extend only the narrow V11 contact hook surface needed by the Privacy Pro
    # envelope. 8-byte public-key prefixes are used solely as a local lookup
    # hint; the full key stays inside authenticated ciphertext on the Internet.
    old_h = """  bool v11LookupChatContact(const uint8_t pub[32], ContactInfo& out);
  void v11InjectGlobalDm(const uint8_t senderPub[32], uint32_t timestamp, const char* text);
"""
    new_h = """  bool v11LookupChatContact(const uint8_t pub[32], ContactInfo& out);
#if defined(MESH_OFFGRIDNL_V27)
  bool v27LookupChatContactByPrefix(const uint8_t prefix[8], ContactInfo& out);
#endif
  void v11InjectGlobalDm(const uint8_t senderPub[32], uint32_t timestamp, const char* text);
"""
    if mesh_h_text.count(old_h) != 1:
        fail("MyMesh V27 contact hook anchor drifted")
    mesh_h_text = mesh_h_text.replace(old_h, new_h, 1)
    mesh_h_path.write_text(mesh_h_text)

    mesh_cpp_text = mesh_path.read_text()
    old_cpp = """bool MyMesh::v11LookupChatContact(const uint8_t pub[32], ContactInfo& out) {
  if (!pub) return false;
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (!c || c->type != ADV_TYPE_CHAT) return false;
  out = *c;
  return true;
}

void MyMesh::v11InjectGlobalDm"""
    new_cpp = """bool MyMesh::v11LookupChatContact(const uint8_t pub[32], ContactInfo& out) {
  if (!pub) return false;
  ContactInfo* c = lookupContactByPubKey(pub, PUB_KEY_SIZE);
  if (!c || c->type != ADV_TYPE_CHAT) return false;
  out = *c;
  return true;
}

#if defined(MESH_OFFGRIDNL_V27)
bool MyMesh::v27LookupChatContactByPrefix(const uint8_t prefix[8], ContactInfo& out) {
  if (!prefix) return false;
  ContactInfo* c = lookupContactByPubKey(prefix, 8);
  if (!c || c->type != ADV_TYPE_CHAT) return false;
  if (memcmp(c->id.pub_key, prefix, 8) != 0) return false;
  out = *c;
  return true;
}
#endif

void MyMesh::v11InjectGlobalDm"""
    if mesh_cpp_text.count(old_cpp) != 1:
        fail("MyMesh V27 contact lookup anchor drifted")
    mesh_cpp_text = mesh_cpp_text.replace(old_cpp, new_cpp, 1)
    mesh_path.write_text(mesh_cpp_text)

    # Replace only the Internet bridge implementation. MyMesh chat, V26 RF and
    # the P1 V8-compatible radio path remain untouched.
    here = pathlib.Path(__file__).resolve().parent
    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # Existing hybrid DM behavior remains, but its network-side wire format is
    # now V27 Privacy Pro.
    joined = "\n".join((main_path.read_text(), mesh_path.read_text(),
                         mesh_h_path.read_text(), bridge_h.read_text(), bridge_cpp.read_text()))
    for marker in (
        "v11_global_bridge.mirrorDM",
        "v11_global_bridge.noteLoRaDM",
        "mbedtls_gcm_crypt_and_tag",
        "mbedtls_gcm_auth_decrypt",
        "MOG27-DM-KEY",
        "PLAIN_LEN = 32 + 4 + 2 + MAX_TEXT",
        "v27LookupChatContactByPrefix",
    ):
        if marker not in joined:
            fail("hybrid/privacy baseline missing " + marker)

    print("V27 applied: zero-config Privacy Pro global DM + V26/P1-V8 RF compatibility preserved")

if __name__ == "__main__":
    main()
