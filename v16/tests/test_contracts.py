#!/usr/bin/env python3
from __future__ import annotations
import pathlib
import sys

def die(message: str) -> None:
    raise SystemExit("V16 contract failure: " + message)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <patched checkout>")
    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    main_cpp = (root / "src/main.cpp").read_text()
    ui = (root / "src/ui-touch/UITask.cpp").read_text()

    for flag in (
        "MESH_OFFGRIDNL_V11=1",
        "MESH_OFFGRIDNL_V12=1",
        "MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1",
        "MESH_OFFGRIDNL_V16=1",
    ):
        if flag not in pio:
            die("missing " + flag)

    required_main = [
        "g_v16_wifi_join_in_progress",
        "g_v16_wifi_phase",
        "g_v16_wifi_attempt",
        "wifiConfigClearApHint();",
        "WIFI_RETRY_INTERVAL_MS = 12000",
        "WIFI_RETRY_BACKOFF_MS = 60000",
        "[V16][wifi] attempt %u: selected AP once",
        "[V16][wifi] attempt %u: all-channel",
        "ARDUINO_EVENT_WIFI_STA_CONNECTED",
    ]
    for marker in required_main:
        if marker not in main_cpp:
            die("main.cpp missing " + marker)

    required_ui = [
        "CONNECT always outranks SCAN",
        "never start a scan under WPA/DHCP",
        "worker owns this scan: safe abort point",
        "esp_wifi_scan_stop();",
        "V16: exactly one reconnect owner, even after scans",
        "retrying in background",
        "associating... (%u/3)",
        "refreshStatusLabels();",
    ]
    for marker in required_ui:
        if marker not in ui:
            die("UITask.cpp missing " + marker)

    start = main_cpp.find("static void v13WifiBegin")
    end = main_cpp.find("#endif", start)
    if start < 0 or end < 0:
        die("association helper missing")
    helper = main_cpp[start:end]
    if "g_wifi_last_disc_reason = 0;" in helper:
        die("retry helper still destroys the previous disconnect reason")

    join_start = ui.find("static void wifiDoJoin")
    join_end = ui.find("static void wifiJoinConfirmCb", join_start)
    if join_start < 0 or join_end < 0:
        die("wifiDoJoin block missing")
    join = ui[join_start:join_end]
    if "wifiRebuildNetworkList();" not in join:
        die("V15 fallback branch missing")
    if "MESH_OFFGRIDNL_V16" not in join or "refreshStatusLabels();" not in join:
        die("V16 post-join scan suppression missing")

    print("V16 contracts OK")

if __name__ == "__main__":
    main()
