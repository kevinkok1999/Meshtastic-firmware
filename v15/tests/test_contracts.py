#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def die(msg: str) -> None:
    raise SystemExit("V15 contract failure: " + msg)


def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <patched checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    main_cpp = (root / "src/main.cpp").read_text()
    ui = (root / "src/ui-touch/UITask.cpp").read_text()

    for flag in ("MESH_OFFGRIDNL_V11=1", "MESH_OFFGRIDNL_V12=1",
                 "MESH_OFFGRIDNL_V13=1", "MESH_OFFGRIDNL_V15=1"):
        if flag not in pio:
            die("missing " + flag)

    # V13 behaviour must remain exactly present.
    required_v13 = [
        "static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;",
        "WiFi.disconnect(false, false);",
        "[V13][wifi] join selected AP",
        "[V13][wifi] join ssid=",
        "WIFI_ALL_CHANNEL_SCAN",
        "WiFi.status() != WL_CONNECTED) wifiKickScan()",
        "handshake timeout",
    ]
    combined = main_cpp + "\n" + ui
    for marker in required_v13:
        if marker not in combined:
            die("V13 behaviour changed unexpectedly: missing " + marker)

    start = main_cpp.find("static void v13WifiBegin")
    end = main_cpp.find("#endif", start)
    if start < 0 or end < 0:
        die("V13 association helper missing")
    helper = main_cpp[start:end]

    active_lines = [
        line.strip() for line in helper.splitlines()
        if line.strip() and not line.lstrip().startswith("//")
    ]
    if any(line.startswith("WiFi.setSleep(false)") for line in active_lines):
        die("association helper still forces WIFI_PS_NONE")

    # Extra behavioural changes are explicitly forbidden for this release.
    if "[V15][wifi] join selected AP once" in main_cpp:
        die("unexpected BSSID one-shot change present")
    if "WIFI_RETRY_INTERVAL_MS = 20000" in main_cpp:
        die("unexpected retry interval change present")

    print("V15 contracts OK: V13 preserved; only hotspot crash power-management override removed")


if __name__ == "__main__":
    main()
