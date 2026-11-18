// Included at the native map renderer only by the explicit V30 development
// switch. The compile-time pin must be obtained from a separately verified
// signed map manifest; never from the image supplied by a user.
// No pin is shipped. This is not a signature verifier or release authorization.
namespace v30_native_ui {
using namespace meshtastic::nanomap;
#if !defined(MESH_OFFGRIDNL_V30_DEV_NANOMAP_SHA256)
#define MESH_OFFGRIDNL_V30_DEV_NANOMAP_SHA256 ""
#endif
static constexpr char kV30NativePackPin[]=MESH_OFFGRIDNL_V30_DEV_NANOMAP_SHA256;
static bool s_v30_map_active=false,s_v30_map_limited=false,s_v30_map_pending=false,s_v30_map_blocked=false;
static const char* s_v30_map_error=nullptr;
static uint32_t s_v30_map_generation=0;
static uint32_t s_v30_map_draw_ms=0;
static ViewportSpec s_v30_map_view{};
static bool s_v30_map_night=false;
static lv_obj_t* s_v30_map_img=nullptr;
static uint16_t* s_v30_map_pixels=nullptr;
static lv_img_dsc_t s_v30_map_dsc{};

static bool v30NanoMapRaw() {
  static const bool raw=classifyInternalTiles()==TileStorage::NanoMap;
  return raw;
}
static bool v30NanoMapCandidate() {
  return s_map_style==0 && !s_v30_map_blocked && validTrustPin(kV30NativePackPin) && v30NanoMapRaw();
}
static bool v30NanoMapAuthorize(const uint8_t* pack,size_t size,MapProgress p,void*) {
  return verifyTrustPin(pack,size,kV30NativePackPin,p);
}
static bool v30NanoMapBusy() {
  // Let a completed frame reach the display before continuous GPS movement
  // can clear it and request the next generation.
  return s_v30_map_active&&(s_v30_map_pending||(uint32_t)(millis()-s_v30_map_draw_ms)<1000);
}
static void v30NanoMapClearImage() {
  if(s_v30_map_img&&lv_obj_is_valid(s_v30_map_img))lv_obj_del(s_v30_map_img);
  s_v30_map_img=nullptr;
  lv_img_cache_invalidate_src(&s_v30_map_dsc);
  if(s_v30_map_pixels)heap_caps_free(s_v30_map_pixels);
  s_v30_map_pixels=nullptr;
  s_v30_map_dsc.data=nullptr;
}
static void v30NanoMapCancel() {
  if(s_v30_map_active)NanoMapJob::instance().cancel();
  s_v30_map_active=s_v30_map_limited=s_v30_map_pending=false;
  s_v30_map_generation=0;
  v30NanoMapClearImage();
}
static void v30NanoMapStatus(const char* message) {
  if(!s_map_status_lbl)return;
  lv_label_set_text(s_map_status_lbl,message);
  lv_obj_clear_flag(s_map_status_lbl,LV_OBJ_FLAG_HIDDEN);
}
static void v30NanoMapFallbackHint() {
  if(s_map_style==0&&!s_map_has_pack&&v30NanoMapRaw())
    v30NanoMapStatus(s_v30_map_error?s_v30_map_error:TR("Offline map not trusted.\nUse an approved map package."));
}
static bool v30SameView(const ViewportSpec& a,const ViewportSpec& b) {
  return a.minLon==b.minLon&&a.minLat==b.minLat&&a.maxLon==b.maxLon&&a.maxLat==b.maxLat&&
     a.width==b.width&&a.height==b.height&&a.zoom==b.zoom;
}
static bool v30NanoMapRequest() {
  if(!s_map_canvas||!v30NanoMapCandidate()) {
    if(s_v30_map_active)v30NanoMapCancel();
    return false;
  }
  if(s_map_center_lat==0.0&&s_map_center_lon==0.0) {
    // Browsable default, never written into real GPS/profile location.
    s_map_center_lat=52.2;s_map_center_lon=5.4;s_map_zoom=6;s_map_view_inited=true;
  }
  ViewportSpec view{};
  if(!centeredViewport(s_map_center_lat,s_map_center_lon,s_map_zoom,k_map_canvas_w,k_map_canvas_h,view)) {
    v30NanoMapCancel();
    s_v30_map_error=TR("Map view outside supported bounds.");
    return false;
  }
  if(s_v30_map_active&&s_v30_map_night==s_map_night&&v30SameView(view,s_v30_map_view))return true;
  if(!NanoMapJob::instance().begin(v30NanoMapAuthorize,nullptr)) {
    v30NanoMapCancel();
    s_v30_map_blocked=true;s_v30_map_error=TR("Offline map worker unavailable.");
    return false;
  }
  v30NanoMapClearImage();
  freeMapTiles();
  if(s_map_pan_layer)lv_obj_set_pos(s_map_pan_layer,0,0);
  mapSetHasPack(false);
  s_map_last_missing=0;
  s_v30_map_view=view;s_v30_map_night=s_map_night;
  s_v30_map_generation=NanoMapJob::instance().request(view);
  if(!s_v30_map_generation) {
    v30NanoMapCancel();s_v30_map_blocked=true;s_v30_map_error=TR("Offline map request failed.");return false;
  }
  s_v30_map_active=s_v30_map_pending=true;s_v30_map_limited=false;
  v30NanoMapStatus(TR("Checking offline map..."));
  return true;
}
static bool v30NanoMapDraw(const MapFrame& frame) {
  static_assert(sizeof(lv_color_t)==sizeof(uint16_t),"V30 native map requires RGB565");
  if(!v30SameView(frame.viewport,s_v30_map_view)||!s_map_pan_layer)return false;
  const size_t count=size_t(frame.viewport.width)*frame.viewport.height;
  if(count>320u*240u||!count)return false;
  v30NanoMapClearImage();
  s_v30_map_pixels=static_cast<uint16_t*>(heap_caps_malloc(count*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(!s_v30_map_pixels)return false; // never consume scarce internal heap
  RasterPalette palette{};
  const auto color=[](uint32_t rgb)->uint16_t {
    const uint16_t p=lv_color_hex(rgb).full;return s_map_night?uint16_t(~p):p;
  };
  palette.background=color(0xf2f0e7);
  const uint32_t layers[]={0x726652,0x242421,0xd47931,0xd2a05e,0xc7ac52,0x93876f,0x5da1bb,0xaba397,0xb8b5ae};
  for(size_t i=0;i<9;++i)palette.layer[i]=color(layers[i]);
  if(!rasterizeViewport(frame.primitives,frame.count,frame.viewport.width,frame.viewport.height,palette,s_v30_map_pixels,count)) {
    v30NanoMapClearImage();return false;
  }
  memset(&s_v30_map_dsc,0,sizeof s_v30_map_dsc);
  s_v30_map_dsc.header.cf=LV_IMG_CF_TRUE_COLOR;
  s_v30_map_dsc.header.w=frame.viewport.width;s_v30_map_dsc.header.h=frame.viewport.height;
  s_v30_map_dsc.data=reinterpret_cast<const uint8_t*>(s_v30_map_pixels);
  s_v30_map_dsc.data_size=count*sizeof(uint16_t);
  s_v30_map_img=lv_img_create(s_map_pan_layer);
  if(!s_v30_map_img){v30NanoMapClearImage();return false;}
  lv_img_set_src(s_v30_map_img,&s_v30_map_dsc);
  lv_obj_set_pos(s_v30_map_img,0,0);
  lv_obj_move_background(s_v30_map_img); // preserve existing contact/route overlays
  return true;
}
static void v30NanoMapPoll(bool active,bool dragging) {
  if(!active) {
    if(s_v30_map_active)v30NanoMapCancel();
    s_v30_map_blocked=false;s_v30_map_error=nullptr;
    return;
  }
  if(dragging||!s_v30_map_pending)return;
  MapJobResult result{};
  if(!NanoMapJob::instance().take(result))return;
  if(result.generation!=s_v30_map_generation){NanoMapJob::release(result);return;}
  s_v30_map_pending=false;
  const bool ready=(result.status==MapJobStatus::Ready||result.status==MapJobStatus::Truncated)&&result.frame;
  const bool drawn=ready&&v30NanoMapDraw(*result.frame);
  if(drawn) {
    s_v30_map_draw_ms=millis();
    s_v30_map_limited=result.status==MapJobStatus::Truncated;
    mapSetHasPack(true);
    if(result.frame->count) {
      if(s_map_status_lbl)lv_obj_add_flag(s_map_status_lbl,LV_OBJ_FLAG_HIDDEN);
    } else v30NanoMapStatus(TR("No offline data for this area."));
    renderMapMarkers();
  } else {
    s_v30_map_error=result.status==MapJobStatus::Untrusted?TR("Offline map authentication failed."):
       result.status==MapJobStatus::NoMemory?TR("Insufficient map memory."):TR("Offline map unavailable.");
    s_v30_map_blocked=true;
    v30NanoMapCancel();
    renderMapTiles();renderMapMarkers(); // existing SD/raster fallback if available
  }
  NanoMapJob::release(result);
}
}
