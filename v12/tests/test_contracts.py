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
ui = (root / "src/ui-touch/UITask.cpp").read_text()
wifi_store = (root / "src/helpers/esp32/TouchPrefsStore.h").read_text()

try:
    tdeck = pio.split("[env:LilyGo_TDeck_companion_radio_touch]", 1)[1].split(
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]", 1
    )[0]
except IndexError:
    fail("T-Deck PlatformIO section missing")

checks = [
    ("V11 global transport stays enabled", "MESH_OFFGRIDNL_V11=1" in tdeck),
    ("V12 enabled", "MESH_OFFGRIDNL_V12=1" in tdeck),
    ("LoRa frequency unchanged", "LORA_FREQ=869.618" in tdeck),
    ("LoRa BW unchanged", "LORA_BW=62.5" in tdeck),
    ("LoRa SF unchanged", "LORA_SF=8" in tdeck),
    ("LoRa TX remains 22 dBm", "LORA_TX_POWER=22" in tdeck),
    ("hardware cap remains 22 dBm", "MAX_LORA_TX_POWER=22" in tdeck),
    ("DIO2 RF switch unchanged", "SX126X_DIO2_AS_RF_SWITCH=true" in tdeck),
    ("boosted RX unchanged", "SX126X_RX_BOOSTED_GAIN=1" in tdeck),
    ("global DM mirror preserved", "v11_global_bridge.mirrorDM" in mesh),
    ("LoRa/internet dedup preserved", "v11_global_bridge.noteLoRaDM" in mesh),
    ("global bridge lifecycle preserved", "v11_global_bridge.begin" in main),
    ("base known-network capacity preserved", "TOUCH_WIFI_NET_COUNT = 8" in wifi_store),
    ("base known-network API preserved", "touchPrefsConnectWifiNet" in wifi_store),
    ("base scan path preserved", "wifiKickScan()" in ui),
    ("base saved-network list preserved", 'wifiListHeader("Saved networks")' in ui),
    ("base other-network list preserved", '"Other networks"' in ui),
    ("V12 list viewport fix present", "wifiNetworkListViewportHeight" in ui),
    ("V12 list is directly scrollable", "lv_obj_add_flag(s_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE)" in ui),
    ("V12 list has vertical scroll", "lv_obj_set_scroll_dir(s_wifi_list_cont, LV_DIR_VER)" in ui),
]

bad = [name for name, ok in checks if not ok]
if bad:
    fail("; ".join(bad))

# V12 must not fork the WadaMesh Wi-Fi association state machine. The ONLY V12
# Wi-Fi-specific change is the network-list touch/scroll viewport.
for marker in (
    "v12WifiPrepareSta",
    "v12WifiPrepareAssociation",
    "v12WifiReasonBrief",
    'esp_wifi_set_country_code("NL", true)',
    "[V12][wifi]",
):
    if marker in main:
        fail(f"unexpected Wi-Fi-hardening override remains: {marker}")

# Keep the UI patch narrow and unambiguous.
if ui.count("static lv_coord_t wifiNetworkListViewportHeight()") != 1:
    fail("unexpected Wi-Fi viewport helper count")
if ui.count("lv_obj_add_flag(s_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE)") != 1:
    fail("unexpected direct-scroll enable count")

print("V12 contract tests passed")
for name, _ in checks:
    print(" -", name)
