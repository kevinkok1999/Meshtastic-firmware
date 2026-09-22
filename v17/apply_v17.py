#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(m): raise SystemExit("V17 patch failed: "+m)
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
    if len(sys.argv)!=2: fail("usage: apply_v17.py <V16-patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio_p=root/"platformio.ini"; main_p=root/"src/main.cpp"; ui_p=root/"src/ui-touch/UITask.cpp"
    for p in (pio_p,main_p,ui_p):
        if not p.exists(): fail("missing "+str(p))

    pio=pio_p.read_text()
    if "MESH_OFFGRIDNL_V16=1" not in pio: fail("V16 base is not present")
    pio=section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V16=1\n",
        "  -D MESH_OFFGRIDNL_V16=1\n  -D MESH_OFFGRIDNL_V17=1\n",
        "V17 build flag",
    )
    pio_p.write_text(pio)

    main=main_p.read_text()
    main=one(
        main,
        '#include "esp_task_wdt.h"   // task-watchdog reconfigure — see setup() (GH #56)\n',
        '#include <esp_wifi.h>\n#include "esp_task_wdt.h"   // task-watchdog reconfigure — see setup() (GH #56)\n',
        "esp_wifi include",
    )

    anchor='''static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;

  // A phone hotspot association is timing-sensitive. Keep the station awake
  // until GOT_IP; the existing post-connect path turns modem sleep back on.
  WiFi.setAutoReconnect(false);
'''
    replacement='''static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;

#if defined(MESH_OFFGRIDNL_V17)
  // Galaxy S21 compatibility path: configure ESP-IDF station security
  // explicitly instead of relying on Arduino WiFi.begin() defaults.
  WiFi.setAutoReconnect(false);
  wifiConfigClearApHint();

  wifi_config_t cfg = {};
  strlcpy(reinterpret_cast<char*>(cfg.sta.ssid), ssid, sizeof(cfg.sta.ssid));
  if (pwd && pwd[0])
    strlcpy(reinterpret_cast<char*>(cfg.sta.password), pwd, sizeof(cfg.sta.password));

  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.bssid_set = false;
  cfg.sta.channel = 0;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  cfg.sta.threshold.rssi = -127;
  cfg.sta.threshold.authmode = (pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

  // Samsung hotspots can use WPA2, WPA3 or transition mode. Advertise PMF
  // capability without requiring it so WPA2 stays compatible, while WPA3 APs
  // can require PMF. Enable both SAE password-element methods, including H2E.
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;
  cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
  cfg.sta.failure_retry_cnt = 2;

  esp_wifi_disconnect();

  esp_err_t proto_rc = esp_wifi_set_protocol(
      WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_err_t bw_rc = esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);
  esp_err_t cfg_rc = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  esp_err_t con_rc = (cfg_rc == ESP_OK) ? esp_wifi_connect() : cfg_rc;

#if defined(CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE) && CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE
  const int wpa3_sdk = 1;
#else
  const int wpa3_sdk = 0;
#endif
  Serial.printf("[V17][S21] attempt=%u ssid='%s' bgn=%d ht20=%d cfg=%d connect=%d wpa3sdk=%d pmf=optional sae=both\\n",
                (unsigned)g_v16_wifi_attempt, ssid,
                (int)proto_rc, (int)bw_rc, (int)cfg_rc, (int)con_rc, wpa3_sdk);
  return;
#endif

  WiFi.setAutoReconnect(false);
'''
    main=one(main,anchor,replacement,"V17 ESP-IDF Samsung association path")
    main_p.write_text(main)

    ui=ui_p.read_text()
    ui=one(
        ui,
        '''      snprintf(s_v16_join, sizeof s_v16_join, "associating... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
      return s_v16_join;
''',
        '''#if defined(MESH_OFFGRIDNL_V17)
      snprintf(s_v16_join, sizeof s_v16_join, "S21 hotspot auth... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
#else
      snprintf(s_v16_join, sizeof s_v16_join, "associating... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
#endif
      return s_v16_join;
''',
        "V17 association status",
    )
    ui_p.write_text(ui)

    pio=pio_p.read_text(); main=main_p.read_text(); ui=ui_p.read_text()
    for f in ("MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1"):
        if f not in pio: fail("missing "+f)
    for marker in (
        "wifi_config_t cfg = {};",
        "WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N",
        "WIFI_BW_HT20",
        "cfg.sta.bssid_set = false;",
        "cfg.sta.channel = 0;",
        "cfg.sta.pmf_cfg.capable = true;",
        "cfg.sta.pmf_cfg.required = false;",
        "cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;",
        "cfg.sta.failure_retry_cnt = 2;",
        "esp_wifi_set_config(WIFI_IF_STA, &cfg)",
        "esp_wifi_connect()",
        "[V17][S21]",
    ):
        if marker not in main: fail("main missing "+marker)
    if "S21 hotspot auth... (%u/3)" not in ui: fail("UI S21 status missing")

    start=main.find("// Galaxy S21 compatibility path:", main.find("static void v13WifiBegin"))
    end=main.find("  return;\n",start)
    if start<0 or end<0: fail("V17 helper block missing")
    block=main[start:end]
    if "WiFi.begin(" in block: fail("V17 path must not call Arduino WiFi.begin")
    if "WiFi.setSleep(false)" in block: fail("V17 must not reintroduce unsafe sleep override")

    print("V17 applied: Samsung S21 WPA2/WPA3 PMF+SAE compatibility via direct ESP-IDF station config")

if __name__=="__main__": main()
