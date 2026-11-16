import struct
import tempfile
import unittest
from pathlib import Path
import importlib.util
SPEC=importlib.util.spec_from_file_location("journal",Path(__file__).parents[1]/"overlay"/"src"/"v30_journal_storage.py")
M=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(M)
class JournalTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(); self.path=Path(self.tmp.name)/"outbox.journal"
    def tearDown(self): self.tmp.cleanup()
    def test_missing_is_distinct(self):
        with self.assertRaises(M.JournalMissing): M.V30JournalStorage(self.path).read()
    def test_append_flush_reopen_and_order(self):
        j=M.V30JournalStorage(self.path,max_bytes=1024); j.append(1,b"one"); j.append(2,b"two"); j.flush()
        self.assertEqual(M.V30JournalStorage(self.path,max_bytes=1024).open().read(),[(1,b"one"),(2,b"two")])
    def test_partial_tail_fails_closed(self):
        j=M.V30JournalStorage(self.path); j.append(1,b"valid")
        with self.path.open("ab") as f: f.write(b"VJ01\x01\x00")
        with self.assertRaises(M.JournalPartial): j.read()
    def test_corrupt_payload_is_not_accepted(self):
        j=M.V30JournalStorage(self.path); j.append(1,b"valid")
        raw=bytearray(self.path.read_bytes()); raw[-5]^=1; self.path.write_bytes(raw)
        with self.assertRaises(M.JournalCorrupt): j.read()
    def test_capacity_is_bounded(self):
        j=M.V30JournalStorage(self.path,max_bytes=40); j.append(1,b"1234")
        with self.assertRaises(M.JournalFull): j.append(2,b"x"*40)
    def test_no_implicit_format_or_truncate(self):
        self.path.write_bytes(b"legacy")
        with self.assertRaises(M.JournalCorrupt): M.V30JournalStorage(self.path).open()
        self.assertEqual(self.path.read_bytes(),b"legacy")
if __name__=="__main__": unittest.main()
