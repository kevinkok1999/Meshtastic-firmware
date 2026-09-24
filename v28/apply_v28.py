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
    mesh_h_path = root / "src/MyMesh.h"

    for p in (pio_path, ui_path, ws_path, mesh_path, mesh_h_path):
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
            "  -D V28_RF_PRIMARY=1\n"
            "  -D V28_INTERNET_SECONDARY=1\n"
            "  -D V28_PRO_UX=1\n"
            "  -D V28_NO_DEAD_ENDS=1\n"
            "  -D V28_BROWSER_CHAT_SHELL=1\n",
            "V28 T-Deck flags",
        )
        pio = pio[:tdeck_start] + block + pio[tdeck_end:]
        pio_path.write_text(pio)


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

    # V28 route policy: RF was tried first, but an RF queue failure must not
    # suppress route 2 when Wi-Fi is available. Queue the Internet copy
    # independently; truthful UI success still depends on recent relay health.
    dm_mirror_old = """      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text,
                                       result != MSG_SEND_FAILED)
          : false;"""
    dm_mirror_new = """      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text, true)
          : false;"""
    if dm_mirror_old not in mesh:
        fail("V28 independent Internet route anchor missing")
    mesh = replace_once(mesh, dm_mirror_old, dm_mirror_new,
                        "V28 Internet route independent of RF result")

    # The inherited V11 tail may report direct success when the Internet route
    # accepted a message. Under V28, only do that when the relay has been
    # healthy recently; an offline RAM queue is not a delivery success.
    sent_old = """  if (result == MSG_SEND_FAILED && v11_global_ok) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }"""
    sent_new = """  if (result == MSG_SEND_FAILED && v11_global_ok &&
      v11_global_bridge.connected()) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }"""
    if sent_old not in mesh:
        fail("V28 truthful route-2 status anchor missing")
    mesh = replace_once(mesh, sent_old, sent_new,
                        "V28 truthful Internet route status")

    mesh_path.write_text(mesh)

    # V28 short-code joins need ECDH with a requester that is not a saved
    # contact yet. Keep the V27 cached-contact helper unchanged and add one
    # narrow V28-only raw-public-key helper.
    mesh_h = mesh_h_path.read_text()
    h_old = """  bool v27CalcSharedSecretCached(const uint8_t peerPub[32], uint8_t out[32]);
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
"""
    h_new = """  bool v27CalcSharedSecretCached(const uint8_t peerPub[32], uint8_t out[32]);
#if defined(MESH_OFFGRIDNL_V28)
  bool v28CalcSharedSecretAny(const uint8_t peerPub[32], uint8_t out[32]);
#endif
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
"""
    if h_old not in mesh_h:
        fail("V28 raw ECDH header anchor missing")
    mesh_h = replace_once(mesh_h, h_old, h_new, "V28 raw requester ECDH declaration")
    mesh_h_path.write_text(mesh_h)

    mesh = mesh_path.read_text()
    cpp_old = """bool MyMesh::v27CalcSharedSecretCached(const uint8_t peerPub[32], uint8_t out[32]) {
  if (!peerPub || !out) return false;
  ContactInfo* contact = lookupContactByPubKey(peerPub, PUB_KEY_SIZE);
  if (!contact || contact->type != ADV_TYPE_CHAT) return false;
  memcpy(out, contact->getSharedSecret(self_id), PUB_KEY_SIZE);
  return true;
}
"""
    cpp_new = cpp_old + """
#if defined(MESH_OFFGRIDNL_V28)
bool MyMesh::v28CalcSharedSecretAny(const uint8_t peerPub[32], uint8_t out[32]) {
  if (!peerPub || !out) return false;
  self_id.calcSharedSecret(out, peerPub);
  bool any = false;
  for (size_t i = 0; i < PUB_KEY_SIZE; ++i) any = any || out[i] != 0;
  if (!any) memset(out, 0, PUB_KEY_SIZE);
  return any;
}
#endif
"""
    if cpp_old not in mesh:
        fail("V28 raw ECDH cpp anchor missing")
    mesh = replace_once(mesh, cpp_old, cpp_new, "V28 raw requester ECDH implementation")
    mesh_path.write_text(mesh)

    ui = ui_path.read_text()

    # V28 private-channel UX: human short codes only. The real 128-bit secret
    # stays device-local and is transferred only inside the E2E join bundle.
    include_old = '#include "../MyMesh.h"\n'
    include_new = '#include "../MyMesh.h"\n#if defined(MESH_OFFGRIDNL_V28)\n#include "../helpers/esp32/V11GlobalBridge.h"\n#endif\n'
    if include_old not in ui:
        fail("V28 UI bridge include anchor missing")
    ui = replace_once(ui, include_old, include_new, "V28 UI bridge include")

    create_guard_old = '  if (!s_addch_name_ta || !s_addch_secret_ta) return;\n'
    create_guard_new = '''#if defined(MESH_OFFGRIDNL_V28)
  if (!s_addch_name_ta) return;
#else
  if (!s_addch_name_ta || !s_addch_secret_ta) return;
#endif
'''
    if create_guard_old not in ui:
        fail("V28 create-channel guard anchor missing")
    ui = replace_once(ui, create_guard_old, create_guard_new, "V28 create-channel simple guard")

    create_secret_old = '''  const char* sec_raw = lv_textarea_get_text(s_addch_secret_ta);
  char hex[33]; int hn = 0;
  for (const char* p = sec_raw; *p && hn < 32; ++p) {
    if (*p == ' ' || *p == '\\t' || *p == '\\n') continue;
    hex[hn++] = *p;
  }
  hex[hn] = '\\0';

  uint8_t secret[16];
  if (hn == 0) {
#if defined(ESP32)
    esp_fill_random(secret, sizeof(secret));
#else
    for (int i = 0; i < 16; ++i) secret[i] = static_cast<uint8_t>(rand() & 0xFF);
#endif
  } else if (!hexToSecret16(hex, secret)) {
    setAddChannelError(TR("Secret must be 32 hex chars (or empty)."));
    return;
  }
'''
    create_secret_new = '''  uint8_t secret[16];
#if defined(MESH_OFFGRIDNL_V28)
  esp_fill_random(secret, sizeof(secret));
#else
  const char* sec_raw = lv_textarea_get_text(s_addch_secret_ta);
  char hex[33]; int hn = 0;
  for (const char* p = sec_raw; *p && hn < 32; ++p) {
    if (*p == ' ' || *p == '\\t' || *p == '\\n') continue;
    hex[hn++] = *p;
  }
  hex[hn] = '\\0';
  if (hn == 0) {
#if defined(ESP32)
    esp_fill_random(secret, sizeof(secret));
#else
    for (int i = 0; i < 16; ++i) secret[i] = static_cast<uint8_t>(rand() & 0xFF);
#endif
  } else if (!hexToSecret16(hex, secret)) {
    setAddChannelError(TR("Secret must be 32 hex chars (or empty)."));
    return;
  }
#endif
'''
    if create_secret_old not in ui:
        fail("V28 create-channel secret anchor missing")
    ui = replace_once(ui, create_secret_old, create_secret_new, "V28 random private secret")

    saved_anchor = '''  if (!the_mesh.uiAddOrUpdateChannel(slot, name, secret)) {
    setAddChannelError(TR("Failed to save channel."));
    return;
  }
'''
    saved_new = saved_anchor + '''#if defined(MESH_OFFGRIDNL_V28)
  (void)v11_global_bridge.createChannelInvite((uint8_t)slot);
#endif
'''
    if saved_anchor not in ui:
        fail("V28 invite-create anchor missing")
    ui = replace_once(ui, saved_anchor, saved_new, "V28 create short invite")

    join_start_old = '''static void joinPrivateChannelSubmitCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  if (!s_addch_secret_ta) return;
'''
    join_start_new = join_start_old + '''#if defined(MESH_OFFGRIDNL_V28)
  const char* code = lv_textarea_get_text(s_addch_secret_ta);
  if (!v11_global_bridge.requestChannelJoin(code)) {
    setAddChannelError(TR("Enter a valid XXXX-XXXX join code."));
    return;
  }
  closeSettingsModal();
  if (g_lv.task) g_lv.task->showAlert(TR("Join request sent - waiting for approval"), 1800);
  return;
#endif
'''
    if join_start_old not in ui:
        fail("V28 short join submit anchor missing")
    ui = replace_once(ui, join_start_old, join_start_new, "V28 short join submit")

    create_modal_sig = 'static void openCreatePrivateChannelModal() {\n'
    create_modal_v28 = create_modal_sig + '''#if defined(MESH_OFFGRIDNL_V28)
  {
    lv_obj_t* body = createSettingsModal(TR("Create private channel"), SettingsModalKind::ChCreatePrv);
    int y = 0;
    lv_obj_t* hint = lv_label_create(body);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, channelFormControlWidth());
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
    lv_label_set_text(hint, TR("Choose a name. V28 creates the secure key and a short join code automatically."));
    lv_obj_set_pos(hint, 0, y); y += 48;
    lv_obj_t* name_l = lv_label_create(body);
    lv_label_set_text(name_l, TR("Channel name"));
    lv_obj_set_style_text_color(name_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(name_l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(name_l, 0, y); y += 16;
    s_addch_name_ta = lv_textarea_create(body);
    channelFormLayoutTextarea(body, s_addch_name_ta, y);
    lv_textarea_set_one_line(s_addch_name_ta, true);
    taSetPlaceholder(s_addch_name_ta, TR("e.g. Family"));
    lv_textarea_set_max_length(s_addch_name_ta, 31);
    attachSettingsTaEvents(s_addch_name_ta); y += 42;
    s_addch_error_l = lv_label_create(body);
    lv_label_set_long_mode(s_addch_error_l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_addch_error_l, channelFormControlWidth());
    lv_obj_set_style_text_color(s_addch_error_l, lightSurfaceTextColor(0xE08080), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_addch_error_l, &g_font_12, LV_PART_MAIN);
    lv_label_set_text(s_addch_error_l, "");
    lv_obj_set_pos(s_addch_error_l, 0, y); y += 28;
    lv_obj_t* b = lv_btn_create(body);
    channelFormSetFullWidth(b, y, 36);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_add_event_cb(b, createPrivateChannelSubmitCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* bl = lv_label_create(b);
    useChainedFont(bl); lv_label_set_text(bl, TR("Create securely")); lv_obj_center(bl);
  }
  return;
#endif
'''
    if create_modal_sig not in ui:
        fail("V28 create modal anchor missing")
    ui = replace_once(ui, create_modal_sig, create_modal_v28, "V28 simple create modal")

    join_modal_sig = 'static void openJoinPrivateChannelModal() {\n'
    join_modal_v28 = join_modal_sig + '''#if defined(MESH_OFFGRIDNL_V28)
  {
    lv_obj_t* body = createSettingsModal(TR("Join private channel"), SettingsModalKind::ChJoinPrv);
    int y = 0;
    lv_obj_t* hint = lv_label_create(body);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, channelFormControlWidth());
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(hint, &g_font_12, LV_PART_MAIN);
    lv_label_set_text(hint, TR("Enter the 8-character code. The owner must approve before the secure channel is added."));
    lv_obj_set_pos(hint, 0, y); y += 48;
    lv_obj_t* sec_l = lv_label_create(body);
    lv_label_set_text(sec_l, TR("Join code"));
    lv_obj_set_style_text_color(sec_l, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
    lv_obj_set_style_text_font(sec_l, &g_font_12, LV_PART_MAIN);
    lv_obj_set_pos(sec_l, 0, y); y += 16;
    s_addch_secret_ta = lv_textarea_create(body);
    channelFormLayoutTextarea(body, s_addch_secret_ta, y);
    lv_textarea_set_one_line(s_addch_secret_ta, true);
    taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX"));
    lv_textarea_set_max_length(s_addch_secret_ta, 10);
    attachSettingsTaEvents(s_addch_secret_ta); y += 42;
    s_addch_error_l = lv_label_create(body);
    lv_label_set_long_mode(s_addch_error_l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_addch_error_l, channelFormControlWidth());
    lv_obj_set_style_text_color(s_addch_error_l, lightSurfaceTextColor(0xE08080), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_addch_error_l, &g_font_12, LV_PART_MAIN);
    lv_label_set_text(s_addch_error_l, "");
    lv_obj_set_pos(s_addch_error_l, 0, y); y += 28;
    lv_obj_t* b = lv_btn_create(body);
    channelFormSetFullWidth(b, y, 36);
    styleButton(b);
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_STATUS_OK), LV_PART_MAIN);
    lv_obj_add_event_cb(b, joinPrivateChannelSubmitCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* bl = lv_label_create(b);
    useChainedFont(bl); lv_label_set_text(bl, TR("Request access")); lv_obj_center(bl);
  }
  return;
#endif
'''
    if join_modal_sig not in ui:
        fail("V28 join modal anchor missing")
    ui = replace_once(ui, join_modal_sig, join_modal_v28, "V28 simple join modal")

    approval_anchor = 'static void addChannelCreatePrivateCb(lv_event_t* e) {\n'
    approval_code = '''#if defined(MESH_OFFGRIDNL_V28)
static uint32_t s_v28_approve_invite = 0;
static uint8_t s_v28_approve_requester[PUB_KEY_SIZE] = {};

static void v28ApproveJoinApply() {
  if (!s_v28_approve_invite) return;
  if (!v11_global_bridge.decideJoinRequest(s_v28_approve_invite,
                                            s_v28_approve_requester, true)) {
    if (g_lv.task) g_lv.task->showAlert(TR("Could not queue approval"), 1600);
  } else if (g_lv.task) {
    g_lv.task->showAlert(TR("Approving securely..."), 1400);
  }
}

static void v28ApproveJoinCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  closeAddChannelSheet();
  if (!v11_global_bridge.refreshJoinRequests()) {
    if (g_lv.task) g_lv.task->showAlert(TR("Could not check join requests"), 1600);
  } else if (g_lv.task) {
    g_lv.task->showAlert(TR("Checking join requests..."), 1200);
  }
}
#endif

''' + approval_anchor
    if approval_anchor not in ui:
        fail("V28 approval callback anchor missing")
    ui = replace_once(ui, approval_anchor, approval_code, "V28 owner approval callbacks")

    rows_old = '''  const int rows  = 4;
  const int pad   = PSC(10);
'''
    rows_new = '''#if defined(MESH_OFFGRIDNL_V28)
  const int rows  = 5;
#else
  const int rows  = 4;
#endif
  const int pad   = PSC(10);
'''
    if rows_old not in ui:
        fail("V28 channel-sheet rows anchor missing")
    ui = replace_once(ui, rows_old, rows_new, "V28 channel-sheet approvals row")

    buttons_old = '''  mk(TR("Create a private channel"), addChannelCreatePrivateCb);
  mk(TR("Join a private channel"), addChannelJoinPrivateCb);
  mk(TR("Join the public channel"), addChannelJoinPublicCb);
'''
    buttons_new = '''  mk(TR("Create a private channel"), addChannelCreatePrivateCb);
  mk(TR("Join a private channel"), addChannelJoinPrivateCb);
#if defined(MESH_OFFGRIDNL_V28)
  mk(TR("Approve join request"), v28ApproveJoinCb);
#endif
  mk(TR("Join the public channel"), addChannelJoinPublicCb);
'''
    if buttons_old not in ui:
        fail("V28 channel-sheet buttons anchor missing")
    ui = replace_once(ui, buttons_old, buttons_new, "V28 channel-sheet approval action")

    event_anchor = '''  // Web mesh terminal: run any command the browser typed through the exact same dispatch
'''
    event_code = '''#if defined(MESH_OFFGRIDNL_V28)
  {
    V11GlobalBridge::UiEvent ev;
    while (v11_global_bridge.takeUiEvent(ev)) {
      if (ev.type == V11GlobalBridge::UI_INVITE_CREATED) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: %s", ev.channel[0] ? ev.channel : "Private", ev.code);
        if (g_lv.task) g_lv.task->showAlert(msg, 4200);
      } else if (ev.type == V11GlobalBridge::UI_JOIN_REQUESTED) {
        if (g_lv.task) g_lv.task->showAlert(TR("Waiting for owner approval"), 1800);
      } else if (ev.type == V11GlobalBridge::UI_JOIN_LIST_READY) {
        V11GlobalBridge::JoinRequest jr{};
        if (v11_global_bridge.joinRequestCount() > 0 &&
            v11_global_bridge.getJoinRequest(0, jr)) {
          s_v28_approve_invite = jr.inviteId;
          memcpy(s_v28_approve_requester, jr.requester, PUB_KEY_SIZE);
          char msg[120];
          snprintf(msg, sizeof(msg), "Allow %02X%02X%02X... to join %s?",
                   jr.requester[0], jr.requester[1], jr.requester[2],
                   jr.channel[0] ? jr.channel : "private channel");
          showConfirm(msg, TR("Approve"), v28ApproveJoinApply);
        } else if (g_lv.task) {
          g_lv.task->showAlert(TR("No pending join requests"), 1400);
        }
      } else if (ev.type == V11GlobalBridge::UI_JOIN_APPROVED) {
        if (g_lv.task) g_lv.task->showAlert(ev.message, 1500);
      } else if (ev.type == V11GlobalBridge::UI_JOINED) {
        if (g_lv.task) {
          g_lv.task->refreshThreadsFromMesh();
          g_lv.dirty_threads = true;
          g_lv.task->showAlert(TR("Private channel joined"), 1800);
        }
      } else if (ev.type == V11GlobalBridge::UI_JOIN_DENIED ||
                 ev.type == V11GlobalBridge::UI_ERROR) {
        if (g_lv.task) g_lv.task->showAlert(ev.message[0] ? ev.message : "V28 join error", 2200);
      }
    }
  }
#endif
  // Web mesh terminal: run any command the browser typed through the exact same dispatch
'''
    if event_anchor not in ui:
        fail("V28 bridge event loop anchor missing")
    ui = replace_once(ui, event_anchor, event_code, "V28 native invite events")

    # V28 UX changes presentation/navigation only. Native message format,
    # storage and chat behavior remain unchanged; the only MyMesh difference
    # allowed by V28 is RF-first / Internet-second route ordering.
    # IMPORTANT: continue modifying the same in-memory UI source so the secure
    # short-code/approval patches above are preserved until the single write.
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

    # Browser uses the SAME V28 bridge/state machine as the native UI.
    # These @v* commands only queue high-level actions; keys never enter JS.
    web_cmd_anchor = '''  const char* a = cmd + 1;
  auto argAfter = [](const char* s) { while (*s && *s != ' ') ++s; if (*s == ' ') ++s; return s; };
'''
    web_cmd_new = web_cmd_anchor + '''#if defined(MESH_OFFGRIDNL_V28)
  auto v28Notice = [](const char* kind, const char* msg) {
    if (!s_webdata_buf) return;
    char* p = s_webdata_buf; const char* e = s_webdata_buf + WEBDATA_BUF;
    p += snprintf(p, e - p, "{\\\"t\\\":\\\"v28\\\",\\\"k\\\":\\\"%s\\\",\\\"message\\\":\\\"", kind ? kind : "info");
    jsonEsc(p, e, msg ? msg : "");
    p += snprintf(p, e - p, "\\\"}");
    g_web_mirror.pushTermData(s_webdata_buf);
  };
  if (a[0] == 'v' && a[1] == 'c' && (a[2] == 0 || a[2] == ' ')) {
    const char* raw = (a[2] == ' ') ? a + 3 : "";
    char name[32] = {};
    strncpy(name, raw, sizeof(name) - 1);
    for (int i = (int)strlen(name) - 1; i >= 0 && (name[i] == ' ' || name[i] == '\\t'); --i) name[i] = '\\0';
    if (!name[0]) strncpy(name, "Private", sizeof(name) - 1);
    const int slot = the_mesh.findFirstEmptyChannelSlot();
    if (slot < 0) { v28Notice("error", "Channel table is full"); return true; }
    uint8_t secret[16] = {};
    esp_fill_random(secret, sizeof(secret));
    if (!the_mesh.uiAddOrUpdateChannel(slot, name, secret)) {
      memset(secret, 0, sizeof(secret));
      v28Notice("error", "Could not save private channel");
      return true;
    }
    memset(secret, 0, sizeof(secret));
    if (!v11_global_bridge.createChannelInvite((uint8_t)slot))
      v28Notice("error", "Could not queue join code");
    webPushContacts();
    return true;
  }
  if (a[0] == 'v' && a[1] == 'j' && a[2] == ' ') {
    if (!v11_global_bridge.requestChannelJoin(a + 3))
      v28Notice("error", "Enter a valid XXXX-XXXX join code");
    else
      v28Notice("waiting", "Join request sent - waiting for owner approval");
    return true;
  }
  if (a[0] == 'v' && a[1] == 'l' && (a[2] == 0 || a[2] == ' ')) {
    if (!v11_global_bridge.refreshJoinRequests())
      v28Notice("error", "Could not check join requests");
    return true;
  }
  if (a[0] == 'v' && (a[1] == 'a' || a[1] == 'd') && a[2] == ' ') {
    unsigned long invite = 0;
    char requesterHex[65] = {};
    if (sscanf(a + 3, "%lu %64s", &invite, requesterHex) != 2) {
      v28Notice("error", "Invalid approval request");
      return true;
    }
    uint8_t requester[PUB_KEY_SIZE] = {};
    if (!hexToPubkey32(requesterHex, requester) ||
        !v11_global_bridge.decideJoinRequest((uint32_t)invite, requester, a[1] == 'a')) {
      memset(requester, 0, sizeof(requester));
      v28Notice("error", "Could not queue approval");
      return true;
    }
    memset(requester, 0, sizeof(requester));
    v28Notice("waiting", a[1] == 'a' ? "Approving securely..." : "Denying request...");
    return true;
  }
#endif
'''
    if web_cmd_anchor not in ui:
        fail("V28 browser command API anchor missing")
    ui = replace_once(ui, web_cmd_anchor, web_cmd_new, "V28 browser invite commands")

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

    # V28 HTTPS relay overlay. V27 remains immutable; only the patched
    # checkout receives these V28 transport files.
    overlay_root = pathlib.Path(__file__).resolve().parent / "overlay"
    ca_path = pathlib.Path(__file__).resolve().parent / "relay_ca_bundle.pem"
    ca_bundle = ca_path.read_text().strip()
    if "BEGIN CERTIFICATE" not in ca_bundle or "END CERTIFICATE" not in ca_bundle:
        fail("V28 relay CA bundle invalid")

    for rel in (
        pathlib.Path("src/helpers/esp32/V11GlobalBridge.h"),
        pathlib.Path("src/helpers/esp32/V11GlobalBridge.cpp"),
    ):
        src = overlay_root / rel
        dst = root / rel
        if not src.exists():
            fail("missing V28 relay overlay " + str(src))
        content = src.read_text()
        if rel.suffix == ".cpp":
            if "__V28_CA_BUNDLE__" not in content:
                fail("V28 relay CA token missing")
            content = content.replace("__V28_CA_BUNDLE__", ca_bundle)
        dst.write_text(content)

    relay_cpp = (root / "src/helpers/esp32/V11GlobalBridge.cpp").read_text()
    relay_h = (root / "src/helpers/esp32/V11GlobalBridge.h").read_text()
    for marker in (
        "https://meshoffgridnl.vercel.app/api/v28-relay",
        "MOG28-RELAY-V1",
        "HTTPClient",
        "_mesh->v27SignGlobal",
        "_wc.setCACert(V28_RELAY_ROOT_CA)",
    ):
        if marker not in relay_cpp + "\n" + relay_h:
            fail("V28 HTTPS relay marker missing " + marker)
    for forbidden in ("PubSubClient", "broker.emqx.io", ".setInsecure("):
        if forbidden in relay_cpp + "\n" + relay_h:
            fail("V28 relay must not use " + forbidden)

    print("V28 applied: RF route 1 + Internet route 2 + professional no-dead-end UX")

if __name__ == "__main__":
    main()
