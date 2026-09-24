#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

def die(msg: str) -> None:
    raise SystemExit("V27 contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <V27-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    main_src = (root / "src/main.cpp").read_text()
    mesh_cpp = (root / "src/MyMesh.cpp").read_text()
    mesh_h = (root / "src/MyMesh.h").read_text()
    bridge_h = (root / "src/helpers/esp32/V11GlobalBridge.h").read_text()
    bridge_cpp = (root / "src/helpers/esp32/V11GlobalBridge.cpp").read_text()

    # V27 must remain a strict extension of the proven chain.
    for flag in (
        "MESH_OFFGRIDNL_V11=1",
        "MESH_OFFGRIDNL_V12=1",
        "MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1",
        "MESH_OFFGRIDNL_V16=1",
        "MESH_OFFGRIDNL_V17=1",
        "MESH_OFFGRIDNL_V18=1",
        "MESH_OFFGRIDNL_V19=1",
        "MESH_OFFGRIDNL_V26=1",
        "MESH_OFFGRIDNL_V27=1",
        "V27_PRIVACY_PRO=1",
        "V27_P1_V8_COMPAT=1",
        "V27_ZERO_CONFIG=1",
    ):
        if flag not in pio:
            die("missing " + flag)

    start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start < 0 or end < 0:
        die("T-Deck target section missing")
    tdeck = pio[start:end]

    # P1 Pro V8 radio contract. CR5 is the WadaMesh/MeshCore default used by
    # this pinned profile; V27 may not introduce an override.
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
            die("P1 V8/T-Deck RF contract missing " + marker)
    if "#define LORA_CR 5" not in mesh_h:
        die("P1 V8/T-Deck RF contract missing CR5 default")

    # V27 must not add a competing radio profile.
    forbidden_rf = (
        "V27_LORA_FREQ",
        "V27_LORA_BW",
        "V27_LORA_SF",
        "V27_LORA_CR",
        "V27_TX_POWER",
    )
    for marker in forbidden_rf:
        if marker in "\n".join((pio, main_src, mesh_h, mesh_cpp, bridge_h, bridge_cpp)):
            die("V27 must not override RF via " + marker)

    # Existing hybrid behavior must survive: one logical DM mirrors to Internet,
    # LoRa remains the compatibility path and duplicate UI delivery is suppressed.
    for marker in (
        "v11_global_bridge.mirrorDM",
        "v11_global_bridge.noteLoRaDM",
        "v11_global_bridge.begin",
        'STALL_SCOPE("v11-global"',
        "mbedtls_gcm_crypt_and_tag",
        "mbedtls_gcm_auth_decrypt",
    ):
        if marker not in "\n".join((main_src, mesh_cpp, bridge_h, bridge_cpp)):
            die("global/off-grid compatibility marker missing " + marker)

    # V26 RF guard remains untouched.
    for marker in (
        "V26_RF_EVAL_MS = 15000",
        "radio_driver.resetAGC();",
        "radio_driver.triggerNoiseFloorCalibrate(0);",
    ):
        if marker not in main_src:
            die("V26 RF guard missing " + marker)

    prefs = (root / "src/helpers/esp32/TouchPrefsStore.cpp").read_text()
    # Low-level MQTT remains hidden from the normal user surface by default.
    if "APPHIDE_MQTT" not in prefs or "app_hide" not in prefs:
        die("zero-config UX lost the hidden-by-default MQTT guard")
    # Do not silently opt users into open Wi-Fi auto-join.
    if "c.boot_wifi_open    = 0" not in prefs:
        die("zero-config privacy requires open Wi-Fi auto-join OFF by default")

    print("V27 contracts OK: V26 preserved, P1 Pro V8 preserved, hybrid chat preserved, zero-config privacy defaults preserved")

if __name__ == "__main__":
    main()
