#!/usr/bin/env python3
from __future__ import annotations
import pathlib, shutil, sys

def fail(m): raise SystemExit("V20 patch failed: " + m)
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
    if len(sys.argv)!=2: fail("usage: apply_v20.py <V19-patched checkout>")
    root=pathlib.Path(sys.argv[1]).resolve()
    here=pathlib.Path(__file__).resolve().parent
    pio_p=root/"platformio.ini"
    mesh_p=root/"src/MyMesh.cpp"
    console_p=root/"src/ui-touch/ConsoleUI.cpp"
    for p in (pio_p,mesh_p,console_p):
        if not p.exists(): fail("missing "+str(p))

    # Install the isolated Local AI header.
    src_ai=here/"overlay/src/mesh-ai/MeshAiCore.h"
    dst_ai=root/"src/mesh-ai/MeshAiCore.h"
    if not src_ai.exists(): fail("missing overlay MeshAiCore.h")
    dst_ai.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(src_ai,dst_ai)

    # Enable V20 only on the T-Deck target, preserving all earlier flags.
    pio=pio_p.read_text()
    if "MESH_OFFGRIDNL_V19=1" not in pio: fail("V19 base flag missing")
    pio=section(
        pio,
        "[env:LilyGo_TDeck_companion_radio_touch]",
        "[env:LilyGo_TDeck_Pro_companion_radio_touch]",
        "  -D MESH_OFFGRIDNL_V19=1\n",
        "  -D MESH_OFFGRIDNL_V19=1\n"
        "  -D MESH_OFFGRIDNL_V20=1\n"
        "  -D MESH_AI_ENABLED=1\n"
        "  -D MESH_AI_TINY_ML=0\n"
        "  -D MESH_AI_VOICE=0\n",
        "V20 T-Deck flags",
    )
    pio_p.write_text(pio)

    mesh=mesh_p.read_text()
    mesh=one(
        mesh,
        '#include "WiFiConfig.h"\n',
        '#include "WiFiConfig.h"\n'
        '#if defined(MESH_OFFGRIDNL_V20) && defined(MESH_AI_ENABLED)\n'
        '#include "mesh-ai/MeshAiCore.h"\n'
        '#endif\n',
        "Mesh AI include",
    )

    old='''void MyMesh::runLocalCli(const char* cmd) {
  if (cmd && *cmd) handleMeshcomodCommand(cmd, (int)strlen(cmd));
}
'''
    new='''void MyMesh::runLocalCli(const char* cmd) {
#if defined(MESH_OFFGRIDNL_V20) && defined(MESH_AI_ENABLED)
  if (cmd && *cmd && MeshAi::isCommand(cmd)) {
    MeshAi::Context ctx{};

    const char* nn = getNodeName();
    if (nn) {
      strncpy(ctx.node_name, nn, sizeof(ctx.node_name) - 1);
      ctx.node_name[sizeof(ctx.node_name) - 1] = '\\0';
    }

    const uint32_t now = getRTCClock() ? getRTCClock()->getCurrentTime() : 0;
    ctx.contacts_total = getNumContacts();
    for (uint32_t i = 0; i < ctx.contacts_total; ++i) {
      ContactInfo c{};
      if (!getContactByIdx(i, c)) continue;
      if (c.out_path_len == 0) ctx.contacts_direct++;
      if (now && c.last_advert_timestamp && now >= c.last_advert_timestamp &&
          (now - c.last_advert_timestamp) <= 7200U) {
        ctx.contacts_recent++;
      }
    }

#if defined(ESP32)
    ctx.internal_heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ctx.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif

#if defined(ESP32) && (defined(WIFI_SSID) || defined(MULTI_TRANSPORT_COMPANION))
    ctx.wifi_supported = true;
    ctx.wifi_status = (int)WiFi.status();
    ctx.wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (ctx.wifi_connected) {
      String ai_ssid = WiFi.SSID();
      strncpy(ctx.wifi_ssid, ai_ssid.c_str(), sizeof(ctx.wifi_ssid) - 1);
      ctx.wifi_ssid[sizeof(ctx.wifi_ssid) - 1] = '\\0';
      ctx.wifi_rssi = WiFi.RSSI();
      IPAddress ai_ip = WiFi.localIP();
      snprintf(ctx.ip, sizeof(ctx.ip), "%u.%u.%u.%u",
               ai_ip[0], ai_ip[1], ai_ip[2], ai_ip[3]);
    }
#endif

#ifdef LORA_FREQ
    ctx.lora_freq_mhz = (float)LORA_FREQ;
#endif
#ifdef LORA_BW
    ctx.lora_bw_khz = (float)LORA_BW;
#endif
#ifdef LORA_SF
    ctx.lora_sf = (int)LORA_SF;
#endif
#ifdef LORA_TX_POWER
    ctx.lora_tx_dbm = (int)LORA_TX_POWER;
#endif

    ctx.gps_lat = sensors.node_lat;
    ctx.gps_lon = sensors.node_lon;
    ctx.gps_known = (ctx.gps_lat > 0.00001 || ctx.gps_lat < -0.00001 ||
                     ctx.gps_lon > 0.00001 || ctx.gps_lon < -0.00001);

    char ai_reply[512];
    if (MeshAi::answer(cmd, ctx, ai_reply, sizeof(ai_reply))) {
      pushMeshcomodReply(ai_reply, true);
    } else {
      pushMeshcomodReply("Mesh AI: opdracht niet herkend. Typ 'ai help'.", true);
    }
    return;
  }
#endif
  if (cmd && *cmd) handleMeshcomodCommand(cmd, (int)strlen(cmd));
}
'''
    mesh=one(mesh,old,new,"Mesh AI local CLI hook")
    mesh_p.write_text(mesh)

    # Discoverability in the physical console. LVGL Terminal automatically gains
    # the same command because both use MyMesh::runLocalCli().
    console=console_p.read_text()
    old_menu='''  for (int i = 0; i < kQuickN; i += 3) {
    char line[kLineCap]; int o = 0;
    for (int k = i; k < i + 3 && k < kQuickN; k++)
      o += snprintf(line + o, sizeof line - o, "%s %-9s", kQuick[k].key, kQuick[k].label);
    consoleWriteLine(line);
  }
}
'''
    new_menu='''  for (int i = 0; i < kQuickN; i += 3) {
    char line[kLineCap]; int o = 0;
    for (int k = i; k < i + 3 && k < kQuickN; k++)
      o += snprintf(line + o, sizeof line - o, "%s %-9s", kQuick[k].key, kQuick[k].label);
    consoleWriteLine(line);
  }
#if defined(MESH_OFFGRIDNL_V20) && defined(MESH_AI_ENABLED)
  consoleWriteLine("Mesh AI: type 'ai help' (fully local)");
#endif
}
'''
    console=one(console,old_menu,new_menu,"Console AI discoverability")
    console_p.write_text(console)

    # Final structural checks.
    pio=pio_p.read_text(); mesh=mesh_p.read_text(); console=console_p.read_text()
    for marker in (
        "MESH_OFFGRIDNL_V19=1",
        "MESH_OFFGRIDNL_V20=1",
        "MESH_AI_ENABLED=1",
        "MESH_AI_TINY_ML=0",
        "MESH_AI_VOICE=0",
    ):
        if marker not in pio: fail("platformio missing "+marker)
    for marker in (
        '#include "mesh-ai/MeshAiCore.h"',
        "MeshAi::isCommand(cmd)",
        "ctx.contacts_total = getNumContacts();",
        "ctx.contacts_recent++",
        "ctx.wifi_connected",
        "ctx.gps_lat = sensors.node_lat;",
        "MeshAi::answer(cmd, ctx, ai_reply",
        "pushMeshcomodReply(ai_reply, true);",
    ):
        if marker not in mesh: fail("MyMesh missing "+marker)
    if "Mesh AI: type 'ai help' (fully local)" not in console:
        fail("console discoverability missing")

    print("V20 applied: local Mesh AI intent/diagnostics layer on top of V19")

if __name__=="__main__":
    main()
