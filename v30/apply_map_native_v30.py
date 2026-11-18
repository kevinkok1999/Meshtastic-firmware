"""Opt-in native presentation integration for the immutable V30 checkout."""
from pathlib import Path
import sys

GATE="MESH_OFFGRIDNL_V30_NATIVE_MAP_ACTIVE"
def guarded(body):
    return f"#if defined({GATE})\n{body}\n#endif"

def patch_ui(text):
    anchors=[
      ('#include "TouchSleep.h"\n',
       '#include "TouchSleep.h"\n'+'''#if defined(ESP32) && defined(MESH_OFFGRIDNL_V30) && defined(MESH_OFFGRIDNL_V30_NANOMAP_DEV) && MESH_OFFGRIDNL_V30_NANOMAP_DEV
#define MESH_OFFGRIDNL_V30_NATIVE_MAP_ACTIVE 1
#include "NanoMapJob.h"
#include "NanoMapFlash.h"
#include "NanoMapPresentation.h"
#include "NanoMapTrustPin.h"
#endif
'''),
      ('static uint8_t bestAvailableZoom(double lat, double lon) {',
       guarded('#include "NanoMapNative.inc"')+'\nstatic uint8_t bestAvailableZoom(double lat, double lon) {\n'+guarded('  if (v30_native_ui::v30NanoMapCandidate()) return 0;')),
      ('static void renderMapTiles() {',
       'static void renderMapTilesLegacy() {'),
      ('// ----- Markers -----',
       '''static void renderMapTiles() {
'''+guarded('  if (v30_native_ui::v30NanoMapRequest()) return;')+'''
  renderMapTilesLegacy();
'''+guarded('  v30_native_ui::v30NanoMapFallbackHint();')+'''
}

// ----- Markers -----'''),
      ('static bool mapZoomReachable(uint8_t z) {',
       'static bool mapZoomReachable(uint8_t z) {\n'+guarded('  if (v30_native_ui::v30NanoMapCandidate()) return z >= 3 && z <= 19;')),
      ('static void mapAutoFollowTick() {',
       'static void mapAutoFollowTick() {\n'+guarded('  if (v30_native_ui::v30NanoMapBusy() || (v30_native_ui::v30NanoMapCandidate() && s_map_panning)) return;')),
      ('  if (active_tab == MAP_TAB_INDEX) { mapAutoFollowTick(); refreshMapInfoLabel(); }',
       guarded('  v30_native_ui::v30NanoMapPoll(active_tab == MAP_TAB_INDEX, s_map_panning);')+'\n  if (active_tab == MAP_TAB_INDEX) { mapAutoFollowTick(); refreshMapInfoLabel(); }'),
      ('  // Coords → bottom-left corner; count/status → bottom-right corner.',
       guarded('  if (v30_native_ui::s_v30_map_active && v30_native_ui::s_v30_map_limited) snprintf(tail,sizeof tail,"Map limited / %d nodes",with_gps);')+'\n  // Coords → bottom-left corner; count/status → bottom-right corner.'),
    ]
    if GATE in text:
        if all(text.count(new)==1 for _,new in anchors):
            return text
        raise ValueError("partial V30 native map patch")
    for old,new in anchors:
        if text.count(old)!=1:
            raise ValueError(f"native map anchor count {text.count(old)}: {old[:60]}")
        text=text.replace(old,new,1)
    return text

def apply(root):
    ui=Path(root)/"src/ui-touch/UITask.cpp"
    original=ui.read_text(encoding="utf-8")
    updated=patch_ui(original)
    if updated!=original: ui.write_text(updated,encoding="utf-8")

if __name__=="__main__":
    if len(sys.argv)!=2: raise SystemExit("usage: apply_map_native_v30.py <V30 checkout>")
    apply(sys.argv[1])
