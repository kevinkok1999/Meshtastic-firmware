#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(m): raise SystemExit("V18 patch failed: "+m)
def one(s,a,b,label):
    n=s.count(a)
    if n!=1: fail(f"{label}: expected 1 anchor, found {n}")
    return s.replace(a,b,1)
def section(s,h,nxt,a,b,label):
    i=s.find(h)
    if i<0: fail(label+": section missing")
    j=s.find(nxt,i+len(h))
    if j<0: j=len(s)
    return s[:i]+one(s[i:j],a,b,label)+s[j:]

def main():
    if len(sys.argv)!=2: fail("usage: apply_v18.py <V17-patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    pio_p=root/"platformio.ini"; main_p=root/"src/main.cpp"; ui_p=root/"src/ui-touch/UITask.cpp"
    for p in (pio_p,main_p,ui_p):
        if not p.exists(): fail("missing "+str(p))

    pio=pio_p.read_text()
    if "MESH_OFFGRIDNL_V17=1" not in pio: fail("V17 base is not present")
    pio=section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V17=1\n",
        "  -D MESH_OFFGRIDNL_V17=1\n  -D MESH_OFFGRIDNL_V18=1\n",
        "V18 build flag",
    )
    pio_p.write_text(pio)

    main=main_p.read_text()
    anchor='''static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;

#if defined(MESH_OFFGRIDNL_V17)
'''
    repl='''static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;

#if defined(MESH_OFFGRIDNL_V18)
  // V18 follows the conservative recovery strategy used by mature ESP32
  // projects: make our app preferences authoritative, throw away stale driver
  // AP state, cold-start STA once, then let Arduino/ESP-IDF own WPA + DHCP.
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  wifiConfigClearApHint();
  const char* v18_pwd = (pwd && pwd[0]) ? pwd : nullptr;

  if (g_v16_wifi_attempt <= 1) {
    Serial.printf("[V18][wifi] attempt=1 clean STA boot ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);

    // eraseap only affects the ESP32 driver's saved AP; MeshOffGridNL keeps
    // the user's SSID/password in its own Preferences store.
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_MODE_NULL);
    delay(1200);                         // allow driver/netif teardown to settle
    WiFi.mode(WIFI_STA);
    delay(150);                          // allow STA netif/DHCP client to exist

    WiFi.setAutoReconnect(false);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.setMinSecurity(v18_pwd ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
    WiFi.begin(ssid, v18_pwd);           // standard Arduino path restores DHCP
    return;
  }

  if (g_v16_wifi_attempt == 2) {
    Serial.printf("[V18][wifi] attempt=2 plain retry ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, true);
    delay(250);
    WiFi.setAutoReconnect(false);
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
    WiFi.setMinSecurity(v18_pwd ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
    WiFi.begin(ssid, v18_pwd);
    return;
  }

  Serial.printf("[V18][wifi] attempt=%u entering V17 PMF/SAE fallback ssid='%s' reason=%u\\n",
                (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
#endif

#if defined(MESH_OFFGRIDNL_V17)
'''
    main=one(main,anchor,repl,"V18 staged clean-boot association")
    main_p.write_text(main)

    ui=ui_p.read_text()
    ui=one(
        ui,
        '''#if defined(MESH_OFFGRIDNL_V17)
      snprintf(s_v16_join, sizeof s_v16_join, "S21 hotspot auth... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
#else
''',
        '''#if defined(MESH_OFFGRIDNL_V18)
      if (g_v16_wifi_attempt <= 1)
        snprintf(s_v16_join, sizeof s_v16_join, "clean S21 connect... (1/3)");
      else if (g_v16_wifi_attempt == 2)
        snprintf(s_v16_join, sizeof s_v16_join, "standard S21 retry... (2/3)");
      else
        snprintf(s_v16_join, sizeof s_v16_join, "S21 security fallback... (3/3)");
#elif defined(MESH_OFFGRIDNL_V17)
      snprintf(s_v16_join, sizeof s_v16_join, "S21 hotspot auth... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
#else
''',
        "V18 staged UI status",
    )
    ui_p.write_text(ui)

    pio=pio_p.read_text(); main=main_p.read_text(); ui=ui_p.read_text()
    for f in ("MESH_OFFGRIDNL_V16=1","MESH_OFFGRIDNL_V17=1","MESH_OFFGRIDNL_V18=1"):
        if f not in pio: fail("missing "+f)
    for m in (
        "WiFi.persistent(false);",
        "WiFi.disconnect(true, true);",
        "WiFi.mode(WIFI_MODE_NULL);",
        "delay(1200);",
        "WiFi.mode(WIFI_STA);",
        "WiFi.disconnect(false, true);",
        "WiFi.begin(ssid, v18_pwd);",
        "[V18][wifi] attempt=1 clean STA boot",
        "[V18][wifi] attempt=2 plain retry",
        "entering V17 PMF/SAE fallback",
    ):
        if m not in main: fail("main missing "+m)
    for m in ("clean S21 connect... (1/3)","standard S21 retry... (2/3)","S21 security fallback... (3/3)"):
        if m not in ui: fail("UI missing "+m)

    start=main.find("#if defined(MESH_OFFGRIDNL_V18)", main.find("static void v13WifiBegin"))
    end=main.find("#if defined(MESH_OFFGRIDNL_V17)", start)
    if start<0 or end<0: fail("V18 helper block missing")
    block=main[start:end]
    if "WiFi.setSleep(false)" in block: fail("unsafe association sleep override returned")
    if "esp_wifi_set_protocol" in block or "esp_wifi_set_bandwidth" in block:
        fail("V18 attempts 1/2 must not force PHY/bandwidth")
    if "bssid" in block.lower() or ".channel =" in block:
        fail("V18 attempts 1/2 must not pin BSSID/channel")

    print("V18 applied: clean driver boot -> plain retry -> V17 security fallback")

if __name__=="__main__":
    main()
