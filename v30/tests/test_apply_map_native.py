import importlib.util
from pathlib import Path
import tempfile
import unittest

HERE=Path(__file__).parent
SCRIPT=HERE.parent/"apply_map_native_v30.py"
if not SCRIPT.exists(): SCRIPT=HERE/"apply_map_native_v30.py"

# The pinned native anchors are retained verbatim. Run the actual patcher
# against a checkout-shaped directory, including a partial-modification case.
FIXTURE='''#include "TouchSleep.h"
static uint8_t bestAvailableZoom(double lat, double lon) {
  return 0;
}
static void renderMapTiles() {
  if (!s_map_canvas) return;
  legacyTiles();
}
// ----- Markers -----
static bool mapZoomReachable(uint8_t z) {
  return false;
}
static void mapAutoFollowTick() {
}
  // (The "Loading map…" hint + frame flush moved to the top of this function —
  renderMapTiles();
  if (active_tab == MAP_TAB_INDEX) { mapAutoFollowTick(); refreshMapInfoLabel(); }
  // Coords → bottom-left corner; count/status → bottom-right corner.
'''

class NativePatch(unittest.TestCase):
    def test_patch_is_atomic_idempotent_and_only_changes_v30_checkout(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            ui=root/"src/ui-touch/UITask.cpp";ui.parent.mkdir(parents=True)
            ui.write_text(FIXTURE)
            spec=importlib.util.spec_from_file_location("native", SCRIPT)
            module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
            module.apply(root)
            result=ui.read_text()
            self.assertNotEqual(result,FIXTURE)
            module.apply(root)
            self.assertEqual(ui.read_text(),result)
            # Unrecognised baseline must not partially write anything.
            incomplete=FIXTURE.replace("static bool mapZoomReachable(uint8_t z) {","broken anchor")
            ui.write_text(incomplete)
            with self.assertRaises(ValueError):module.apply(root)
            self.assertEqual(ui.read_text(),incomplete)

if __name__=="__main__":unittest.main()
