#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys


def die(message: str) -> None:
    raise SystemExit("V11 contract test failed: " + message)


if len(sys.argv) != 2:
    die("usage: test_contracts.py <patched wadamesh checkout>")

root = pathlib.Path(sys.argv[1])
pio = (root / "platformio.ini").read_text()
myh = (root / "src/MyMesh.h").read_text()
mycpp = (root / "src/MyMesh.cpp").read_text()
main = (root / "src/main.cpp").read_text()
bridge_h = (root / "src/helpers/esp32/V11GlobalBridge.h").read_text()
bridge_cpp = (root / "src/helpers/esp32/V11GlobalBridge.cpp").read_text()

try:
    section = pio.split("[env:LilyGo_TDeck_companion_radio_touch]", 1)[1].split(
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]", 1
    )[0]
except IndexError:
    die("T-Deck PlatformIO section missing")

checks = [
    ("EU868 frequency preserved", "LORA_FREQ=869.618" in section),
    ("upstream bandwidth preserved", "LORA_BW=62.5" in section),
    ("upstream spreading factor preserved", "LORA_SF=8" in section),
    ("T-Deck power remains 22 dBm", "LORA_TX_POWER=22" in section),
    ("runtime power ceiling explicit", "MAX_LORA_TX_POWER=22" in section),
    ("SX1262 DIO2 RF switch remains enabled", "SX126X_DIO2_AS_RF_SWITCH=true" in section),
    ("RX boosted gain remains enabled", "SX126X_RX_BOOSTED_GAIN=1" in section),
    ("V11 build flag enabled", "MESH_OFFGRIDNL_V11=1" in section),
    ("shared secret hook declared", "v11CalcSharedSecret" in myh),
    ("global DM injection hook declared", "v11InjectGlobalDm" in myh),
    ("internet mirror attached to direct-message send", "v11_global_bridge.mirrorDM" in mycpp),
    ("LoRa/internet duplicate suppression attached", "v11_global_bridge.noteLoRaDM" in mycpp),
    ("bridge starts after Wi-Fi config load", main.find("wifiConfigBegin()") < main.find("v11_global_bridge.begin")),
    ("AES-GCM encrypt present", "mbedtls_gcm_crypt_and_tag" in bridge_cpp),
    ("AES-GCM authenticated decrypt present", "mbedtls_gcm_auth_decrypt" in bridge_cpp),
    ("global transport restricted to chat contacts", "recipient.type != ADV_TYPE_CHAT" in bridge_cpp),
    ("pending queue bounded", "PENDING_CAP = 8" in bridge_h),
    ("dedup cache bounded", "DEDUP_CAP = 32" in bridge_h),
]

failed = [name for name, ok in checks if not ok]
if failed:
    die("; ".join(failed))

# The V11 bridge must never publish the chat text buffer itself. The only publish
# path should send the encrypted binary wire buffer.
if re.search(r"_mqtt\.publish\([^;]*(?:text|safeText|json)", bridge_cpp, flags=re.S):
    die("possible plaintext publish path detected")

if "_mqtt.publish(\n        topic,\n        wire," not in bridge_cpp:
    die("encrypted wire publish path missing")

print("V11 contract tests passed")
for name, _ in checks:
    print(" -", name)
