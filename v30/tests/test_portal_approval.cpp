#include "../overlay/src/helpers/esp32/V30PortalApproval.h"
#include <cassert>
using namespace v30;
int main(){
 PortalApproval p;
 assert(p.request(PortalRequest::Help,100));
 auto first=p.pending(); assert(first!=0 && p.action(first)==PortalRequest::Help);
 assert(p.take(101)==PortalRequest::Deny);
 assert(!p.request(PortalRequest::Safe,102));
 assert(!p.approve(first+16));
 assert(p.approve(first)); assert(!p.approve(first));
 assert(p.take(103)==PortalRequest::Help); assert(p.take(104)==PortalRequest::Deny);
 assert(p.request(PortalRequest::Safe,200));
 auto second=p.pending(); assert(second!=first && !p.approve(first));
 assert(p.approve(second)); assert(p.take(30200)==PortalRequest::Deny); assert(!p.pending());
 assert(p.request(PortalRequest::Moving,0xfffffff0u)); auto third=p.pending();
 assert(p.approve(third)); assert(p.take(16)==PortalRequest::Moving);
 assert(p.request(PortalRequest::Help,500)); auto fourth=p.pending(); p.clear();
 assert(!p.approve(fourth)); assert(!p.request(PortalRequest::Page,600));
}
