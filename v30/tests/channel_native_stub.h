#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#define MESH_OFFGRIDNL_V30 1
#define MESH_OFFGRIDNL_V28 1
#define MAX_GROUP_CHANNELS 5
#define PUB_KEY_SIZE 32
#define TR(x) (x)
#define CHAT_INBOX_TAB_INDEX 2
#define LV_EVENT_CLICKED 1
struct lv_event_t{}; static lv_event_t event;
static std::string input, error, openedName;
static std::string* s_addch_secret_ta = &input;
static std::string* s_addch_name_ta = nullptr;
static void* hint=nullptr, *sec_l=nullptr, *bl=nullptr;
static int writes=0, openedSlot=-1, currentTab=-1, bridgeCalls=0;
static bool closed=false;
static int lv_event_get_code(lv_event_t*) {return LV_EVENT_CLICKED;}
static void kbMirrorSyncToReal() {}
static const char* lv_textarea_get_text(std::string* ta) {return ta->c_str();}
static void lv_textarea_set_text(std::string* ta,const char* value) {*ta=value;}
static void lv_textarea_set_max_length(std::string*,size_t) {}
static void lv_textarea_set_password_mode(std::string*,bool) {}
static void lv_textarea_set_password_show_time(std::string*,uint32_t) {}
static void taSetPlaceholder(std::string*,const char*) {}
static void lv_label_set_text(void*,const char*) {}
static void useChainedFont(void*) {}
static void setAddChannelError(const char* value){error=value;}
static void closeSettingsModal(){closed=true;}
static void goToTab(int tab){currentTab=tab;}
struct ChannelDetails {char name[32]; struct {uint8_t secret[32];} channel;};
struct Mesh {
  ChannelDetails channels[MAX_GROUP_CHANNELS]{};
  bool getChannel(int slot, ChannelDetails& out){if(slot<0||slot>=MAX_GROUP_CHANNELS||!channels[slot].name[0])return false;out=channels[slot];return true;}
  int findFirstEmptyChannelSlot(){for(int i=0;i<MAX_GROUP_CHANNELS;++i)if(!channels[i].name[0])return i;return -1;}
  bool uiAddOrUpdateChannel(int slot,const char*name,const uint8_t*key){++writes;std::strncpy(channels[slot].name,name,31);std::memcpy(channels[slot].channel.secret,key,16);return true;}
}; static Mesh the_mesh;
static uint8_t n(char c){return c<='9'?c-'0':c-'a'+10;}
static void seed(int slot,const char*name,const char*key){std::strncpy(the_mesh.channels[slot].name,name,31);for(int i=0;i<16;++i)the_mesh.channels[slot].channel.secret[i]=uint8_t(n(key[i*2])*16+n(key[i*2+1]));}
struct UITask {
 static constexpr int MAX_UI_THREADS=10, MAX_THREAD_NAME=31;
 std::vector<std::pair<std::string,int>> threads;
 void refreshThreadsFromMesh(){for(int slot=0;slot<MAX_GROUP_CHANNELS;++slot){ChannelDetails cd{};if(!the_mesh.getChannel(slot,cd))continue;bool found=false;for(auto &t:threads)if(t.first==cd.name){t.second=slot;found=true;break;}if(!found)threads.push_back({cd.name,slot});}}
 bool getThreadInfo(int idx,bool& ch,uint16_t& unread,uint32_t& ts,char* name,size_t cap) const {if(idx<0||idx>=int(threads.size()))return false;ch=true;unread=0;ts=0;std::snprintf(name,cap,"%s",threads[idx].first.c_str());return true;}
 int16_t threadMeshChannelSlot(int idx) const {return idx>=0&&idx<int(threads.size())?threads[idx].second:-1;}
 void showAlert(const char*,int){}
}; static UITask task;
static struct {UITask* task;bool dirty_threads;} g_lv{&task,false};
static struct {bool requestChannelJoin(const char* s){++bridgeCalls;return std::string(s)=="ABCD-EFGH";}} v11_global_bridge;
static void resetUi(){openedSlot=-1;closed=false;currentTab=-1;error.clear();openedName.clear();}
