#include "NanoMapPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
namespace meshtastic::nanomap {
namespace {
constexpr double pi=3.14159265358979323846;
constexpr double limit=85.05112878;
bool dimensions(uint16_t w,uint16_t h) {
  return w>=2&&h>=2&&w<=320&&h<=320&&size_t(w)*h<=320u*240u;
}
}
bool centeredViewport(double lat,double lon,uint8_t zoom,uint16_t w,uint16_t h,ViewportSpec& out) {
  if(!dimensions(w,h)||zoom<3||zoom>19||!std::isfinite(lat)||!std::isfinite(lon)||lat<-limit||lat>limit||lon<-180||lon>180)return false;
  const double world=256.0*(1u<<zoom);
  const double centerX=(lon+180.0)*world/360.0;
  const double centerY=(0.5-std::asinh(std::tan(lat*pi/180.0))/(2*pi))*world;
  const double left=centerX-w/2,right=left+w-1,top=centerY-h/2,bottom=top+h-1;
  if(left<0||right>world||top<0||bottom>world)return false; // no seam-distorted frame
  const auto latitude=[&](double y){return std::atan(std::sinh(pi*(1-2*y/world)))*180.0/pi;};
  out=ViewportSpec{left*360.0/world-180.0,latitude(bottom),right*360.0/world-180.0,latitude(top),w,h,zoom};
  return true;
}
bool rasterizeViewport(const PixelPrimitive* features,size_t count,uint16_t w,uint16_t h,const RasterPalette& palette,uint16_t* pixels,size_t capacity) {
  if(!dimensions(w,h)||!pixels||capacity<size_t(w)*h||count>kMaxViewportPrimitives||(count&&!features))return false;
  for(size_t i=0;i<count;++i) {
    const auto& p=features[i];
    if(p.x0>=w||p.x1>=w||p.y0>=h||p.y1>=h||static_cast<unsigned>(p.layer)>=9)return false;
  }
  std::fill(pixels,pixels+size_t(w)*h,palette.background);
  for(size_t i=0;i<count;++i) {
    const auto& p=features[i];
    const auto color=palette.layer[static_cast<unsigned>(p.layer)];
    const auto put=[&](int x,int y){if(x>=0&&x<w&&y>=0&&y<h)pixels[size_t(y)*w+x]=color;};
    if(p.point) {
      for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)put(int(p.x0)+dx,int(p.y0)+dy);
      continue;
    }
    int x=p.x0,y=p.y0,dx=std::abs(int(p.x1)-x),dy=-std::abs(int(p.y1)-y);
    const int sx=x<int(p.x1)?1:-1,sy=y<int(p.y1)?1:-1;
    int error=dx+dy;
    for(;;) {
      put(x,y);if(x==p.x1&&y==p.y1)break;
      const int e2=2*error;
      if(e2>=dy){error+=dy;x+=sx;}
      if(e2<=dx){error+=dx;y+=sy;}
    }
  }
  return true;
}
}
