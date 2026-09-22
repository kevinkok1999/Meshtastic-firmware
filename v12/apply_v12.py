#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V12 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_exact_count(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        fail(f"{label}: expected {expected} anchors, found {count}")
    return text.replace(old, new)


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
        fail("usage: apply_v12.py <V11-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    if not (root / "src/main.cpp").exists():
        fail("target is not a WadaMesh checkout")

    # Refuse to build a V12 that somehow skipped the V11 overlay.
    mymesh = (root / "src/MyMesh.cpp").read_text()
    if "v11_global_bridge.mirrorDM" not in mymesh:
        fail("V11 overlay is not present")

    # V12 is a T-Deck-only flag layered over the already-patched V11 target.
    p = root / "platformio.ini"
    s = p.read_text()
    s = patch_section(
        s,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V11=1\n",
        "  -D MESH_OFFGRIDNL_V11=1\n"
        "  -D MESH_OFFGRIDNL_V12=1\n",
        "T-Deck V12 flag",
    )
    p.write_text(s)

    p = root / "src/main.cpp"
    s = p.read_text()

    # esp_wifi_set_country_code() is the ESP-IDF primitive behind the Wi-Fi
    # country/channel policy. Include it only in the V12 T-Deck build.
    include_anchor = '#include "esp_task_wdt.h"   // task-watchdog reconfigure — see setup() (GH #56)\n'
    helper_block = r'''#if defined(MESH_OFFGRIDNL_V12)
#include <esp_wifi.h>

static bool s_v12_wifi_country_ready = false;
static bool s_v12_wifi_sleep_enabled = false;

static void v12WifiPrepareSta() {
  // WiFi.mode(WIFI_STA) must have initialized/started the driver before this.
  if ((WiFi.getMode() & WIFI_MODE_STA) == 0) return;

  // ESP-IDF defaults an unassociated station to a world-safe country policy.
  // MeshOffGridNL is a Netherlands/EU build: set NL explicitly so legal 2.4 GHz
  // channels 1..13 can be discovered/used. Do this once per boot to avoid
  // repeated NVS writes from the IDF country API.
  if (!s_v12_wifi_country_ready) {
    const esp_err_t err = esp_wifi_set_country_code("NL", true);
    if (err == ESP_OK) {
      s_v12_wifi_country_ready = true;
      Serial.println("[V12][wifi] country=NL (802.11d on)");
    } else {
      Serial.printf("[V12][wifi] country setup failed err=%d\n", (int)err);
    }
  }

  // T-Deck owns its normal Arduino reconnect loop. Keep it enabled.
  WiFi.setAutoReconnect(true);
}

static void v12WifiPrepareAssociation() {
  v12WifiPrepareSta();

  // Association and active scan must get a fully-awake modem. Upstream already
  // avoids sleep before the first join; V12 applies the same rule to *every*
  // later network switch/retry and restores sleep only after GOT_IP.
  WiFi.setSleep(false);
  s_v12_wifi_sleep_enabled = false;

  // A new attempt should report only its own disconnect reason.
  g_wifi_last_disc_reason = 0;
}

static const char* v12WifiReasonBrief(uint8_t reason) {
  switch (reason) {
    case 2:
    case 202:
      return "auth failed: security mismatch or weak signal";
    case 15:
    case 204:
      return "handshake failed: check Wi-Fi password";
    case 200:
      return "signal lost: beacon timeout";
    case 201:
      return "network not found: check 2.4 GHz / channel";
    case 205:
      return "connection failed";
    case 210:
      return "incompatible Wi-Fi security";
    case 211:
      return "Wi-Fi security threshold mismatch";
    case 212:
      return "Wi-Fi signal too weak";
    default:
      return "Wi-Fi association failed";
  }
}
#endif

'''
    s = replace_once(s, include_anchor, helper_block + include_anchor, "V12 Wi-Fi helpers")

    # There are two T-Deck-relevant STA initializations: setup() and the runtime
    # state machine. Apply the NL country before scans or association at both.
    mode_anchor = "const bool wifi_mode_ready = WiFi.mode(WIFI_STA);"
    mode_replacement = mode_anchor + r'''
#if defined(MESH_OFFGRIDNL_V12)
    if (wifi_mode_ready) v12WifiPrepareSta();
#endif'''
    s = replace_exact_count(s, mode_anchor, mode_replacement, 2, "STA initialization hooks")

    # Credential/network changes must not inherit stale BSSID/security state from
    # the previous AP. This is V12 source only, so use the same full supplicant
    # clear that upstream already uses for its 10-second retry path.
    s = replace_once(
        s,
        "        WiFi.disconnect(false, false);\n        delay(50);",
        "        WiFi.disconnect(false, true);\n        delay(50);",
        "network-change supplicant reset",
    )

    # All begin(ssid,pwd) calls in this source get the association preflight.
    # Board-specific branches compiled out for T-Deck remain harmless source text.
    begin_anchor = "WiFi.begin(ssid, pwd[0] ? pwd : nullptr);"
    begin_replacement = r'''#if defined(MESH_OFFGRIDNL_V12)
        v12WifiPrepareAssociation();
#endif
        WiFi.begin(ssid, pwd[0] ? pwd : nullptr);'''
    s = replace_exact_count(s, begin_anchor, begin_replacement, 4, "association preflight hooks")

    # Upstream enables modem sleep once after the first successful association.
    # V12 disables it before every later attempt, so it also needs to restore it
    # after every successful re-association instead of relying on a one-shot flag.
    sleep_anchor = r'''      static bool modem_sleep_set = false;
      if (!modem_sleep_set) {
        WiFi.setSleep(true);
        modem_sleep_set = true;
      }'''
    sleep_replacement = r'''#if defined(MESH_OFFGRIDNL_V12)
      if (!s_v12_wifi_sleep_enabled) {
        WiFi.setSleep(true);
        s_v12_wifi_sleep_enabled = true;
      }
#else
      static bool modem_sleep_set = false;
      if (!modem_sleep_set) {
        WiFi.setSleep(true);
        modem_sleep_set = true;
      }
#endif'''
    s = replace_once(s, sleep_anchor, sleep_replacement, "post-association modem sleep")

    # Make the already-recorded ESP-IDF disconnect reason useful without a USB
    # serial console. Ignore reason 8 (our own deliberate disconnect when
    # switching networks), show each failure reason once, and reset on success.
    diag_anchor = r'''      // Link dropped: allow re-sync on next reconnect.
      if (sntp_kicked && !sntp_pushed) sntp_kicked = false;
    }
#if defined(TLORA_PAGER) && defined(BLE_PIN_CODE)'''
    diag_replacement = r'''      // Link dropped: allow re-sync on next reconnect.
      if (sntp_kicked && !sntp_pushed) sntp_kicked = false;
    }
#if defined(MESH_OFFGRIDNL_V12) && defined(DISPLAY_CLASS)
    {
      static uint8_t last_reason_shown = 0;
      if (WiFi.status() == WL_CONNECTED) {
        last_reason_shown = 0;
        g_wifi_last_disc_reason = 0;
      } else {
        const uint8_t reason = g_wifi_last_disc_reason;
        // 8 = ASSOC_LEAVE, generated by our own disconnect() during an AP switch.
        if (reason != 0 && reason != 8 && reason != last_reason_shown) {
          char msg[112];
          snprintf(msg, sizeof msg, "%s (r%u)", v12WifiReasonBrief(reason),
                   (unsigned)reason);
          Serial.printf("[V12][wifi] %s\n", msg);
          ui_task.showAlert(msg, 4200);
          last_reason_shown = reason;
        }
      }
    }
#endif
#if defined(TLORA_PAGER) && defined(BLE_PIN_CODE)'''
    s = replace_once(s, diag_anchor, diag_replacement, "on-device Wi-Fi diagnostics")

    p.write_text(s)

    # Fail before PlatformIO if any key V12 contract disappeared.
    required = {
        "platformio.ini": [
            "MESH_OFFGRIDNL_V11=1",
            "MESH_OFFGRIDNL_V12=1",
            "LORA_FREQ=869.618",
            "LORA_TX_POWER=22",
            "MAX_LORA_TX_POWER=22",
        ],
        "src/main.cpp": [
            'esp_wifi_set_country_code("NL", true)',
            "v12WifiPrepareAssociation();",
            "WiFi.disconnect(false, true);",
            "s_v12_wifi_sleep_enabled",
            "incompatible Wi-Fi security",
            "network not found: check 2.4 GHz / channel",
        ],
        "src/MyMesh.cpp": [
            "v11_global_bridge.mirrorDM",
            "v11_global_bridge.noteLoRaDM",
        ],
    }
    for rel, markers in required.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("V12 Wi-Fi hardening applied successfully")


if __name__ == "__main__":
    main()
