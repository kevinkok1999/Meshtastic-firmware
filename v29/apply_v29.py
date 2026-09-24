#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import shutil
import sys

def fail(msg: str) -> None:
    raise SystemExit("V29 patch failed: " + msg)

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected 1 anchor, found {count}")
    return text.replace(old, new, 1)

def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v29.py <V28-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    here = pathlib.Path(__file__).resolve().parent

    pio_path = root / "platformio.ini"
    mesh_h_path = root / "src/MyMesh.h"
    mesh_cpp_path = root / "src/MyMesh.cpp"
    main_path = root / "src/main.cpp"
    ui_path = root / "src/ui-touch/UITask.cpp"

    for p in (pio_path, mesh_h_path, mesh_cpp_path, main_path, ui_path):
        if not p.exists():
            fail("missing " + str(p))

    # V29 is strictly additive on top of the validated V28 baseline.
    pio = pio_path.read_text()
    tdeck_start = pio.find("[env:LilyGo_TDeck_companion_radio_touch]")
    tdeck_end = pio.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]", tdeck_start)
    if tdeck_start < 0 or tdeck_end < 0:
        fail("T-Deck env missing")
    block = pio[tdeck_start:tdeck_end]
    if "MESH_OFFGRIDNL_V28=1" not in block:
        fail("V28 baseline missing from T-Deck env")

    if "MESH_OFFGRIDNL_V29=1" not in block:
        block = replace_once(
            block,
            "  -D V28_BROWSER_CHAT_SHELL=1\n",
            "  -D V28_BROWSER_CHAT_SHELL=1\n"
            "  -D MESH_OFFGRIDNL_V29=1\n"
            "  -D V29_OFFLINE_CORE=1\n"
            "  -D V29_EMERGENCY_192H_TARGET=1\n"
            "  -D V29_MEMORY_TARGET_PERCENT=80\n"
            "  -D V29_SIMPLE_EMERGENCY_UX=1\n",
            "V29 T-Deck flags",
        )
        pio = pio[:tdeck_start] + block + pio[tdeck_end:]
        pio_path.write_text(pio)

    # Copy the V29 emergency fabric. It has no HTTP/MQTT dependency.
    overlay = here / "overlay"
    for src in overlay.rglob("*"):
        if src.is_dir():
            continue
        rel = src.relative_to(overlay)
        dst = root / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    # Narrow MyMesh surface: raw V29 emergency envelope TX only.
    mesh_h = mesh_h_path.read_text()
    h_anchor = """#if defined(MESH_OFFGRIDNL_V28)
  bool v28CalcSharedSecretAny(const uint8_t peerPub[32], uint8_t out[32]);
#endif
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
"""
    h_new = """#if defined(MESH_OFFGRIDNL_V28)
  bool v28CalcSharedSecretAny(const uint8_t peerPub[32], uint8_t out[32]);
#endif
#if defined(MESH_OFFGRIDNL_V29)
  bool v29SendEmergencyRaw(const uint8_t* data, size_t len);
#endif
  bool v27GetChannelByIndex(uint8_t idx, ChannelDetails& out);
"""
    if "v29SendEmergencyRaw" not in mesh_h:
        mesh_h = replace_once(mesh_h, h_anchor, h_new, "V29 MyMesh header hook")
        mesh_h_path.write_text(mesh_h)

    mesh_cpp = mesh_cpp_path.read_text()
    include_anchor = """#if defined(MESH_OFFGRIDNL_V11)
#include "helpers/esp32/V11GlobalBridge.h"
#endif
"""
    include_new = include_anchor + """#if defined(MESH_OFFGRIDNL_V29)
#include "helpers/esp32/V29EmergencyFabric.h"
#endif
"""
    if "V29EmergencyFabric.h" not in mesh_cpp:
        mesh_cpp = replace_once(mesh_cpp, include_anchor, include_new, "V29 MyMesh include")

    helper_anchor = """bool MyMesh::v27GetChannelByIndex(uint8_t idx, ChannelDetails& out) {
"""
    helper_code = """#if defined(MESH_OFFGRIDNL_V29)
bool MyMesh::v29SendEmergencyRaw(const uint8_t* data, size_t len) {
  if (!data || len == 0 || len > MAX_PACKET_PAYLOAD) return false;
  mesh::Packet* pkt = createRawData(data, len);
  if (!pkt) return false;
  sendFlood(pkt, 0, floodPathHashSize());
  return true;
}
#endif

""" + helper_anchor
    if "bool MyMesh::v29SendEmergencyRaw" not in mesh_cpp:
        mesh_cpp = replace_once(mesh_cpp, helper_anchor, helper_code,
                                "V29 raw emergency sender")

    raw_anchor = """void MyMesh::onRawDataRecv(mesh::Packet *packet) {
  if (packet->payload_len + 4 > sizeof(out_frame)) {
"""
    raw_new = """void MyMesh::onRawDataRecv(mesh::Packet *packet) {
#if defined(MESH_OFFGRIDNL_V29)
  if (packet && v29_emergency_fabric.onRawFrame(packet->payload, packet->payload_len)) {
    return;
  }
#endif
  if (packet->payload_len + 4 > sizeof(out_frame)) {
"""
    if "v29_emergency_fabric.onRawFrame" not in mesh_cpp:
        mesh_cpp = replace_once(mesh_cpp, raw_anchor, raw_new,
                                "V29 raw emergency receive hook")

    mesh_cpp_path.write_text(mesh_cpp)

    # Lifecycle: initialize after normal storage/Wi-Fi config has initialized,
    # but V29 itself never requires Wi-Fi or Internet.
    main_cpp = main_path.read_text()
    main_include_anchor = """    #if defined(MESH_OFFGRIDNL_V11)
      #include "helpers/esp32/V11GlobalBridge.h"
    #endif
"""
    main_include_new = main_include_anchor + """    #if defined(MESH_OFFGRIDNL_V29)
      #include "helpers/esp32/V29EmergencyFabric.h"
      #include "helpers/esp32/V29EmergencyPortal.h"
    #endif
"""
    if "V29EmergencyFabric.h" not in main_cpp:
        main_cpp = replace_once(main_cpp, main_include_anchor, main_include_new,
                                "V29 main include")

    begin_anchor = """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)
  v11_global_bridge.begin(&the_mesh);
#endif
"""
    begin_new = begin_anchor + """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)
  v29_emergency_fabric.begin(&the_mesh);
  v29_emergency_portal.begin();
#endif
"""
    if "v29_emergency_fabric.begin(&the_mesh)" not in main_cpp:
        main_cpp = replace_once(main_cpp, begin_anchor, begin_new, "V29 main begin")

    loop_anchor = """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V11)
#ifdef DISPLAY_CLASS
  STALL_SCOPE("v11-global", v11_global_bridge.loop());
#else
  v11_global_bridge.loop();
#endif
#endif
"""
    loop_new = loop_anchor + """#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)
#ifdef DISPLAY_CLASS
  STALL_SCOPE("v29-emergency", v29_emergency_fabric.loop());
#else
  v29_emergency_fabric.loop();
  v29_emergency_portal.loop();
#endif
#endif
"""
    if "v29_emergency_fabric.loop()" not in main_cpp:
        main_cpp = replace_once(main_cpp, loop_anchor, loop_new, "V29 main loop")

    # The local emergency portal owns AP mode while active. Keep the ordinary
    # STA reconnect state machine from forcing WIFI_STA over it, and do not
    # start the companion TCP/WS listeners on the same emergency session.
    wifi_anchor = "  bool wifi_radio_en = wifiConfigWantsWifi();\n"
    wifi_new = """#if defined(MESH_OFFGRIDNL_V29)
  const bool v29_portal_active = v29_emergency_portal.active();
  bool wifi_radio_en = wifiConfigWantsWifi() || v29_portal_active;
#else
  bool wifi_radio_en = wifiConfigWantsWifi();
#endif
"""
    if "const bool v29_portal_active" not in main_cpp:
        main_cpp = replace_once(main_cpp, wifi_anchor, wifi_new, "V29 portal Wi-Fi ownership")

    sm_anchor = "  bool wifi_state_machine_active = wifi_radio_en;\n"
    sm_new = """  bool wifi_state_machine_active = wifi_radio_en;
#if defined(MESH_OFFGRIDNL_V29)
  wifi_state_machine_active = wifi_state_machine_active && !v29_portal_active;
#endif
"""
    if "!v29_portal_active" not in main_cpp:
        main_cpp = replace_once(main_cpp, sm_anchor, sm_new, "V29 portal STA interlock")

    tcp_anchor = "  if (millis() > TCP_DEFER_MS && wifi_started) {\n"
    tcp_new = """#if defined(MESH_OFFGRIDNL_V29)
  if (millis() > TCP_DEFER_MS && wifi_started && !v29_portal_active) {
#else
  if (millis() > TCP_DEFER_MS && wifi_started) {
#endif
"""
    if "wifi_started && !v29_portal_active" not in main_cpp:
        main_cpp = replace_once(main_cpp, tcp_anchor, tcp_new, "V29 portal TCP interlock")

    main_path.write_text(main_cpp)

    # Native V29 emergency UI: intentionally simple and human-readable.
    # The heavy routing/crypto/storage logic stays inside V29EmergencyFabric.
    ui = ui_path.read_text()

    ui_include_old = '''#if defined(MESH_OFFGRIDNL_V28)
#include "../helpers/esp32/V11GlobalBridge.h"
#endif
'''
    ui_include_new = ui_include_old + '''#if defined(MESH_OFFGRIDNL_V29)
#include "../helpers/esp32/V29EmergencyFabric.h"
#include "../helpers/esp32/V29EmergencyPortal.h"
#endif
'''
    if "V29EmergencyFabric.h" not in ui:
        ui = replace_once(ui, ui_include_old, ui_include_new, "V29 UI fabric include")

    v28_cb_anchor = '''static void v28HomeSettingsCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  goToTab(SETTINGS_TAB_INDEX);
}
#endif
'''
    v29_cb_code = v28_cb_anchor + r'''
#if defined(MESH_OFFGRIDNL_V29) && defined(HAS_TDECK_GT911)
static lv_obj_t* s_v29_emergency_overlay = nullptr;

static void v29EmergencyCloseOverlay() {
  if (!s_v29_emergency_overlay) return;
  lv_obj_del(s_v29_emergency_overlay);
  s_v29_emergency_overlay = nullptr;
}

static void v29EmergencyCloseCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  v29EmergencyCloseOverlay();
}

static void v29EmergencySafeCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const uint8_t n = v29_emergency_fabric.sendCheckInToEmergencyContacts(
      V29EmergencyFabric::CheckInState::Safe);
  if (g_lv.task) {
    if (n) {
      char msg[96];
      snprintf(msg, sizeof(msg), "Veilig-melding bewaard/verzonden naar %u noodcontact%s",
               (unsigned)n, n == 1 ? "" : "en");
      g_lv.task->showAlert(msg, 2400);
    } else {
      g_lv.task->showAlert("Geen noodcontacten ingesteld. Markeer eerst een contact als favoriet.", 3200);
    }
  }
}

static void v29EmergencyHelpCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const uint8_t n = v29_emergency_fabric.sendHelpToEmergencyContacts(0, 2);
  if (g_lv.task) {
    if (n) {
      g_lv.task->showAlert("Lokale mesh-hulpvraag bewaard/verzonden. 112 is niet automatisch gebeld.", 4200);
    } else {
      g_lv.task->showAlert("Geen noodcontacten ingesteld. 112 is niet automatisch gebeld.", 4200);
    }
  }
}

static void v29EmergencyMessageCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  v29EmergencyCloseOverlay();
  goToTab(CHAT_INBOX_TAB_INDEX);
}

static void v29PortalStartApply() {
  if (!v29_emergency_portal.start()) {
    if (g_lv.task) g_lv.task->showAlert("Lokaal telefoonnetwerk kon niet starten.", 2800);
    return;
  }
  char msg[176];
  snprintf(msg, sizeof(msg),
           "Telefoon: verbind met %s  wachtwoord %s  open http://%s",
           v29_emergency_portal.ssid(),
           v29_emergency_portal.password(),
           v29_emergency_portal.ipText());
  if (g_lv.task) g_lv.task->showAlert(msg, 9000);
}

static void v29EmergencyInfoCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (v29_emergency_portal.active()) {
    v29PortalStartApply();
    return;
  }
  showConfirm(
      "Noodinformatie blijft lokaal beschikbaar. Start tijdelijk een telefoonnetwerk zonder internet?",
      "Telefoon verbinden", v29PortalStartApply);
}

static void v29EmergencyFamilyCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  v29EmergencyCloseOverlay();
  goToTab(CONTACTS_TAB_INDEX);
  if (g_lv.task) g_lv.task->showAlert("Favorieten zijn je V29-noodcontacten.", 2400);
}

static void v29EmergencyStatusCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const auto st = v29_emergency_fabric.memoryStats();
  const uint8_t contacts = v29_emergency_fabric.emergencyContactCount();
  char msg[160];
  snprintf(msg, sizeof(msg),
           "Lokaal noodnetwerk actief - %u noodcontact%s - %u bericht%s bewaard",
           (unsigned)contacts, contacts == 1 ? "" : "en",
           (unsigned)st.queueUsed, st.queueUsed == 1 ? "" : "en");
  if (g_lv.task) g_lv.task->showAlert(msg, 3600);
}

static lv_obj_t* v29EmergencyButton(lv_obj_t* parent, const char* text,
                                    lv_coord_t x, lv_coord_t y,
                                    lv_event_cb_t cb) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, 142, 46);
  lv_obj_set_pos(b, x, y);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, 126);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

static void v29EmergencyHomeCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (s_v29_emergency_overlay) return;

  v29_emergency_fabric.setEmergencyMode(true);
  s_v29_emergency_overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(s_v29_emergency_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_v29_emergency_overlay, 0, 0);
  lv_obj_clear_flag(s_v29_emergency_overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* title = lv_label_create(s_v29_emergency_overlay);
  lv_label_set_text(title, "NOODMODUS");
  lv_obj_set_style_text_font(title, &g_font_16, LV_PART_MAIN);
  lv_obj_set_pos(title, 12, 10);

  lv_obj_t* sub = lv_label_create(s_v29_emergency_overlay);
  lv_label_set_text(sub, "Werkt lokaal zonder internet");
  lv_obj_set_style_text_font(sub, &g_font_12, LV_PART_MAIN);
  lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(sub, 12, 30);

  lv_obj_t* close = lv_btn_create(s_v29_emergency_overlay);
  lv_obj_set_size(close, 52, 30);
  lv_obj_set_pos(close, 252, 8);
  lv_obj_add_event_cb(close, v29EmergencyCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* closeLabel = lv_label_create(close);
  lv_label_set_text(closeLabel, "Sluiten");
  lv_obj_center(closeLabel);

  v29EmergencyButton(s_v29_emergency_overlay, "Ik ben veilig",       8,  52, v29EmergencySafeCb);
  v29EmergencyButton(s_v29_emergency_overlay, "Ik heb hulp nodig", 164,  52, v29EmergencyHelpCb);
  v29EmergencyButton(s_v29_emergency_overlay, "Stuur bericht",       8, 106, v29EmergencyMessageCb);
  v29EmergencyButton(s_v29_emergency_overlay, "Noodinformatie",    164, 106, v29EmergencyInfoCb);
  v29EmergencyButton(s_v29_emergency_overlay, "Gezin / contacten",   8, 160, v29EmergencyFamilyCb);
  v29EmergencyButton(s_v29_emergency_overlay, "Netwerkstatus",     164, 160, v29EmergencyStatusCb);
}
#endif
'''
    if "v29EmergencyHomeCb" not in ui:
        ui = replace_once(ui, v28_cb_anchor, v29_cb_code, "V29 emergency callbacks")

    home_anchor = '''    s_home_nav_right[HOME_NAV_TERMINAL] =
        make_launcher(TR(LV_SYMBOL_ENVELOPE "  Chats"), tdBtnY(1), v28HomeChatsCb, 0, td_btn_h);
'''
    home_new = '''#if defined(MESH_OFFGRIDNL_V29)
    s_home_nav_right[HOME_NAV_TERMINAL] =
        make_launcher(TR("Noodmodus"), tdBtnY(1), v29EmergencyHomeCb, 0, td_btn_h);
#else
''' + home_anchor + '''#endif
'''
    if 'make_launcher(TR("Noodmodus")' not in ui:
        ui = replace_once(ui, home_anchor, home_new, "V29 Home emergency entry")

    # Surface received emergency events in plain language on the T-Deck.
    # This drains the V29 event queue from the existing UI loop; no second app loop.
    v29_event_anchor = '''  // Web mesh terminal: run any command the browser typed through the exact same dispatch
'''
    v29_event_code = r'''#if defined(MESH_OFFGRIDNL_V29)
  {
    V29EmergencyFabric::Event ev;
    while (v29_emergency_fabric.takeEvent(ev)) {
      const char* msg = "Noodbericht ontvangen via lokaal netwerk";
      char detail[144] = {};

      if (ev.kind == V29EmergencyFabric::Kind::CheckIn && ev.bodyLen >= 2) {
        const auto state = (V29EmergencyFabric::CheckInState)ev.body[1];
        if (state == V29EmergencyFabric::CheckInState::Safe)
          msg = "Noodcontact meldt: ik ben veilig";
        else if (state == V29EmergencyFabric::CheckInState::NeedHelp)
          msg = "Noodcontact meldt: ik heb hulp nodig";
        else if (state == V29EmergencyFabric::CheckInState::Moving)
          msg = "Noodcontact meldt: ik ben onderweg";
        else if (state == V29EmergencyFabric::CheckInState::AtMeetingPoint)
          msg = "Noodcontact meldt: ik ben bij het verzamelpunt";
      } else if (ev.kind == V29EmergencyFabric::Kind::HelpRequest) {
        msg = "Hulpvraag ontvangen via lokaal netwerk - 112 is niet automatisch gebeld";
      } else if (ev.kind == V29EmergencyFabric::Kind::MeetingPoint) {
        msg = "Update over verzamelpunt ontvangen";
      } else if (ev.kind == V29EmergencyFabric::Kind::Household) {
        msg = "Gezinsupdate ontvangen via lokaal netwerk";
      }

      snprintf(detail, sizeof(detail), "%s [%02X%02X%02X]", msg,
               ev.origin[0], ev.origin[1], ev.origin[2]);
      if (g_lv.task) g_lv.task->showAlert(detail,
          ev.priority == V29EmergencyFabric::Priority::Critical ? 5200 : 3200);
    }
  }
#endif
  // Web mesh terminal: run any command the browser typed through the exact same dispatch
'''
    if "Noodcontact meldt: ik ben veilig" not in ui:
        ui = replace_once(ui, v29_event_anchor, v29_event_code, "V29 received emergency events")

    ui_path.write_text(ui)

    joined = "\n".join((
        pio_path.read_text(),
        mesh_h_path.read_text(),
        mesh_cpp_path.read_text(),
        main_path.read_text(),
        ui_path.read_text(),
        (root / "src/helpers/esp32/V29EmergencyFabric.h").read_text(),
        (root / "src/helpers/esp32/V29EmergencyFabric.cpp").read_text(),
    ))

    for marker in (
        "MESH_OFFGRIDNL_V29=1",
        "V29_OFFLINE_CORE=1",
        "V29_MEMORY_TARGET_PERCENT=80",
        "v29SendEmergencyRaw",
        "v29_emergency_fabric.onRawFrame",
        "v29_emergency_fabric.begin(&the_mesh)",
        "v29_emergency_fabric.loop()",
        "v29_emergency_portal.begin()",
        "v29_emergency_portal.loop()",
        "v29EmergencyHomeCb",
        "Ik ben veilig",
        "Ik heb hulp nodig",
        "112 is niet automatisch gebeld",
        "Noodcontact meldt: ik ben veilig",
        "Hulpvraag ontvangen via lokaal netwerk",
        "v29_emergency_fabric.takeEvent",
        "v29PortalStartApply",
        "Telefoon verbinden",
        "const bool v29_portal_active",
        "wifi_started && !v29_portal_active",
        "MOG29-DIRECT-V1",
        "v29q0.bin",
        "v29q1.bin",
    ):
        if marker not in joined:
            fail("missing V29 marker " + marker)

    # The V29 emergency core must stay genuinely off-grid.
    fabric = (root / "src/helpers/esp32/V29EmergencyFabric.cpp").read_text()
    for forbidden in ("HTTPClient", "WiFiClientSecure", "PubSubClient", "V28_RELAY_URL"):
        if forbidden in fabric:
            fail("offline core unexpectedly depends on " + forbidden)

    print("V29 applied: offline emergency fabric + 80/20 memory governor + V28/P1 compatibility preserved")

if __name__ == "__main__":
    main()
