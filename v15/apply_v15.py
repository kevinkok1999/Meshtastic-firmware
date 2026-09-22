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

    pio = patch_section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V13=1\n",
        "  -D MESH_OFFGRIDNL_V13=1\n"
        "  -D MESH_OFFGRIDNL_V15=1\n",
        "T-Deck V15 flag",
    )
    pio_path.write_text(pio)

    s = main_path.read_text()

    # V13 forced WIFI_PS_NONE on each begin(). The pinned WadaMesh base owns
    # Wi-Fi/BLE coexistence and documents that forcing power-save off while the
    # BT controller is resident can abort in the Wi-Fi PM path. Also, the base
    # tracks its post-connect sleep setup as one-shot, so toggling it here made
    # software state and driver state diverge across reconnects.
    s = replace_once(
        s,
        """  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
""",
        """  WiFi.setAutoReconnect(false);
  // V15: do not call WiFi.setSleep(false) here. The base firmware owns
  // ESP32-S3 Wi-Fi/BLE coexistence and applies modem sleep after GOT_IP.
  // Re-association must not override that policy or desynchronise its state.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
""",
        "coexistence-safe association",
    )

    # A phone hotspot may rotate BSSID/channel. Use a fresh scan hint exactly
    # once, then clear it before association so every later retry naturally
    # falls back to the all-channel SSID path.
    s = replace_once(
        s,
        """  if (wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n", ssid);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
""",
        """  if (wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
    Serial.printf("[V15][wifi] join selected AP once ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
    wifiConfigClearApHint();
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
    Serial.printf("[V15][wifi] join ssid='%s' all-channel fallback\\n", ssid);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
""",
        "one-shot AP hint",
    )

    # Give Android WPA/WPA2/WPA3 hotspot handshakes more time before the explicit
    # reconnect owner tears down and restarts the attempt.
    s = replace_once(
        s,
        """#if defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""",
        """#if defined(MESH_OFFGRIDNL_V15)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 20000;
#elif defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""",
        "V15 retry interval",
    )

    main_path.write_text(s)

    # Contracts: V15 must keep the useful V13 hardening while removing the
    # association-time sleep override that can conflict with Bluetooth.
    pio = pio_path.read_text()
    s = main_path.read_text()
    required = [
        "MESH_OFFGRIDNL_V15=1",
        "WiFi.setAutoReconnect(false);",
        "WIFI_ALL_CHANNEL_SCAN",
        "wifiConfigClearApHint();",
        "[V15][wifi] join selected AP once",
        "WIFI_RETRY_INTERVAL_MS = 20000",
        "WiFi.disconnect(false, false);",
    ]
    blob = pio + "\n" + s
    for marker in required:
        if marker not in blob:
            fail("missing V15 contract marker: " + marker)

    helper_start = s.find("static void v13WifiBegin")
    helper_end = s.find("#endif", helper_start)
    if helper_start < 0 or helper_end < 0:
        fail("V13 association helper missing")
    helper = s[helper_start:helper_end]
    if "WiFi.setSleep(false)" in helper and "// V15: do not call WiFi.setSleep(false)" not in helper:
        fail("unsafe association-time WiFi.setSleep(false) still active")

    print("V15 applied: coexistence-safe hotspot association + one-shot AP hint + slower retry")


if __name__ == "__main__":
    main()
