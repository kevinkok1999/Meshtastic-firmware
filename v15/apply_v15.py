#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V15 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_section(text: str, header: str, next_header: str,
                  old: str, new: str, label: str) -> str:
    start = text.find(header)
    if start < 0:
        fail(f"{label}: section start missing")
    end = text.find(next_header, start + len(header))
    if end < 0:
        end = len(text)
    section = text[start:end]
    patched = replace_once(section, old, new, label)
    return text[:start] + patched + text[end:]


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v15.py <V13-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    main_path = root / "src/main.cpp"
    pio_path = root / "platformio.ini"
    if not main_path.exists() or not pio_path.exists():
        fail("target is not a WadaMesh checkout")

    pio = pio_path.read_text()
    if "MESH_OFFGRIDNL_V13=1" not in pio:
        fail("V13 base is not present")

    # Release identity only. This does not change runtime behaviour.
    pio = patch_section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V13=1\n",
        "  -D MESH_OFFGRIDNL_V13=1\n"
        "  -D MESH_OFFGRIDNL_V15=1\n",
        "T-Deck V15 identity flag",
    )
    pio_path.write_text(pio)

    s = main_path.read_text()

    # ONLY V15 functional change:
    # V13 forced WIFI_PS_NONE immediately before association. The pinned
    # WadaMesh base already owns ESP32-S3 Wi-Fi/BLE coexistence power policy.
    # Do not override that policy during hotspot association/reconnect.
    s = replace_once(
        s,
        """  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
""",
        """  WiFi.setAutoReconnect(false);
  // V15 crash fix: leave Wi-Fi/Bluetooth power-management policy to WadaMesh.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
""",
        "remove unsafe association-time sleep override",
    )

    main_path.write_text(s)

    # Prove that V15 is still V13 apart from the single sleep override removal.
    pio = pio_path.read_text()
    s = main_path.read_text()
    required = [
        "MESH_OFFGRIDNL_V15=1",
        "WiFi.setAutoReconnect(false);",
        "WIFI_ALL_CHANNEL_SCAN",
        "[V13][wifi] join selected AP",
        "[V13][wifi] join ssid=",
        "WIFI_RETRY_INTERVAL_MS = 15000",
        "WiFi.disconnect(false, false);",
    ]
    blob = pio + "\n" + s
    for marker in required:
        if marker not in blob:
            fail("V13 behaviour changed unexpectedly; missing: " + marker)

    helper_start = s.find("static void v13WifiBegin")
    helper_end = s.find("#endif", helper_start)
    if helper_start < 0 or helper_end < 0:
        fail("V13 association helper missing")
    helper = s[helper_start:helper_end]

    active_lines = [
        line.strip() for line in helper.splitlines()
        if line.strip() and not line.lstrip().startswith("//")
    ]
    if any(line.startswith("WiFi.setSleep(false)") for line in active_lines):
        fail("unsafe association-time WiFi.setSleep(false) still active")

    # Guard against accidentally carrying over the earlier broader V15 draft.
    forbidden = [
        "[V15][wifi] join selected AP once",
        "WIFI_RETRY_INTERVAL_MS = 20000",
    ]
    for marker in forbidden:
        if marker in s:
            fail("V15 must be V13 plus only the crash fix; found extra change: " + marker)

    print("V15 applied: exact V13 behaviour with only association-time WiFi.setSleep(false) removed")


if __name__ == "__main__":
    main()
