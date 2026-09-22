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
    pio=r/"platformio.ini"
    mainp=r/"src/main.cpp"
    ui=r/"src/ui-touch/UITask.cpp"
    hh=r/"src/helpers/esp32/WifiRuntimeStore.h"
    cc=r/"src/helpers/esp32/WifiRuntimeStore.cpp"
    prefs=r/"src/helpers/esp32/TouchPrefsStore.cpp"
    for p in (pio,mainp,ui,hh,cc,prefs):
        if not p.exists(): fail("missing "+str(p.relative_to(r)))

    # ---------- Phase 1: explicit join ownership/state ----------
    s=pio.read_text()
    if "MESH_OFFGRIDNL_V15=1" not in s: fail("V15 base missing")
    s=section(s,"[env:LilyGo_TDeck_companion_radio_touch]",
              "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
              "  -D MESH_OFFGRIDNL_V15=1\n",
              "  -D MESH_OFFGRIDNL_V15=1\n  -D MESH_OFFGRIDNL_V16=1\n",
              "V16 flag")
    pio.write_text(s)

    s=hh.read_text()
    s=one(s,"""void wifiConfigClearApHint();
#endif

#endif
""","""void wifiConfigClearApHint();
#endif

#if defined(MESH_OFFGRIDNL_V16)
enum class V16WifiJoinPhase : uint8_t {
  Idle=0, Preparing, Directed, AllChannel, Recovering, Connected, Failed
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
static volatile bool s_v16_wifi_join_in_progress = false;
static volatile V16WifiJoinPhase s_v16_wifi_join_phase = V16WifiJoinPhase::Idle;
#endif
#if defined(TLORA_PAGER)
""","join state storage")
    s=one(s,"""bool wifiScanIsActive() { return s_wifi_scan_active; }

#if defined(MESH_OFFGRIDNL_V13)
""","""bool wifiScanIsActive() { return s_wifi_scan_active; }

#if defined(MESH_OFFGRIDNL_V16)
void wifiJoinSetInProgress(bool active) { s_v16_wifi_join_in_progress = active; }
bool wifiJoinInProgress() { return s_v16_wifi_join_in_progress; }
void wifiJoinSetPhase(V16WifiJoinPhase phase) { s_v16_wifi_join_phase = phase; }
V16WifiJoinPhase wifiJoinGetPhase() { return s_v16_wifi_join_phase; }
#endif

#if defined(MESH_OFFGRIDNL_V13)
""","join state implementation")
    cc.write_text(s)

    # All UI connect paths end here. Set the foreground-join latch BEFORE
    # requesting Apply, so a network-list rebuild cannot kick off a fresh scan.
    s=prefs.read_text()
    s=one(s,"""  wifiConfigSetRadioEnabled(true);
  wifiConfigRequestApply();
  touchPrefsSaveWifiNet(n.ssid, n.pwd, n.auto_join);   // re-save bumps recency
""","""  wifiConfigSetRadioEnabled(true);
#if defined(MESH_OFFGRIDNL_V16)
  wifiJoinSetInProgress(true);
  wifiJoinSetPhase(V16WifiJoinPhase::Preparing);
#endif
  wifiConfigRequestApply();
  touchPrefsSaveWifiNet(n.ssid, n.pwd, n.auto_join);   // re-save bumps recency
""","foreground join latch")
    prefs.write_text(s)

    # Suppress automatic scan-on-page-open while a foreground connect is active.
    # Also keep Arduino auto reconnect OFF after scan completion: V16 owns retries.
    s=ui.read_text()
    s=one(s,
          "  if (wifiConfigWantsWifi() && WiFi.status() != WL_CONNECTED) wifiKickScan();\n",
          """#if defined(MESH_OFFGRIDNL_V16)
  if (wifiConfigWantsWifi() && WiFi.status() != WL_CONNECTED && !wifiJoinInProgress()) wifiKickScan();
#else
  if (wifiConfigWantsWifi() && WiFi.status() != WL_CONNECTED) wifiKickScan();
#endif
""","scan-after-connect guard")
    s=one(s,"""#if !defined(TLORA_PAGER)
      WiFi.setAutoReconnect(true);
#endif
""","""#if !defined(TLORA_PAGER)
#if defined(MESH_OFFGRIDNL_V16)
      WiFi.setAutoReconnect(false);
#else
      WiFi.setAutoReconnect(true);
#endif
#endif
""","single retry owner after scan")

    # Turn the old endless "connecting..." into an observable staged state.
    s=one(s,"""static const char* wifiStaStatusBrief(int s) {
  switch (s) {
""","""static const char* wifiStaStatusBrief(int s) {
#if defined(MESH_OFFGRIDNL_V16)
  if (wifiJoinInProgress()) {
    switch (wifiJoinGetPhase()) {
      case V16WifiJoinPhase::Preparing:  return "preparing...";
      case V16WifiJoinPhase::Directed:   return "authenticating...";
      case V16WifiJoinPhase::AllChannel: return "retrying all channels...";
      case V16WifiJoinPhase::Recovering: return "recovering Wi-Fi...";
      default: break;
    }
  }
#endif
  switch (s) {
""","staged status text")
    ui.write_text(s)

    # ---------- Phase 2: deterministic 3-stage association ----------
    s=mainp.read_text()

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
static uint8_t s_v16_wifi_stage = 0;

