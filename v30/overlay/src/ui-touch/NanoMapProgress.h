#pragma once
namespace meshtastic::nanomap {
// A background consumer can yield or cancel at bounded scan checkpoints.
struct MapProgress {
  using Step=bool(*)(void*);
  MapProgress(Step fn=nullptr,void* ctx=nullptr):step(fn),context(ctx){}
  Step step;
  void* context;
  bool proceed() const { return !step || step(context); }
};
}
