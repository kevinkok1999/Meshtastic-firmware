#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V13 contract test failed: " + message)


if len(sys.argv) != 2:
    fail("usage: test_contracts.py <patched wadamesh checkout>")

root = pathlib.Path(sys.argv[1])
pio = (root / "platformio.ini").read_text()
main = (root / "src/main.cpp").read_text()
ui = (root / "src/ui-touch/UITask.cpp").read_text()
wifi_h = (root / "src/helpers/esp32/WifiRuntimeStore.h").read_text()
wifi_cpp = (root / "src/helpers/esp32/WifiRuntimeStore.cpp").read_text()
mesh = (root / "src/MyMesh.cpp").read_text()

try:
    tdeck = pio.split("[env:LilyGo_TDeck_companion_radio_touch]", 1)[1].split(
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]", 1
    )[0]
except IndexError:
    fail("T-Deck PlatformIO section missing")

checks = [
    ("V11 preserved", "MESH_OFFGRIDNL_V11=1" in tdeck),
    ("V12 preserved", "MESH_OFFGRIDNL_V12=1" in tdeck),
    ("V13 enabled", "MESH_OFFGRIDNL_V13=1" in tdeck),
    ("global encrypted DM preserved", "v11_global_bridge.mirrorDM" in mesh),
    ("V12 direct list scrolling preserved", "wifiNetworkListViewportHeight" in ui),
    ("scan metadata stores channel", "s_wifiscan_channels" in ui),
    ("scan metadata stores BSSID", "s_wifiscan_bssids" in ui),
    ("scan metadata stores auth", "s_wifiscan_auth" in ui),
    ("selected AP hint API declared", "wifiConfigSetApHint" in wifi_h),
    ("selected AP hint implemented", "V13WifiApHint" in wifi_cpp),
    ("connection manager present", "static void v13WifiBegin" in main),
    ("association stays awake", "WiFi.setSleep(false)" in main),
    ("all-channel fallback enabled", "WIFI_ALL_CHANNEL_SCAN" in main),
    ("single reconnect owner", "WiFi.setAutoReconnect(false);  // V13: one reconnect owner" in main),
    ("retry cadence is 15s", "WIFI_RETRY_INTERVAL_MS = 15000" in main),
    ("retry does not erase AP", "WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry" in main),
    ("settings do not scan over healthy link", "wifiConfigWantsWifi() && WiFi.status() != WL_CONNECTED" in ui),
    ("disconnect reason visible", "handshake timeout" in ui),
]

bad = [name for name, ok in checks if not ok]
if bad:
    fail("; ".join(bad))

# V13 must leave LoRa/radio policy untouched.
for marker in (
    "LORA_FREQ=869.618",
    "LORA_BW=62.5",
    "LORA_SF=8",
    "LORA_TX_POWER=22",
    "SX126X_DIO2_AS_RF_SWITCH=true",
):
    if marker not in tdeck:
        fail("radio invariant missing: " + marker)

# There must be exactly one V13 manager and one AP-hint implementation.
if main.count("static void v13WifiBegin") != 1:
    fail("unexpected v13WifiBegin count")
if wifi_cpp.count("struct V13WifiApHint") != 1:
    fail("unexpected V13WifiApHint count")

# The V13 branch must not introduce 5 GHz claims: ESP32-S3 remains 2.4 GHz.
if "5 GHz support" in main or "5GHz" in main:
    fail("unexpected 5 GHz implementation claim")

print("V13 contract tests passed")
for name, _ in checks:
    print(" -", name)
