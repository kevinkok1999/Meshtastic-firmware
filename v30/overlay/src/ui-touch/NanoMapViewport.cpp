#include "NanoMapViewport.h"
#include <algorithm>
#include <cmath>

namespace meshtastic::nanomap {
namespace {
constexpr double kMercatorLat=85.05112878;
constexpr double kPi=3.14159265358979323846;
double merc(double lat) { return std::log(std::tan(kPi/4+lat*kPi/360)); }
MapLayer layer(const std::string& kind) {
  if(kind=="boundary")return MapLayer::Boundary;
  if(kind=="place")return MapLayer::Place;
  if(kind=="motorway")return MapLayer::Motorway;
  if(kind=="trunk")return MapLayer::Trunk;
  if(kind=="primary")return MapLayer::Primary;
  if(kind=="secondary")return MapLayer::Secondary;
  if(kind=="water")return MapLayer::Water;
  if(kind=="tertiary")return MapLayer::Tertiary;
  return MapLayer::Local;
}
bool important(const PixelPrimitive& a,const PixelPrimitive& b){return a.layer<b.layer;}
bool valid(const ViewportSpec& s) {
  return std::isfinite(s.minLon)&&std::isfinite(s.minLat)&&std::isfinite(s.maxLon)&&std::isfinite(s.maxLat)&&
      s.minLon>=-180&&s.maxLon<=180&&s.minLon<s.maxLon&&s.minLat>=-kMercatorLat&&s.maxLat<=kMercatorLat&&
      s.minLat<s.maxLat&&s.width>0&&s.height>0&&s.width<=320&&s.height<=320&&
      size_t(s.width)*s.height<=320u*240u&&s.zoom>=3&&s.zoom<=19;
}
uint16_t pixel(double fraction,uint16_t dimension) {
  if(fraction<=0)return 0;
  if(fraction>=1)return dimension-1;
  return static_cast<uint16_t>(std::lround(fraction*(dimension-1)));
}
}
ViewportStatus stageViewport(const NanoMapPack& pack,const ViewportSpec& s, PixelPrimitive* out,
                             size_t capacity,size_t& count,MapProgress progress) {
  count=0;
  if(!out||!capacity||capacity>kMaxViewportPrimitives||!valid(s))return ViewportStatus::InvalidViewport;
  if(pack.status()!=PackStatus::Ok)return ViewportStatus::CorruptPack;
  const double top=merc(s.maxLat), bottom=merc(s.minLat);
  if(!std::isfinite(top)||!std::isfinite(bottom)||top<=bottom)return ViewportStatus::InvalidViewport;
  const double width=s.maxLon-s.minLon;
  size_t staged=0;bool truncated=false,cancelled=false;
  struct ProgressState{MapProgress p;bool* cancelled;} ps{progress,&cancelled};
  const MapProgress tracked{[](void* ptr)->bool {auto& x=*static_cast<ProgressState*>(ptr);if(!x.p.proceed()){*x.cancelled=true;return false;}return true;},&ps};
  const auto px=[&](int32_t lon){return pixel((lon/100000.0-s.minLon)/width,s.width);};
  const auto py=[&](int32_t lat){return pixel((top-merc(lat/100000.0))/(top-bottom),s.height);};
  const bool ok=pack.forEachFeature(s.minLon,s.minLat,s.maxLon,s.maxLat,s.zoom,[&](const FeatureRecord& f){
    const auto a=f.geometry.front(),b=f.geometry.back();
    PixelPrimitive p{px(a.first),py(a.second),px(b.first),py(b.second),f.nameIndex,layer(f.kind),f.geometry.size()==1};
    if(staged<capacity){out[staged++]=p;std::push_heap(out,out+staged,important);}
    else {truncated=true;if(important(p,out[0])){std::pop_heap(out,out+staged,important);out[staged-1]=p;std::push_heap(out,out+staged,important);}}
    return true;
  },tracked);
  if(!ok)return cancelled?ViewportStatus::Cancelled:ViewportStatus::CorruptPack;
  // Low-priority lines first; important boundary/place markers can draw last.
  std::sort(out,out+staged,[](const PixelPrimitive& a,const PixelPrimitive& b){return a.layer>b.layer;});
  count=staged;
  return truncated?ViewportStatus::Truncated:ViewportStatus::Ready;
}
}
