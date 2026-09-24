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
    mesh_path = root / "src/MyMesh.cpp"

    for p in (pio_path, ui_path, ws_path, mesh_path):
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
            "  -D V28_BROWSER_CHAT_SHELL=1\n"
            "  -D V28_RF_PRIMARY=1\n",
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

    # V28 transport policy override: V27 contained an optional early
    # global-first short-circuit. V28 explicitly restores RF as route 1.
    # Internet remains the secondary encrypted delivery path after RF has
    # already been attempted.
    mesh = mesh_path.read_text()

    dm_global_first_old = """#if defined(MESH_OFFGRIDNL_V27)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif
"""
    dm_global_first_new = """#if defined(MESH_OFFGRIDNL_V27) && !defined(MESH_OFFGRIDNL_V28)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif
"""
    if dm_global_first_old not in mesh:
        fail("V28 RF-primary DM anchor missing")
    mesh = replace_once(mesh, dm_global_first_old, dm_global_first_new,
                        "V28 disable inherited global-first DM")

    group_old = """void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  uiTrackSentFp(txtFloodFp(pkt));
#if defined(MESH_OFFGRIDNL_V27)
  if (pkt && pkt->getPayloadType() == PAYLOAD_TYPE_GRP_TXT) {
    v11_global_bridge.mirrorChannelPacket(channel, pkt);
  }
#endif
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, floodPathHashSize());  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);   // the lower overload applies path_hash_mode
  }
}
"""
    group_new = """void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  uiTrackSentFp(txtFloodFp(pkt));
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, floodPathHashSize());  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);   // the lower overload applies path_hash_mode
  }
#if defined(MESH_OFFGRIDNL_V27)
  // V28 policy: RF was attempted above. The encrypted Internet copy is route 2.
  if (pkt && pkt->getPayloadType() == PAYLOAD_TYPE_GRP_TXT) {
    v11_global_bridge.mirrorChannelPacket(channel, pkt);
  }
#endif
}
"""
    if group_old not in mesh:
        fail("V28 RF-primary group anchor missing")
    mesh = replace_once(mesh, group_old, group_new,
                        "V28 RF-first group ordering")

    mesh_path.write_text(mesh)

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
    # V28 only modernizes presentation and adds shortcuts that call existing
    # browser functions. No second composer/chat implementation is introduced.
    ws = ws_path.read_text()
    if "V28_BROWSER_CHAT_SHELL" not in ws:
        marker = "static const char"
        pos = ws.find(marker)
        if pos < 0:
            fail("browser HTML anchor missing")
        ws = ws[:pos] + """#if defined(MESH_OFFGRIDNL_V28)
// V28_BROWSER_CHAT_SHELL: browser UX only; existing WebSocket command protocol is unchanged.
#endif
""" + ws[pos:]

        # Brand/browser chrome. These strings live only in the embedded HTML.
        ws = ws.replace("<title>wadamesh</title>", "<title>MeshOffGridNL</title>")
        ws = ws.replace("<span id=dot>&bull;</span><b>WADAMESH</b><span id=hnm></span>",
                        "<span id=dot>&bull;</span><b>MeshOffGridNL</b><span id=hnm></span>")
        ws = ws.replace(
            "<div id=tabs><button data-t=chats class=on>Chats</button><button data-t=contacts>Contacts</button><button data-t=term>Terminal</button></div>",
            "<div id=tabs><button data-t=chats class=on>Chats</button><button data-t=contacts>People &amp; #Channels</button><button data-t=term>Advanced</button></div>",
        )
        ws = ws.replace("placeholder='Message'", "placeholder='Message...'")
        ws = ws.replace("placeholder='Search'", "placeholder='Search people or channels'")

        # Familiar messenger spacing/chrome; functionality stays untouched.
        ws = ws.replace(
            "#hd{padding:8px 10px;border-bottom:1px solid #1c1c1f;display:flex;align-items:center;gap:8px}#hdl{flex:1}",
            "#hd{padding:12px 14px;border-bottom:1px solid #24262b;display:flex;align-items:center;gap:10px;background:#101114}#hdl{flex:1}",
        )
        ws = ws.replace(
            "#tabs{display:flex;border-bottom:1px solid #1c1c1f}",
            "#tabs{display:flex;border-bottom:1px solid #24262b;background:#101114;padding:0 6px}",
        )
        ws = ws.replace(
            "#tabs button{flex:1;background:none;border:none;color:#7f868c;padding:11px 4px;font:inherit;font-size:13px;border-bottom:2px solid transparent;cursor:pointer}",
            "#tabs button{flex:1;background:none;border:none;color:#8e959c;padding:12px 6px;font:inherit;font-size:13px;font-weight:600;border-bottom:2px solid transparent;cursor:pointer}",
        )
        ws = ws.replace(
            ".row{display:flex;padding:11px 13px;border-bottom:1px solid #141416;cursor:pointer;gap:10px;align-items:center}",
            ".row{display:flex;padding:13px 14px;border-bottom:1px solid #181a1e;cursor:pointer;gap:11px;align-items:center}",
        )
        ws = ws.replace(
            ".empty{padding:26px 13px;color:#63696e;text-align:center}",
            ".empty{padding:34px 18px;color:#7f868c;text-align:center;line-height:1.55}.empty button{margin-top:14px;background:#19d6c2;color:#04201d;border:0;border-radius:18px;padding:9px 16px;font:inherit;font-weight:700;cursor:pointer}",
        )

        # No dead empty inbox: one button simply opens the existing contacts/channels tab.
        ws = ws.replace(
            "function renderThreads(){var h='';if(!threads.length)h='<div class=empty>No chats yet.<br>Start one from Contacts.</div>';",
            "function renderThreads(){var h='';if(!threads.length)h='<div class=empty><b>No conversations yet</b><br>Start with a person or #channel.<br><button id=v28newchat>New chat</button></div>';",
        )
        ws = ws.replace(
            " E('tlist').innerHTML=h;each(E('tlist').querySelectorAll('.row'),function(r){var ti=+r.getAttribute('data-ti');",
            " E('tlist').innerHTML=h;var nb=E('v28newchat');if(nb)nb.onclick=function(){showTab('contacts')};each(E('tlist').querySelectorAll('.row'),function(r){var ti=+r.getAttribute('data-ti');",
        )
        ws = ws.replace(
            "h+='<div class=sec>CONTACTS'+(cs.length?' ('+cs.length+')':'')+'</div>';if(!cs.length)h+='<div class=empty>No contacts.</div>';",
            "h+='<div class=sec>PEOPLE'+(cs.length?' ('+cs.length+')':'')+'</div>';if(!cs.length)h+='<div class=empty>No saved people yet.<br>Use Discovered nodes above to add someone.</div>';",
        )

        ws_path.write_text(ws)

    print("V28 applied: RF route 1 + Internet route 2 + professional no-dead-end UX")

if __name__ == "__main__":
    main()
