#include "NanoMapViewport.h"
#include "NanoMapGeneration.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include <zlib.h>
using namespace meshtastic::nanomap;

static bool cancel(void* ptr){ ++*static_cast<unsigned*>(ptr);return false; }
int main(int argc,char** argv){
  MapGeneration generation;
  const auto first=generation.next();assert(first&&generation.isCurrent(first));
  const auto second=generation.next();assert(second&&second!=first&&!generation.isCurrent(first)&&generation.isCurrent(second));
  generation.next();assert(!generation.isCurrent(second));
  assert(argc==2);
  std::ifstream f(argv[1],std::ios::binary);assert(f.good());
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  NanoMapPack pack;assert(pack.open(bytes.data(),bytes.size())==PackStatus::Ok);
  ViewportSpec spec{4.0,51.0,4.12,51.12,320,240,8};
  PixelPrimitive lines[16]{};size_t count=987;
  assert(stageViewport(pack,spec,lines,16,count)==ViewportStatus::Ready&&count==12);
  bool foundCorner=false;
  for(size_t i=0;i<count;++i){
    const auto& p=lines[i];assert(p.x0<320&&p.x1<320&&p.y0<240&&p.y1<240);
    if(p.layer==MapLayer::Boundary&&p.x0==0){foundCorner=true;assert(p.y0==239&&p.x1>0&&p.y1<239);}
  }
  assert(foundCorner);
  assert(stageViewport(pack,spec,lines,1,count)==ViewportStatus::Truncated&&count==1);
  assert(lines[0].layer==MapLayer::Boundary); // highest priority retained
  spec.zoom=7;assert(stageViewport(pack,spec,lines,16,count)==ViewportStatus::Ready&&count==0);
  spec.zoom=8;unsigned calls=0;
  assert(stageViewport(pack,spec,lines,16,count,{cancel,&calls})==ViewportStatus::Cancelled&&count==0&&calls==1);
  spec.width=1000;assert(stageViewport(pack,spec,lines,16,count)==ViewportStatus::InvalidViewport&&count==0);
  spec.width=320;assert(stageViewport(pack,spec,lines,0,count)==ViewportStatus::InvalidViewport&&count==0);
  // A hostile image may carry a valid outer CRC but a corrupt zlib Adler
  // footer. No staged geometry becomes visible before the stream validates.
  auto corrupt=bytes;corrupt[corrupt.size()-5]^=1;
  const uint32_t outer=crc32(0,corrupt.data(),corrupt.size()-4);
  for(unsigned i=0;i<4;++i)corrupt[corrupt.size()-4+i]=uint8_t(outer>>(8*i));
  NanoMapPack bad;assert(bad.open(corrupt.data(),corrupt.size())==PackStatus::Ok);
  count=99;assert(stageViewport(bad,spec,lines,16,count)==ViewportStatus::CorruptPack&&count==0);
  std::puts("NanoMap viewport staging, truncation, clipping and cancellation PASS");
}
