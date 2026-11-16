import json
import struct
import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "maps"))
from build_nl import BuildError, PROVINCES, build_pack


class MapBuildTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.profile = json.loads((Path(__file__).parents[1] / "maps" / "nl_profile.json").read_text())
        self.features = []
        kinds = ["boundary", "motorway", "trunk", "primary", "secondary", "tertiary", "local", "water", "place"]
        for index, province in enumerate(PROVINCES):
            self.features.append({
                "id": f"way/{index}",
                "kind": kinds[index % len(kinds)],
                "province": province,
                "name": "N225" if index % 2 else "Dorp",
                "zoom_min": 8,
                "data_date": "2026-11-16",
                "geometry": [[4.0 + index / 100, 51.0 + index / 100],
                             [4.01 + index / 100, 51.01 + index / 100]],
            })
        self.source = self.root / "source.jsonl"
        self.source.write_text("\n".join(json.dumps(x, sort_keys=True) for x in self.features) + "\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_deterministic_pack_and_report(self):
        first = self.root / "one.meshmap"
        second = self.root / "two.meshmap"
        report1 = build_pack(self.source, self.profile, first)
        report2 = build_pack(self.source, self.profile, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        self.assertEqual(report1.pack_sha256, report2.pack_sha256)
        self.assertEqual(report1.provinces, PROVINCES)
        self.assertEqual(report1.source_features, 12)
        self.assertEqual(report1.packed_features, 12)
        self.assertEqual(report1.names, 2)
        self.assertLessEqual(report1.bytes_written, self.profile["pack_budget_bytes"])
        self.assertEqual(json.loads((first.with_suffix(".meshmap.report.json")).read_text())["data_date"], "2026-11-16")

    def test_missing_province_is_hard_failure(self):
        lines = self.features[:-1]
        self.source.write_text("\n".join(json.dumps(x) for x in lines) + "\n")
        with self.assertRaisesRegex(BuildError, "missing province coverage"):
            build_pack(self.source, self.profile, self.root / "bad.meshmap")

    def test_invalid_coordinate_is_hard_failure(self):
        bad = dict(self.features[0])
        bad["geometry"] = [[181.0, 52.0]]
        self.source.write_text("\n".join(json.dumps(x) for x in [bad] + self.features[1:]) + "\n")
        with self.assertRaisesRegex(BuildError, "invalid coordinate"):
            build_pack(self.source, self.profile, self.root / "bad.meshmap")

    def test_duplicate_names_are_dictionary_indexed(self):
        report = build_pack(self.source, self.profile, self.root / "names.meshmap")
        self.assertEqual(report.names, 2)

    def test_impossible_budget_fails_instead_of_fake_truncation(self):
        tiny = dict(self.profile)
        tiny["pack_budget_bytes"] = 100
        with self.assertRaisesRegex(BuildError, "exceeds budget|budget would remove"):
            build_pack(self.source, tiny, self.root / "tiny.meshmap")

    def test_profile_budget_hard_cap(self):
        too_large = dict(self.profile)
        too_large["pack_budget_bytes"] = 3670017
        with self.assertRaisesRegex(BuildError, "range"):
            build_pack(self.source, too_large, self.root / "large-budget.meshmap")

    def test_profile_must_define_exactly_all_provinces(self):
        bad = dict(self.profile)
        bad["provinces"] = list(PROVINCES[:-1])
        with self.assertRaisesRegex(BuildError, "exactly the 12"):
            build_pack(self.source, bad, self.root / "bad-profile.meshmap")


if __name__ == "__main__":
    unittest.main()
