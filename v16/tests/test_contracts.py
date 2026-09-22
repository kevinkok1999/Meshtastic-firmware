#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(m): raise SystemExit("V16 contract failure: "+m)

def main():
    if len(sys.argv)!=2: die("usage: test_contracts.py <patched checkout>")
    r=pathlib.Path(sys.argv[1]).resolve()
    pio=(r/"platformio.ini").read_text()
    main=(r/"src/main.cpp").read_text()
    ui=(r/"src/ui-touch/UITask.cpp").read_text()
    prefs=(r/"src/helpers/esp32/TouchPrefsStore.cpp").read_text()
    store=(r/"src/helpers/esp32/WifiRuntimeStore.cpp").read_text()

    for flag in ("MESH_OFFGRIDNL_V11=1","MESH_OFFGRIDNL_V12=1","MESH_OFFGRIDNL_V13=1",
                 "MESH_OFFGRIDNL_V15=1","MESH_OFFGRIDNL_V16=1"):
        if flag not in pio: die("missing "+flag)

    for x in (
      "static void v16WifiBeginStage",
      "[V16][wifi] stage1 directed",
      "[V16][wifi] stage2 all-channel",
      "[V16][wifi] stage3 soft-recovery",
      "WIFI_RETRY_INTERVAL_MS = 12000",
      "v16WifiAdvanceSequence",
      "foreground join failed",
      "WiFi.disconnect(false, false)",
    ):
        if x not in main: die("main missing "+x)

    if "wifiConfigClearApHint();  // selected BSSID/channel is one-shot in V16" not in main:
        die("BSSID/channel hint is not one-shot")
    if "wifiJoinSetInProgress(true);" not in prefs:
        die("foreground join latch missing")
    if "!wifiJoinInProgress()) wifiKickScan();" not in ui:
        die("scan-after-connect suppression missing")
    if "retrying all channels..." not in ui:
        die("staged status text missing")
    if "s_v16_wifi_join_in_progress" not in store:
        die("join state store missing")

    i=main.find("static void v16WifiBeginStage")
    j=main.find("static void v16WifiStartSequence",i)
    helper=main[i:j]
    if "WiFi.setSleep(false)" in helper: die("V15 crash trigger reintroduced")
    if "WiFi.setAutoReconnect(true)" in helper: die("parallel reconnect owner in V16 helper")
    if "g_wifi_last_disc_reason = 0" in helper: die("disconnect reason erased between stages")
    print("V16 contracts OK")

if __name__=="__main__": main()
