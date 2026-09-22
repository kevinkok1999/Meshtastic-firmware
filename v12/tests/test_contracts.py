#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V12 contract test failed: " + message)


if len(sys.argv) != 2:
    fail("usage: test_contracts.py <patched wadamesh checkout>")

root = pathlib.Path(sys.argv[1])
pio = (root / "platformio.ini").read_text()
main = (root / "src/main.cpp").read_text()
mesh = (root / "src/MyMesh.cpp").read_text()

try:
    tdeck = pio.split("[env:LilyGo_TDeck_companion_radio_touch]", 1)[1].split(
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]", 1
    )[0]
except IndexError:
    fail("T-Deck PlatformIO section missing")

checks = [
    ("V11 stays enabled", "MESH_OFFGRIDNL_V11=1" in tdeck),
    ("V12 enabled", "MESH_OFFGRIDNL_V12=1" in tdeck),
    ("LoRa frequency unchanged", "LORA_FREQ=869.618" in tdeck),
    ("LoRa BW unchanged", "LORA_BW=62.5" in tdeck),
    ("LoRa SF unchanged", "LORA_SF=8" in tdeck),
    ("LoRa TX remains 22 dBm", "LORA_TX_POWER=22" in tdeck),
    ("hardware cap remains 22 dBm", "MAX_LORA_TX_POWER=22" in tdeck),
    ("DIO2 RF switch unchanged", "SX126X_DIO2_AS_RF_SWITCH=true" in tdeck),
    ("boosted RX unchanged", "SX126X_RX_BOOSTED_GAIN=1" in tdeck),
    ("NL country policy present", 'esp_wifi_set_country_code("NL", true)' in main),
    ("sleep disabled before association", "WiFi.setSleep(false);" in main),
    ("sleep restored after connection", "s_v12_wifi_sleep_enabled = true;" in main),
    ("new AP clears stale supplicant state", "WiFi.disconnect(false, true);" in main),
    ("auto reconnect preserved", "WiFi.setAutoReconnect(true);" in main),
    ("wrong password diagnostic", "handshake failed: check Wi-Fi password" in main),
    ("wrong-band/channel diagnostic", "network not found: check 2.4 GHz / channel" in main),
    ("security mismatch diagnostic", "incompatible Wi-Fi security" in main),
    ("V11 internet bridge preserved", "v11_global_bridge.mirrorDM" in mesh),
    ("V11 dual-route dedup preserved", "v11_global_bridge.noteLoRaDM" in mesh),
]

bad = [name for name, ok in checks if not ok]
if bad:
    fail("; ".join(bad))

# We expect exactly two station-mode hooks (setup + runtime) and four begin
# preflights in the pinned beta_83 source. Drift must be reviewed, not guessed.
if main.count("if (wifi_mode_ready) v12WifiPrepareSta();") != 2:
    fail("unexpected STA setup hook count")
if main.count("v12WifiPrepareAssociation();") != 4:
    fail("unexpected association preflight count")

print("V12 contract tests passed")
for name, _ in checks:
    print(" -", name)
