#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(m): raise SystemExit("V19 contract failure: "+m)

def main():
    if len(sys.argv)!=2: die("usage: test_contracts.py <patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio=(root/"platformio.ini").read_text()
    main=(root/"src/main.cpp").read_text()
    ui=(root/"src/ui-touch/UITask.cpp").read_text()

    for f in (
        "MESH_OFFGRIDNL_V11=1","MESH_OFFGRIDNL_V12=1","MESH_OFFGRIDNL_V13=1",
        "MESH_OFFGRIDNL_V15=1","MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1",
        "MESH_OFFGRIDNL_V18=1","MESH_OFFGRIDNL_V19=1",
    ):
        if f not in pio: die("missing "+f)

    # V19 must clear the authoritative file-backed Wi-Fi prefs once, including
    # an SD-backed meshcomod profile, and only commit its marker after verifying.
    for m in (
        'v19_factory.begin("mog_v19", false)',
        'v19_factory.getBool("wifi_clean", false)',
        "wifiConfigClear();",
        "wifiConfigSetRadioEnabled(true);",
        "wifiConfigSetWifiChosen(true);",
        "const bool runtime_clear = !wifiConfigHasRuntime();",
        "const bool prefs_flushed = SdNvsPrefs::flush(4000);",
        'v19_factory.putBool("wifi_clean", true);',
    ):
        if m not in main: die("first-boot sanitizer missing "+m)

    s=main.find("#if defined(MESH_OFFGRIDNL_V19)", main.find("static void v13WifiBegin"))
    e=main.find("#elif defined(MESH_OFFGRIDNL_V18)", s)
    if s<0 or e<0: die("V19 association block missing")
    block=main[s:e]

    required=(
        "WiFi.setAutoReconnect(false);",
        "WiFi.persistent(false);",
        "wifiConfigClearApHint();",
        "WiFi.disconnect(false, false);",
        "WiFi.mode(WIFI_OFF);",
        "WiFi.mode(WIFI_STA);",
        "WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);",
        "WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);",
        "WiFi.begin(ssid, v19_pwd);",
    )
    for m in required:
        if m not in block: die("V19 association path missing "+m)

    for bad in (
        "WiFi.disconnect(true",
        "setMinSecurity",
        "pmf_cfg",
        "sae_pwe_h2e",
        "esp_wifi_set_protocol",
        "esp_wifi_set_bandwidth",
        "bssid_set",
        ".channel =",
        "WiFi.setSleep(false)",
    ):
        if bad in block: die("V19 association path contains forbidden override "+bad)

    # Preserve the proven race/crash hardening from earlier layers.
    for m in (
        "esp_wifi_scan_stop();",
        "V16: exactly one reconnect owner, even after scans",
        "WIFI_RETRY_BACKOFF_MS = 60000",
        "g_v16_wifi_join_in_progress",
    ):
        if m not in (main+"\n"+ui): die("lost earlier safety marker "+m)

    for m in (
        "V19 clean: choose network",
        "V19 linked -> DHCP...",
        "V19 fresh connect (1/3)",
        "V19 plain retry (2/3)",
        "V19 radio retry (3/3)",
        "V19 fresh Wi-Fi...",
    ):
        if m not in ui: die("V19 UI marker missing "+m)

    print("V19 contracts OK")

if __name__=="__main__":
    main()
