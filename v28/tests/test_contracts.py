#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def die(msg): raise SystemExit("V28 contract failure: "+msg)
root=pathlib.Path(sys.argv[1]).resolve() if len(sys.argv)>1 else None
if not root: die("usage: test_contracts.py <patched checkout>")
pio=(root/"platformio.ini").read_text()
mesh=(root/"src/MyMesh.cpp").read_text()
ui=(root/"src/ui-touch/UITask.cpp").read_text()
ws=(root/"src/helpers/esp32/WebSocketCompanionServer.cpp").read_text()

for marker in (
 "MESH_OFFGRIDNL_V28=1","V28_RF_FIRST=1","V28_INTERNET_SECONDARY=1",
 "V28_PRO_UX=1","V28_NO_DEAD_ENDS=1","V28_BROWSER_CHAT_SHELL=1"):
    if marker not in pio: die("missing "+marker)

if "#if defined(MESH_OFFGRIDNL_V27) && !defined(MESH_OFFGRIDNL_V28)" not in mesh:
    die("V28 must disable V27 global-first early return")
if "v11_global_bridge.mirrorDM" not in mesh:
    die("Internet secondary mirror path missing")
if "tryGlobalFirstDM(recipient, timestamp, text)" not in mesh:
    die("V27 compatibility path unexpectedly removed")

for marker in ("v28HomeChatsCb","v28HomeContactsCb","v28HomeSettingsCb",
               "V28 professional shell","No conversations yet","Start a chat"):
    if marker not in ui: die("V28 UI marker missing: "+marker)

for marker in ("MeshOffGridNL","People &amp; #Channels","Advanced",
               "v28newchat","showTab('contacts')",
               "No saved people yet"):
    if marker not in ws: die("V28 browser marker missing: "+marker)

# Existing browser chat protocol must remain intact.
for cmd in ("@m ","@s ","@sc ","@sh ","@oh ","@oc ","@t","@c"):
    if cmd not in ws: die("existing browser chat command missing: "+cmd)

# Browser UX must not invent a second network/chat stack.
for forbidden in ("fetch('http://", 'new EventSource(', 'new RTCPeerConnection('):
    if forbidden in ws: die("browser introduced unsupported parallel transport: "+forbidden)

print("V28 UX/RF-first contracts PASS")
