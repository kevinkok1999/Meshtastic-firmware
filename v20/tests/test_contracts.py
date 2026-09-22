#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(m): raise SystemExit("V20 contract failure: "+m)

def main():
    if len(sys.argv)!=2: die("usage: test_contracts.py <patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio=(root/"platformio.ini").read_text()
    main=(root/"src/main.cpp").read_text()
    mesh=(root/"src/MyMesh.cpp").read_text()
    console=(root/"src/ui-touch/ConsoleUI.cpp").read_text()
    ai=(root/"src/mesh-ai/MeshAiCore.h").read_text()

    for f in (
      "MESH_OFFGRIDNL_V11=1","MESH_OFFGRIDNL_V12=1","MESH_OFFGRIDNL_V13=1",
      "MESH_OFFGRIDNL_V15=1","MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1",
      "MESH_OFFGRIDNL_V18=1","MESH_OFFGRIDNL_V19=1","MESH_OFFGRIDNL_V20=1",
      "MESH_AI_ENABLED=1","MESH_AI_TINY_ML=0","MESH_AI_VOICE=0",
    ):
      if f not in pio: die("missing "+f)

    # V19 Wi-Fi recovery must remain untouched.
    for m in (
      'v19_factory.begin("mog_v19", false)',
      "wifiConfigClear();",
      "SdNvsPrefs::flush(4000)",
      "[V19][wifi] attempt=%u standard join",
      "[V19][wifi] attempt=%u safe STA restart",
    ):
      if m not in main: die("lost V19 marker "+m)

    s=main.find("#if defined(MESH_OFFGRIDNL_V19)", main.find("static void v13WifiBegin"))
    e=main.find("#elif defined(MESH_OFFGRIDNL_V18)", s)
    if s<0 or e<0: die("V19 association block missing")
    block=main[s:e]
    for bad in ("WiFi.disconnect(true","setMinSecurity","pmf_cfg","sae_pwe_h2e",
                "esp_wifi_set_protocol","esp_wifi_set_bandwidth","bssid_set",".channel ="):
      if bad in block: die("V19 association regression "+bad)

    for m in (
      '#include "mesh-ai/MeshAiCore.h"',
      "MeshAi::isCommand(cmd)",
      "MeshAi::Context ctx{};",
      "ctx.contacts_total = getNumContacts();",
      "ctx.contacts_recent++",
      "ctx.internal_heap_free",
      "ctx.psram_free",
      "ctx.wifi_connected",
      "ctx.gps_lat = sensors.node_lat;",
      "MeshAi::answer(cmd, ctx, ai_reply",
      "pushMeshcomodReply(ai_reply, true);",
    ):
      if m not in mesh: die("AI integration missing "+m)

    # AI must be advisory and use already-collected state only.
    for bad in (
      "esp_wifi_set_", "radio_driver.", "setRadioParams(", "setMinSecurity(",
      "private_key", "channel_psk", "wifi_password", "WiFi.begin("
    ):
      if bad in ai: die("AI core contains forbidden direct control/secret path "+bad)

    if "Mesh AI: type 'ai help' (fully local)" not in console:
      die("console discoverability missing")

    if len(ai) > 24000:
      die("AI header unexpectedly large")

    print("V20 contracts OK")

if __name__=="__main__":
    main()
