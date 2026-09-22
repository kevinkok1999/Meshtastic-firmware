#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(m): raise SystemExit("V17 contract failure: "+m)

def main():
    if len(sys.argv)!=2: die("usage: test_contracts.py <patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio=(root/"platformio.ini").read_text()
    main=(root/"src/main.cpp").read_text()
    ui=(root/"src/ui-touch/UITask.cpp").read_text()

    for f in (
        "MESH_OFFGRIDNL_V11=1","MESH_OFFGRIDNL_V12=1","MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1","MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1"
    ):
        if f not in pio: die("missing "+f)

    required=[
        "wifi_config_t cfg = {};",
        "WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N",
        "WIFI_BW_HT20",
        "cfg.sta.bssid_set = false;",
        "cfg.sta.channel = 0;",
        "WIFI_ALL_CHANNEL_SCAN",
        "WIFI_AUTH_WPA2_PSK",
        "cfg.sta.pmf_cfg.capable = true;",
        "cfg.sta.pmf_cfg.required = false;",
        "cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;",
        "cfg.sta.failure_retry_cnt = 2;",
        "esp_wifi_set_config(WIFI_IF_STA, &cfg)",
        "esp_wifi_connect()",
        "[V17][S21]",
        "CONFIG_ESP32_WIFI_ENABLE_WPA3_SAE",
    ]
    for m in required:
        if m not in main: die("main.cpp missing "+m)

    start=main.find("// Galaxy S21 compatibility path:", main.find("static void v13WifiBegin"))
    end=main.find("  return;\n",start)
    if start<0 or end<0: die("V17 helper block missing")
    block=main[start:end]
    if "WiFi.begin(" in block: die("V17 still delegates association to WiFi.begin")
    if "WiFi.setSleep(false)" in block: die("unsafe sleep override returned")

    if "S21 hotspot auth... (%u/3)" not in ui:
        die("S21 phase status missing")

    # V16 safety rules must remain.
    for m in (
        "esp_wifi_scan_stop();",
        "V16: exactly one reconnect owner, even after scans",
        "WIFI_RETRY_BACKOFF_MS = 60000",
        "g_v16_wifi_join_in_progress",
    ):
        if m not in (ui+"\n"+main): die("lost V16 safety marker "+m)

    print("V17 contracts OK")

if __name__=="__main__": main()
