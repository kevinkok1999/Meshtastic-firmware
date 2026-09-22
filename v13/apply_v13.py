#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V13 patch failed: " + message)


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
        fail("usage: apply_v13.py <V12-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    if not (root / "src/main.cpp").exists():
        fail("target is not a WadaMesh checkout")

    # V13 sits on top of the already validated V11 + V12 layers.
    pio_path = root / "platformio.ini"
    pio = pio_path.read_text()
    if "MESH_OFFGRIDNL_V12=1" not in pio:
        fail("V12 base is not present")

    # T-Deck-only identity flag.
    pio = patch_section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V12=1\n",
        "  -D MESH_OFFGRIDNL_V12=1\n"
        "  -D MESH_OFFGRIDNL_V13=1\n",
        "T-Deck V13 flag",
    )
    pio_path.write_text(pio)

    # ------------------------------------------------------------------
    # 1. Runtime AP hint: keep scan metadata (channel/BSSID/auth) in RAM.
    #    This lets a user's tap connect to the AP they actually tapped instead
    #    of immediately doing another blind fast scan by SSID.
    # ------------------------------------------------------------------
    hdr = root / "src/helpers/esp32/WifiRuntimeStore.h"
    s = hdr.read_text()
    anchor = """void wifiScanSetActive(bool active);
bool wifiScanIsActive();

#endif
"""
    replacement = """void wifiScanSetActive(bool active);
bool wifiScanIsActive();

#if defined(MESH_OFFGRIDNL_V13)
/* Ephemeral hint from the on-device scan. Never persisted: Android hotspots
 * can change BSSID/channel between sessions. A hint is used only when its SSID
 * still matches the active credential. authmode is wifi_auth_mode_t as uint8_t
 * to keep this store header independent of WiFi.h. */
void wifiConfigSetApHint(const char* ssid, int32_t channel,
                         const uint8_t bssid[6], uint8_t authmode);
bool wifiConfigGetApHint(const char* ssid, int32_t* channel,
                         uint8_t bssid[6], uint8_t* authmode);
void wifiConfigClearApHint();
#endif

#endif
"""
    s = replace_once(s, anchor, replacement, "V13 AP-hint declarations")
    hdr.write_text(s)

    cpp = root / "src/helpers/esp32/WifiRuntimeStore.cpp"
    s = cpp.read_text()
    anchor = """static volatile bool s_wifi_scan_active = false;
void wifiScanSetActive(bool active) { s_wifi_scan_active = active; }
bool wifiScanIsActive() { return s_wifi_scan_active; }

void wifiConfigApply() {
"""
    replacement = """static volatile bool s_wifi_scan_active = false;
void wifiScanSetActive(bool active) { s_wifi_scan_active = active; }
bool wifiScanIsActive() { return s_wifi_scan_active; }

#if defined(MESH_OFFGRIDNL_V13)
struct V13WifiApHint {
  bool valid;
  char ssid[WIFI_CONFIG_SSID_MAX];
  int32_t channel;
  uint8_t bssid[6];
  uint8_t authmode;
};
static V13WifiApHint s_v13_ap_hint = {};

void wifiConfigSetApHint(const char* ssid, int32_t channel,
                         const uint8_t bssid[6], uint8_t authmode) {
  if (!ssid || !ssid[0] || !bssid || channel < 1 || channel > 14) {
    wifiConfigClearApHint();
    return;
  }
  memset(&s_v13_ap_hint, 0, sizeof(s_v13_ap_hint));
  strlcpy(s_v13_ap_hint.ssid, ssid, sizeof(s_v13_ap_hint.ssid));
  s_v13_ap_hint.channel = channel;
  memcpy(s_v13_ap_hint.bssid, bssid, sizeof(s_v13_ap_hint.bssid));
  s_v13_ap_hint.authmode = authmode;
  s_v13_ap_hint.valid = true;
}

bool wifiConfigGetApHint(const char* ssid, int32_t* channel,
                         uint8_t bssid[6], uint8_t* authmode) {
  if (!s_v13_ap_hint.valid || !ssid || strcmp(ssid, s_v13_ap_hint.ssid) != 0)
    return false;
  if (channel) *channel = s_v13_ap_hint.channel;
  if (bssid) memcpy(bssid, s_v13_ap_hint.bssid, sizeof(s_v13_ap_hint.bssid));
  if (authmode) *authmode = s_v13_ap_hint.authmode;
  return true;
}

void wifiConfigClearApHint() {
  memset(&s_v13_ap_hint, 0, sizeof(s_v13_ap_hint));
}
#endif

void wifiConfigApply() {
"""
    s = replace_once(s, anchor, replacement, "V13 AP-hint implementation")
    cpp.write_text(s)

    # ------------------------------------------------------------------
    # 2. UI scan: retain AP metadata, don't drop a healthy connection merely
    #    because the user opened Wi-Fi settings, and hand the selected AP hint
    #    to the main-loop connection manager.
    # ------------------------------------------------------------------
    ui_path = root / "src/ui-touch/UITask.cpp"
    ui = ui_path.read_text()

    anchor = """static constexpr int kWifiScanMax = 14;
static char          s_wifiscan_ssids[kWifiScanMax][WIFI_CONFIG_SSID_MAX];
static volatile int  s_wifiscan_count   = 0;
"""
    replacement = """static constexpr int kWifiScanMax = 14;
static char          s_wifiscan_ssids[kWifiScanMax][WIFI_CONFIG_SSID_MAX];
#if defined(MESH_OFFGRIDNL_V13)
static int32_t       s_wifiscan_channels[kWifiScanMax] = {};
static uint8_t       s_wifiscan_bssids[kWifiScanMax][6] = {};
static uint8_t       s_wifiscan_auth[kWifiScanMax] = {};
#endif
static volatile int  s_wifiscan_count   = 0;
"""
    ui = replace_once(ui, anchor, replacement, "V13 scan metadata arrays")

    anchor = """        strncpy(s_wifiscan_ssids[n], s.c_str(), WIFI_CONFIG_SSID_MAX - 1);
        s_wifiscan_ssids[n][WIFI_CONFIG_SSID_MAX - 1] = '\\0';
        ++n;
"""
    replacement = """        strncpy(s_wifiscan_ssids[n], s.c_str(), WIFI_CONFIG_SSID_MAX - 1);
        s_wifiscan_ssids[n][WIFI_CONFIG_SSID_MAX - 1] = '\\0';
#if defined(MESH_OFFGRIDNL_V13)
        s_wifiscan_channels[n] = WiFi.channel(idx);
        s_wifiscan_auth[n] = static_cast<uint8_t>(WiFi.encryptionType(idx));
        if (const uint8_t* bssid = WiFi.BSSID(idx))
          memcpy(s_wifiscan_bssids[n], bssid, 6);
        else
          memset(s_wifiscan_bssids[n], 0, 6);
#endif
        ++n;
"""
    ui = replace_once(ui, anchor, replacement, "V13 capture scan metadata")

    anchor = """static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
  const int idx = touchPrefsSaveWifiNet(ssid, pwd, auto_join);
"""
    replacement = """static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
#if defined(MESH_OFFGRIDNL_V13)
  // Bind this join attempt to the AP row the user selected. The hint is RAM-only
  // and is ignored automatically if the active SSID differs later.
  wifiConfigClearApHint();
  for (int i = 0; i < s_wifiscan_count; ++i) {
    if (strcmp(s_wifiscan_ssids[i], ssid) == 0) {
      wifiConfigSetApHint(ssid, s_wifiscan_channels[i],
                          s_wifiscan_bssids[i], s_wifiscan_auth[i]);
      break;
    }
  }
#endif
  const int idx = touchPrefsSaveWifiNet(ssid, pwd, auto_join);
"""
    ui = replace_once(ui, anchor, replacement, "V13 selected-AP handoff")

    # Opening Settings must not tear down a working hotspot just to refresh the list.
    ui = replace_once(
        ui,
        '  if (wifiConfigWantsWifi()) wifiKickScan();\n',
        '  if (wifiConfigWantsWifi() && WiFi.status() != WL_CONNECTED) wifiKickScan();\n',
        "V13 no auto-scan while connected",
    )

    # V13 owns reconnects in one place; a scan must not silently re-enable the
    # Arduino core's parallel auto-reconnect state machine.
    auto_anchor = """#if !defined(TLORA_PAGER)
      WiFi.setAutoReconnect(true);
#endif
"""
    auto_replacement = """#if !defined(TLORA_PAGER)
#if defined(MESH_OFFGRIDNL_V13)
      WiFi.setAutoReconnect(false);
#else
      WiFi.setAutoReconnect(true);
#endif
#endif
"""
    ui = replace_exact_count(ui, auto_anchor, auto_replacement, 2,
                             "V13 scan reconnect ownership")

    # Surface useful failure reasons even when Arduino reports WL_DISCONNECTED
    # rather than WL_CONNECT_FAILED.
    anchor = """    case WL_CONNECTION_LOST: return "link lost";
    case WL_DISCONNECTED: return "disconnected";
    default: return "connecting…";
"""
    replacement = """    case WL_CONNECTION_LOST: return "link lost";
    case WL_DISCONNECTED: {
#if defined(MESH_OFFGRIDNL_V13)
      extern volatile uint8_t g_wifi_last_disc_reason;
      static char s_disc[48];
      const uint8_t r = g_wifi_last_disc_reason;
      const char* why =
          (r == 15 || r == 204) ? "handshake timeout" :
          (r == 2 || r == 202)  ? "authentication failed" :
          (r == 201)            ? "hotspot not found" :
          (r == 203)            ? "association failed" :
          (r == 200)            ? "beacon timeout" :
                                  "disconnected";
      if (r) {
        snprintf(s_disc, sizeof s_disc, "%s (r%u)", why, (unsigned)r);
        return s_disc;
      }
#endif
      return "disconnected";
    }
    default: return "connecting…";
"""
    ui = replace_once(ui, anchor, replacement, "V13 visible disconnect reason")
    ui_path.write_text(ui)

    # ------------------------------------------------------------------
    # 3. Main-loop association manager. This is deliberately small: one owner
    #    for retries, no eraseap on retry, awake radio during WPA handshake,
    #    full-channel selection for SSID-only fallback, and exact AP connection
    #    when a fresh scan hint exists.
    # ------------------------------------------------------------------
    main_path = root / "src/main.cpp"
    main_cpp = main_path.read_text()

    # T-Deck V13 uses the explicit main-loop retry policy, not Arduino's hidden
    # background reconnect at the same time.
    setup_anchor = """#if defined(TLORA_PAGER)
    if (!wifi_mode_ready) {
"""
    # Patch only the non-Pager else below, by its exact text.
    main_cpp = replace_once(
        main_cpp,
        """#else
    WiFi.setAutoReconnect(true);   // safe while NimBLE is not resident; disabled below once BLE
                                   // exists so a later WPA re-auth cannot bypass Wi-Fi-first ordering
#endif
""",
        """#else
#if defined(MESH_OFFGRIDNL_V13)
    WiFi.setAutoReconnect(false);  // V13: one reconnect owner; avoids parallel WPA attempts
#else
    WiFi.setAutoReconnect(true);   // base behaviour
#endif
#endif
""",
        "V13 boot reconnect ownership",
    )

    # Insert the association helper immediately before loop().
    anchor = """void loop() {
"""
    helper = """#if defined(MESH_OFFGRIDNL_V13)
static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;

  // A phone hotspot association is timing-sensitive. Keep the station awake
  // until GOT_IP; the existing post-connect path turns modem sleep back on.
  WiFi.setAutoReconnect(false);
  WiFi.setSleep(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  g_wifi_last_disc_reason = 0;

  int32_t channel = 0;
  uint8_t bssid[6] = {};
  uint8_t authmode = 0;
  if (wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n", ssid);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
}
#endif

void loop() {
"""
    main_cpp = replace_once(main_cpp, anchor, helper, "V13 association helper")

    # Slightly less aggressive retry cadence: don't reset a slow WPA/SAE
    # handshake every ten seconds.
    main_cpp = replace_once(
        main_cpp,
        """  static uint32_t last_wifi_retry_ms = 0;
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
""",
        """  static uint32_t last_wifi_retry_ms = 0;
#if defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""",
        "V13 retry interval",
    )

    # Only the two ordinary main-loop begin() calls are changed; Pager's boot
    # pre-association remains untouched.
    loop_start = main_cpp.find("void loop() {")
    if loop_start < 0:
        fail("loop missing after helper insert")
    loop_text = main_cpp[loop_start:]
    old_begin = "          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);"
    if loop_text.count(old_begin) != 2:
        fail(f"V13 loop begin anchors: expected 2, found {loop_text.count(old_begin)}")
    loop_text = loop_text.replace(
        old_begin,
        """#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif""",
    )
    main_cpp = main_cpp[:loop_start] + loop_text

    # Critical reliability fix: the base retry erased the driver's AP config
    # every 10 s even though other WadaMesh paths explicitly warn eraseap can
    # wedge this S3 radio. Clear only the live association, then begin again.
    main_cpp = replace_once(
        main_cpp,
        """          WiFi.disconnect(false, true);
""",
        """#if defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry
#else
          WiFi.disconnect(false, true);
#endif
""",
        "V13 no eraseap retry",
    )
    main_path.write_text(main_cpp)

    # ------------------------------------------------------------------
    # Guardrails / contracts.
    # ------------------------------------------------------------------
    required = {
        "platformio.ini": [
            "MESH_OFFGRIDNL_V11=1",
            "MESH_OFFGRIDNL_V12=1",
            "MESH_OFFGRIDNL_V13=1",
        ],
        "src/helpers/esp32/WifiRuntimeStore.h": [
            "wifiConfigSetApHint",
            "wifiConfigGetApHint",
        ],
        "src/helpers/esp32/WifiRuntimeStore.cpp": [
            "V13WifiApHint",
            "wifiConfigClearApHint",
        ],
        "src/ui-touch/UITask.cpp": [
            "s_wifiscan_channels",
            "s_wifiscan_bssids",
            "WiFi.status() != WL_CONNECTED) wifiKickScan()",
            "handshake timeout",
        ],
        "src/main.cpp": [
            "v13WifiBegin",
            "WIFI_ALL_CHANNEL_SCAN",
            "WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry",
            "WIFI_RETRY_INTERVAL_MS = 15000",
        ],
    }
    for rel, markers in required.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("V13 applied: Android-hotspot-focused Wi-Fi manager + V12 UI/global chat preserved")


if __name__ == "__main__":
    main()
