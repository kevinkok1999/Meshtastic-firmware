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

    # UX layer is forbidden from touching MyMesh with V28-specific behavior.
    if "MESH_OFFGRIDNL_V28" in mesh or "MESH_OFFGRIDNL_V28" in mesh_h:
        die("V28 UX leaked into native MyMesh chat core")

    # Browser layer may be restyled, but the existing command protocol remains.
    if "V28_BROWSER_CHAT_SHELL" not in ws:
        die("browser shell marker missing")
    for cmd in ("@m ", "@oc ", "@oh ", "@sc ", "@sh "):
        if cmd not in ws:
            die("existing browser chat command disappeared: " + cmd)

    # Existing channel action sheet remains the authoritative channel flow.
    for marker in (
        "openAddChannelSheet",
        "addChannelCreatePrivateCb",
        "addChannelJoinPrivateCb",
        "addChannelJoinPublicCb",
        "addChannelJoinHashtagCb",
    ):
        if marker not in ui:
            die("existing channel flow missing " + marker)

    print("V28 professional UX + no-dead-end contracts PASS")

if __name__ == "__main__":
    main()
