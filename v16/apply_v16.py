#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(m): raise SystemExit('V16 patch failed: '+m)
def one(s,a,b,label):
    n=s.count(a)
    if n!=1: fail(f'{label}: expected 1 anchor, found {n}')
    return s.replace(a,b,1)
def first(s,a,b,label):
    if a not in s: fail(label+': anchor missing')
    return s.replace(a,b,1)
def section(s,h,nxt,a,b,label):
    i=s.find(h)
    if i<0: fail(label+': section missing')
    j=s.find(nxt,i+len(h))
    if j<0: j=len(s)
    return s[:i]+one(s[i:j],a,b,label)+s[j:]

def main():
    if len(sys.argv)!=2: fail('usage: apply_v16.py <V15-patched WadaMesh checkout>')
    root=pathlib.Path(sys.argv[1]).resolve()
    pio_p=root/'platformio.ini'; main_p=root/'src/main.cpp'; ui_p=root/'src/ui-touch/UITask.cpp'
    for p in (pio_p,main_p,ui_p):
        if not p.exists(): fail('missing '+str(p))

    pio=pio_p.read_text()
    if 'MESH_OFFGRIDNL_V15=1' not in pio: fail('V15 base is not present')
    pio=section(pio,'[env:LilyGo_TDeck_companion_radio_touch]','[env:LilyGo_TDeck_Pro_companion_radio_touch]',
        '  -D MESH_OFFGRIDNL_V15=1\n',
        '  -D MESH_OFFGRIDNL_V15=1\n  -D MESH_OFFGRIDNL_V16=1\n',
        'V16 build flag')
    pio_p.write_text(pio)

    main=main_p.read_text()
    main=one(main,
'''extern volatile uint8_t g_wifi_last_disc_reason;
#endif
''',
'''extern volatile uint8_t g_wifi_last_disc_reason;
#if defined(MESH_OFFGRIDNL_V16)
volatile bool g_v16_wifi_join_in_progress = false;
volatile uint8_t g_v16_wifi_phase = 0;   // 0 idle, 1 associating, 2 waiting-IP, 3 connected, 4 failed, 5 paused/backoff
volatile uint8_t g_v16_wifi_attempt = 0;
#endif
#endif
''','V16 globals')

    main=one(main,
'''    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        g_wifi_last_disc_reason = info.wifi_sta_disconnected.reason;
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
''',
'''    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info){
        g_wifi_last_disc_reason = info.wifi_sta_disconnected.reason;
#if defined(MESH_OFFGRIDNL_V16)
        g_v16_wifi_phase = 4;
#endif
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
''','disconnect event diagnostics')

    main=one(main,
'''    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)   wifi_needs_reconnect = true;
        else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)    wifi_needs_reconnect = false;
    });
''',
'''    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
          wifi_needs_reconnect = true;
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 4;
#endif
        } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 2;
#endif
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
          wifi_needs_reconnect = false;
#if defined(MESH_OFFGRIDNL_V16)
          g_v16_wifi_phase = 3;
          g_v16_wifi_join_in_progress = false;
          g_v16_wifi_attempt = 0;
#endif
        }
    });
''','connection phase events')

    main=one(main,
'''  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  g_wifi_last_disc_reason = 0;

  int32_t channel = 0;
''',
'''  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  // V16 preserves the previous disconnect reason across retries. Only a new
  // user-requested join clears it in UITask.

  int32_t channel = 0;
''','preserve disconnect reason')

    main=one(main,
'''  if (wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n", ssid);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
''',
'''  if (wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
#if defined(MESH_OFFGRIDNL_V16)
    wifiConfigClearApHint();
    Serial.printf("[V16][wifi] attempt %u: selected AP once ssid='%s' ch=%ld auth=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (long)channel, (unsigned)authmode);
#else
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
#endif
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
#if defined(MESH_OFFGRIDNL_V16)
    Serial.printf("[V16][wifi] attempt %u: all-channel ssid='%s' reason=%u\\n",
                  (unsigned)g_v16_wifi_attempt, ssid, (unsigned)g_wifi_last_disc_reason);
#else
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n", ssid);
#endif
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
''','one-shot AP hint')

    main=one(main,
'''#if defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
''',
'''#if defined(MESH_OFFGRIDNL_V16)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 12000;
  static const uint32_t WIFI_RETRY_BACKOFF_MS = 60000;
#elif defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
''','V16 retry policy')

    main=one(main,
'''    wifi_started = false;
    last_wifi_retry_ms = 0;
  }
  bool wifi_state_machine_active = wifi_radio_en;
''',
'''    wifi_started = false;
    last_wifi_retry_ms = 0;
#if defined(MESH_OFFGRIDNL_V16)
    g_v16_wifi_attempt = 0;
    g_v16_wifi_phase = 0;
#endif
  }
  bool wifi_state_machine_active = wifi_radio_en;
''','reset foreground state on apply')

    main=first(main,
'''#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
''',
'''#if defined(MESH_OFFGRIDNL_V13)
#if defined(MESH_OFFGRIDNL_V16)
          if (g_v16_wifi_attempt < 250) ++g_v16_wifi_attempt;
          g_v16_wifi_phase = 1;
#endif
          v13WifiBegin(ssid, pwd);
#else
''','initial attempt phase')

    main=one(main,
'''      uint32_t now = millis();
      if ((uint32_t)(now - last_wifi_retry_ms) >= WIFI_RETRY_INTERVAL_MS) {
        last_wifi_retry_ms = now;
''',
'''      uint32_t now = millis();
#if defined(MESH_OFFGRIDNL_V16)
      const uint32_t retry_interval =
          (!g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 3)
              ? WIFI_RETRY_BACKOFF_MS : WIFI_RETRY_INTERVAL_MS;
#else
      const uint32_t retry_interval = WIFI_RETRY_INTERVAL_MS;
#endif
      if ((uint32_t)(now - last_wifi_retry_ms) >= retry_interval) {
#if defined(MESH_OFFGRIDNL_V16)
        if (g_v16_wifi_join_in_progress && g_v16_wifi_attempt >= 3) {
          g_v16_wifi_join_in_progress = false;
          g_v16_wifi_phase = 5;
          last_wifi_retry_ms = now;
        } else {
#endif
        last_wifi_retry_ms = now;
''','bounded foreground retry gate')

    old='''#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif
        }
      }
    }
'''
    new='''#if defined(MESH_OFFGRIDNL_V13)
#if defined(MESH_OFFGRIDNL_V16)
          if (g_v16_wifi_attempt < 250) ++g_v16_wifi_attempt;
          g_v16_wifi_phase = 1;
#endif
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif
        }
#if defined(MESH_OFFGRIDNL_V16)
        }
#endif
      }
    }
'''
    main=one(main,old,new,'retry attempt phase and close V16 gate')
    main_p.write_text(main)

    ui=ui_p.read_text()
    ui=one(ui,
'''  #include <esp_system.h>
  #include <esp_sleep.h>   // esp_deep_sleep_start / ext0 wakeup for the power-off menu
''',
'''  #include <esp_system.h>
  #include <esp_wifi.h>
  #include <esp_sleep.h>   // esp_deep_sleep_start / ext0 wakeup for the power-off menu
''','esp_wifi include')

    ui=one(ui,
'''static volatile int  s_wifiscan_count   = 0;
static volatile bool s_wifiscan_request = false;   // UI -> worker: scan now
static volatile bool s_wifiscan_done    = false;   // worker -> UI: results ready
''',
'''static volatile int  s_wifiscan_count   = 0;
static volatile bool s_wifiscan_request = false;   // UI -> worker: scan now
static volatile bool s_wifiscan_done    = false;   // worker -> UI: results ready
#if defined(MESH_OFFGRIDNL_V16)
extern volatile bool g_v16_wifi_join_in_progress;
extern volatile uint8_t g_v16_wifi_phase;
extern volatile uint8_t g_v16_wifi_attempt;
#endif
''','UI V16 externs')

    ui=one(ui,
'''static void wifiQueueScanWhenReady() {
  if (wifiScanIsActive() ||
''',
'''static void wifiQueueScanWhenReady() {
#if defined(MESH_OFFGRIDNL_V16)
  if (g_v16_wifi_join_in_progress) return;   // CONNECT always outranks SCAN
#endif
  if (wifiScanIsActive() ||
''','queue scan priority')

    ui=one(ui,
'''static void wifiKickScan() {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  if (wifiScanIsActive() ||
''',
'''static void wifiKickScan() {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#if defined(MESH_OFFGRIDNL_V16)
  if (g_v16_wifi_join_in_progress) return;   // never start a scan under WPA/DHCP
#endif
  if (wifiScanIsActive() ||
''','kick scan priority')

    ui=one(ui,
'''#if !defined(TLORA_PAGER)
    WiFi.setAutoReconnect(true);
#endif
    if (reconnect_after_scan)
''',
'''#if !defined(TLORA_PAGER)
#if defined(MESH_OFFGRIDNL_V16)
    WiFi.setAutoReconnect(false);   // V16: exactly one reconnect owner, even after scans
#else
    WiFi.setAutoReconnect(true);
#endif
#endif
    if (reconnect_after_scan)
''','single reconnect owner after scan')

    ui=one(ui,
'''static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
#if defined(MESH_OFFGRIDNL_V13)
''',
'''static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
#if defined(MESH_OFFGRIDNL_V16)
  g_v16_wifi_join_in_progress = true;
  g_v16_wifi_phase = 0;
  g_v16_wifi_attempt = 0;
  extern volatile uint8_t g_wifi_last_disc_reason;
  g_wifi_last_disc_reason = 0;
  bool cancelled_before_worker = false;
  if (s_wifiscan_drop_req) { s_wifiscan_drop_req = false; cancelled_before_worker = true; }
  if (s_wifiscan_drop_ms != 0) { s_wifiscan_drop_ms = 0; cancelled_before_worker = true; }
  if (__atomic_exchange_n(&s_wifiscan_request, false, __ATOMIC_ACQ_REL))
    cancelled_before_worker = true;
  if (cancelled_before_worker) {
    s_wifiscan_reconnect_after = false;
    s_wifiscan_guard_ms = 0;
    wifiScanSetActive(false);
  }
#endif
#if defined(MESH_OFFGRIDNL_V13)
''','manual join priority')

    ui=section(ui,'static void wifiDoJoin','static void wifiJoinConfirmCb',
'''  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
  wifiRebuildNetworkList();
}
''',
'''  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
#if defined(MESH_OFFGRIDNL_V16)
  refreshStatusLabels();
#else
  wifiRebuildNetworkList();
#endif
}
''','suppress post-join rescan')

    ui=one(ui,
'''  for (int attempt = 0; attempt < 4; ++attempt) {
    WiFi.scanDelete();
''',
'''  for (int attempt = 0; attempt < 4; ++attempt) {
#if defined(MESH_OFFGRIDNL_V16)
    if (g_v16_wifi_join_in_progress) return 0;
#endif
    WiFi.scanDelete();
''','abort scan before start')

    ui=one(ui,
'''    while ((st = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && (millis() - t0) < cap_ms)
      vTaskDelay(pdMS_TO_TICKS(50));             // yield -> feeds the task watchdog
''',
'''    while ((st = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && (millis() - t0) < cap_ms) {
#if defined(MESH_OFFGRIDNL_V16)
      if (g_v16_wifi_join_in_progress) {
        esp_wifi_scan_stop();                    // worker owns this scan: safe abort point
        WiFi.scanDelete();
        return 0;
      }
#endif
      vTaskDelay(pdMS_TO_TICKS(50));             // yield -> feeds the task watchdog
    }
''','abort active scan safely')

    ui=one(ui,
'''static const char* wifiStaStatusBrief(int s) {
  switch (s) {
''',
'''static const char* wifiStaStatusBrief(int s) {
#if defined(MESH_OFFGRIDNL_V16)
  if (g_v16_wifi_join_in_progress) {
    static char s_v16_join[36];
    if (g_v16_wifi_phase == 2) return "linked, getting IP...";
    if (g_v16_wifi_phase == 1) {
      snprintf(s_v16_join, sizeof s_v16_join, "associating... (%u/3)",
               (unsigned)(g_v16_wifi_attempt > 3 ? 3 : g_v16_wifi_attempt));
      return s_v16_join;
    }
  } else if (g_v16_wifi_phase == 5) {
    return "retrying in background";
  }
#endif
  switch (s) {
''','V16 status phases')
    ui_p.write_text(ui)

    pio=pio_p.read_text(); main=main_p.read_text(); ui=ui_p.read_text()
    for f in ('MESH_OFFGRIDNL_V11=1','MESH_OFFGRIDNL_V12=1','MESH_OFFGRIDNL_V13=1','MESH_OFFGRIDNL_V15=1','MESH_OFFGRIDNL_V16=1'):
        if f not in pio: fail('missing '+f)
    for m in ('g_v16_wifi_join_in_progress','g_v16_wifi_phase','g_v16_wifi_attempt','wifiConfigClearApHint();','WIFI_RETRY_INTERVAL_MS = 12000','WIFI_RETRY_BACKOFF_MS = 60000','[V16][wifi] attempt %u: selected AP once','ARDUINO_EVENT_WIFI_STA_CONNECTED'):
        if m not in main: fail('main missing '+m)
    for m in ('CONNECT always outranks SCAN','never start a scan under WPA/DHCP','esp_wifi_scan_stop();','V16: exactly one reconnect owner, even after scans','associating... (%u/3)','retrying in background','refreshStatusLabels();'):
        if m not in ui: fail('UI missing '+m)
    hs=main.find('static void v13WifiBegin'); he=main.find('\n#endif\n\nvoid loop()',hs)
    if hs<0 or he<0: fail('association helper range missing')
    if 'g_wifi_last_disc_reason = 0;' in main[hs:he]: fail('retry helper erases disconnect reason')
    print('V16 applied: scan arbitration + single reconnect owner + one-shot AP hint + bounded foreground recovery')

if __name__=='__main__': main()
