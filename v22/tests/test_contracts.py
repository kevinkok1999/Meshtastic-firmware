#!/usr/bin/env python3
from pathlib import Path
import sys
r=Path(sys.argv[1]); p=(r/"platformio.ini").read_text(); w=(r/"src/mesh/wifi/WiFiAPClient.cpp").read_text()
for m in ("MESH_OFFGRIDNL_V22=1","MESH_V22_WIFI_SINGLE_OWNER=1","MESH_V22_WIFI_DIAGNOSTICS=1","WiFi.begin(wifiName, wifiPsw);","WiFi.reconnect();","[V22][wifi] phase=GOT_IP","[V22][wifi] phase=DISCONNECTED"):
    if m not in p+w: raise SystemExit("V22 contract failure: "+m)
b=w[w.find("#if defined(MESH_OFFGRIDNL_V22)"):w.find("#elif defined(MESH_OFFGRIDNL_V21)")]
if "WiFi.disconnect(" in b or "WIFI_MODE_NULL" in b: raise SystemExit("V22 association resets radio")
print("V22 contracts OK")
