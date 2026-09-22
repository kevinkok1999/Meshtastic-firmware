#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(m): raise SystemExit("V21 contract failure: "+m)

def main():
    if len(sys.argv)!=2: die("usage: test_contracts.py <patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio=(root/"platformio.ini").read_text()
    main=(root/"src/main.cpp").read_text()
    ui=(root/"src/ui-touch/UITask.cpp").read_text()

    for flag in (
        "MESH_OFFGRIDNL_V19=1","MESH_OFFGRIDNL_V20=1","MESH_OFFGRIDNL_V21=1",
        "MESH_V21_WIFI_SINGLE_OWNER=1","MESH_V21_WIFI_PRIORITY=1"
    ):
        if flag not in pio: die("missing "+flag)

    if "55.03.312-1/platform-espressif32.zip" not in pio:
        die("modern pioarduino platform not pinned")

    start=main.find("#if defined(MESH_OFFGRIDNL_V21)",main.find("static void v13WifiBegin"))
    end=main.find("#elif defined(MESH_OFFGRIDNL_V19)",start)
    if start<0 or end<0: die("V21 minimal association block missing")
    block=main[start:end]

    for good in (
        "WiFi.persistent(false);",
        "WiFi.setAutoReconnect(false);",
        "WiFi.begin(ssid, v21_pwd);",
    ):
        if good not in block: die("V21 association missing "+good)

    for bad in (
        "WiFi.disconnect(", "WiFi.mode(", "setScanMethod", "setSortMethod",
        "setMinSecurity", "esp_wifi_", "pmf_cfg", "sae_pwe", "bssid", ".channel ="
    ):
        if bad in block: die("forbidden V21 association behavior "+bad)

    for good in (
        "[V21][wifi] event=DISCONNECTED",
        "[V21][wifi] event=STA_CONNECTED",
        "[V21][wifi] event=GOT_IP",
        "WIFI_RETRY_INTERVAL_MS = 20000",
    ):
        if good not in main: die("missing diagnostic/state marker "+good)

    # V20 AI and older functionality must still be present.
    if "MeshAi::isCommand(cmd)" not in (root/"src/MyMesh.cpp").read_text():
        die("V20 Local AI integration lost")
    for marker in ("V21 Wi-Fi: choose network","V21 linked -> DHCP...","V21 associating..."):
        if marker not in ui: die("missing UI marker "+marker)

    print("V21 contracts OK")

if __name__=="__main__":
    main()
