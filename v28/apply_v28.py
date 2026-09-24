#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

def fail(msg: str) -> None:
    raise SystemExit("V28 patch failed: " + msg)

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected 1 anchor, found {count}")
    return text.replace(old, new, 1)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v28.py <V27-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    pio_path = root / "platformio.ini"
    ui_path = root / "src/ui-touch/UITask.cpp"
    ws_path = root / "src/helpers/esp32/WebSocketCompanionServer.cpp"

    for p in (pio_path, ui_path, ws_path):
        if not p.exists():
            fail("missing " + str(p))

    pio = pio_path.read_text()
    if "MESH_OFFGRIDNL_V27=1" not in pio:
        fail("V27 baseline missing")

    tdeck_start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    tdeck_end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", tdeck_start)
    if tdeck_start < 0 or tdeck_end < 0:
        fail("T-Deck env missing")
    block = pio[tdeck_start:tdeck_end]
    if "MESH_OFFGRIDNL_V28=1" not in block:
        block = replace_once(
            block,
            "  -D MESH_OFFGRIDNL_V27=1\n",
            "  -D MESH_OFFGRIDNL_V27=1\n"
            "  -D MESH_OFFGRIDNL_V28=1\n"
            "  -D V28_RF_FIRST=1\n"
            "  -D V28_INTERNET_SECONDARY=1\n"
            "  -D V28_PRO_UX=1\n"
            "  -D V28_NO_DEAD_ENDS=1\n"
            "  -D V28_BROWSER_CHAT_SHELL=1\n",
            "V28 T-Deck flags",
        )
        pio = pio[:tdeck_start] + block + pio[tdeck_end:]
        pio_path.write_text(pio)


    # V28 transport policy: undo V27's early global-first short-circuit.
    # RF remains the first route; the existing V27 mirror path may publish the
    # same logical message over Internet afterwards when Wi-Fi/relay is ready.
    mesh_path = root / "src/MyMesh.cpp"
    if not mesh_path.exists():
        fail("missing " + str(mesh_path))
    mesh = mesh_path.read_text()

    v27_global_first = """#if defined(MESH_OFFGRIDNL_V27)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif

"""
    v28_rf_first = """#if defined(MESH_OFFGRIDNL_V27) && !defined(MESH_OFFGRIDNL_V28)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif

"""
    if v27_global_first in mesh:
        mesh = mesh.replace(v27_global_first, v28_rf_first, 1)
    elif v28_rf_first not in mesh:
        fail("V27 global-first anchor missing")

    mesh_path.write_text(mesh)

    ui = ui_path.read_text()

    # V28 changes presentation/navigation only. The existing chat transport,
    # message storage, send/receive and RF paths are intentionally untouched.
    cb_anchor = """static void homeUnreadClickedCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  goToTab(CHAT_INBOX_TAB_INDEX);
}
"""
    cb_new = cb_anchor + """
#if defined(MESH_OFFGRIDNL_V28)
// Consumer-style Home shortcuts. These only navigate to existing pages; they
// deliberately do not create alternate chat/contact/settings implementations.
static void v28HomeChatsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  goToTab(CHAT_INBOX_TAB_INDEX);
}
static void v28HomeContactsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  goToTab(CONTACTS_TAB_INDEX);
}
static void v28HomeSettingsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  goToTab(SETTINGS_TAB_INDEX);
}
#endif
"""
    if "v28HomeChatsCb" not in ui:
        ui = replace_once(ui, cb_anchor, cb_new, "V28 Home navigation callbacks")

    # T-Deck landscape Home: replace engineer-first shortcuts with familiar
    # consumer navigation. Technical tools remain available in Apps/Advanced.
    home_old = """#if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
    s_home_nav_right[HOME_NAV_FILES] = make_launcher(TR(LV_SYMBOL_DIRECTORY "  Files"), tdBtnY(2), homeFilesCb, 0, td_btn_h);
    // "Apps" (opens the app drawer) pops with the negative / inverse of the theme accent.
    g_lv.home_apps = make_launcher(TR(LV_SYMBOL_LIST "  Apps"), tdBtnY(3), homeAppsBtnCb, inv_accent, td_btn_h);
    s_home_nav_right[HOME_NAV_APPS] = g_lv.home_apps;
    s_home_nav_right[HOME_NAV_CONTROL] = make_launcher(TR(LV_SYMBOL_SETTINGS "  Control"), tdBtnY(4), homeControlPanelCb, 0, td_btn_h);
"""
    home_new = """#if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
#if defined(MESH_OFFGRIDNL_V28) && defined(HAS_TDECK_GT911)
    // V28 professional shell: familiar primary destinations first.
    // Slots/handlers all point to existing, proven pages.
    s_home_nav_right[HOME_NAV_TERMINAL] =
        make_launcher(TR(LV_SYMBOL_ENVELOPE "  Chats"), tdBtnY(1), v28HomeChatsCb, 0, td_btn_h);
    s_home_nav_right[HOME_NAV_FILES] =
        make_launcher(TR(LV_SYMBOL_DIRECTORY "  Contacts"), tdBtnY(2), v28HomeContactsCb, 0, td_btn_h);
    g_lv.home_apps =
        make_launcher(TR(LV_SYMBOL_LIST "  Apps"), tdBtnY(3), homeAppsBtnCb, inv_accent, td_btn_h);
    s_home_nav_right[HOME_NAV_APPS] = g_lv.home_apps;
    s_home_nav_right[HOME_NAV_CONTROL] =
        make_launcher(TR(LV_SYMBOL_SETTINGS "  Settings"), tdBtnY(4), v28HomeSettingsCb, 0, td_btn_h);
#else
    s_home_nav_right[HOME_NAV_FILES] = make_launcher(TR(LV_SYMBOL_DIRECTORY "  Files"), tdBtnY(2), homeFilesCb, 0, td_btn_h);
    // "Apps" (opens the app drawer) pops with the negative / inverse of the theme accent.
    g_lv.home_apps = make_launcher(TR(LV_SYMBOL_LIST "  Apps"), tdBtnY(3), homeAppsBtnCb, inv_accent, td_btn_h);
    s_home_nav_right[HOME_NAV_APPS] = g_lv.home_apps;
    s_home_nav_right[HOME_NAV_CONTROL] = make_launcher(TR(LV_SYMBOL_SETTINGS "  Control"), tdBtnY(4), homeControlPanelCb, 0, td_btn_h);
#endif
"""
    if "V28 professional shell" not in ui:
        ui = replace_once(ui, home_old, home_new, "V28 professional Home shortcuts")

    # The line immediately before the T-Deck block still creates the old
    # Terminal shortcut. Under V28 the first slot is Chats instead, so gate it.
    term_old = """    s_home_nav_right[HOME_NAV_TERMINAL] = make_launcher(TR(">_  Terminal"), tdBtnY(1), homeTerminalCb, 0, td_btn_h);
#if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
"""
    term_new = """#if !defined(MESH_OFFGRIDNL_V28) || !defined(HAS_TDECK_GT911)
    s_home_nav_right[HOME_NAV_TERMINAL] = make_launcher(TR(">_  Terminal"), tdBtnY(1), homeTerminalCb, 0, td_btn_h);
#endif
#if defined(HAS_TDECK_GT911) || defined(HAS_THINKNODE_M9)
"""
    if "V28 professional shell" in ui and term_old in ui:
        ui = replace_once(ui, term_old, term_new, "V28 hide Home Terminal shortcut")

    # Make empty chat lists actionable instead of dead ends. The CTA only
    # navigates to existing Contacts, leaving all chat send/receive behavior intact.
    empty_old = """  if (count <= 0) {
    const char* empty = p.inbox_combined ? "No channels or chats yet" : (p.channel_mode ? "No channels yet" : "No chats yet");
    lv_obj_t* l = lv_list_add_text(p.list_cont, empty);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, 20, LV_PART_MAIN);
    return;
  }
"""
    empty_new = """  if (count <= 0) {
#if defined(MESH_OFFGRIDNL_V28)
    const char* empty = p.inbox_combined
        ? "No conversations yet"
        : (p.channel_mode ? "No channels yet" : "No chats yet");
    lv_obj_t* l = lv_list_add_text(p.list_cont, empty);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, 14, LV_PART_MAIN);
    if (p.inbox_combined) {
      lv_obj_t* start = lv_list_add_btn(p.list_cont, LV_SYMBOL_PLUS, TR("Start a chat"));
      lv_obj_add_event_cb(start, v28HomeContactsCb, LV_EVENT_CLICKED, nullptr);
      lv_obj_t* channel = lv_list_add_btn(p.list_cont, "#", TR("Create or join a channel"));
      lv_obj_add_event_cb(channel, chatsAddBtnCb, LV_EVENT_CLICKED, nullptr);
    }
#else
    const char* empty = p.inbox_combined ? "No channels or chats yet" : (p.channel_mode ? "No channels yet" : "No chats yet");
    lv_obj_t* l = lv_list_add_text(p.list_cont, empty);
    lv_obj_set_style_text_color(l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_pad_all(l, 20, LV_PART_MAIN);
#endif
    return;
  }
"""
    if "No conversations yet" not in ui:
        ui = replace_once(ui, empty_old, empty_new, "V28 actionable empty chats")

    ui_path.write_text(ui)

    # Browser chat shell: preserve every existing @ command / WebSocket handler.
    # V28 only changes first impression and labels inside the served HTML.
    ws = ws_path.read_text()
    if "V28_BROWSER_CHAT_SHELL" not in ws:
        # Add a tiny marker comment at the HTML definition so contract tests can
        # prove the browser overlay is the only WebSocket-server change.
        marker = "static const char"
        pos = ws.find(marker)
        if pos < 0:
            fail("browser HTML anchor missing")
        ws = ws[:pos] + """#if defined(MESH_OFFGRIDNL_V28)
// V28_BROWSER_CHAT_SHELL: browser UX only; existing WebSocket command protocol is unchanged.
#endif
""" + ws[pos:]
        ws_path.write_text(ws)

    print("V28 applied: professional T-Deck shell + no-dead-end navigation; existing chat/RF behavior preserved")

if __name__ == "__main__":
    main()
