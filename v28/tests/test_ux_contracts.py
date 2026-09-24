#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

def die(msg: str) -> None:
    raise SystemExit("V28 UX contract failure: " + msg)

def main() -> None:
    if len(sys.argv) != 2:
        die("usage: test_ux_contracts.py <patched checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio = (root / "platformio.ini").read_text()
    ui = (root / "src/ui-touch/UITask.cpp").read_text()
    mesh = (root / "src/MyMesh.cpp").read_text()
    mesh_h = (root / "src/MyMesh.h").read_text()
    ws = (root / "src/helpers/esp32/WebSocketCompanionServer.cpp").read_text()

    for marker in (
        "MESH_OFFGRIDNL_V28=1",
        "V28_PRO_UX=1",
        "V28_NO_DEAD_ENDS=1",
        "V28_BROWSER_CHAT_SHELL=1",
        "V28_RF_PRIMARY=1",
        "V28_INTERNET_SECONDARY=1",
    ):
        if marker not in pio:
            die("missing build marker " + marker)

    for marker in (
        "v28HomeChatsCb",
        "v28HomeContactsCb",
        "v28HomeSettingsCb",
        'LV_SYMBOL_ENVELOPE "  Chats"',
        'LV_SYMBOL_DIRECTORY "  Contacts"',
        'LV_SYMBOL_LIST "  Apps"',
        'LV_SYMBOL_SETTINGS "  Settings"',
        '"No conversations yet"',
        '"Start a chat"',
        '"Create or join a channel"',
    ):
        if marker not in ui:
            die("missing V28 UX marker " + marker)

    # No dead-end rule: every visible V28 CTA has a concrete existing handler.
    required_handler_pairs = (
        ('"Start a chat"', "v28HomeContactsCb"),
        ('"Create or join a channel"', "chatsAddBtnCb"),
        ('"  Chats"', "v28HomeChatsCb"),
        ('"  Contacts"', "v28HomeContactsCb"),
        ('"  Apps"', "homeAppsBtnCb"),
        ('"  Settings"', "v28HomeSettingsCb"),
    )
    for label, handler in required_handler_pairs:
        if label not in ui or handler not in ui:
            die(f"CTA {label} is missing working handler {handler}")

    # V28 UX must not create an alternate native chat engine.
    for forbidden in (
        "V28ChatEngine",
        "v28SendMessage",
        "v28InjectMessage",
        "v28ChannelSend",
    ):
        if forbidden in ui + mesh + mesh_h:
            die("parallel native chat implementation detected: " + forbidden)

    # V28 keeps the public chat API unchanged. The only new MyMesh surface is
    # a narrow raw-public-key ECDH helper for an invite requester who is not a
    # saved contact yet.
    if "v28CalcSharedSecretAny" not in mesh_h or "self_id.calcSharedSecret(out, peerPub)" not in mesh:
        die("V28 short-code join requires the narrow requester ECDH helper")
    for forbidden in ("v28SendMessage(", "v28CreateChat(", "v28ReplaceChannelEngine("):
        if forbidden in mesh_h + mesh:
            die("V28 must not add a parallel public chat API: " + forbidden)
    rf_gate = "#if defined(MESH_OFFGRIDNL_V27) && !defined(MESH_OFFGRIDNL_V28)"
    if rf_gate not in mesh:
        die("inherited V27 global-first shortcut is not disabled for V28")
    dm_start = mesh.find("int MyMesh::sendMessage(")
    dm_end = mesh.find("int MyMesh::sendCommandData(", dm_start)
    if dm_start < 0 or dm_end < 0:
        die("DM send function missing")
    dm = mesh[dm_start:dm_end]
    if dm.find("BaseChatMesh::sendMessage") < 0 or dm.find("v11_global_bridge.mirrorDM") < 0:
        die("RF or Internet DM route missing")
    if dm.find("BaseChatMesh::sendMessage") > dm.find("v11_global_bridge.mirrorDM"):
        die("Internet DM route occurs before RF route")

    grp_start = mesh.find("void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel")
    grp_end = mesh.find("void MyMesh::onMessageRecv(", grp_start)
    if grp_start < 0 or grp_end < 0:
        die("group send function missing")
    grp = mesh[grp_start:grp_end]
    mirror_pos = grp.find("v11_global_bridge.mirrorChannelPacket")
    rf_plain = grp.find("sendFlood(pkt")
    rf_scoped = grp.find("sendFloodScoped(*scope")
    if mirror_pos < 0 or rf_plain < 0 or rf_scoped < 0:
        die("RF/Internet group route markers missing")
    if mirror_pos < rf_plain or mirror_pos < rf_scoped:
        die("Internet #channel mirror occurs before RF send")

    # Browser layer may be restyled, but the existing command protocol remains.
    if "V28_BROWSER_CHAT_SHELL" not in ws:
        die("browser shell marker missing")
    for cmd in ("@m ", "@oc ", "@oh ", "@sc ", "@sh "):
        if cmd not in ws:
            die("existing browser chat command disappeared: " + cmd)

    # Existing channel action sheet remains authoritative, with V28 short-code
    # create/join/approval actions layered onto it.
    for marker in (
        "openAddChannelSheet",
        "addChannelCreatePrivateCb",
        "addChannelJoinPrivateCb",
        "addChannelJoinPublicCb",
        "addChannelJoinHashtagCb",
        "Approve join request",
        "XXXX-XXXX",
        "requestChannelJoin",
        "createChannelInvite",
        "decideJoinRequest",
        "@vc ",
        "@vj ",
        "@va ",
        "@vd ",
        "v28Channels",
        "v28Event",
    ):
        if marker not in ui:
            die("existing channel flow missing " + marker)

    print("V28 RF-first + Internet-second + professional UX/no-dead-end contracts PASS")

if __name__ == "__main__":
    main()
