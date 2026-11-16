#include <cassert>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>
#include <cstdio>
constexpr uint16_t MAX_FRAME_SIZE=256;
enum {WS_STATE_HEADER_0,WS_STATE_HEADER_1,WS_STATE_LEN_EXT,WS_STATE_MASK,WS_STATE_PAYLOAD};
struct WiFiClient { std::deque<uint8_t> bytes; size_t available()const{return bytes.size();} int read(){int b=bytes.front();bytes.pop_front();return b;} };
struct WSClientState { WiFiClient client; uint8_t ws_opcode=0,ws_state=WS_STATE_HEADER_0,ws_mask[4]{},comp_rx_buf[MAX_FRAME_SIZE]{}; uint64_t ws_payload_len=0,ws_payload_read=0; uint16_t comp_rx_len=0;};
struct WebMirror {std::vector<std::string> cmds;void pushTermCmd(const char*p,size_t n){cmds.emplace_back(p,n);} };
struct WebSocketCompanionServer {WSClientState _clients[1];bool disconnected=false;void disconnectClient(int){disconnected=true;}void drainTermInput(int,WebMirror&);};
#include "term_fixture.inc"
static void add(WebSocketCompanionServer&s,uint8_t first,uint8_t second,const std::vector<uint8_t>&bytes){auto&q=s._clients[0].client.bytes;q.push_back(first);q.push_back(second);for(auto b:bytes)q.push_back(b);}
int main(){
  WebMirror m;
  {WebSocketCompanionServer s;add(s,0x81,0x82,{0,0,0,0,'@','t'});s.drainTermInput(0,m);assert(!s.disconnected&&m.cmds.size()==1&&m.cmds[0]=="@t");}
  {WebSocketCompanionServer s;add(s,0x81,0x02,{0,0,0,0,'@','x'});s.drainTermInput(0,m);assert(s.disconnected&&m.cmds.size()==1);}
  {WebSocketCompanionServer s;add(s,0x01,0x82,{0,0,0,0,'@','x'});s.drainTermInput(0,m);assert(s.disconnected&&m.cmds.size()==1);}
  {WebSocketCompanionServer s;add(s,0x81,0xfe,{1,1});s.drainTermInput(0,m);assert(s.disconnected&&m.cmds.size()==1);}
  {WebSocketCompanionServer s;add(s,0x81,0xff,{});s.drainTermInput(0,m);assert(s.disconnected&&m.cmds.size()==1);}
  {WebSocketCompanionServer s;add(s,0x81,0xfe,{0,2});s.drainTermInput(0,m);assert(s.disconnected&&m.cmds.size()==1);}
  {WebSocketCompanionServer s;add(s,0x81,0x80,{0,0,0,0});add(s,0x81,0x82,{0,0,0,0,'@','v'});s.drainTermInput(0,m);assert(!s.disconnected&&m.cmds.size()==2&&m.cmds[1]=="@v");}
  {WebSocketCompanionServer s;add(s,0x88,0x80,{0,0,0,0});s.drainTermInput(0,m);assert(s.disconnected);}
  std::puts("browser frame tests passed");
}
