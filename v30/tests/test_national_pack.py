import hashlib
import json
import importlib.util
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / "maps"))
import national_pack as national

PROVINCES = ("Drenthe", "Flevoland", "Friesland", "Gelderland", "Groningen",
             "Limburg", "Noord-Brabant", "Noord-Holland", "Overijssel", "Utrecht",
             "Zeeland", "Zuid-Holland")


def boundaries():
    # Deliberately disjoint synthetic provinces: the mean of the first two
    # points below lands in a gap. These are not real geography.
    features = []
    for i, name in enumerate(PROVINCES):
        x = 3.5 + i * 0.25
        ring = [[x, 52], [x + 0.1, 52], [x + 0.1, 52.1],
                [x, 52.1], [x, 52]]
        features.append({"type": "Feature", "properties": {"naam": name},
                         "geometry": {"type": "Polygon", "coordinates": [ring]}})
    return {"type": "FeatureCollection", "features": features}


def candidate(id_, points, kind="primary", name="N12"):
    return {"id": id_, "kind": kind, "geometry": points, "name": name, "zoom_min": 8}


class NationalPackTests(unittest.TestCase):
    def test_assigns_cross_border_way_using_real_vertices_not_centroid(self):
        locator = national.ProvinceLocator(boundaries())
        p, count = locator.assign([[3.51, 52.01], [3.55, 52.02], [3.79, 52.05]])
        self.assertEqual(p, "Drenthe")
        self.assertTrue(count)
        self.assertIsNone(locator.assign([[4.0, 51.0]])[0])

    def test_fails_duplicate_and_missing_province(self):
        doc = boundaries()
        doc["features"][-1] = doc["features"][0]
        with self.assertRaises(ValueError):
            national.ProvinceLocator(doc)

    def test_selection_is_order_independent_bounded_and_counts_dropped(self):
        locator = national.ProvinceLocator(boundaries())
        candidates = [candidate(f"way/{i}", [[3.51, 52.01], [3.52, 52.02]])
                      for i in range(20)]
        candidates += [candidate("way/outside", [[4, 51], [4.1, 51.1]])]
        one, stats = national.select_candidates(iter(candidates), locator, per_cell=2)
        two, _ = national.select_candidates(iter(reversed(candidates)), locator, per_cell=2)
        self.assertEqual(one, two)
        self.assertEqual(len(one), 2)
        self.assertEqual(stats["outside_or_unassigned"], 1)
        self.assertEqual(stats["eligible_by_province_kind"]["Drenthe"]["primary"], 20)
        self.assertEqual(stats["selected_by_province_kind"]["Drenthe"]["primary"], 2)

    def test_generated_pack_measures_actual_provinces_and_kind_and_padding(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            locator = national.ProvinceLocator(boundaries())
            rows = [candidate("way/first", [[3.51, 52.01], [3.52, 52.02]])]
            result = national.build_candidate(rows, locator, out, "2026-11-16",
                Path(__file__).parents[1] / "maps" / "nl_profile.json")
            self.assertEqual(set(result["packed_by_province_kind"]), set(PROVINCES))
            self.assertEqual(result["packed_by_province_kind"]["Drenthe"]["primary"], 1)
            self.assertEqual(result["packed_by_province_kind"]["Drenthe"]["boundary"], 1)
            self.assertEqual(result["packed_by_province_kind"]["Zeeland"]["boundary"], 1)
            self.assertFalse(result["production"])
            self.assertEqual(result["coverage_class"], "measured-feature-inventory-not-completeness")
            data = (out / "candidate.meshmap").read_bytes()
            image = (out / "candidate.tiles.bin").read_bytes()
            self.assertEqual(len(image), 0x4C0000)
            self.assertEqual(image[:len(data)], data)
            self.assertTrue(all(b == 255 for b in image[len(data):]))
            self.assertEqual(result["pack_sha256"], hashlib.sha256(data).hexdigest())

    def test_long_way_geometry_bounded_and_endpoints_preserved(self):
        points = [[3.51 + i * 0.000001, 52.02] for i in range(5000)]
        locator = national.ProvinceLocator(boundaries())
        rows, _ = national.select_candidates([candidate("way/long", points)], locator)
        geometry = rows[0]["geometry"]
        self.assertLessEqual(len(geometry), 512)
        self.assertEqual(geometry[0], points[0])
        self.assertEqual(geometry[-1], points[-1])

    def test_small_island_still_has_closed_non_degenerate_boundary(self):
        ring = [[3.51,52.01], [3.51001,52.01], [3.51001,52.01001],
                [3.51,52.01001], [3.51,52.01]]
        simplified = national.simplify(ring, 0.00015)
        self.assertEqual(simplified[0], simplified[-1])
        self.assertGreaterEqual(len(set(tuple(p) for p in simplified)), 3)

    def test_selected_geometry_budget_is_hard_bound(self):
        locator = national.ProvinceLocator(boundaries())
        rows = [candidate("way/a", [[3.51,52.01], [3.52,52.02]]),
                candidate("way/b", [[3.55,52.05], [3.56,52.06]])]
        with self.assertRaisesRegex(ValueError, "geometry budget"):
            national.select_candidates(rows, locator, max_points=3)

    def test_integrity_requires_exact_input_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "input.pbf"
            p.write_bytes(b"fixture")
            self.assertEqual(national.verify_sha256(p, hashlib.sha256(b"fixture").hexdigest()),
                             hashlib.sha256(b"fixture").hexdigest())
            with self.assertRaises(ValueError):
                national.verify_sha256(p, "0" * 64)

    @unittest.skipUnless(importlib.util.find_spec("osmium") and shutil.which("osmium"),
                         "national CI installs osmium Python and CLI")
    def test_binary_pbf_reference_filter_and_real_reader(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "sample.osm"
            source.write_text("""<osm version='0.6'>
<node id='1' lat='52.01' lon='3.51'/>
<node id='2' lat='52.02' lon='3.52'/>
<node id='3' lat='52.03' lon='3.53'><tag k='place' v='town'/><tag k='name' v='Teststad'/></node>
<way id='100'><nd ref='1'/><nd ref='2'/><tag k='highway' v='primary'/><tag k='ref' v='N12'/></way>
</osm>""")
            binary = root / "source.osm.pbf"
            filtered = root / "filtered.osm.pbf"
            subprocess.run(["osmium", "cat", str(source), "-o", str(binary)], check=True)
            subprocess.run(["osmium", "tags-filter", str(binary), "w/highway=primary",
                            "n/place=town", "-o", str(filtered)], check=True)
            rows = list(national.pbf_candidates(filtered))
            self.assertEqual({r["id"] for r in rows}, {"node/3", "way/100"})
            road = next(r for r in rows if r["id"] == "way/100")
            self.assertEqual(road["name"], "N12")
            self.assertEqual(road["geometry"], [[3.51, 52.01], [3.52, 52.02]])

if __name__ == "__main__":
    unittest.main()
