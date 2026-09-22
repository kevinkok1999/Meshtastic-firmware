#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V16 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def replace_exact(text: str, old: str, new: str, expected: int, label: str) -> str:
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
    section = replace_once(section, old, new, label)
    return text[:start] + section + text[end:]


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v16.py <V15-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio_path = root / "platformio.ini"
    main_path = root / "src/main.cpp"
    ui_path = root / "src/ui-touch/UITask.cpp"
    if not pio_path.exists() or not main_path.exists() or not ui_path.exists():
        fail("target is not a WadaMesh checkout")

    pio = pio_path.read_text()
    if "MESH_OFFGRIDNL_V15=1" not in pio:
        fail("V15 base is not present")

    # ------------------------------------------------------------------
    # 1. Identity: V16 is a T-Deck-only layer on the validated V15 base.
    # ------------------------------------------------------------------
    pio = patch_section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V15=1\n",
        "  -D MESH_OFFGRIDNL_V15=1\n"
        "  -D MESH_OFFGRIDNL_V16=1\n",
        "V16 build flag",
    )
    pio_path.write_text(pio)

    # ------------------------------------------------------------------
    # 2. Main-loop connection engine.
    # ------------------------------------------------------------------
    s = main_path.read_text()

    s = replace_once(
        s,
        """extern volatile uint8_t g_wifi_last_disc_reason;
#endif
""",
        """extern volatile uint8_t g_wifi_last_disc_reason;
#if defined(MESH_OFFGRIDNL_V16)
// V16 owns the user-initiated join lifecycle. UITask reads these to suppress
// scans during association and to show an actual phase instead of an endless
// generic "connecting..." label.
volatile bool g_v16_wifi_join_in_progress = false;
volatile uint8_t g_v16_wifi_phase = 0;   // 0 idle, 1 associating, 2 waiting-IP, 3 connected, 4 failed, 5 paused/backoff
volatile uint8_t g_v16_wifi_attempt = 0;
#endif
#endif
""",
        "V16 connection state globals",
    )

    s = replace_once(
        s,
        """    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        g_wifi_last_disc_reason = info.wifi_sta_disconnected.reason;
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
""",
        """    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        g_wifi_last_disc_reason = info.wifi_sta_disconnected.reason;
#if defined(MESH_OFFGRIDNL_V16)
        g_v16_wifi_phase = 4;
#endif
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
""",
        "disconnect event phase",
    )

    s = replace_once(
        s,
        """    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)   wifi_needs_reconnect = true;
        else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)    wifi_needs_reconnect = false;
    });
""",
        """    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          wifi_needs_reconnect = true;
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 4;
#endif
        } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 2;
#endif
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          wifi_needs_reconnect = false;
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 3;
          g_v16_wifi_join_in_progress = false;
          g_v16_wifi_attempt = 0;
#endif
        }
    });
""",
        "connection phase events",
    )

    # V15 still inherited V13's clearing of the reason at every begin(). V16
    # clears it once in wifiDoJoin so retry diagnostics are never destroyed.
    s = replace_once(
        s,
        """  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  g_wifi_last_disc_reason = 0;

  int32_t channel = 0;
""",
        """  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  // V16 preserves the previous disconnect reason across retries. A NEW manual
  // join clears it once in UITask before association starts.

  int32_t channel = 0;
""",
        "preserve disconnect reason",
    )

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
#if defined(MESH_OFFGRIDNL_V16)
    // First attempt only: Android hotspots can rotate BSSID/channel. Clear the
    // hint BEFORE begin so the next retry becomes SSID-only/all-channel.
    wifiConfigClearApHint();
    Serial.printf("[V16][wifi] attempt %u: selected AP once ssid='%s' ch=%ld auth=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (long)channel, (unsigned)authmode);
#else
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
#endif
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
#if defined(MESH_OFFGRIDNL_V16)
    Serial.printf("[V16][wifi] attempt %u: all-channel ssid='%s'\\n",
                  (unsigned)g_v16_wifi_attempt, ssid);
#else
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n", ssid);
#endif
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
""",
        "one-shot AP hint",
    )

    s = replace_once(
        s,
        """#if defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""",
        """#if defined(MESH_OFFGRIDNL_V16)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 12000;
  static const uint32_t WIFI_RETRY_BACKOFF_MS = 60000;
#elif defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""",
        "V16 retry timing",
    )

    s = replace_once(
        s,
        """    wifi_started = false;
    last_wifi_retry_ms = 0;
  }
  bool wifi_state_machine_active = wifi_radio_en;
""",
        """    wifi_started = false;
    last_wifi_retry_ms = 0;
#if defined(MESH_OFFGRIDNL_V16)
    g_v16_wifi_attempt = 0;
    g_v16_wifi_phase = 0;
#endif
  }
  bool wifi_state_machine_active = wifi_radio_en;
""",
        "fresh apply resets V16 attempt counter",
    )

    # There are exactly two V13 begin call sites in loop(): initial association
    # and periodic retry. Instrument both, while leaving non-V16 V13 untouched.
    begin_anchor = """#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif"""
    begin_repl = """#if defined(MESH_OFFGRIDNL_V13)
#if defined(MESH_OFFGRIDNL_V16)
          if (g_v16_wifi_attempt < 250) ++g_v16_wifi_attempt;
          g_v16_wifi_phase = 1;
#endif
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif"""
    s = replace_exact(s, begin_anchor, begin_repl, 2, "V16 attempt instrumentation")

    s = replace_once(
        s,
        """      uint32_t now = millis();
      if ((uint32_t)(now - last_wifi_retry_ms) >= WIFI_RETRY_INTERVAL_MS) {
        last_wifi_retry_ms = now;
""",
        """      uint32_t now = millis();
#if defined(MESH_OFFGRIDNL_V16)
      const uint32_t retry_interval =
          (!g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 3)
              ? WIFI_RETRY_BACKOFF_MS : WIFI_RETRY_INTERVAL_MS;
#else
      const uint32_t retry_interval = WIFI_RETRY_INTERVAL_MS;
#endif
      if ((uint32_t)(now - last_wifi_retry_ms) >= retry_interval) {
#if defined(MESH_OFFGRIDNL_V16)
        // After three foreground attempts, release the UI instead of showing an
        // endless Connecting state. Keep credentials and recover in background.
        if (g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 3) {
          g_v16_wifi_join_in_progress = false;
          g_v16_wifi_phase = 5;
          last_wifi_retry_ms = now;
        } else {
#endif
        last_wifi_retry_ms = now;
""",
        "bounded foreground retry entry",
    )

    retry_close_anchor = """        }
      }
    }
    /* SNTP: kick off when Wi-Fi associates; once system time syncs, push it
"""
    retry_close_repl = """        }
#if defined(MESH_OFFGRIDNL_V16)
        }
#endif
      }
    }
    /* SNTP: kick off when Wi-Fi associates; once system time syncs, push it
"""
    s = replace_once(s, retry_close_anchor, retry_close_repl,
                     "bounded foreground retry close")

    main_path.write_text(s)

    # ------------------------------------------------------------------
    # 3. UI/scan arbitration. CONNECT owns the radio until association ends.
    # ------------------------------------------------------------------
    ui = ui_path.read_text()

    ui = replace_once(
        ui,
        """  #include <esp_system.h>
  #include <esp_sleep.h>
""",
        """  #include <esp_system.h>
  #include <esp_wifi.h>
  #include <esp_sleep.h>
""",
        "esp_wifi scan-stop include",
    )

    ui = replace_once(
        ui,
        """static volatile int  s_wifiscan_count   = 0;
static volatile bool s_wifiscan_request = false;   // UI -> worker: scan now
static volatile bool s_wifiscan_done    = false;   // worker -> UI: results ready
""",
        """static volatile int  s_wifiscan_count   = 0;
static volatile bool s_wifiscan_request = false;   // UI -> worker: scan now
static volatile bool s_wifiscan_done    = false;   // worker -> UI: results ready
#if defined(MESH_OFFGRIDNL_V16)
extern volatile bool g_v16_wifi_join_in_progress;
extern volatile uint8_t g_v16_wifi_phase;
extern volatile uint8_t g_v16_wifi_attempt;
#endif
""",
        "V16 UI state externs",
    )

    ui = replace_once(
        ui,
        """static void wifiQueueScanWhenReady() {
  if (wifiScanIsActive() ||
""",
        """static void wifiQueueScanWhenReady() {
#if defined(MESH_OFFGRIDNL_V16)
  if (g_v16_wifi_join_in_progress) return;   // CONNECT always outranks SCAN
#endif
  if (wifiScanIsActive() ||
""",
        "queued scan join guard",
    )

    ui = replace_once(
        ui,
        """static void wifiKickScan() {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  if (wifiScanIsActive() ||
""",
        """static void wifiKickScan() {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#if defined(MESH_OFFGRIDNL_V16)
  if (g_v16_wifi_join_in_progress) return;   // never start a scan under WPA/DHCP
#endif
  if (wifiScanIsActive() ||
""",
        "manual scan join guard",
    )

    ui = replace_once(
        ui,
        """#if !defined(TLORA_PAGER)
    WiFi.setAutoReconnect(true);
#endif
    if (reconnect_after_scan)
""",
        """#if !defined(TLORA_PAGER)
#if defined(MESH_OFFGRIDNL_V16)
    WiFi.setAutoReconnect(false);   // V16: exactly one reconnect owner, even after scans
#else
    WiFi.setAutoReconnect(true);
#endif
#endif
    if (reconnect_after_scan)
""",
        "single reconnect owner after scan",
    )

    ui = replace_once(
        ui,
        """static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
#if defined(MESH_OFFGRIDNL_V13)
""",
        """static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
#if defined(MESH_OFFGRIDNL_V16)
  // CONNECT has priority over discovery. Cancel scan work not yet claimed by
  // the worker; an active worker observes the join flag and stops its own scan.
  g_v16_wifi_join_in_progress = true;
  g_v16_wifi_phase = 0;
  g_v16_wifi_attempt = 0;
  extern volatile uint8_t g_wifi_last_disc_reason;
  g_wifi_last_disc_reason = 0;
  bool cancelled_before_worker = false;
  if (s_wifiscan_drop_req) { s_wifiscan_drop_req = false; cancelled_before_worker = true; }
  if (s_wifiscan_drop_ms != 0) { s_wifiscan_drop_ms = 0; cancelled_before_worker = true; }
  if (__atomic_exchange_n(&s_wifiscan_request, false, __ATOMIC_ACQ_REL))
    cancelled_before_worker = true;
  if (cancelled_before_worker) {
    s_wifiscan_reconnect_after = false;
    s_wifiscan_guard_ms = 0;
    wifiScanSetActive(false);
  }
#endif
#if defined(MESH_OFFGRIDNL_V13)
""",
        "manual join owns radio",
    )

    ui = replace_once(
        ui,
        """  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
  wifiRebuildNetworkList();
}
""",
        """  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
#if defined(MESH_OFFGRIDNL_V16)
  // Rebuilding here auto-kicks another scan while disconnected. Keep the join
  // path scan-free and update only the status labels.
  refreshStatusLabels();
#else
  wifiRebuildNetworkList();
#endif
}
""",
        "no post-join autoscan",
    )

    ui = replace_once(
        ui,
        """  for (int attempt = 0; attempt < 4; ++attempt) {
    WiFi.scanDelete();
    const int16_t kick = WiFi.scanNetworks(true, true, false, per_chan_ms, 0);
""",
        """  for (int attempt = 0; attempt < 4; ++attempt) {
#if defined(MESH_OFFGRIDNL_V16)
    if (g_v16_wifi_join_in_progress) return 0;
#endif
    WiFi.scanDelete();
    const int16_t kick = WiFi.scanNetworks(true, true, false, per_chan_ms, 0);
""",
        "worker scan pre-start guard",
    )

    ui = replace_once(
        ui,
        """    int16_t st;
    while ((st = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && (millis() - t0) < cap_ms)
      vTaskDelay(pdMS_TO_TICKS(50));             // yield -> feeds the task watchdog
""",
        """    int16_t st;
    while ((st = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && (millis() - t0) < cap_ms) {
#if defined(MESH_OFFGRIDNL_V16)
      if (g_v16_wifi_join_in_progress) {
        // Arduino-ESP32 2.0.17 scanDelete() frees results but does not cancel
        // esp_wifi_scan_start(). The worker/core that owns the scan stops it.
        esp_wifi_scan_stop();
        WiFi.scanDelete();                        // worker owns this scan: safe abort point
        return 0;
      }
#endif
      vTaskDelay(pdMS_TO_TICKS(50));             // yield -> feeds the task watchdog
    }
""",
        "active scan stop for foreground join",
    )

    ui = replace_once(
        ui,
        """static const char* wifiStaStatusBrief(int s) {
  switch (s) {
""",
        """static const char* wifiStaStatusBrief(int s) {
#if defined(MESH_OFFGRIDNL_V16)
  if (g_v16_wifi_join_in_progress) {
    static char s_v16_join[36];
    if (g_v16_wifi_phase == 2) return "linked, getting IP...";
    if (g_v16_wifi_phase == 1) {
      snprintf(s_v16_join, sizeof s_v16_join, "associating... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
      return s_v16_join;
    }
  } else if (g_v16_wifi_phase == 5) {
    return "retrying in background";
  }
#endif
  switch (s) {
""",
        "V16 phase status text",
    )

    ui_path.write_text(ui)

    # ------------------------------------------------------------------
    # 4. Hard contracts before CI spends runner time compiling.
    # ------------------------------------------------------------------
    pio = pio_path.read_text()
    main_cpp = main_path.read_text()
    ui = ui_path.read_text()
    required = [
        (pio, "MESH_OFFGRIDNL_V16=1"),
        (main_cpp, "WIFI_RETRY_INTERVAL_MS = 12000"),
        (main_cpp, "WIFI_RETRY_BACKOFF_MS = 60000"),
        (main_cpp, "wifiConfigClearApHint();"),
        (main_cpp, "[V16][wifi] attempt %u: selected AP once"),
        (main_cpp, "ARDUINO_EVENT_WIFI_STA_CONNECTED"),
        (ui, "CONNECT always outranks SCAN"),
        (ui, "esp_wifi_scan_stop();"),
        (ui, "V16: exactly one reconnect owner, even after scans"),
        (ui, "retrying in background"),
        (ui, "associating... (%u/3)"),
    ]
    for blob, marker in required:
        if marker not in blob:
            fail("missing V16 contract marker: " + marker)

    print("V16 applied: scan arbitration + one reconnect owner + one-shot AP hint + bounded join/recovery")


if __name__ == "__main__":
    main()
