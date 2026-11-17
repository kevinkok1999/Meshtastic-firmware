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
static inline void wipe(void* p,size_t n){volatile uint8_t* bytes=(volatile uint8_t*)p;while(n--) *bytes++=0;}
} // namespace v30_direct_join
#endif
"""
def fail(m): raise SystemExit("V30 channel patch failed: "+m)
def once(t,o,n,l):
    if t.count(o)!=1: fail(l+": expected one anchor")
    return t.replace(o,n,1)
def apply(root):
    root=pathlib.Path(root).resolve(); pio=root/"platformio.ini"; mesh=root/"src/MyMesh.cpp"; ui=root/"src/ui-touch/UITask.cpp"
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
    forward='''#if defined(HAS_M9_KEYBOARD)
static int findChannelThreadByName(const char* name);
static void openThreadDetailByIdx(int idx, bool channel);
#endif
'''
    forward_new=forward+'''#if defined(MESH_OFFGRIDNL_V30) && !defined(HAS_M9_KEYBOARD)
static void openThreadDetailByIdx(int idx, bool channel);
#endif
#if defined(MESH_OFFGRIDNL_V30)
static bool v30ChannelNameInUse(const char* label, int except_slot, bool history) {
#ifdef MAX_GROUP_CHANNELS
  for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
    if (i == except_slot) continue;
    ChannelDetails cd{};
    const bool found = the_mesh.getChannel(i, cd) && cd.name[0] &&
                       strcmp(cd.name, label) == 0;
    v30_direct_join::wipe(cd.channel.secret, sizeof(cd.channel.secret));
    if (found) return true;
  }
#endif
  if (history && g_lv.task) {
    for (int i = 0; i < UITask::MAX_UI_THREADS; ++i) {
      bool channel = false; uint16_t unread = 0; uint32_t ts = 0;
      char name[UITask::MAX_THREAD_NAME + 1] = {};
      if (g_lv.task->getThreadInfo(i, channel, unread, ts, name, sizeof(name)) &&
          channel && strcmp(name, label) == 0) return true;
    }
  }
  return false;
}
#endif
'''
    direct='''#if defined(MESH_OFFGRIDNL_V30)
  {
    const char* raw = lv_textarea_get_text(s_addch_secret_ta);
    const size_t n = strlen(raw);
    uint8_t secret[PUB_KEY_SIZE] = {};  // native key has 16 bytes, plus canonical zero padding
    if (v30_direct_join::parseKey(raw, n, secret)) {
      int slot = -1; char label[32] = {};
#ifdef MAX_GROUP_CHANNELS
      for (int i = 0; i < MAX_GROUP_CHANNELS; ++i) {
        ChannelDetails cd{};
        const bool matches = the_mesh.getChannel(i, cd) && cd.name[0] &&
                             memcmp(cd.channel.secret, secret, sizeof(secret)) == 0;
        if (matches) { slot = i; strncpy(label, cd.name, sizeof(label)-1); }
        v30_direct_join::wipe(cd.channel.secret, sizeof(cd.channel.secret));
        if (matches) break;
      }
#endif
      if (slot >= 0) {
        // Existing channel configuration/history belongs to the key; never rewrite it.
        if (v30ChannelNameInUse(label, slot, false)) {
          v30_direct_join::wipe(secret, sizeof(secret));
          setAddChannelError(TR("Duplicate channel name. Rename the other channel first."));
          return;
        }
      } else {
        slot = the_mesh.findFirstEmptyChannelSlot();
        if (slot < 0) {
          v30_direct_join::wipe(secret, sizeof(secret));
          setAddChannelError(TR("Channel table is full.")); return;
        }
        // WadaMesh history is name-keyed. Even a stale history must not be reused
        // for a different key. Generate a unique local name without using key bytes.
        bool named = false;
        for (unsigned serial = 1; serial <= 256; ++serial) {
          if (serial == 1) snprintf(label, sizeof(label), "Private");
          else snprintf(label, sizeof(label), "Private %u", serial);
          if (!v30ChannelNameInUse(label, -1, true)) { named = true; break; }
        }
        if (!named || !the_mesh.uiAddOrUpdateChannel(slot, label, secret)) {
          v30_direct_join::wipe(secret, sizeof(secret));
          setAddChannelError(TR("Failed to save channel.")); return;
        }
      }
      v30_direct_join::wipe(secret, sizeof(secret));
      // Resolve the new thread by the actual mesh slot, not by display name.
      int thread_idx = -1;
      if (g_lv.task) {
        g_lv.task->refreshThreadsFromMesh();
        g_lv.dirty_threads = true;
        for (int i = 0; i < UITask::MAX_UI_THREADS; ++i) {
          if (g_lv.task->threadMeshChannelSlot(i) != slot) continue;
          bool channel = false; uint16_t unread = 0; uint32_t ts = 0;
          char thread_name[UITask::MAX_THREAD_NAME + 1] = {};
          if (g_lv.task->getThreadInfo(i, channel, unread, ts, thread_name, sizeof(thread_name)) &&
              channel && strcmp(thread_name, label) == 0) { thread_idx = i; break; }
        }
      }
      lv_textarea_set_text(s_addch_secret_ta, "");
      closeSettingsModal();
      if (thread_idx >= 0) {
        goToTab(CHAT_INBOX_TAB_INDEX);
        openThreadDetailByIdx(thread_idx, true);
      } else if (g_lv.task) {
        g_lv.task->showAlert(TR("Channel saved; open it from Chats"), 1800);
      }
      return;
    }
    v30_direct_join::wipe(secret, sizeof(secret));
    // Keep the V28 short invitation path, but never pass malformed long keys to it.
    if (n > 10) { setAddChannelError(TR("Enter exactly 32 hex characters.")); return; }
  }
#endif
'''
    if "v30_direct_join::parseKey" not in src:
        src=once(src,forward,forward_new,"V30 exact-slot helper")
        src=once(src,anchor,direct+anchor,"V30 direct-key submit")
        max_old='lv_textarea_set_max_length(s_addch_secret_ta, 10);'
        if src.count(max_old)!=1: fail("V30 join max-length anchor missing or duplicated")
        src=src.replace(max_old,'lv_textarea_set_max_length(s_addch_secret_ta, 64);',1)
        placeholder_old='taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX"));'
        if src.count(placeholder_old)!=1: fail("V30 join placeholder anchor missing or duplicated")
        src=src.replace(placeholder_old,'taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX or 32-hex key"));\n    lv_textarea_set_password_mode(s_addch_secret_ta, true);\n    lv_textarea_set_password_show_time(s_addch_secret_ta, 0);',1)
        src=once(src,'lv_label_set_text(hint, TR("Enter the 8-character code. The owner must approve before the secure channel is added."));','lv_label_set_text(hint, TR("Enter the 32-hex channel key to join directly, or a short invitation code for owner approval."));',"V30 join hint")
        src=once(src,'lv_label_set_text(sec_l, TR("Join code"));','lv_label_set_text(sec_l, TR("Key or invitation"));',"V30 join label")
        src=once(src,'lv_label_set_text(bl, TR("Request access"));','lv_label_set_text(bl, TR("Join channel"));',"V30 join button")
    elif 'lv_textarea_set_max_length(s_addch_secret_ta, 64);' not in src or '32-hex key' not in src or 'lv_textarea_set_password_mode(s_addch_secret_ta, true);' not in src or 'v30ChannelNameInUse' not in src or 'threadMeshChannelSlot(i) != slot' not in src or 'strcmp(thread_name, label) == 0' not in src:
        fail("V30 direct-key patch is incomplete")
    ui.write_text(src,encoding="utf-8")
    print("V30 native direct-key join installed; browser WebSocket remains invite-only.")
def main():
    if len(sys.argv)!=2: fail("usage: apply_channel_v30.py <V30-patched WadaMesh checkout>")
    apply(sys.argv[1])
if __name__=="__main__": main()
