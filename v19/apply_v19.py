#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(m): raise SystemExit("V19 patch failed: "+m)
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
    if len(sys.argv)!=2: fail("usage: apply_v19.py <V18-patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio_p=root/"platformio.ini"; main_p=root/"src/main.cpp"; ui_p=root/"src/ui-touch/UITask.cpp"
    for p in (pio_p,main_p,ui_p):
        if not p.exists(): fail("missing "+str(p))

    pio=pio_p.read_text()
    if "MESH_OFFGRIDNL_V18=1" not in pio: fail("V18 base is not present")
    pio=section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V18=1\n",
        "  -D MESH_OFFGRIDNL_V18=1\n  -D MESH_OFFGRIDNL_V19=1\n",
        "V19 build flag",
    )
    pio_p.write_text(pio)

    main=main_p.read_text()

    # Deep fix 1: sanitize the ACTIVE WadaMesh prefs backend once on V19.
    # This runs after SdNvsPrefs::useFile(...) selected SPIFFS or SD and after
    # wifiConfigBegin(), so it clears whichever backend is actually authoritative.
    boot_anchor='''#if defined(WIFI_SSID) || defined(MULTI_TRANSPORT_COMPANION)
  wifiConfigBegin();
  Serial.println("[BOOT] wifiConfig ok");
'''
    boot_repl='''#if defined(WIFI_SSID) || defined(MULTI_TRANSPORT_COMPANION)
  wifiConfigBegin();
  Serial.println("[BOOT] wifiConfig ok");
#if defined(MESH_OFFGRIDNL_V19)
  {
    // V11-V18 preserved internal NVS and WadaMesh touch builds can additionally
    // keep Wi-Fi prefs in SPIFFS or SD. Clean the backend that is actually active.
    Preferences v19_factory;
    bool already_clean = false;
    if (v19_factory.begin("mog_v19", false)) {
      already_clean = v19_factory.getBool("wifi_clean", false);
      if (!already_clean) {
        Serial.println("[V19][factory] sanitizing active Wi-Fi preferences backend");
        wifiConfigClear();
        wifiConfigSetRadioEnabled(true);
        wifiConfigSetWifiChosen(true);

        const bool runtime_clear = !wifiConfigHasRuntime();
        const bool prefs_flushed = SdNvsPrefs::flush(4000);
        if (runtime_clear && prefs_flushed) {
          v19_factory.putBool("wifi_clean", true);
          Serial.println("[V19][factory] Wi-Fi preferences clean + flushed");
        } else {
          Serial.printf("[V19][factory] sanitize incomplete runtime_clear=%d flushed=%d; will retry next boot\\n",
                        (int)runtime_clear, (int)prefs_flushed);
        }
      }
      v19_factory.end();
    } else {
      Serial.println("[V19][factory] marker NVS unavailable; Wi-Fi sanitize marker not committed");
    }
  }
#endif
'''
    main=one(main,boot_anchor,boot_repl,"V19 active-backend first-boot sanitizer")

    # Deep fix 2: do not execute V18's erase-AP path on V19.
    assoc_anchor='''#if defined(MESH_OFFGRIDNL_V18)
  // V18 follows the conservative recovery strategy used by mature ESP32
'''
    assoc_repl='''#if defined(MESH_OFFGRIDNL_V19)
  // V19 assumes the installer already performed the destructive factory clean.
  // Runtime joining is intentionally boring: no erase-AP, no forced PMF/SAE,
  // no BSSID/channel pin and no PHY/bandwidth override.
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  wifiConfigClearApHint();
  const char* v19_pwd = (pwd && pwd[0]) ? pwd : nullptr;

  if (g_v16_wifi_attempt >= 3) {
    Serial.printf("[V19][wifi] attempt=%u safe STA restart ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(150);
    WiFi.mode(WIFI_OFF);
    delay(350);
    WiFi.mode(WIFI_STA);
    delay(200);
  } else {
    Serial.printf("[V19][wifi] attempt=%u standard join ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(g_v16_wifi_attempt <= 1 ? 120 : 300);
    WiFi.mode(WIFI_STA);
  }

  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid, v19_pwd);
  return;
#elif defined(MESH_OFFGRIDNL_V18)
  // V18 follows the conservative recovery strategy used by mature ESP32
'''
    main=one(main,assoc_anchor,assoc_repl,"V19 standard association path")
    main_p.write_text(main)

    ui=ui_p.read_text()

    # Make V19 unmistakable even before credentials are entered.
    ui=one(
        ui,
        '''static const char* wifiStaStatusBrief(int s) {
#if defined(MESH_OFFGRIDNL_V16)
''',
        '''static const char* wifiStaStatusBrief(int s) {
#if defined(MESH_OFFGRIDNL_V19)
  if (wifiConfigGetRadioEnabled() && !wifiConfigHasRuntime())
    return "V19 clean: choose network";
#endif
#if defined(MESH_OFFGRIDNL_V16)
''',
        "V19 clean-ready status",
    )

    ui=one(
        ui,
        '''    if (g_v16_wifi_phase == 2) return "linked, getting IP...";
    if (g_v16_wifi_phase == 1) {
''',
        '''    if (g_v16_wifi_phase == 2) {
#if defined(MESH_OFFGRIDNL_V19)
      return "V19 linked -> DHCP...";
#else
      return "linked, getting IP...";
#endif
    }
    if (g_v16_wifi_phase == 1) {
''',
        "V19 DHCP phase status",
    )

    ui=one(
        ui,
        '''#if defined(MESH_OFFGRIDNL_V18)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "clean S21 connect... (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "standard S21 retry... (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "S21 security fallback... (3/3)");
#elif defined(MESH_OFFGRIDNL_V17)
''',
        '''#if defined(MESH_OFFGRIDNL_V19)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 fresh connect (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "V19 plain retry (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "V19 radio retry (3/3)");
#elif defined(MESH_OFFGRIDNL_V18)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "clean S21 connect... (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "standard S21 retry... (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "S21 security fallback... (3/3)");
#elif defined(MESH_OFFGRIDNL_V17)
''',
        "V19 staged UI status",
    )

    ui=one(
        ui,
        '''  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
#if defined(MESH_OFFGRIDNL_V16)
''',
        '''#if defined(MESH_OFFGRIDNL_V19)
  if (g_lv.task) g_lv.task->showAlert("V19 fresh Wi-Fi...", 1800);
#else
  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
#endif
#if defined(MESH_OFFGRIDNL_V16)
''',
        "V19 immediate join banner",
    )
    ui_p.write_text(ui)

    pio=pio_p.read_text(); main=main_p.read_text(); ui=ui_p.read_text()
    for f in ("MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1","MESH_OFFGRIDNL_V18=1","MESH_OFFGRIDNL_V19=1"):
        if f not in pio: fail("missing "+f)

    for marker in (
        'v19_factory.begin("mog_v19", false)',
        "wifiConfigClear();",
        "wifiConfigSetRadioEnabled(true);",
        "wifiConfigSetWifiChosen(true);",
        "SdNvsPrefs::flush(4000)",
        "[V19][factory] Wi-Fi preferences clean + flushed",
        "[V19][wifi] attempt=%u standard join",
        "[V19][wifi] attempt=%u safe STA restart",
        "WiFi.disconnect(false, false);",
        "WiFi.mode(WIFI_OFF);",
        "WiFi.begin(ssid, v19_pwd);",
    ):
        if marker not in main: fail("main missing "+marker)

    start=main.find("#if defined(MESH_OFFGRIDNL_V19)", main.find("static void v13WifiBegin"))
    end=main.find("#elif defined(MESH_OFFGRIDNL_V18)", start)
    if start<0 or end<0: fail("V19 association block missing")
    block=main[start:end]
    for bad in (
        "WiFi.disconnect(true",
        "setMinSecurity",
        "pmf_cfg",
        "sae_pwe_h2e",
        "esp_wifi_set_protocol",
        "esp_wifi_set_bandwidth",
        "bssid_set",
        ".channel =",
    ):
        if bad in block: fail("V19 normal path contains forbidden override "+bad)

    for marker in (
        "V19 clean: choose network",
        "V19 linked -> DHCP...",
        "V19 fresh connect (1/3)",
        "V19 plain retry (2/3)",
        "V19 radio retry (3/3)",
        "V19 fresh Wi-Fi...",
    ):
        if marker not in ui: fail("UI missing "+marker)

    print("V19 applied: factory-clean prefs sanitation + no erase-AP runtime + visible standard association diagnostics")

if __name__=="__main__":
    main()
