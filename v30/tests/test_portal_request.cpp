#include "../overlay/src/helpers/esp32/V30PortalRequest.h"
#include <cassert>
#include <string>
using namespace v30;
static const char token[]="0123456789ABCDEF0123456789ABCDEF";
static PortalRequest parse(const std::string&s) { PortalRequest p; for(char c:s) { if(p.state()!=PortalRequest::Reading) break; p.feed(c); } return p; }
static std::string post(const std::string&path="/help",const std::string&host="192.168.4.1",const std::string&origin="http://192.168.4.1",const std::string&body=std::string("t=")+token) {
 return "POST "+path+" HTTP/1.1\r\nHost: "+host+"\r\nOrigin: "+origin+"\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
}
int main(){
 auto p=parse(post()); assert(p.state()==PortalRequest::Complete); assert(p.authorize("192.168.4.1",token)==PortalRequest::Help);
 assert(parse(post("/help?t="+std::string(token))).authorize("192.168.4.1",token)==PortalRequest::Deny);
 assert(parse(post("/help","evil.test")).authorize("192.168.4.1",token)==PortalRequest::Deny);
 assert(parse(post("/help","192.168.4.1","http://evil.test")).authorize("192.168.4.1",token)==PortalRequest::Deny);
 assert(parse(post("/help","192.168.4.1","http://192.168.4.1","t=BAD")).authorize("192.168.4.1",token)==PortalRequest::Deny);
 assert(parse(post("/help","192.168.4.1","http://192.168.4.1",std::string("t=")+token+"&t="+token)).authorize("192.168.4.1",token)==PortalRequest::Deny);
 assert(parse("GET /help HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n").authorize("192.168.4.1",token)==PortalRequest::Deny);
 assert(parse("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n").authorize("192.168.4.1",token)==PortalRequest::Page);
 assert(parse("GET / HTTP/1.1\r\nHost: evil.test\r\n\r\n").authorize("192.168.4.1",token)==PortalRequest::Redirect);
 assert(parse("POST /help HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: evil.test\r\n\r\n").state()==PortalRequest::Invalid);
 assert(parse("POST /help HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n").state()==PortalRequest::Invalid);
 assert(parse("GET / HTTP/1.1\r\nX: "+std::string(900,'x')+"\r\n\r\n").state()==PortalRequest::Invalid);
 auto partial=parse(post().substr(0,post().size()-1)); assert(partial.state()==PortalRequest::Reading); assert(partial.authorize("192.168.4.1",token)==PortalRequest::Deny); partial.feed(token[31]); assert(partial.authorize("192.168.4.1",token)==PortalRequest::Help);
}
