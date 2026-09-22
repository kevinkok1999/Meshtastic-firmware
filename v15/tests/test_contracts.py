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

    if "static const uint32_t WIFI_RETRY_INTERVAL_MS = 20000;" not in main_cpp:
        die("20 second V15 retry interval missing")
    if "WiFi.disconnect(false, false);" not in main_cpp:
        die("non-erasing reconnect missing")
    if "[V15][wifi] join selected AP once" not in main_cpp:
        die("one-shot selected AP path missing")
    if "wifiConfigClearApHint();" not in main_cpp:
        die("AP hint is not cleared after first use")
    if "WIFI_ALL_CHANNEL_SCAN" not in main_cpp:
        die("all-channel fallback missing")

    start = main_cpp.find("static void v13WifiBegin")
    end = main_cpp.find("#endif", start)
    if start < 0 or end < 0:
        die("association helper missing")
    helper = main_cpp[start:end]
    active_lines = [
        line.strip() for line in helper.splitlines()
        if line.strip() and not line.lstrip().startswith("//")
    ]
    if any(line.startswith("WiFi.setSleep(false)") for line in active_lines):
        die("association helper still forces WIFI_PS_NONE")

    if "WiFi.status() != WL_CONNECTED) wifiKickScan()" not in ui:
        die("connected-link scan guard lost")
    if "handshake timeout" not in ui:
        die("disconnect diagnostics lost")

    print("V15 contracts OK")


if __name__ == "__main__":
    main()
