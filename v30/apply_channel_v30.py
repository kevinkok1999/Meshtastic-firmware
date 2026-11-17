#!/usr/bin/env python3
"""V30-gated native direct-key channel join; browser WS remains invite-only."""
from __future__ import annotations
import pathlib,sys
HEADER="""#pragma once
#if defined(MESH_OFFGRIDNL_V30)
#include <stddef.h>
#include <stdint.h>
namespace v30_direct_join {
static inline bool hex(char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
static inline uint8_t nibble(char c){if(c>='0'&&c<='9')return(uint8_t)(c-'0');if(c>='a'&&c<='f')return(uint8_t)(c-'a'+10);return(uint8_t)(c-'A'+10);}
static inline bool parseKey(const char*t,size_t n,uint8_t out[16]){if(!t||!out||n!=32)return false;for(size_t i=0;i<32;++i)if(!hex(t[i]))return false;for(size_t i=0;i<16;++i)out[i]=(uint8_t)((nibble(t[i*2])<<4)|nibble(t[i*2+1]));return true;}
} // namespace v30_direct_join
#endif
"""
def fail(m): raise SystemExit("V30 channel patch failed: "+m)
def once(t,o,n,l):
    if t.count(o)!=1: fail(l+": expected one anchor")
    return t.replace(o,n,1)
def main():
    if len(sys.argv)!=2: fail("usage: apply_channel_v30.py <V30-patched WadaMesh checkout>")
    root=pathlib.Path(sys.argv[1]).resolve(); pio=root/"platformio.ini"; mesh=root/"src/MyMesh.cpp"; ui=root/"src/ui-touch/UITask.cpp"
    if not pio.exists() or not mesh.exists() or not ui.exists(): fail("missing V30 checkout files")
    text=pio.read_text(encoding="utf-8"); start=text.find("[env:LilyGo_TDeck_companion_radio_touch]"); end=text.find("[env:LilyGo_TDeck_Pro_companion_radio_touch]",start)
    if start<0 or end<0: fail("T-Deck environment missing")
    if "MESH_OFFGRIDNL_V30=1" not in text[start:end]: fail("MESH_OFFGRIDNL_V30=1 required; apply V30 baseline first")
    target=root/"src/helpers/esp32/V30DirectChannelJoin.h"; target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() and target.read_text(encoding="utf-8")!=HEADER: fail("refusing to overwrite a different V30 header")
    target.write_text(HEADER,encoding="utf-8"); src=ui.read_text(encoding="utf-8")
    if "V30DirectChannelJoin.h" not in src: src=once(src,'#include "../MyMesh.h"\n','#include "../MyMesh.h"\n#if defined(MESH_OFFGRIDNL_V30)\n#include "../helpers/esp32/V30DirectChannelJoin.h"\n#endif\n',"V30 UI include")
    anchor='''#if defined(MESH_OFFGRIDNL_V28)
  const char* code = lv_textarea_get_text(s_addch_secret_ta);
'''
    direct='''#if defined(MESH_OFFGRIDNL_V30)
  {
    const char* raw = lv_textarea_get_text(s_addch_secret_ta);
    char compact[33] = {}; size_t n = 0; bool overflow = false;
    for (const char* p = raw; *p; ++p) {
      if (*p == ' ' || *p == '\\t' || *p == '\\r' || *p == '\\n') continue;
      if (n >= sizeof(compact)-1) { overflow = true; continue; }
      compact[n++] = *p;
    }
    uint8_t secret[16] = {};
    if (overflow) { memset(secret, 0, sizeof(secret)); memset(compact, 0, sizeof(compact)); setAddChannelError(TR("Input is too long.")); return; }
    if (v30_direct_join::parseKey(compact, n, secret)) {
      int slot = -1; char existing_name[32] = {};
#ifdef MAX_GROUP_CHANNELS
      for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        if (the_mesh.getChannel(i, cd) && memcmp(cd.channel.secret, secret, sizeof(secret)) == 0) {
          slot = i; strncpy(existing_name, cd.name, sizeof(existing_name)-1); break;
        }
      }
#endif
      if (slot < 0) slot = the_mesh.findFirstEmptyChannelSlot();
      if (slot < 0) { memset(secret, 0, sizeof(secret)); memset(compact, 0, sizeof(compact)); setAddChannelError(TR("Channel table is full.")); return; }
      const char* entered = s_addch_name_ta ? lv_textarea_get_text(s_addch_name_ta) : nullptr;
      const char* label = existing_name[0] ? existing_name : ((entered && entered[0]) ? entered : "Private");
      if (!the_mesh.uiAddOrUpdateChannel(slot, label, secret)) {
        memset(secret, 0, sizeof(secret)); memset(compact, 0, sizeof(compact)); setAddChannelError(TR("Failed to save channel.")); return;
      }
      memset(secret, 0, sizeof(secret)); memset(compact, 0, sizeof(compact)); closeSettingsModal();
      if (g_lv.task) g_lv.task->showAlert(TR("Private channel saved; open it from Channels"), 1800);
      return;
    }
  }
#endif
'''
    if "v30_direct_join::parseKey" not in src:
        src=once(src,anchor,direct+anchor,"V30 direct-key submit")
        max_old='lv_textarea_set_max_length(s_addch_secret_ta, 10);'
        if src.count(max_old)!=1: fail("V30 join max-length anchor missing or duplicated")
        src=src.replace(max_old,'lv_textarea_set_max_length(s_addch_secret_ta, 32);',1)
        placeholder_old='taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX"));'
        if src.count(placeholder_old)!=1: fail("V30 join placeholder anchor missing or duplicated")
        src=src.replace(placeholder_old,'taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX or 32-hex key"));\n    lv_textarea_set_password_mode(s_addch_secret_ta, true);',1)
    elif 'lv_textarea_set_max_length(s_addch_secret_ta, 32);' not in src or '32-hex key' not in src or 'lv_textarea_set_password_mode(s_addch_secret_ta, true);' not in src:
        fail("V30 direct-key patch is incomplete")
    ui.write_text(src,encoding="utf-8")
    print("V30 native direct-key join installed; browser WebSocket remains invite-only.")
if __name__=="__main__": main()