static void v16WifiBeginStage(const char* ssid, const char* pwd, uint8_t stage) {
  if (!ssid || !ssid[0]) return;
  WiFi.setAutoReconnect(false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.setMinSecurity((pwd && pwd[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN);
  s_v16_wifi_stage = stage;

  int32_t channel = 0;
  uint8_t bssid[6] = {};
  uint8_t authmode = 0;
  if (stage == 1 &&
      wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14) {
    wifiJoinSetPhase(V16WifiJoinPhase::Directed);
    wifiConfigClearApHint();  // selected BSSID/channel is one-shot in V16
    Serial.printf("[V16][wifi] stage1 directed ssid='%s' ch=%ld auth=%u\\n",
                  ssid, (long)channel, (unsigned)authmode);
    WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr, channel, bssid, true);
    return;
  }

  wifiConfigClearApHint();
  if (stage == 3) {
    wifiJoinSetPhase(V16WifiJoinPhase::Recovering);
    Serial.printf("[V16][wifi] stage3 soft-recovery ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
    WiFi.disconnect(false, false);
    delay(180);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    delay(40);
  } else {
    wifiJoinSetPhase(V16WifiJoinPhase::AllChannel);
    Serial.printf("[V16][wifi] stage2 all-channel ssid='%s' reason=%u\\n",
                  ssid, (unsigned)g_wifi_last_disc_reason);
  }
  WiFi.begin(ssid, (pwd && pwd[0]) ? pwd : nullptr);
}

static void v16WifiStartSequence(const char* ssid, const char* pwd) {
  // Clear diagnostics only once for a NEW user-requested sequence. Never erase
  // the failure reason between stage retries.
  g_wifi_last_disc_reason = 0;
  int32_t channel = 0;
  uint8_t bssid[6] = {};
  uint8_t authmode = 0;
  const bool has_hint =
      wifiConfigGetApHint(ssid, &channel, bssid, &authmode) &&
      channel >= 1 && channel <= 14;
  v16WifiBeginStage(ssid, pwd, has_hint ? 1 : 2);
}

static bool v16WifiAdvanceSequence(const char* ssid, const char* pwd) {
  if (s_v16_wifi_stage == 1) {
    WiFi.disconnect(false, false);
    delay(80);
    v16WifiBeginStage(ssid, pwd, 2);
    return true;
  }
  if (s_v16_wifi_stage == 2) {
    v16WifiBeginStage(ssid, pwd, 3);
    return true;
  }
  if (s_v16_wifi_stage == 3 && wifiJoinInProgress()) {
    wifiJoinSetPhase(V16WifiJoinPhase::Failed);
    wifiJoinSetInProgress(false);
    s_v16_wifi_stage = 0;
    Serial.printf("[V16][wifi] foreground join failed reason=%u status=%d\\n",
                  (unsigned)g_wifi_last_disc_reason, (int)WiFi.status());
    ui_task.showAlert(TR("Wi-Fi failed; check password/security"), 3000);
    return false;
  }

  // Background recovery after the bounded foreground attempt alternates between
  // all-channel association and a soft station recovery, without eraseap.
  v16WifiBeginStage(ssid, pwd, s_v16_wifi_stage == 2 ? 3 : 2);
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
    s=one(s,old,new,"staged V16 engine")

    s=one(s,"""  static uint32_t last_wifi_retry_ms = 0;
#if defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""","""  static uint32_t last_wifi_retry_ms = 0;
#if defined(MESH_OFFGRIDNL_V16)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 12000;
#elif defined(MESH_OFFGRIDNL_V13)
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 15000;
#else
  static const uint32_t WIFI_RETRY_INTERVAL_MS = 10000;
#endif
""","V16 retry deadline")

    # V13 patch creates exactly two main-loop association call sites. First start
    # uses the staged sequence; periodic retry advances it.
    begin_block="""#if defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif"""
    if s.count(begin_block)!=2:
        fail(f"expected 2 V13 begin blocks, found {s.count(begin_block)}")
    s=s.replace(begin_block,"""#if defined(MESH_OFFGRIDNL_V16)
          v16WifiStartSequence(ssid, pwd);
#elif defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif""",1)
    s=s.replace(begin_block,"""#if defined(MESH_OFFGRIDNL_V16)
          v16WifiAdvanceSequence(ssid, pwd);
#elif defined(MESH_OFFGRIDNL_V13)
          v13WifiBegin(ssid, pwd);
#else
          WiFi.begin(ssid, pwd[0] ? pwd : nullptr);
#endif""",1)

    # The V13 retry clear remains safe; for V16 don't do it twice because the
    # stage engine owns its own disconnect/recovery sequence.
    s=one(s,"""#if defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry
#else
          WiFi.disconnect(false, true);
#endif
""","""#if defined(MESH_OFFGRIDNL_V16)
          // V16 stage engine owns disconnect/recovery.
#elif defined(MESH_OFFGRIDNL_V13)
          WiFi.disconnect(false, false);   // keep driver config; never erase AP on a retry
#else
          WiFi.disconnect(false, true);
#endif
""","retry ownership")

    s=one(s,"""    if (WiFi.status() == WL_CONNECTED) {
""","""    if (WiFi.status() == WL_CONNECTED) {
#if defined(MESH_OFFGRIDNL_V16)
      if (wifiJoinInProgress()) {
        wifiJoinSetPhase(V16WifiJoinPhase::Connected);
        wifiJoinSetInProgress(false);
      }
      wifiConfigClearApHint();
      s_v16_wifi_stage = 0;
#endif
""","successful join completion")

    s=one(s,"""    if (!wifi_radio_en) {
      WiFi.disconnect(true);
""","""    if (!wifi_radio_en) {
#if defined(MESH_OFFGRIDNL_V16)
      wifiJoinSetInProgress(false);
      wifiJoinSetPhase(V16WifiJoinPhase::Idle);
      s_v16_wifi_stage = 0;
#endif
      WiFi.disconnect(true);
""","radio-off reset")
    mainp.write_text(s)

    # ---------- Phase 3: hard contracts ----------
    alltxt="\n".join(p.read_text() for p in (pio,hh,cc,prefs,ui,mainp))
    required=(
      "MESH_OFFGRIDNL_V16=1",
      "wifiJoinSetInProgress(true);",
      "!wifiJoinInProgress()) wifiKickScan();",
      "WiFi.setAutoReconnect(false);",
      "[V16][wifi] stage1 directed",
      "[V16][wifi] stage2 all-channel",
      "[V16][wifi] stage3 soft-recovery",
      "WIFI_RETRY_INTERVAL_MS = 12000",
      "wifiConfigClearApHint();  // selected BSSID/channel is one-shot in V16",
      "foreground join failed",
      "retrying all channels..."
    )
    for x in required:
        if x not in alltxt: fail("missing contract "+x)

    # V16 must never reintroduce the crash trigger inside the association helper.
    a=mainp.read_text()
    i=a.find("static void v16WifiBeginStage")
    j=a.find("static void v16WifiStartSequence",i)
    if i<0 or j<0: fail("V16 helper range missing")
    if "WiFi.setSleep(false)" in a[i:j]:
        fail("unsafe association-time WiFi.setSleep(false) returned")

    print("V16 applied: bounded foreground join + one retry owner + one-shot BSSID + all-channel fallback")

if __name__=="__main__":
    main()
