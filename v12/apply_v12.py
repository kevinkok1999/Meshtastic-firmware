#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys


def fail(message: str) -> None:
    raise SystemExit("V12 patch failed: " + message)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_section(text: str, header: str, next_header: str,
                  old: str, new: str, label: str) -> str:
    start = text.find(header)
    if start < 0:
        fail(f"{label}: section start missing")
    end = text.find(next_header, start + len(header))
    if end < 0:
        end = len(text)
    section = text[start:end]
    patched = replace_once(section, old, new, label)
    return text[:start] + patched + text[end:]


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: apply_v12.py <V11-patched WadaMesh checkout>")

    root = pathlib.Path(sys.argv[1]).resolve()
    if not (root / "src/main.cpp").exists():
        fail("target is not a WadaMesh checkout")

    # V12 is deliberately a THIN layer on top of V11:
    # - WadaMesh beta_83 remains authoritative for Wi-Fi scanning, saved
    #   networks, association, reconnect and normal settings behaviour.
    # - V11 remains authoritative for worldwide encrypted direct-message
    #   transport whenever the T-Deck already has a Wi-Fi connection.
    # V12 must not fork the Wi-Fi state machine again.
    mymesh = (root / "src/MyMesh.cpp").read_text()
    main_cpp = (root / "src/main.cpp").read_text()
    if "v11_global_bridge.mirrorDM" not in mymesh:
        fail("V11 global-chat overlay is not present")
    if "v11_global_bridge.begin" not in main_cpp:
        fail("V11 global-chat lifecycle is not present")

    # T-Deck-only V12 identity flag. Radio values and the underlying Wi-Fi
    # implementation stay exactly as supplied by V11 + pinned WadaMesh beta_83.
    p = root / "platformio.ini"
    s = p.read_text()
    s = patch_section(
        s,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V11=1\n",
        "  -D MESH_OFFGRIDNL_V11=1\n"
        "  -D MESH_OFFGRIDNL_V12=1\n",
        "T-Deck V12 flag",
    )
    p.write_text(s)

    # UI-only fix for the reported T-Deck issue where the Wi-Fi page showed
    # roughly the first three networks but a finger drag on those rows could
    # not reach the rest. The base page intentionally made the nested list
    # content-sized and non-scrollable, relying on the outer settings page.
    # On this touch hierarchy the button rows can retain the drag target, so
    # the outer page never receives a useful vertical scroll.
    #
    # Keep ALL network discovery/selection/storage logic untouched. Only make
    # the existing network-list container a bounded vertical viewport, using
    # the same LVGL pattern as WadaMesh's full-screen scan list.
    p = root / "src/ui-touch/UITask.cpp"
    s = p.read_text()

    cursor_anchor = """static int s_wifi_list_y = 0;

"""
    cursor_replacement = """static int s_wifi_list_y = 0;

#if defined(MESH_OFFGRIDNL_V12)
// A bounded viewport makes the existing base-WadaMesh network rows directly
// scrollable on the T-Deck. Leave enough room for the double-height settings
// title bar and the Wi-Fi status/toggle row, but keep a useful minimum on
// smaller/scaled layouts.
static lv_coord_t wifiNetworkListViewportHeight() {
  lv_coord_t h = lv_disp_get_ver_res(nullptr) - (STATUSBAR_H * 2) - SC(44);
  if (h < SC(96)) h = SC(96);
  return h;
}
#endif

"""
    s = replace_once(s, cursor_anchor, cursor_replacement, "Wi-Fi list viewport helper")

    list_anchor = """  s_wifi_list_cont = lv_obj_create(body);
  lv_obj_remove_style_all(s_wifi_list_cont);
  lv_obj_set_width(s_wifi_list_cont, cw);
  lv_obj_set_height(s_wifi_list_cont, LV_SIZE_CONTENT);
  lv_obj_set_pos(s_wifi_list_cont, 0, y);
  lv_obj_clear_flag(s_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE);   // explicit pixel layout (see wifiListRow); the modal page scrolls
"""
    list_replacement = """  s_wifi_list_cont = lv_obj_create(body);
  lv_obj_remove_style_all(s_wifi_list_cont);
  lv_obj_set_width(s_wifi_list_cont, cw);
#if defined(MESH_OFFGRIDNL_V12)
  lv_obj_set_height(s_wifi_list_cont, wifiNetworkListViewportHeight());
  lv_obj_set_scroll_dir(s_wifi_list_cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_wifi_list_cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(s_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE);
#else
  lv_obj_set_height(s_wifi_list_cont, LV_SIZE_CONTENT);
  lv_obj_clear_flag(s_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE);   // base beta_83 behaviour
#endif
  lv_obj_set_pos(s_wifi_list_cont, 0, y);
"""
    s = replace_once(s, list_anchor, list_replacement, "Wi-Fi list direct scrolling")

    height_anchor = """  lv_obj_set_height(s_wifi_list_cont, s_wifi_list_y + SC(2));   // grow the card to fit
  navMarkDirty();
"""
    height_replacement = """#if defined(MESH_OFFGRIDNL_V12)
  // Grow only until the viewport is full. Beyond that, keep the viewport
  // bounded so LVGL has a real scroll range and every scanned/saved SSID stays
  // reachable by a normal vertical swipe.
  {
    const lv_coord_t content_h = s_wifi_list_y + SC(2);
    const lv_coord_t viewport_h = wifiNetworkListViewportHeight();
    lv_obj_set_height(s_wifi_list_cont,
                      content_h < viewport_h ? content_h : viewport_h);
  }
#else
  lv_obj_set_height(s_wifi_list_cont, s_wifi_list_y + SC(2));   // base beta_83 behaviour
#endif
  navMarkDirty();
"""
    s = replace_once(s, height_anchor, height_replacement, "Wi-Fi list bounded rebuild height")
    p.write_text(s)

    # Guardrail: V12 must NOT carry the previous Wi-Fi-hardening fork. Association,
    # country policy, modem sleep and reconnect remain the pinned WadaMesh base.
    main_after = (root / "src/main.cpp").read_text()
    forbidden = [
        "v12WifiPrepareSta",
        "v12WifiPrepareAssociation",
        "v12WifiReasonBrief",
        'esp_wifi_set_country_code("NL", true)',
        "[V12][wifi]",
    ]
    for marker in forbidden:
        if marker in main_after:
            fail(f"base Wi-Fi invariant violated by leftover marker: {marker}")

    required = {
        "platformio.ini": [
            "MESH_OFFGRIDNL_V11=1",
            "MESH_OFFGRIDNL_V12=1",
            "LORA_FREQ=869.618",
            "LORA_BW=62.5",
            "LORA_SF=8",
            "LORA_TX_POWER=22",
            "MAX_LORA_TX_POWER=22",
        ],
        "src/MyMesh.cpp": [
            "v11_global_bridge.mirrorDM",
            "v11_global_bridge.noteLoRaDM",
        ],
        "src/main.cpp": [
            "v11_global_bridge.begin",
            'STALL_SCOPE("v11-global"',
        ],
        "src/ui-touch/UITask.cpp": [
            "wifiNetworkListViewportHeight",
            "lv_obj_add_flag(s_wifi_list_cont, LV_OBJ_FLAG_SCROLLABLE)",
            "wifiKickScan()",
            "touchPrefsConnectWifiNet",
            'wifiListHeader("Saved networks")',
            '"Other networks"',
        ],
        "src/helpers/esp32/TouchPrefsStore.h": [
            "TOUCH_WIFI_NET_COUNT = 8",
            "touchPrefsGetWifiNet",
            "touchPrefsConnectWifiNet",
        ],
    }
    for rel, markers in required.items():
        data = (root / rel).read_text()
        for marker in markers:
            if marker not in data:
                fail(f"{rel}: missing marker {marker}")

    print("V12 applied: base WadaMesh Wi-Fi preserved; global chat + Wi-Fi list scroll fix enabled")


if __name__ == "__main__":
    main()
