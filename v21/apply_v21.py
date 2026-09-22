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
    for p in (pio_p,main_p,ui_p):
        if not p.exists(): fail("missing "+str(p))

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
        "  -D MESH_V21_WIFI_PRIORITY=1\n",
        "V21 flags"
    )
    pio_p.write_text(pio)

    # ------------------------------------------------------------------
    # 2. Replace the V19 association body with the smallest possible
    #    event-driven Arduino station join for V21.  No disconnect(), mode
    #    cycling, scan tuning, BSSID/channel pinning, PMF/SAE forcing or PHY
    #    overrides on the normal path.
    # ------------------------------------------------------------------
    main=main_p.read_text()
    anchor='''#if defined(MESH_OFFGRIDNL_V19)
  // V19 assumes the installer already performed the destructive factory clean.
'''
    repl='''#if defined(MESH_OFFGRIDNL_V21)
  // V21 Wi-Fi First: the station is initialized by the existing setup/loop
  // owner. Association itself is deliberately minimal and mirrors the
  // official Arduino/Espressif station path.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  wifiConfigClearApHint();
  const char* v21_pwd = (pwd && pwd[0]) ? pwd : nullptr;

  Serial.printf("[V21][wifi] attempt=%u begin ssid='%s' previous_reason=%u\\n",
                (unsigned)g_v16_wifi_attempt, ssid,
                (unsigned)g_wifi_last_disc_reason);

  // Do not mutate station mode, tear down the live link, tune scan/security,
  // pin an AP, or call low-level Wi-Fi driver mutators here. WiFi.begin() owns
  // association and DHCP is observed independently through GOT_IP.
  WiFi.begin(ssid, v21_pwd);
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

    # Add low-cost event diagnostics.  The callbacks only record state; no
    # connect/disconnect/scan operation is executed from the event task.
    main=one(
        main,
        '''    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        g_wifi_last_disc_reason = info.wifi_sta_disconnected.reason;
#if defined(MESH_OFFGRIDNL_V16)
        g_v16_wifi_phase = 4;
#endif
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
''',
        '''    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        g_wifi_last_disc_reason = info.wifi_sta_disconnected.reason;
#if defined(MESH_OFFGRIDNL_V16)
        g_v16_wifi_phase = 4;
#endif
#if defined(MESH_OFFGRIDNL_V21)
        Serial.printf("[V21][wifi] event=DISCONNECTED reason=%u\\n",
                      (unsigned)info.wifi_sta_disconnected.reason);
#endif
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
''',
        "V21 disconnect event log"
    )
    main=one(
        main,
        '''        } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 2;
#endif
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
''',
        '''        } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 2;
#endif
#if defined(MESH_OFFGRIDNL_V21)
          Serial.println("[V21][wifi] event=STA_CONNECTED waiting_for=GOT_IP");
#endif
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
''',
        "V21 connected event log"
    )
    main=one(
        main,
        '''          g_v16_wifi_phase = 3;
          g_v16_wifi_join_in_progress = false;
          g_v16_wifi_attempt = 0;
#endif
        }
''',
        '''          g_v16_wifi_phase = 3;
          g_v16_wifi_join_in_progress = false;
          g_v16_wifi_attempt = 0;
#endif
#if defined(MESH_OFFGRIDNL_V21)
          Serial.printf("[V21][wifi] event=GOT_IP ip=%s rssi=%d channel=%d\\n",
                        WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
#endif
        }
''',
        "V21 got-ip event log"
    )
    main_p.write_text(main)

    # ------------------------------------------------------------------
    # 3. Make the UI describe the new state machine instead of V19 attempts.
    # ------------------------------------------------------------------
    ui=ui_p.read_text()
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
    for m in (
        "MESH_OFFGRIDNL_V21=1",
        "MESH_V21_WIFI_SINGLE_OWNER=1",
        "MESH_V21_WIFI_PRIORITY=1",
        "55.03.312-1/platform-espressif32.zip",
    ):
        if m not in pio: fail("platformio missing "+m)

    start=main.find("#if defined(MESH_OFFGRIDNL_V21)", main.find("static void v13WifiBegin"))
    end=main.find("#elif defined(MESH_OFFGRIDNL_V19)", start)
    if start<0 or end<0: fail("V21 association block missing")
    block=main[start:end]
    for must in ("WiFi.persistent(false);","WiFi.setAutoReconnect(false);","WiFi.begin(ssid, v21_pwd);"):
        if must not in block: fail("V21 association missing "+must)
    for bad in (
        "WiFi.disconnect(", "WiFi.mode(", "setScanMethod", "setSortMethod",
        "setMinSecurity", "pmf_cfg", "sae_pwe", "esp_wifi_", "bssid", ".channel ="
    ):
        if bad in block: fail("V21 minimal path contains forbidden behavior: "+bad)

    for marker in (
        "[V21][wifi] event=DISCONNECTED",
        "[V21][wifi] event=STA_CONNECTED",
        "[V21][wifi] event=GOT_IP",
        "WIFI_RETRY_INTERVAL_MS = 20000",
    ):
        if marker not in main: fail("main missing "+marker)

    for marker in (
        "V21 Wi-Fi: choose network",
        "V21 linked -> DHCP...",
        "V21 associating...",
        "V21 Wi-Fi connecting...",
    ):
        if marker not in ui: fail("UI missing "+marker)

    print("V21 applied: modern Arduino/IDF + minimal single-owner Wi-Fi association path")

if __name__=="__main__":
    main()
