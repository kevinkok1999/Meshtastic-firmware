#!/usr/bin/env python3
from __future__ import annotations
import pathlib, sys

def fail(m): raise SystemExit("V16 patch failed: "+m)
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
    if len(sys.argv)!=2: fail("usage: apply_v16.py <V15 checkout>")
    r=pathlib.Path(sys.argv[1]).resolve()
    pio=r/"platformio.ini"; mainp=r/"src/main.cpp"; ui=r/"src/ui-touch/UITask.cpp"
    hh=r/"src/helpers/esp32/WifiRuntimeStore.h"; cc=r/"src/helpers/esp32/WifiRuntimeStore.cpp"
    for p in (pio,mainp,ui,hh,cc):
        if not p.exists(): fail("missing "+str(p.relative_to(r)))

    s=pio.read_text()
    if "MESH_OFFGRIDNL_V15=1" not in s: fail("V15 base missing")
    s=section(s,"[env:LilyGo_TDeck_companion_radio_touch]","[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V15=1\n",
        "  -D MESH_OFFGRIDNL_V15=1\n  -D MESH_OFFGRIDNL_V16=1\n","V16 flag")
    pio.write_text(s)

    s=hh.read_text()
    s=one(s,"""void wifiConfigClearApHint();
#endif

#endif
""","""void wifiConfigClearApHint();
#endif

#if defined(MESH_OFFGRIDNL_V16)
enum class V16WifiJoinPhase : uint8_t {
  Idle=0, Preparing, Directed, AllChannel, Recovering, WaitingIp, Connected, Failed
};
void wifiJoinSetInProgress(bool active);
bool wifiJoinInProgress();
void wifiJoinSetPhase(V16WifiJoinPhase phase);
V16WifiJoinPhase wifiJoinGetPhase();
#endif

#endif
""","join declarations")
    hh.write_text(s)

    s=cc.read_text()
    s=one(s,"""static volatile bool s_wifi_apply_requested = false;
#if defined(TLORA_PAGER)
""","""static volatile bool s_wifi_apply_requested = false;
#if defined(MESH_OFFGRIDNL_V16)
static volatile bool s_v16_wifi_join_in_progress=false;
static volatile V16WifiJoinPhase s_v16_wifi_join_phase=V16WifiJoinPhase::Idle;
#endif
#if defined(TLORA_PAGER)
""","join storage")
    s=one(s,"""bool wifiScanIsActive() { return s_wifi_scan_active; }

#if defined(MESH_OFFGRIDNL_V13)
""","""bool wifiScanIsActive() { return s_wifi_scan_active; }

#if defined(MESH_OFFGRIDNL_V16)
void wifiJoinSetInProgress(bool v){s_v16_wifi_join_in_progress=v;}
bool wifiJoinInProgress(){return s_v16_wifi_join_in_progress;}
void wifiJoinSetPhase(V16WifiJoinPhase p){s_v16_wifi_join_phase=p;}
V16WifiJoinPhase wifiJoinGetPhase(){return s_v16_wifi_join_phase;}
#endif

#if defined(MESH_OFFGRIDNL_V13)
""","join implementation")
    cc.write_text(s)

    s=ui.read_text()
    s=one(s,"""static void wifiKickScan() {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
  if (wifiScanIsActive() ||
""","""static void wifiKickScan() {
#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#if defined(MESH_OFFGRIDNL_V16)
  if (wifiJoinInProgress()) return;
#endif
  if (wifiScanIsActive() ||
""","scan suppression")
    s=one(s,"""static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
""","""static void wifiDoJoin(const char* ssid, const char* pwd, bool auto_join) {
  if (!ssid || !ssid[0]) return;
#if defined(MESH_OFFGRIDNL_V16)
  wifiJoinSetInProgress(true);
  wifiJoinSetPhase(V16WifiJoinPhase::Preparing);
  bool cancelled=false;
  if (s_wifiscan_drop_req){s_wifiscan_drop_req=false; cancelled=true;}
  if (s_wifiscan_drop_ms!=0){s_wifiscan_drop_ms=0; cancelled=true;}
  if (__atomic_exchange_n(&s_wifiscan_request,false,__ATOMIC_ACQ_REL)) cancelled=true;
  if (cancelled){
    s_wifiscan_guard_ms=0;
    s_wifiscan_reconnect_after=false;
    wifiScanSetActive(false);
  }
#endif
""","connect priority")
    s=one(s,"""  const int idx = touchPrefsSaveWifiNet(ssid, pwd, auto_join);
  if (idx >= 0) touchPrefsConnectWifiNet(idx);
  wifiSheetClose();
  hideKb();
  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
  wifiRebuildNetworkList();
""","""  const int idx = touchPrefsSaveWifiNet(ssid, pwd, auto_join);
  bool queued=false;
  if (idx>=0) queued=touchPrefsConnectWifiNet(idx);
#if defined(MESH_OFFGRIDNL_V16)
  if (!queued){
    wifiJoinSetInProgress(false);
    wifiJoinSetPhase(V16WifiJoinPhase::Failed);
    if (g_lv.task) g_lv.task->showAlert(TR("Could not save Wi-Fi network"),2200);
  }
#endif
  wifiSheetClose();
  hideKb();
#if defined(MESH_OFFGRIDNL_V16)
  if (queued && g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"),1400);
#else
  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"),1400);
#endif
  wifiRebuildNetworkList();
""","join queue")
    s=one(s,"""static void wifiDetailsConnectCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int idx = s_wifi_sheet_net_idx;
  wifiSheetClose();
  if (idx >= 0) touchPrefsConnectWifiNet(idx);
  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"), 1400);
  wifiRebuildNetworkList();
}
""","""static void wifiDetailsConnectCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  const int idx=s_wifi_sheet_net_idx;
#if defined(MESH_OFFGRIDNL_V16)
  TouchWifiNet n;
  if (idx>=0 && touchPrefsGetWifiNet(idx,n) && n.used && n.ssid[0]){
    wifiDoJoin(n.ssid,n.pwd,n.auto_join);
    return;
  }
  wifiSheetClose();
  if (g_lv.task) g_lv.task->showAlert(TR("Saved Wi-Fi network unavailable"),1800);
#else
  wifiSheetClose();
  if (idx>=0) touchPrefsConnectWifiNet(idx);
  if (g_lv.task) g_lv.task->showAlert(TR("Connecting\\xE2\\x80\\xA6"),1400);
  wifiRebuildNetworkList();
#endif
}
""","saved connect")
    s=one(s,"""#if !defined(TLORA_PAGER)
    WiFi.setAutoReconnect(true);
#endif
    if (reconnect_after_scan)
""","""#if !defined(TLORA_PAGER)
#if defined(MESH_OFFGRIDNL_V16)
    WiFi.setAutoReconnect(false);
#else
    WiFi.setAutoReconnect(true);
#endif
#endif
    if (reconnect_after_scan)
""","single reconnect owner")
    s=one(s,"""  if (wifiConfigWantsWifi() && WiFi.status() != WL_CONNECTED) wifiKickScan();
""","""#if defined(MESH_OFFGRIDNL_V16)
  if (wifiConfigWantsWifi() && WiFi.status()!=WL_CONNECTED && !wifiJoinInProgress()) wifiKickScan();
#else
  if (wifiConfigWantsWifi() && WiFi.status()!=WL_CONNECTED) wifiKickScan();
#endif
""","page scan guard")
    s=one(s,"""static const char* wifiStaStatusBrief(int s) {
  switch (s) {
""","""static const char* wifiStaStatusBrief(int s) {
#if defined(MESH_OFFGRIDNL_V16)
  if (wifiJoinInProgress()){
    switch(wifiJoinGetPhase()){
      case V16WifiJoinPhase::Preparing: return "preparing...";
      case V16WifiJoinPhase::Directed: return "authenticating...";
      case V16WifiJoinPhase::AllChannel: return "retrying all channels...";
      case V16WifiJoinPhase::Recovering: return "recovering Wi-Fi...";
      case V16WifiJoinPhase::WaitingIp: return "waiting for IP...";
      default: break;
    }
  }
#endif
  switch (s) {
""","phase UI")
    ui.write_text(s)

    s=mainp.read_text()
    s=one(s,"""    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED)   wifi_needs_reconnect = true;
        else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP)    wifi_needs_reconnect = false;
    });
""","""    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t){
        if (event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
          wifi_needs_reconnect=true;
        } else if (event==ARDUINO_EVENT_WIFI_STA_CONNECTED){
#if defined(MESH_OFFGRIDNL_V16)
          if (wifiJoinInProgress()) wifiJoinSetPhase(V16WifiJoinPhase::WaitingIp);
#endif
        } else if (event==ARDUINO_EVENT_WIFI_STA_GOT_IP){
          wifi_needs_reconnect=false;
#if defined(MESH_OFFGRIDNL_V16)
          if (wifiJoinInProgress()) wifiJoinSetPhase(V16WifiJoinPhase::Connected);
#endif
        }
    });
""","event phases")

    old="""#if defined(MESH_OFFGRIDNL_V13)
static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;

  // A phone hotspot association is timing-sensitive. Keep the station awake
  // until GOT_IP; the existing post-connect path turns modem sleep back on.
  WiFi.setAutoReconnect(false);
  // V15 crash fix: keep the base firmware in control of ESP32-S3
  // Wi-Fi/Bluetooth power management during association.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  g_wifi_last_disc_reason = 0;

  int32_t channel = 0;
  uint8_t bssid[6] = {};
  uint8_t authmode = 0;
  if (wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
  } else {
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n", ssid);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
  }
}
#endif
"""
    new="""#if defined(MESH_OFFGRIDNL_V16)
static uint8_t s_v16_wifi_stage=0;
static void v16WifiBeginStage(const char* ssid,const char* pwd,uint8_t stage){
  if(!ssid||!ssid[0]) return;
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setMinSecurity((pwd&&pwd[0])?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN);
  s_v16_wifi_stage=stage;
  int32_t ch=0; uint8_t bssid[6]={}; uint8_t auth=0;
  if(stage==1 && wifiConfigGetApHint(ssid,&ch,bssid,&auth) && ch>=1 && ch<=14){
    wifiJoinSetPhase(V16WifiJoinPhase::Directed);
    wifiConfigClearApHint();
    Serial.printf("[V16][wifi] stage1 directed ssid='%s' ch=%ld auth=%u\\n",ssid,(long)ch,(unsigned)auth);
    WiFi.begin(ssid,(pwd&&pwd[0])?pwd:nullptr,ch,bssid,true);
    return;
  }
  wifiConfigClearApHint();
  if(stage==3){
    wifiJoinSetPhase(V16WifiJoinPhase::Recovering);
    Serial.printf("[V16][wifi] stage3 recovery ssid='%s' reason=%u\\n",ssid,(unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false,false); delay(180); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false); delay(40);
  } else {
    wifiJoinSetPhase(V16WifiJoinPhase::AllChannel);
    Serial.printf("[V16][wifi] stage2 all-channel ssid='%s' reason=%u\\n",ssid,(unsigned)g_wifi_last_disc_reason);
  }
  WiFi.begin(ssid,(pwd&&pwd[0])?pwd:nullptr);
}
static void v16WifiStartSequence(const char* ssid,const char* pwd){
  g_wifi_last_disc_reason=0;
  int32_t ch=0; uint8_t b[6]={}; uint8_t a=0;
  bool hint=wifiConfigGetApHint(ssid,&ch,b,&a) && ch>=1 && ch<=14;
  v16WifiBeginStage(ssid,pwd,hint?1:2);
}
static bool v16WifiAdvanceSequence(const char* ssid,const char* pwd){
  if(s_v16_wifi_stage==1){WiFi.disconnect(false,false);delay(80);v16WifiBeginStage(ssid,pwd,2);return true;}
  if(s_v16_wifi_stage==2){v16WifiBeginStage(ssid,pwd,3);return true;}
  if(wifiJoinInProgress()){
    wifiJoinSetPhase(V16WifiJoinPhase::Failed); wifiJoinSetInProgress(false); s_v16_wifi_stage=0;
    Serial.printf("[V16][wifi] foreground join failed reason=%u status=%d\\n",(unsigned)g_wifi_last_disc_reason,(int)WiFi.status());
    ui_task.showAlert(TR("Wi-Fi failed; check password/security"),3000);
    return false;
  }
  v16WifiBeginStage(ssid,pwd,s_v16_wifi_stage==3?2:3);
  return true;
}
#elif defined(MESH_OFFGRIDNL_V13)
static void v13WifiBegin(const char* ssid, const char* pwd) {
  if (!ssid || !ssid[0]) return;
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  g_wifi_last_disc_reason = 0;
  int32_t channel=0; uint8_t bssid[6]={}; uint8_t authmode=0;
  if (wifiConfigGetApHint(ssid,&channel,bssid,&authmode) && channel>=1 && channel<=14) {
    Serial.printf("[V13][wifi] join selected AP ssid='%s' ch=%ld auth=%u\\n",ssid,(long)channel,(unsigned)authmode);
    WiFi.begin(ssid,(pwd&&pwd[0])?pwd:nullptr,channel,bssid,true);
  } else {
    Serial.printf("[V13][wifi] join ssid='%s' all-channel fallback\\n",ssid);
    WiFi.begin(ssid,(pwd&&pwd[0])?pwd:nullptr);
  }
}
#endif
"""
    s=one(s,old,new,"staged engine")

    s=one(s,"""#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif""","""#if defined(MESH_OFFGRIDNL_V16)
          v16WifiStartSequence(ssid,pwd);
#elif defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid,pwd);
#else
          WiFi.begin(ssid,pwd[0]?pwd:nullptr);
#endif""","initial sequence")

    s=one(s,"""#if defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry
#else
          WiFi.disconnect(false, true);
#endif
#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif""","""#if defined(MESH_OFFGRIDNL_V16)
          v16WifiAdvanceSequence(ssid,pwd);
#elif defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false,false);
          v13WifiBegin(ssid,pwd);
#else
          WiFi.disconnect(false,true);
          WiFi.begin(ssid,pwd[0]?pwd:nullptr);
#endif""","retry sequence")

    s=one(s,"""    if (WiFi.status() == WL_CONNECTED) {
""","""    if (WiFi.status() == WL_CONNECTED) {
#if defined(MESH_OFFGRIDNL_V16)
      if(wifiJoinInProgress()){wifiJoinSetPhase(V16WifiJoinPhase::Connected);wifiJoinSetInProgress(false);}
      wifiConfigClearApHint(); s_v16_wifi_stage=0;
#endif
""","success completion")
    s=one(s,"""    if (!wifi_radio_en) {
      WiFi.disconnect(true);
""","""    if (!wifi_radio_en) {
#if defined(MESH_OFFGRIDNL_V16)
      wifiJoinSetInProgress(false); wifiJoinSetPhase(V16WifiJoinPhase::Idle); s_v16_wifi_stage=0;
#endif
      WiFi.disconnect(true);
""","radio off")
    mainp.write_text(s)

    alltxt=pio.read_text()+hh.read_text()+cc.read_text()+ui.read_text()+mainp.read_text()
    for x in ("MESH_OFFGRIDNL_V16=1","V16WifiJoinPhase","[V16][wifi] stage1 directed",
              "[V16][wifi] stage2 all-channel","[V16][wifi] stage3 recovery",
              "v16WifiAdvanceSequence","retrying all channels..."):
        if x not in alltxt: fail("missing contract "+x)
    print("V16 applied: connect priority, single reconnect owner, staged hotspot recovery")

if __name__=="__main__": main()
