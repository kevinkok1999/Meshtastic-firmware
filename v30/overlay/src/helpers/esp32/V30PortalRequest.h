#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace v30 {
// Single bounded HTTP/1.1 request. No dynamic allocations, waiting or query
// credentials. A caller must independently enforce AP interface and timeout.
class PortalRequest {
public:
    enum State { Reading, Complete, Invalid };
    enum Action { Deny, Redirect, Page, Status, Safe, Help, Moving, Meeting };
    State state() const { return state_; }
    void reset() { *this = PortalRequest(); }
    void feed(char c) {
        if (state_ != Reading) return;
        if (bodyMode_) {
            if (bodyUsed_ >= bodyLength_) { state_ = Invalid; return; }
            body_[bodyUsed_++] = c;
            if (bodyUsed_ == bodyLength_) state_ = Complete;
            return;
        }
        if (++headerBytes_ > 768) { state_ = Invalid; return; }
        if (pendingCR_) {
            pendingCR_ = false;
            if (c != '\n') { state_ = Invalid; return; }
            line_[lineUsed_] = 0;
            consumeLine();
            lineUsed_ = 0;
            return;
        }
        if (c == '\r') { pendingCR_ = true; return; }
        if (c == '\n' || (unsigned char)c < 32 || (unsigned char)c > 126 ||
            lineUsed_ >= sizeof(line_) - 1) { state_ = Invalid; return; }
        line_[lineUsed_++] = c;
    }
    Action authorize(const char* apHost, const char* sessionToken) const {
        if (state_ != Complete || !apHost || !hostSeen_) return Deny;
        if (strcmp(host_, apHost) != 0) return post_ ? Deny : Redirect;
        if (!post_) {
            if (strcmp(path_, "/") == 0) return Page;
            if (strcmp(path_, "/status") == 0) return Status;
            if (strcmp(path_, "/generate_204") == 0 ||
                strcmp(path_, "/hotspot-detect.html") == 0 ||
                strcmp(path_, "/connecttest.txt") == 0) return Redirect;
            return Deny;
        }
        if (!originSeen_ || !typeSeen_ || bodyLength_ != 34 || !sessionToken ||
            strlen(sessionToken) != 32 || body_[0] != 't' || body_[1] != '=') return Deny;
        if (strncmp(origin_, "http://", 7) != 0 || strcmp(origin_ + 7, apHost) != 0) return Deny;
        uint8_t diff = 0;
        for (size_t i=0; i<32; ++i) diff |= (uint8_t)(body_[i+2] ^ sessionToken[i]);
        if (diff != 0) return Deny;
        if (strcmp(path_, "/safe") == 0) return Safe;
        if (strcmp(path_, "/help") == 0) return Help;
        if (strcmp(path_, "/moving") == 0) return Moving;
        if (strcmp(path_, "/meeting") == 0) return Meeting;
        return Deny;
    }
private:
    State state_ = Reading;
    bool requestLine_ = false, post_ = false, pendingCR_ = false, bodyMode_ = false;
    bool hostSeen_ = false, originSeen_ = false, lengthSeen_ = false, typeSeen_ = false;
    size_t lineUsed_ = 0, headerBytes_ = 0, bodyLength_ = 0, bodyUsed_ = 0;
    char line_[256] = {}, path_[64] = {}, host_[32] = {}, origin_[64] = {}, body_[34] = {};
    static bool ieq(const char* a, const char* b) {
        while (*a && *b) {
            char x=*a++,y=*b++;
            if (x >= 'A' && x <= 'Z') x += 'a'-'A';
            if (y >= 'A' && y <= 'Z') y += 'a'-'A';
            if (x!=y) return false;
        }
        return *a==*b;
    }
    static bool copy(char* dest, size_t cap, const char* src) {
        const size_t n = strlen(src);
        if (n >= cap || n == 0) return false;
        memcpy(dest, src, n+1); return true;
    }
    void consumeLine() {
        if (!requestLine_) {
            char* p = line_;
            if (strncmp(p,"GET ",4)==0) { post_=false; p+=4; }
            else if (strncmp(p,"POST ",5)==0) { post_=true; p+=5; }
            else { state_=Invalid; return; }
            char* end=strchr(p,' ');
            if (!end || strcmp(end+1,"HTTP/1.1")!=0) { state_=Invalid; return; }
            *end=0;
            if (p[0]!='/' || strchr(p,'?') || strchr(p,'#') || strchr(p,'%') ||
                !copy(path_,sizeof(path_),p)) { state_=Invalid; return; }
            requestLine_=true; return;
        }
        if (lineUsed_==0) {
            if (!hostSeen_ || (post_ && (!lengthSeen_ || bodyLength_!=34 || !originSeen_ || !typeSeen_)) ||
                (!post_ && bodyLength_!=0)) { state_=Invalid; return; }
            if (!post_) state_=Complete;
            else bodyMode_=true;
            return;
        }
        char* colon=strchr(line_,':');
        if (!colon || colon==line_) { state_=Invalid; return; }
        for(char* p=line_;p<colon;++p) {
            if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-')) {
                state_=Invalid; return;
            }
        }
        *colon=0;
        const char* value=colon+1;
        while (*value==' ') ++value;
        if (ieq(line_,"Host")) {
            if (hostSeen_ || !copy(host_,sizeof(host_),value)) { state_=Invalid; return; }
            hostSeen_=true;
        } else if (ieq(line_,"Origin")) {
            if (originSeen_ || !copy(origin_,sizeof(origin_),value)) { state_=Invalid; return; }
            originSeen_=true;
        } else if (ieq(line_,"Content-Type")) {
            if (typeSeen_ || strcmp(value,"application/x-www-form-urlencoded")!=0) { state_=Invalid; return; }
            typeSeen_=true;
        } else if (ieq(line_,"Content-Length")) {
            if (lengthSeen_ || !*value) { state_=Invalid; return; }
            size_t n=0;
            for (const char* p=value;*p;++p) {
                if (*p<'0'||*p>'9'||n>34) { state_=Invalid; return; }
                n=n*10+(size_t)(*p-'0');
                if (n>34) { state_=Invalid; return; }
            }
            lengthSeen_=true; bodyLength_=n;
        } else if (ieq(line_,"Transfer-Encoding") || ieq(line_,"Expect")) {
            state_=Invalid;
        }
    }
};
} // namespace v30
