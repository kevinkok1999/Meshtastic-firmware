#pragma once
#include "NanoMapViewport.h"
#include "NanoMapGeneration.h"
#if defined(ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace meshtastic::nanomap {
struct MapFrame { ViewportSpec viewport; size_t count=0; PixelPrimitive primitives[kMaxViewportPrimitives]; };
enum class MapJobStatus:uint8_t { Ready,Truncated,InvalidViewport,InvalidImage,Untrusted,NoMemory };
struct MapJobResult { uint32_t generation=0; MapJobStatus status=MapJobStatus::InvalidImage; MapFrame* frame=nullptr; };
// Callbacks must be immutable, thread-safe and cooperative. The default product
// has NO trust anchor, so begin requires an explicitly supplied authorizer.
using MapAuthorize=bool(*)(const uint8_t*,size_t,MapProgress,void*);

// Boot-lifetime single worker. UI submits latest request, takes at most one
// result, and frees that result after rendering. Worker never calls LVGL or RF.
// No code invokes begin automatically; signed-auth + HIL gate stays locked.
class NanoMapJob {
 public:
  static NanoMapJob& instance();
  bool begin(MapAuthorize authorize,void* context);
  uint32_t request(const ViewportSpec& viewport);
  void cancel();
  bool take(MapJobResult& result);
  static void release(MapJobResult& result);
 private:
  struct Request {ViewportSpec viewport;uint32_t generation;};
  struct Work;
  static void task(void* self);
  static bool progress(void* work);
  static bool visit(const uint8_t* data,size_t size,void* work);
  void run();
  void publish(MapJobResult result);
  MapGeneration generation_;
  QueueHandle_t pending_=nullptr,ready_=nullptr;
  TaskHandle_t task_=nullptr;
  MapAuthorize authorize_=nullptr;void* context_=nullptr;
  NanoMapJob()=default;
};
}
#endif
