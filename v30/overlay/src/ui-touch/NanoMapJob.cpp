#include "NanoMapJob.h"
#if defined(ESP32)
#include "NanoMapFlash.h"
#include <new>
#include <esp_heap_caps.h>

namespace meshtastic::nanomap {
struct NanoMapJob::Work {NanoMapJob* owner;Request request;MapJobResult result;};
NanoMapJob& NanoMapJob::instance(){static NanoMapJob job;return job;}
bool NanoMapJob::begin(MapAuthorize authorize,void* context){
  if(!authorize)return false;
  if(task_)return authorize==authorize_&&context==context_;
  pending_=xQueueCreate(1,sizeof(Request));ready_=xQueueCreate(1,sizeof(MapJobResult));
  if(!pending_||!ready_){if(pending_)vQueueDelete(pending_);if(ready_)vQueueDelete(ready_);pending_=ready_=nullptr;return false;}
  authorize_=authorize;context_=context;
  if(xTaskCreatePinnedToCore(task,"NanoMap",8192,this,1,&task_,0)!=pdPASS){
    vQueueDelete(pending_);vQueueDelete(ready_);pending_=ready_=nullptr;task_=nullptr;return false;
  }
  return true;
}
uint32_t NanoMapJob::request(const ViewportSpec& viewport){
  if(!pending_||!task_)return 0;
  const uint32_t id=generation_.next();
  Request request{viewport,id};
  if(xQueueOverwrite(pending_,&request)!=pdPASS){generation_.next();return 0;}
  return id;
}
void NanoMapJob::release(MapJobResult& result){delete result.frame;result.frame=nullptr;result.generation=0;}
void NanoMapJob::cancel(){
  generation_.next();MapJobResult old;
  if(ready_&&xQueueReceive(ready_,&old,0)==pdPASS)release(old);
}
bool NanoMapJob::take(MapJobResult& result){
  if(!ready_||result.frame)return false; // caller must release its previous frame
  if(xQueueReceive(ready_,&result,0)!=pdPASS)return false;
  if(!generation_.isCurrent(result.generation)){release(result);return false;}
  return true;
}
void NanoMapJob::publish(MapJobResult result){
  if(!generation_.isCurrent(result.generation)){release(result);return;}
  MapJobResult old;
  if(xQueueReceive(ready_,&old,0)==pdPASS)release(old);
  if(xQueueOverwrite(ready_,&result)!=pdPASS)release(result);
}
bool NanoMapJob::progress(void* ptr){
  auto& work=*static_cast<Work*>(ptr);
  if(!work.owner->generation_.isCurrent(work.request.generation))return false;
  vTaskDelay(1); // don't starve the Wi-Fi/idle watchdog or radio owner
  return work.owner->generation_.isCurrent(work.request.generation);
}
bool NanoMapJob::visit(const uint8_t* data,size_t size,void* ptr){
  auto& work=*static_cast<Work*>(ptr);
  const MapProgress control{progress,&work};
  if(!work.owner->authorize_(data,size,control,work.owner->context_)){
    work.result.status=MapJobStatus::Untrusted;return false;
  }
  if(!control.proceed())return false;
  NanoMapPack pack;
  if(pack.open(data,size,control)!=PackStatus::Ok)return false;
  auto* frame=work.result.frame;
  const auto status=stageViewport(pack,work.request.viewport,frame->primitives,kMaxViewportPrimitives,frame->count,control);
  if(status==ViewportStatus::Ready||status==ViewportStatus::Truncated){
    work.result.status=status==ViewportStatus::Ready?MapJobStatus::Ready:MapJobStatus::Truncated;
    return true;
  }
  if(status==ViewportStatus::InvalidViewport)work.result.status=MapJobStatus::InvalidViewport;
  return false;
}
void NanoMapJob::task(void* self){static_cast<NanoMapJob*>(self)->run();}
void NanoMapJob::run(){
  for(;;){
    Request request;
    if(xQueueReceive(pending_,&request,portMAX_DELAY)!=pdPASS)continue;
    if(!generation_.isCurrent(request.generation))continue;
    // Conservative preflight only; actual peak heap/PSRAM and RF timing
    // must still be measured on the target before enabling the product.
    MapJobResult result;result.generation=request.generation;
    if(heap_caps_get_free_size(MALLOC_CAP_8BIT)<256u*1024u ||
       heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<64u*1024u){result.status=MapJobStatus::NoMemory;publish(result);continue;}
    result.frame=new(std::nothrow)MapFrame;
    if(!result.frame){result.status=MapJobStatus::NoMemory;publish(result);continue;}
    result.frame->viewport=request.viewport;
    Work work{this,request,result};
    if(!inspectInternalTiles(visit,&work,{progress,&work})){
      delete work.result.frame;work.result.frame=nullptr;
    }
    publish(work.result);
  }
}
}
#endif
