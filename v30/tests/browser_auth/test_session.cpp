#include "V30BrowserSession.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
using namespace v30_browser;

static std::string request(const std::string& path="/term", const std::string& origin="https://device.local:8443") {
    return "GET " + path + " HTTP/1.1\r\nHost: device.local:8443\r\nOrigin: " + origin +
           "\r\nConnection: keep-alive, Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n";
}
static bool accepted(const std::string& s) {
    return validateHandshake(s.data(),s.size(),"device.local:8443","https://device.local:8443") == Route::Chat;
}
int main() {
    assert(accepted(request()));
    assert(!accepted(request("/term?pair=123")));
    assert(!accepted(request("/mirror")));
    assert(!accepted(request("/chat")));
    assert(!accepted(request("/term","http://device.local:8443")));
    assert(!accepted(request("/term","https://evil.example")));
    auto s=request(); s.insert(s.size()-2,"Origin: https://device.local:8443\r\n"); assert(!accepted(s));
    s=request(); s.insert(s.size()-2,"Host: device.local:8443\r\n"); assert(!accepted(s));
    s=request(); s.insert(s.size()-2,"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"); assert(!accepted(s));
    s=request(); s.replace(s.find("13\r\n"),2,"12"); assert(!accepted(s));
    s=request(); s.replace(s.find("dGhl"),4,"****"); assert(!accepted(s));
    s=request(); s.replace(s.find("device.local:8443"),17,"attacker.local"); assert(!accepted(s));
    s=request(); s.insert(s.size()-2," folded\r\n"); assert(!accepted(s));
    s=request(); s.insert(s.size()-2,"X-size: "+std::string(1536,'a')+"\r\n"); assert(!accepted(s));
    s=request(); s.insert(s.find("Upgrade: websocket"),"\0",1); assert(!accepted(s));
    assert(!accepted(request().substr(0,request().size()-2)));
    s=request(); s.insert(s.find("Host:"),"X-Ignored: Sec-WebSocket-Key: forged\r\n");
    Slice key{}; assert(validateHandshake(s.data(),s.size(),"device.local:8443","https://device.local:8443",&key)==Route::Chat);
    assert(equal(key,"dGhlIHNhbXBsZSBub25jZQ=="));
    s=request("/"); assert(!accepted(s));
    assert(validateHandshake(s.data(),s.size(),"device.local:8443","https://device.local:8443")==Route::Reject);
    s.erase(s.find("Origin:"),s.find("Connection:")-s.find("Origin:"));
    assert(validateHandshake(s.data(),s.size(),"device.local:8443","https://device.local:8443")==Route::Native);

    SessionGate gate;
    assert(!gate.begin(1,100,12345,false).generation);
    auto first=gate.begin(1,100,12345,true); assert(first.generation && first.code==12345);
    assert(!gate.begin(2,100,54321,true).generation);
    assert(!gate.allowed(1,first.generation,100,true));
    assert(!gate.approve(first.generation,first.code,101,false,true));
    assert(!gate.approve(first.generation,first.code+1,101,true,true));
    assert(gate.approve(first.generation,first.code,101,true,true));
    assert(gate.allowed(1,first.generation,102,true));
    assert(!gate.allowed(2,first.generation,102,true));
    assert(!gate.allowed(1,first.generation,102,false)); // link loss revokes
    assert(!gate.allowed(1,first.generation,102,true));
    auto next=gate.begin(1,200,12345,true); assert(next.generation!=first.generation);
    assert(!gate.approve(first.generation,first.code,201,true,true));
    assert(!gate.approve(next.generation,next.code,200+SessionGate::PairMs,true,true));
    auto third=gate.begin(1,UINT32_MAX-50,999999,true); assert(third.generation);
    assert(gate.approve(third.generation,third.code,25,true,true)); // millis rollover
    assert(gate.allowed(1,third.generation,26,true));
    assert(!gate.allowed(1,third.generation,25+SessionGate::SessionMs,true));
    auto fourth=gate.begin(1,500,999999,true); assert(fourth.generation);
    gate.close(2,fourth.generation); assert(gate.approve(fourth.generation,fourth.code,501,true,true));
    gate.close(1,fourth.generation); assert(!gate.allowed(1,fourth.generation,502,true));
    assert(!gate.begin(1,600,1000000,true).generation);
    auto fifth=gate.begin(1,600,12345,true); assert(fifth.generation);
    assert(!gate.approve(fifth.generation,fifth.code,601,true,false));
    assert(!gate.pending(1,fifth.generation,602,true));
    std::cout << "browser handshake + pairing lifecycle passed\n";
}
