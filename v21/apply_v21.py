#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(m): raise SystemExit("V21 patch failed: "+m)
def one(s,a,b,label):
    n=s.count(a)
    if n!=1: fail(f"{label}: expected 1 anchor, found {n}")
    return s.replace(a,b,1)
def section(s,h,nxt,a,b,label):
    i=s.find(h)
    if i<0: fail(label+": section missing")
    j=s.find(nxt,i+len(h))
    if j<0: j=len(s)
    return s[:i]+one(s[i:j],a,b,label)+s[j:]

def main():
    if len(sys.argv)!=2: fail("usage: apply_v21.py <V20-patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio_p=root/"platformio.ini"
    main_p=root/"src/main.cpp"
    ui_p=root/"src/ui-touch/UITask.cpp"
    serial_ble_src=pathlib.Path(__file__).resolve().parent/"vendor"/"SerialBLEInterface.cpp"
    for p in (pio_p,main_p,ui_p,serial_ble_src):
        if not p.exists(): fail("missing "+str(p))

    # pioarduino 55.x no longer exposes the old ESP32 platform token to
    # MeshCore's library build filter, so its SerialBLEInterface implementation
    # is skipped while the header is still consumed by the app. Vendor the exact
    # pinned core-v1.17.4 implementation into the app source tree so BLE remains
    # linked without altering the rest of the MeshCore library.
    serial_ble_dst=root/"src/helpers/esp32/SerialBLEInterface.cpp"
    serial_ble_dst.write_text(serial_ble_src.read_text())

    # ------------------------------------------------------------------
    # 1. Pin the T-Deck target to the modern Arduino 3.3.12 / IDF 5.5.5
    #    platform proven by the V21 Golden-B compile.
    # ------------------------------------------------------------------
    pio=pio_p.read_text()
    if "MESH_OFFGRIDNL_V20=1" not in pio: fail("V20 base flag missing")
    tdeck_head="[env:LilyGo_TDeck_companion_radio_touch]"
    tdeck_next="[env:LilyGo_TDeck_Pro_companion_radio_touch]"
    pio=section(
        pio,tdeck_head,tdeck_next,
        "platform = platformio/espressif32@6.11.0\n",
        "platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.312-1/platform-espressif32.zip\n",
        "V21 modern T-Deck platform"
    )
    pio=section(
        pio,tdeck_head,tdeck_next,
        "  -D MESH_OFFGRIDNL_V20=1\n",
        "  -D MESH_OFFGRIDNL_V20=1\n"
        "  -D MESH_OFFGRIDNL_V21=1\n"
        "  -D MESH_V21_WIFI_SINGLE_OWNER=1\n"
        "  -D MESH_V21_WIFI_PRIORITY=1\n"
        "  -Wno-error=return-type\n",
        "V21 flags"
    )

    # Adafruit ST7735 declares a generic dependency named "SD". On modern
    # pioarduino that ambiguous name otherwise pulls arduino-libraries/SD 1.3.0,
    # which is AVR-oriented and fails on ESP32-S3. Satisfy the dependency with
    # the SD library shipped by the exact Arduino-ESP32 framework instead.
    pio=section(
        pio,tdeck_head,tdeck_next,
        "  Wire\n  jgromes/RadioLib @ ^7.6.0\n",
        "  Wire\n"
        "  SD=file://${platformio.packages_dir}/framework-arduinoespressif32/libraries/SD\n"
        "  jgromes/RadioLib @ ^7.6.0\n",
        "V21 framework SD dependency"
    )
    pio_p.write_text(pio)

    # ------------------------------------------------------------------
    # 2. Replace the V19 association body with the smallest possible
    #    event-driven Arduino station join for V21.  No disconnect(), mode
    #    cycling, scan tuning, BSSID/channel pinning, PMF/SAE forcing or PHY
    #    overrides on the normal path.
    # ------------------------------------------------------------------
    main=main_p.read_text()

    # Arduino 3 / IDF 5 changed the TWDT API from positional arguments to a
    # configuration struct. Keep the legacy call for old targets while using
    # reconfigure/init safely on the modern V21 T-Deck toolchain.
    main=one(
        main,
        '#include "esp_task_wdt.h"   // task-watchdog reconfigure — see setup() (GH #56)\n',
        '#include "esp_task_wdt.h"   // task-watchdog reconfigure — see setup() (GH #56)\n'
        '#include "esp_idf_version.h"\n',
        "V21 IDF version include"
    )
    main=one(
        main,
        '''  esp_task_wdt_init(20, true);   // 20 s grace (was ~5 s), keep panic. Re-init reconfigures the
                                 // already-running TWDT + keeps the idle-task subscriptions.
''',
        '''#if ESP_IDF_VERSION_MAJOR >= 5
  {
    esp_task_wdt_config_t twdt = {};
    twdt.timeout_ms = 20000;
    twdt.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
    twdt.trigger_panic = true;
    esp_err_t twdt_rc = esp_task_wdt_reconfigure(&twdt);
    if (twdt_rc == ESP_ERR_INVALID_STATE) {
      twdt_rc = esp_task_wdt_init(&twdt);
    }
    Serial.printf("[V21][wdt] 20s config rc=%d\\n", (int)twdt_rc);
  }
#else
  esp_task_wdt_init(20, true);   // legacy IDF 4 API
#endif
''',
        "V21 IDF5 task watchdog API"
    )

    anchor='''#if defined(MESH_OFFGRIDNL_V19)
  // V19 assumes the installer already performed the destructive factory clean.
'''
    repl='''#if defined(MESH_OFFGRIDNL_V21)
  // V21 Wi-Fi First: the station is initialized by the existing setup/loop
  // owner. Association itself is deliberately minimal and mirrors the
  // official Arduino/Espressif station path.
  WiFi.setAutoReconnect(false);
  const char* v21_pwd = (pwd && pwd[0]) ? pwd : nullptr;

  // First attempt installs credentials through the normal Arduino path.
  // Later application retries use reconnect(), which reuses the station
  // configuration without tearing down or rewriting it.
  if (g_v16_wifi_attempt <= 1) {
    WiFi.persistent(false);
    Serial.printf("[V21][wifi] attempt=1 begin ssid='%s' previous_reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.begin(ssid, v21_pwd);
  } else {
    Serial.printf("[V21][wifi] attempt=%u reconnect previous_reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt,
                  (unsigned)g_wifi_last_disc_reason);
    WiFi.reconnect();
  }
  return;
#elif defined(MESH_OFFGRIDNL_V19)
  // V19 assumes the installer already performed the destructive factory clean.
'''
    main=one(main,anchor,repl,"V21 minimal association path")

    # V21 uses a slower bounded cadence so a WPA/DHCP attempt is not reset
    # prematurely on an Android hotspot.  V16 remains the sole retry owner.
    main=one(
        main,
        '''#if defined(MESH_OFFGRIDNL_V16)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 12000;
  static const uint32_t WIFI_RETRY_BACKOFF_MS = 60000;
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 20000;
  static const uint32_t WIFI_RETRY_BACKOFF_MS = 60000;
#elif defined(MESH_OFFGRIDNL_V16)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 12000;
  static const uint32_t WIFI_RETRY_BACKOFF_MS = 60000;
''',
        "V21 retry timing"
    )

    # V21 must not inherit V13's pre-disconnect retry or V19's redundant
    # first-boot sanitizer. A full-chip installer reset already provides a clean
    # starting point, and retries should let the station stack own association.
    main=one(
        main,
        '''#if defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry
#else
          WiFi.disconnect(false, true);
#endif
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
          // V21: no pre-disconnect. The single owner calls v13WifiBegin(), whose
          // V21 branch performs the one association request.
#elif defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false, false);   // legacy V13-V20 behavior
#else
          WiFi.disconnect(false, true);
#endif
''',
        "V21 remove pre-disconnect retry"
    )
    main=one(
        main,
        '''#if defined(MESH_OFFGRIDNL_V19)
  {
    // V11-V18 preserved internal NVS and WadaMesh touch builds can additionally
''',
        '''#if defined(MESH_OFFGRIDNL_V19) && !defined(MESH_OFFGRIDNL_V21)
  {
    // V11-V18 preserved internal NVS and WadaMesh touch builds can additionally
''',
        "V21 disable redundant V19 first-boot sanitizer"
    )
    main=one(
        main,
        '''#if defined(MESH_OFFGRIDNL_V17)
  // Galaxy S21 compatibility path: configure ESP-IDF station security
''',
        '''#if defined(MESH_OFFGRIDNL_V17) && !defined(MESH_OFFGRIDNL_V21)
  // Legacy V17 only: configure ESP-IDF station security
''',
        "V21 compile out obsolete V17 raw-driver fallback"
    )

    # Event callbacks stay tiny. V21 logs phase changes later from loop(), never
    # from Arduino's Wi-Fi event task.
    main=one(
        main,
        '''    /* SNTP: kick off when Wi-Fi associates; once system time syncs, push it
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
    {
      static uint8_t s_v21_last_phase = 255;
      if (s_v21_last_phase != g_v16_wifi_phase) {
        s_v21_last_phase = g_v16_wifi_phase;
        if (g_v16_wifi_phase == 2) {
          Serial.println("[V21][wifi] phase=STA_CONNECTED waiting_for=GOT_IP");
        } else if (g_v16_wifi_phase == 3) {
          Serial.printf("[V21][wifi] phase=GOT_IP ip=%s rssi=%d channel=%d\\n",
                        WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
        } else if (g_v16_wifi_phase == 4) {
          Serial.printf("[V21][wifi] phase=DISCONNECTED reason=%u\\n",
                        (unsigned)g_wifi_last_disc_reason);
        }
      }
    }
#endif
    /* SNTP: kick off when Wi-Fi associates; once system time syncs, push it
''',
        "V21 loop-side phase diagnostics"
    )
    main_p.write_text(main)

    # ------------------------------------------------------------------
    # 3. Make the UI describe the new state machine instead of V19 attempts.
    # ------------------------------------------------------------------
    ui=ui_p.read_text()

    # Arduino-ESP32 3.x removed the channel-based ledcSetup/ledcAttachPin API.
    # Keep the exact legacy path for Arduino 2.x, but use the pin-based API on
    # the modern V21 toolchain. ledcWrite() is pin-based in 3.x as well.
    ui=one(
        ui,
        '''static void applyBrightness(uint8_t pct) {
  if (pct < 5)   pct = 5;
  if (pct > 100) pct = 100;
  s_brightness_pct = pct;
  if (!s_bl_pwm_ready) {
    ledcSetup(kBlPwmChannel, 20000, 8);              // 20 kHz, 8-bit
    ledcAttachPin(PIN_TFT_LEDA_CTL, kBlPwmChannel);  // takes the pin over from the display's digitalWrite
    s_bl_pwm_ready = true;
  }
  ledcWrite(kBlPwmChannel, (uint32_t)pct * 255u / 100u);
}
''',
        '''static void applyBrightness(uint8_t pct) {
  if (pct < 5)   pct = 5;
  if (pct > 100) pct = 100;
  s_brightness_pct = pct;
  if (!s_bl_pwm_ready) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    s_bl_pwm_ready = ledcAttach(PIN_TFT_LEDA_CTL, 20000, 8);
#else
    ledcSetup(kBlPwmChannel, 20000, 8);              // 20 kHz, 8-bit
    ledcAttachPin(PIN_TFT_LEDA_CTL, kBlPwmChannel);  // takes the pin over from the display's digitalWrite
    s_bl_pwm_ready = true;
#endif
  }
  if (!s_bl_pwm_ready) return;
  const uint32_t duty = (uint32_t)pct * 255u / 100u;
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_TFT_LEDA_CTL, duty);
#else
  ledcWrite(kBlPwmChannel, duty);
#endif
}
''',
        "V21 Arduino3 LEDC backlight compatibility"
    )

    ui=one(
        ui,
        '''#if defined(MESH_OFFGRIDNL_V19)
  if (wifiConfigGetRadioEnabled() && !wifiConfigHasRuntime())
    return "V19 clean: choose network";
#endif
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
  if (wifiConfigGetRadioEnabled() && !wifiConfigHasRuntime())
    return "V21 Wi-Fi: choose network";
#elif defined(MESH_OFFGRIDNL_V19)
  if (wifiConfigGetRadioEnabled() && !wifiConfigHasRuntime())
    return "V19 clean: choose network";
#endif
''',
        "V21 ready UI"
    )
    ui=one(
        ui,
        '''#if defined(MESH_OFFGRIDNL_V19)
      return "V19 linked -> DHCP...";
#else
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
      return "V21 linked -> DHCP...";
#elif defined(MESH_OFFGRIDNL_V19)
      return "V19 linked -> DHCP...";
#else
''',
        "V21 DHCP UI"
    )
    ui=one(
        ui,
        '''#if defined(MESH_OFFGRIDNL_V19)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 fresh connect (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 plain retry (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "V19 radio retry (3/3)");
#elif defined(MESH_OFFGRIDNL_V18)
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
      snprintf(s_v16_join, sizeof s_v16_join, "V21 associating... (%u)",
               (unsigned)(g_v16_wifi_attempt ? g_v16_wifi_attempt : 1));
#elif defined(MESH_OFFGRIDNL_V19)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 fresh connect (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 plain retry (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "V19 radio retry (3/3)");
#elif defined(MESH_OFFGRIDNL_V18)
''',
        "V21 associating UI"
    )
    ui=one(
        ui,
        '''#if defined(MESH_OFFGRIDNL_V19)
  if (g_lv.task) g_lv.task->showAlert("V19 fresh Wi-Fi...", 1800);
#else
''',
        '''#if defined(MESH_OFFGRIDNL_V21)
  if (g_lv.task) g_lv.task->showAlert("V21 Wi-Fi connecting...", 1800);
#elif defined(MESH_OFFGRIDNL_V19)
  if (g_lv.task) g_lv.task->showAlert("V19 fresh Wi-Fi...", 1800);
#else
''',
        "V21 connect banner"
    )
    ui_p.write_text(ui)

    # ------------------------------------------------------------------
    # Contracts
    # ------------------------------------------------------------------
    pio=pio_p.read_text(); main=main_p.read_text(); ui=ui_p.read_text()
    serial_ble=serial_ble_dst.read_text()
    for marker in (
        "SerialBLEInterface::begin",
        "SerialBLEInterface::enable",
        "SerialBLEInterface::disable",
        "SerialBLEInterface::writeFrame",
        "SerialBLEInterface::checkRecvFrame",
    ):
        if marker not in serial_ble: fail("vendored SerialBLE implementation missing "+marker)
    for m in (
        "MESH_OFFGRIDNL_V21=1",
        "MESH_V21_WIFI_SINGLE_OWNER=1",
        "MESH_V21_WIFI_PRIORITY=1",
        "55.03.312-1/platform-espressif32.zip",
        "SD=file://${platformio.packages_dir}/framework-arduinoespressif32/libraries/SD",
        "-Wno-error=return-type",
    ):
        if m not in pio: fail("platformio missing "+m)

    start=main.find("#if defined(MESH_OFFGRIDNL_V21)", main.find("static void v13WifiBegin"))
    end=main.find("#elif defined(MESH_OFFGRIDNL_V19)", start)
    if start<0 or end<0: fail("V21 association block missing")
    block=main[start:end]
    for must in ("WiFi.persistent(false);","WiFi.setAutoReconnect(false);","WiFi.begin(ssid, v21_pwd);","WiFi.reconnect();"):
        if must not in block: fail("V21 association missing "+must)
    for bad in (
        "WiFi.disconnect(", "WiFi.mode(", "setScanMethod", "setSortMethod",
        "setMinSecurity", "pmf_cfg", "sae_pwe", "esp_wifi_", "bssid", ".channel ="
    ):
        if bad in block: fail("V21 minimal path contains forbidden behavior: "+bad)

    for marker in (
        "[V21][wifi] phase=DISCONNECTED",
        "[V21][wifi] phase=STA_CONNECTED",
        "[V21][wifi] phase=GOT_IP",
        "WIFI_RETRY_INTERVAL_MS = 20000",
        "esp_task_wdt_reconfigure(&twdt)",
        "[V21][wdt] 20s config",
    ):
        if marker not in main: fail("main missing "+marker)

    for marker in (
        "V21 Wi-Fi: choose network",
        "V21 linked -> DHCP...",
        "V21 associating...",
        "V21 Wi-Fi connecting...",
        "ledcAttach(PIN_TFT_LEDA_CTL, 20000, 8)",
        "ledcWrite(PIN_TFT_LEDA_CTL, duty)",
    ):
        if marker not in ui: fail("UI missing "+marker)

    print("V21 applied: modern Arduino/IDF + minimal single-owner Wi-Fi association path")

if __name__=="__main__":
    main()
