import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("ingest_osm", Path(__file__).parents[1] / "maps" / "ingest_osm.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

class OSMIngestionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.source = self.root / "sample.osm"
        self.source.write_text((Path(__file__).parents[1] / "maps" / "fixtures" / "osm_sample.osm").read_text())
        self.boundaries = Path(__file__).parents[1] / "maps" / "fixtures" / "provinces.geojson"
    def tearDown(self):
        self.tmp.cleanup()
    def test_xml_normalizes_contract_and_provenance(self):
        out = self.root / "normalized.jsonl"
        result = MODULE.ingest(self.source, out, "https://download.geofabrik.de/europe/netherlands.html", "ODbL-1.0", "2026-11-16", self.boundaries, "2026-11-17T00:00:00+00:00")
        rows = [json.loads(line) for line in out.read_text().splitlines()]
        self.assertEqual(len(rows), 12)
        self.assertEqual({row["province"] for row in rows}, set(MODULE.PROVINCES))
        self.assertTrue(all(row["data_date"] == "2026-11-16" for row in rows))
        self.assertEqual(result["skipped_features"], 0)
        self.assertEqual(rows[0]["id"], "node/20")
        self.assertEqual(result["license"], "ODbL-1.0")
        self.assertEqual(result["network_access"], False)
        self.assertEqual(result["source_sha256"], hashlib.sha256(self.source.read_bytes()).hexdigest())
        sidecar = json.loads(Path(str(out) + ".provenance.json").read_text())
        self.assertEqual(sidecar["normalized_sha256"], hashlib.sha256(out.read_bytes()).hexdigest())
    def test_output_is_deterministic(self):
        one, two = self.root / "one.jsonl", self.root / "two.jsonl"
        args = ("https://example.invalid/osm", "ODbL-1.0", "2026-11-16", "2026-11-17T00:00:00+00:00")
        MODULE.ingest(self.source, one, *args, boundaries=self.boundaries); MODULE.ingest(self.source, two, *args, boundaries=self.boundaries)
        self.assertEqual(one.read_bytes(), two.read_bytes())
        self.assertEqual((one.with_suffix(one.suffix + ".provenance.json")).read_bytes(),
                         (two.with_suffix(two.suffix + ".provenance.json")).read_bytes())
    def test_rejects_empty_or_unsupported_source(self):
        empty = self.root / "empty.osm"; empty.write_text("<osm version='0.6'></osm>")
        with self.assertRaises(MODULE.IngestError):
            MODULE.ingest(empty, self.root / "bad.jsonl", "x", "ODbL-1.0", "2026-11-16", self.boundaries)
if __name__ == "__main__":
    unittest.main()
