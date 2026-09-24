#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(msg: str) -> None:
    raise SystemExit("V28 contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_contracts.py <V28-patched WadaMesh checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio=(root/"platformio.ini").read_text()
    mesh=(root/"src/MyMesh.cpp").read_text()
    ui=(root/"src/ui-touch/UITask.cpp").read_text()
    ws=(root/"src/helpers/esp32/WebSocketCompanionServer.cpp").read_text()
    bridge_h=(root/"src/helpers/esp32/V11GlobalBridge.h").read_text()
    bridge_cpp=(root/"src/helpers/esp32/V11GlobalBridge.cpp").read_text()

    for flag in (
        "MESH_OFFGRIDNL_V27=1",
        "MESH_OFFGRIDNL_V28=1",
        "V28_RF_PRIMARY=1",
        "V28_INTERNET_SECONDARY=1",
        "V28_PRO_UX=1",
        "V28_NO_DEAD_ENDS=1",
        "V28_BROWSER_CHAT_SHELL=1",
    ):
        if flag not in pio:
            die("missing " + flag)

    # RF route 1, Internet route 2.
    if "#if defined(MESH_OFFGRIDNL_V27) && !defined(MESH_OFFGRIDNL_V28)" not in mesh:
        die("V28 must disable inherited V27 global-first short-circuit")
    if "V28 policy: RF was attempted above. The encrypted Internet copy is route 2." not in mesh:
        die("group RF-first ordering marker missing")
    for marker in (
        "v11_global_bridge.mirrorDM",
        "v11_global_bridge.mirrorChannelPacket",
        "v11_global_bridge.noteLoRaDM",
        "v11_global_bridge.noteLoRaChannel",
    ):
        if marker not in mesh:
            die("existing RF/Internet chat path missing " + marker)

    # Existing chat engine stays the engine.
    for forbidden in ("class V28Chat", "V28MessageStore", "v28SendMessage("):
        if forbidden in mesh + "\n" + ui + "\n" + ws:
            die("V28 must not create a second chat engine: " + forbidden)

    # Professional shell / no dead ends.
    for marker in (
        "V28 professional shell",
        "v28HomeChatsCb",
        "v28HomeContactsCb",
        "v28HomeSettingsCb",
        "No conversations yet",
        "Start a chat",
        "Create or join a channel",
    ):
        if marker not in ui:
            die("professional/no-dead-end UI marker missing " + marker)
    for marker in (
        "goToTab(CHAT_INBOX_TAB_INDEX)",
        "goToTab(CONTACTS_TAB_INDEX)",
        "goToTab(SETTINGS_TAB_INDEX)",
        "homeAppsBtnCb",
        "chatsAddBtnCb",
    ):
        if marker not in ui:
            die("V28 visible navigation destination missing " + marker)

    # Professional browser shell remains on proven WebSocket chat commands.
    for marker in (
        "V28_BROWSER_CHAT_SHELL",
        "<title>MeshOffGridNL</title>",
        "People &amp; #Channels",
        "Advanced",
        "No conversations yet",
        "New chat",
        "No saved people yet",
    ):
        if marker not in ws:
            die("V28 browser marker missing " + marker)
    for cmd in ("@m ","@s ","@sc ","@sh ","@oh ","@oc ","@t","@c"):
        if cmd not in ws:
            die("existing browser chat command missing " + cmd)

    # Privacy may secure the secondary Internet copy but never tear down RF/Wi-Fi.
    for forbidden in (".setInsecure(","WiFi.disconnect(true","WiFi.mode(WIFI_OFF)","esp_wifi_stop()"):
        if forbidden in bridge_cpp:
            die("privacy/global bridge must not disable connectivity: " + forbidden)
    if "WiFiClientSecure _wc;" not in bridge_h:
        die("verified secure Internet client missing")

    # Inherit V27's fixed encrypted envelope / dedup.
    for marker in (
        "PROTOCOL_VERSION = 3",
        "MSG_ID_LEN = 16",
        "static_assert(MAX_WIRE == 320",
        "MOG27-DM-KEY",
        "MOG27-CH-KEY",
    ):
        if marker not in bridge_h + "\n" + bridge_cpp:
            die("encrypted global protocol invariant missing " + marker)

    # P1/legacy radio contract remains exactly inherited.
    start=pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    end=pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", start)
    if start<0 or end<0: die("T-Deck env missing")
    td=pio[start:end]
    for marker in (
        "LORA_FREQ=869.618","LORA_BW=62.5","LORA_SF=8",
        "LORA_TX_POWER=22","MAX_LORA_TX_POWER=22",
        "SX126X_RX_BOOSTED_GAIN=1","SX126X_DIO2_AS_RF_SWITCH=true",
    ):
        if marker not in td:
            die("P1 RF invariant missing " + marker)

    print("V28 contracts OK: RF-first, Internet-secondary, professional no-dead-end UX, existing chat core preserved")

if __name__=="__main__":
    main()
