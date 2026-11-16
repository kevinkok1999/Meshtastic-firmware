import pathlib, subprocess, sys, tempfile, unittest
ROOT = pathlib.Path(__file__).parents[1]
SCRIPT = ROOT / "apply_channel_v30.py"
class ChannelPatchTests(unittest.TestCase):
    def make_checkout(self, flag=True):
        root = pathlib.Path(tempfile.mkdtemp())
        block = "[env:LilyGo_TDeck_companion_radio_touch]\n" + ("  -D MESH_OFFGRIDNL_V30=1\n" if flag else "") + "[env:LilyGo_TDeck_Pro_companion_radio_touch]\n"
        (root / "platformio.ini").write_text(block, encoding="utf-8")
        (root / "src").mkdir()
        (root / "src/MyMesh.cpp").write_text("// native channel path\n", encoding="utf-8")
        return root
    def run_patch(self, root):
        return subprocess.run([sys.executable, str(SCRIPT), str(root)], text=True, capture_output=True, check=False)
    def test_pinned_v30_only_install_is_idempotent(self):
        root = self.make_checkout()
        first, second = self.run_patch(root), self.run_patch(root)
        self.assertEqual(first.returncode, 0, first.stderr); self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("parseKey", (root / "src/helpers/esp32/V30DirectChannelJoin.h").read_text())
        self.assertEqual((root / "src/MyMesh.cpp").read_text(), "// native channel path\n")
    def test_patch_rejects_unpinned_checkout(self):
        result = self.run_patch(self.make_checkout(flag=False))
        self.assertNotEqual(result.returncode, 0); self.assertIn("MESH_OFFGRIDNL_V30=1 required", result.stderr)
if __name__ == "__main__": unittest.main()
