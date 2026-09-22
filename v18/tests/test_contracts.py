#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(m): raise SystemExit("V18 contract failure: "+m)

def main():
    if len(sys.argv)!=2: die("usage: test_contracts.py <patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio=(root/"platformio.ini").read_text()
    main=(root/"src/main.cpp").read_text()
    ui=(root/"src/ui-touch/UITask.cpp").read_text()

    for f in (
        "MESH_OFFGRIDNL_V11=1","MESH_OFFGRIDNL_V12=1","MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1","MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1",
        "MESH_OFFGRIDNL_V18=1",
    ):
        if f not in pio: die("missing "+f)

    required=[
        "WiFi.persistent(false);",
        "WiFi.disconnect(true, true);",
        "WiFi.mode(WIFI_MODE_NULL);",
        "delay(1200);",
        "WiFi.mode(WIFI_STA);",
        "WiFi.disconnect(false, true);",
        "WiFi.begin(ssid, v18_pwd);",
        "[V18][wifi] attempt=1 clean STA boot",
        "[V18][wifi] attempt=2 plain retry",
        "entering V17 PMF/SAE fallback",
    ]
    for m in required:
        if m not in main: die("main.cpp missing "+m)

    v18s=main.find("#if defined(MESH_OFFGRIDNL_V18)", main.find("static void v13WifiBegin"))
    v17s=main.find("#if defined(MESH_OFFGRIDNL_V17)",v18s)
    if v18s<0 or v17s<0: die("staged helper boundaries missing")
    block=main[v18s:v17s]
    for bad in ("WiFi.setSleep(false)","esp_wifi_set_protocol","esp_wifi_set_bandwidth"):
        if bad in block: die("V18 normal path contains forbidden override "+bad)
    if "bssid" in block.lower() or ".channel =" in block:
        die("V18 normal path pins BSSID/channel")

    # The direct V17 path must remain as attempt-3 fallback.
    for m in (
        "cfg.sta.pmf_cfg.capable = true;",
        "cfg.sta.pmf_cfg.required = false;",
        "cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;",
        "esp_wifi_set_config(WIFI_IF_STA, &cfg)",
    ):
        if m not in main: die("lost V17 fallback "+m)

    # V16 race/reconnect protections stay present.
    for m in (
        "esp_wifi_scan_stop();",
        "V16: exactly one reconnect owner, even after scans",
        "WIFI_RETRY_BACKOFF_MS = 60000",
        "g_v16_wifi_join_in_progress",
    ):
        if m not in (main+"\n"+ui): die("lost V16 safety marker "+m)

    for m in (
        "clean S21 connect... (1/3)",
        "standard S21 retry... (2/3)",
        "S21 security fallback... (3/3)",
    ):
        if m not in ui: die("UI phase missing "+m)

    print("V18 contracts OK")

if __name__=="__main__":
    main()
