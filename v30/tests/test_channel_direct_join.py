import json,pathlib,sys,unittest,hmac,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parents[1]))
from channel_direct_join import JoinInputError,action_for_native_channel_path,normalize_channel_key,normalize_invite_locator,parse_join_input
class DirectJoinContractTests(unittest.TestCase):
 KEY="A1"*16
 def test_key_is_case_insensitive_and_uses_native_capability(self):
  a=parse_join_input(self.KEY,label="Alpha"); b=parse_join_input("  "+self.KEY.lower()+"  ",label="Different")
  self.assertEqual(a.key_bytes,b.key_bytes); self.assertEqual(a.channel_capability,b.channel_capability)
  self.assertEqual(a.channel_capability,hmac.new(bytes.fromhex(self.KEY),b"MOG27-CH-INBOX",hashlib.sha256).digest()[:16])
 def test_wrong_key_rejected(self):
  for v in ("A1"*15,"A1"*17,"not-a-passphrase","A1"*15+"Z0"):
   with self.assertRaises(JoinInputError): normalize_channel_key(v)
 def test_legacy_invite_path(self):
  a=parse_join_input(" abcd-2345 ",label="Existing"); self.assertEqual(a.kind,"invite"); self.assertEqual(a.invite_locator,"ABCD-2345")
  self.assertEqual(normalize_invite_locator("abcd 2345"),"ABCD-2345")
  with self.assertRaises(JoinInputError): normalize_invite_locator("ABCD-1234")
 def test_direct_key_is_local_only_and_redacted(self):
  a=parse_join_input(self.KEY)
  with self.assertRaises(JoinInputError): a.relay_payload()
  self.assertNotIn(self.KEY.lower(),repr(a)); self.assertNotIn(self.KEY.lower(),repr(action_for_native_channel_path(a)))
 def test_native_path_no_second_history(self):
  n=action_for_native_channel_path(parse_join_input(self.KEY)); self.assertEqual(n.path,"native-channel-lookup"); self.assertEqual(n.key_bytes,bytes.fromhex(self.KEY))
if __name__=="__main__": unittest.main()
