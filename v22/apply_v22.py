#!/usr/bin/env python3
from pathlib import Path
import sys

def die(s): raise SystemExit("V22 patch failed: "+s)
def one(s,a,b,n):
    if s.count(a)!=1: die(n)
    return s.replace(a,b,1)
def main():
    if len(sys.argv)!=2: die("usage: apply_v22.py <checkout>")
    r=Path(sys.argv[1]); p=r/"platformio.ini"; w=r/"src/mesh/wifi/WiFiAPClient.cpp"
    if not p.exists() or not w.exists(): die("missing sources")
    s=p.read_text(); i=s.find("[env:LilyGo_TDeck_companion_radio_touch]"); j=s.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]",i)
    if i<0 or j<0: die("T-Deck section")
    sec=s[i:j]
    sec=one(sec,"  -D MESH_OFFGRIDNL_V21=1\n","  -D MESH_OFFGRIDNL_V21=1\n  -D MESH_OFFGRIDNL_V22=1\n  -D MESH_V22_WIFI_SINGLE_OWNER=1\n  -D MESH_V22_WIFI_DIAGNOSTICS=1\n","flags")
    p.write_text(s[:i]+sec+s[j:])
    x=w.read_text()
    x=one(x,"static bool wifiReconnectPending = false;\n","static bool wifiReconnectPending = false;\nstatic uint32_t v22WifiAttempt = 0;\n","state")
    old='''    if (config.network.wifi_enabled && needReconnect) {
        if (!*wifiPsw) // Treat empty password as no password
            wifiPsw = NULL;
        needReconnect = false;
        isReconnecting = true;
        // Make sure we clear old connection credentials
#ifdef ARCH_ESP32
        WiFi.disconnect(false, true);
#elif defined(ARCH_RP2040)
        WiFi.disconnect(false);
#endif
        LOG_INFO("Reconnecting to WiFi access point %s", wifiName);
        wifiReconnectStartMillis = millis();
        wifiReconnectPending = true;
        return 5000; // Schedule next check soon
    }
'''
    new='''#if defined(MESH_OFFGRIDNL_V22)
    if (config.network.wifi_enabled && needReconnect) {
        if (!*wifiPsw) wifiPsw = nullptr;
        needReconnect = false;
        isReconnecting = true;
        ++v22WifiAttempt;
        WiFi.setAutoReconnect(false);
        LOG_INFO("[V22][wifi] associate attempt=%u ssid=%s", (unsigned)v22WifiAttempt, wifiName);
        if (v22WifiAttempt == 1) {
            WiFi.persistent(false);
            WiFi.begin(wifiName, wifiPsw);
        } else {
            WiFi.reconnect();
        }
        isReconnecting = false;
        return 1000;
    }
#elif defined(MESH_OFFGRIDNL_V21)
    if (config.network.wifi_enabled && needReconnect) {
        if (!*wifiPsw) wifiPsw = nullptr;
        needReconnect = false;
        isReconnecting = true;
        WiFi.setAutoReconnect(false);
        WiFi.begin(wifiName, wifiPsw);
        isReconnecting = false;
        return 1000;
    }
#endif
'''
    x=one(x,old,new,"single owner")
    old2='''        if (!isReconnecting) {
            WiFi.disconnect(false, true);
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1000);
        }
'''
    new2='''        if (!isReconnecting) {
#if defined(MESH_OFFGRIDNL_V22)
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1500);
#else
            WiFi.disconnect(false, true);
            syslog.disable();
            needReconnect = true;
            wifiReconnect->setIntervalFromNow(1000);
#endif
        }
'''
    if x.count(old2)!=2: die("disconnect handlers")
    x=x.replace(old2,new2)
    x=one(x,"            WiFi.setAutoReconnect(true);\n            WiFi.setSleep(false);\n","""#if defined(MESH_OFFGRIDNL_V22)
            WiFi.setAutoReconnect(false);
            WiFi.setSleep(true);
#else
            WiFi.setAutoReconnect(true);
            WiFi.setSleep(false);
#endif
""","radio defaults")
    x=one(x,'        LOG_INFO("Obtained IP address: %s", WiFi.localIP().toString().c_str());\n','''#if defined(MESH_OFFGRIDNL_V22)
        LOG_INFO("[V22][wifi] phase=GOT_IP ip=%s rssi=%d channel=%d",
                 WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
        v22WifiAttempt = 0;
#endif
        LOG_INFO("Obtained IP address: %s", WiFi.localIP().toString().c_str());
''',"got ip")
    x=one(x,'        LOG_INFO("Disconnected from WiFi access point");\n','''#if defined(MESH_OFFGRIDNL_V22)
        LOG_INFO("[V22][wifi] phase=DISCONNECTED reason=%u attempt=%u",
                 (unsigned)wifiDisconnectReason, (unsigned)v22WifiAttempt);
#endif
        LOG_INFO("Disconnected from WiFi access point");
''',"diag")
    w.write_text(x)
    print("V22 applied")
if __name__=="__main__": main()
