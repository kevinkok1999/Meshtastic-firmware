#!/usr/bin/env python3
from __future__ import annotations

import pathlib
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

    # Existing hybrid DM behavior is intentionally retained while the V27
    # privacy transport replaces its network side in later patch stages.
    joined = "\n".join((main_path.read_text(), mesh_path.read_text(),
                         bridge_h.read_text(), bridge_cpp.read_text()))
    for marker in (
        "v11_global_bridge.mirrorDM",
        "v11_global_bridge.noteLoRaDM",
        "mbedtls_gcm_crypt_and_tag",
        "mbedtls_gcm_auth_decrypt",
    ):
        if marker not in joined:
            fail("hybrid baseline missing " + marker)

    print("V27 stage 1 applied: flags + privacy/P1-V8 compatibility gates; RF unchanged")

if __name__ == "__main__":
    main()
