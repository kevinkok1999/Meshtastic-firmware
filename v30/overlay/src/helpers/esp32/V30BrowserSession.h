#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <climits>

namespace v30_browser {
enum class Route { Reject, Chat, Native };

// No heap allocation, substring-search routing or URL credentials. Expected
// authority and origin come from the trusted listener configuration, not Host.
struct Slice { const char* p; size_t n; };
inline char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; }
inline bool equal(Slice s,const char* value,bool ci=false) {
    if (!value || s.n!=std::strlen(value)) return false;
    for(size_t i=0;i<s.n;i++) if(ci ? lower(s.p[i])!=lower(value[i]) : s.p[i]!=value[i]) return false;
    return true;
}
inline bool tokenChar(char c) {
    return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9') ||
           (c != '\0' && std::strchr("!#$%&'*+-.^_`|~",c)!=nullptr);
}
inline Slice trim(Slice s) {
    while(s.n && (*s.p==' '||*s.p=='\t')) { ++s.p; --s.n; }
    while(s.n && (s.p[s.n-1]==' '||s.p[s.n-1]=='\t')) --s.n;
    return s;
}
inline bool containsToken(Slice s,const char* value) {
    bool found=false;
    while(s.n) {
        size_t n=0; while(n<s.n && s.p[n]!=',') ++n;
        Slice token=trim({s.p,n});
        if(!token.n) return false;
        for(size_t i=0;i<token.n;i++) if(!tokenChar(token.p[i])) return false;
        found=found||equal(token,value,true);
        if(n==s.n) return found;
        s.p+=n+1; s.n-=n+1;
    }
    return false;
}
inline int b64(char c) {
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='a'&&c<='z') return c-'a'+26;
    if(c>='0'&&c<='9') return c-'0'+52;
    if(c=='+') return 62;
    if(c=='/') return 63;
    return -1;
}
inline bool validKey(Slice s) {
    // RFC 6455: exactly 16 decoded bytes, canonical base64 padding.
    if(s.n!=24||s.p[22]!='='||s.p[23]!='=') return false;
    for(size_t i=0;i<22;i++) if(b64(s.p[i])<0) return false;
    return (b64(s.p[21])&15)==0;
}
inline Route validateHandshake(const char* bytes,size_t length,const char* host,const char* origin, Slice* key=nullptr) {
    if(key) *key={nullptr,0};
    if(!bytes||length<4||length>1535||!host||!*host||!origin||!*origin) return Route::Reject;
    size_t i=0; unsigned seen=0; bool first=true;
    Route route=Route::Reject;
    Slice parsedKey{nullptr,0};
    while(i<length) {
        size_t start=i;
        while(i<length && bytes[i]!='\r') {
            unsigned char c=static_cast<unsigned char>(bytes[i]);
            if(c<32 && c!='\t') return Route::Reject;
            if(c>=127) return Route::Reject;
            ++i;
        }
        if(i+1>=length||bytes[i+1]!='\n') return Route::Reject;
        Slice line{bytes+start,i-start}; i+=2;
        if(first) {
            first=false;
            if(equal(line,"GET /term HTTP/1.1")) route=Route::Chat;
            else if(equal(line,"GET / HTTP/1.1")) route=Route::Native;
            else return Route::Reject;
            continue;
        }
        if(!line.n) {
            if(i!=length||seen!=(route==Route::Chat?63u:61u)) return Route::Reject;
            if(key) *key=parsedKey;
            return route;
        }
        size_t n=0; while(n<line.n && line.p[n]!=':') ++n;
        if(!n||n==line.n) return Route::Reject;
        for(size_t j=0;j<n;j++) if(!tokenChar(line.p[j])) return Route::Reject;
        Slice name{line.p,n}; Slice value=trim({line.p+n+1,line.n-n-1});
        unsigned bit=0; bool valid=true;
        if(equal(name,"Host",true)) { bit=1; valid=equal(value,host); }
        else if(equal(name,"Origin",true)) { bit=2; valid=route==Route::Chat&&equal(value,origin); }
        else if(equal(name,"Upgrade",true)) { bit=4; valid=equal(value,"websocket",true); }
        else if(equal(name,"Connection",true)) { bit=8; valid=containsToken(value,"upgrade"); }
        else if(equal(name,"Sec-WebSocket-Version",true)) { bit=16; valid=equal(value,"13"); }
        else if(equal(name,"Sec-WebSocket-Key",true)) { bit=32; valid=validKey(value); parsedKey=value; }
        // Reject transfer framing, authentication-in-URL/subprotocol schemes and
        // ambiguous forwarding headers. Browser extensions are not negotiated.
        else if(equal(name,"Content-Length",true)||equal(name,"Transfer-Encoding",true)||
                equal(name,"Sec-WebSocket-Protocol",true)||equal(name,"Forwarded",true)||
                equal(name,"X-Forwarded-Host",true)||equal(name,"X-Forwarded-Proto",true)||
                equal(name,"Authorization",true)||equal(name,"Cookie",true)) return Route::Reject;
        if(!valid||(seen&bit)) return Route::Reject;
        seen|=bit;
    }
    return Route::Reject;
}

struct PairRequest {
    constexpr PairRequest(uint32_t generationValue=0, uint32_t codeValue=0)
        : generation(generationValue), code(codeValue) {}
    uint32_t generation;
    uint32_t code;
};

// A single socket-bound session; caller serializes access under the listener's
// existing lock. A challenge is a display-only number, never a bearer token.
// The approval source MUST be a verified local hardware action, independent of
// remote mirror or companion inputs. No transport is considered trusted by default.
class SessionGate {
public:
    static constexpr uint32_t PairMs=30000, SessionMs=15*60*1000;
    PairRequest begin(int slot,uint32_t now,uint32_t randomCode,bool trusted) {
        expire(now,trusted);
        if(!trusted||slot<0||state!=State::Empty||sequence==UINT32_MAX||randomCode>999999) return {};
        id=++sequence; owner=slot; code=randomCode; since=now; state=State::Pending;
        return {id,code};
    }
    bool approve(uint32_t generation,uint32_t displayedCode,uint32_t now,bool physical,bool trusted) {
        expire(now,trusted);
        if(!physical||state!=State::Pending||generation!=id||displayedCode!=code) return false;
        state=State::Approved; since=now; code=0; return true;
    }
    bool allowed(int slot,uint32_t generation,uint32_t now,bool trusted) {
        expire(now,trusted);
        return state==State::Approved&&owner==slot&&id==generation;
    }
    bool pending(int slot,uint32_t generation,uint32_t now,bool trusted) {
        expire(now,trusted);
        return state==State::Pending&&owner==slot&&id==generation;
    }
    void close(int slot,uint32_t generation) { if(owner==slot&&id==generation) revoke(); }
    void revoke() { state=State::Empty; owner=-1; id=0; code=0; since=0; }
private:
    enum class State { Empty,Pending,Approved };
    State state=State::Empty; int owner=-1;
    uint32_t sequence=0,id=0,code=0,since=0;
    void expire(uint32_t now,bool trusted) {
        if(!trusted||(state==State::Pending && uint32_t(now-since)>=PairMs)||
           (state==State::Approved && uint32_t(now-since)>=SessionMs)) revoke();
    }
};
}
