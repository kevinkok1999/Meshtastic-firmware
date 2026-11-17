#include "../MyMesh.h"
#if defined(HAS_M9_KEYBOARD)
static int findChannelThreadByName(const char* name);
static void openThreadDetailByIdx(int idx, bool channel);
#endif
static void joinPrivateChannelSubmitCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  kbMirrorSyncToReal();
  if (!s_addch_secret_ta) return;
#if defined(MESH_OFFGRIDNL_V28)
  const char* code = lv_textarea_get_text(s_addch_secret_ta);
  if (!v11_global_bridge.requestChannelJoin(code)) {
    setAddChannelError(TR("Enter a valid XXXX-XXXX join code."));
    return;
  }
  closeSettingsModal();
  if (g_lv.task) g_lv.task->showAlert(TR("Join request sent - waiting for approval"), 1800);
  return;
#endif
}
void openJoinPrivateChannelModal() {
  lv_label_set_text(hint, TR("Enter the 8-character code. The owner must approve before the secure channel is added."));
  lv_label_set_text(sec_l, TR("Join code"));
  taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX"));
  lv_textarea_set_max_length(s_addch_secret_ta, 10);
  useChainedFont(bl); lv_label_set_text(bl, TR("Request access"));
}
static void openThreadDetailByIdx(int idx, bool channel) {
  assert(channel);
  openedSlot = g_lv.task->threadMeshChannelSlot(idx);
  openedName = task.threads.at(idx).first;
  assert(closed && currentTab == CHAT_INBOX_TAB_INDEX);
}

int main() {
  auto key = "0123456789abcdef0123456789abcdef";
  seed(0, "Different", key);
  the_mesh.channels[0].channel.secret[16] = 1;  // 32-byte imported secret differs beyond typed 16-byte key
  seed(2, "Shared", key);
  task.refreshThreadsFromMesh();
  input = key;
  joinPrivateChannelSubmitCb(&event);
  assert(writes == 0 && closed && openedSlot == 2);
  assert(the_mesh.channels[2].name == std::string("Shared"));
  resetUi();
  seed(1, "Private", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  task.threads.push_back({"Removed private", 3});  // stale chat pinned to reused slot
  input = "CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC";
  joinPrivateChannelSubmitCb(&event);
  assert(writes == 1 && openedSlot == 3 && closed);
  assert(openedName == "Private 2");
  assert(the_mesh.channels[3].name == std::string("Private 2"));
  resetUi();
  input = std::string(key) + "x";
  joinPrivateChannelSubmitCb(&event);
  assert(writes == 1 && !closed && openedSlot == -1 && bridgeCalls == 0 && !error.empty());
  resetUi();
  input = "ABCD-EFGH";
  joinPrivateChannelSubmitCb(&event);
  assert(bridgeCalls == 1 && closed && openedSlot == -1);
  resetUi();
  seed(4, "Shared", "dddddddddddddddddddddddddddddddd");
  input = key;
  joinPrivateChannelSubmitCb(&event);
  assert(writes == 1 && !closed && openedSlot == -1 && !error.empty());
}
