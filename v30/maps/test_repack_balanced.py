import json
import hashlib
import tempfile
import unittest
from pathlib import Path

from build_nl import PROVINCES, REQUIRED_KINDS, BuildError
from national_pack import _packed_inventory
from repack_balanced import repack, _balanced_geometry


class BalancedRepackTest(unittest.TestCase):
    def test_dense_names_must_not_eliminate_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            source = path / 'input.jsonl'
            rows = []
            for p in PROVINCES:
                for k in sorted(REQUIRED_KINDS):
                    for i in range(100 if k not in ('boundary', 'place') else 2):
                        row = {'id': f'{p}/{k}/{i}', 'province': p, 'kind': k,
                               'name': (f'{p} settlement {i}' if k == 'place' else
                                        p if k == 'boundary' else
                                        'A12' if i == 0 and k == 'motorway' else
                                        f'{p} {k} {i} long optional name'*3),
                               'geometry': [[5.0, 52.0]] if k == 'place' else
                                           [[5.0, 52.0], [5.01, 52.01]],
                               'zoom_min': 11 if k == 'place' else 13,
                               'data_date': '2026-11-16'}
                        rows.append(row)
            source.write_text(''.join(json.dumps(row)+'\n' for row in rows))
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            profile = Path(__file__).with_name('nl_profile.json')
            repack(source, profile, path, digest)
            pack = path / 'candidate.meshmap'
            binary = pack.read_bytes()
            self.assertEqual(_packed_inventory(binary), {
                p: {k: 100 if k not in ('boundary', 'place') else 2
                    for k in REQUIRED_KINDS} for p in PROVINCES})
            meta_size = int.from_bytes(binary[6:10], 'little')
            self.assertLessEqual(meta_size, 65536)
            names = json.loads(binary[10:10+meta_size])['names']
            self.assertIn('A12', names)
            self.assertTrue(all(f'{p} settlement {i}' in names for p in PROVINCES for i in range(2)))
            self.assertEqual(source.read_bytes() and hashlib.sha256(source.read_bytes()).hexdigest(), digest)
            repack(source, profile, path, digest)
            self.assertEqual(pack.read_bytes(), binary)
            with self.assertRaises(ValueError):
                repack(source, profile, path, '0'*64)

    def test_body_budget_reserves_input_strata_and_all_places(self):
        rows = [{'province': p, 'kind': k, 'id': f'{p}/{k}/{i}',
                 'name': '', 'geometry': bytes(10)}
                for p in ('Drenthe', 'Zeeland') for k in ('place', 'water', 'primary', 'boundary')
                for i in range(2 if k in ('place', 'boundary') else 50)]
        result = _balanced_geometry(rows, 600)
        self.assertEqual(len([r for r in result if r['kind'] == 'place']), 4)
        self.assertEqual(len([r for r in result if r['kind'] == 'boundary']), 4)
        self.assertEqual({(r['province'], r['kind']) for r in result},
                         {(p,k) for p in ('Drenthe','Zeeland') for k in ('place','water','primary','boundary')})
        self.assertLess(len(result), len(rows))
        with self.assertRaises(BuildError):
            _balanced_geometry(rows, 1)


if __name__ == '__main__':
    unittest.main()
