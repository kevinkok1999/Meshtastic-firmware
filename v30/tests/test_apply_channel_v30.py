import pathlib, subprocess, sys, tempfile, unittest
ROOT=pathlib.Path(__file__).parents[1]; SCRIPT=ROOT/"apply_channel_v30.py"
class ChannelPatchTests(unittest.TestCase):
 def make_checkout(self,flag=True):
  root=pathlib.Path(tempfile.mkdtemp()); block="[env:LilyGo_TDeck_companion_radio_touch]\n"+("  -D MESH_OFFGRIDNL_V30=1\n" if flag else "")+"[env:LilyGo_TDeck_Pro_companion_radio_touch]\n"
  (root/"platformio.ini").write_text(block); (root/"src/ui-touch").mkdir(parents=True)
  (root/"src/MyMesh.cpp").write_text("// native channel path\n")
  (root/"src/ui-touch/UITask.cpp").write_text('#include "../MyMesh.h"\nstatic void joinPrivateChannelSubmitCb(lv_event_t* e) {\n  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;\n  kbMirrorSyncToReal();\n  if (!s_addch_secret_ta) return;\n#if defined(MESH_OFFGRIDNL_V28)\n  const char* code = lv_textarea_get_text(s_addch_secret_ta);\n  taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX"));\n  lv_textarea_set_max_length(s_addch_secret_ta, 10);\n')
  return root
 def run_patch(self,root): return subprocess.run([sys.executable,str(SCRIPT),str(root)],text=True,capture_output=True,check=False)
 def test_native_direct_join_patch_is_idempotent_and_gated(self):
  root=self.make_checkout(); a,b=self.run_patch(root),self.run_patch(root)
  self.assertEqual(a.returncode,0,a.stderr); self.assertEqual(b.returncode,0,b.stderr)
  header=(root/"src/helpers/esp32/V30DirectChannelJoin.h").read_text(); ui=(root/"src/ui-touch/UITask.cpp").read_text()
  self.assertIn("parseKey",header); self.assertIn("v30_direct_join::parseKey",ui); self.assertIn("32-hex key",ui); self.assertIn("lv_textarea_set_password_mode",ui); self.assertEqual((root/"src/MyMesh.cpp").read_text(),"// native channel path\n")
 def test_pinned_v28_source_contains_join_contract(self):
  pinned=ROOT.parent/"v28"/"apply_v28.py"; text=pinned.read_text(encoding="utf-8")
  self.assertIn("static void joinPrivateChannelSubmitCb",text)
  self.assertIn('lv_textarea_set_max_length(s_addch_secret_ta, 10);',text)
  self.assertIn('taSetPlaceholder(s_addch_secret_ta, TR("XXXX-XXXX"));',text)
  self.assertIn('v11_global_bridge.requestChannelJoin(code)',text)
 def test_patch_rejects_unpinned_checkout(self):
  r=self.run_patch(self.make_checkout(False)); self.assertNotEqual(r.returncode,0); self.assertIn("MESH_OFFGRIDNL_V30=1 required",r.stderr)
if __name__=="__main__": unittest.main()
