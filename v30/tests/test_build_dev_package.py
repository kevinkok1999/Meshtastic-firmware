import hashlib
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

from partition_fixture import partition_image, ROWS
from esp_image_fixture import esp_image

INSTALLER = Path(__file__).parents[1] / 'installer'
BUILDER = INSTALLER / 'build_dev_package.py'
VERIFIER = INSTALLER / 'verify_install_manifest.py'


def pack_bytes():
    # Minimal independently framed pack; the verifier checks the framing.
    metadata = b'{"version":1}'
    prefix = b'MMN1' + struct.pack('<HI', 1, len(metadata)) + metadata + zlib.compress(b'geometry')
    return prefix + struct.pack('<I', zlib.crc32(prefix) & 0xffffffff)


class DevPackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.inputs = {'bootloader': esp_image('bootloader'), 'partition': partition_image(),
                       'ota-data': bytes(8192), 'app': esp_image('app0'), 'map': pack_bytes()}
        for name, data in self.inputs.items():
            (self.root / name).write_bytes(data)

    def build(self, name='output'):
        args = [sys.executable, str(BUILDER), '--output', str(self.root / name)]
        for field in self.inputs:
            args.extend(['--' + field, str(self.root / field)])
        return subprocess.run(args, capture_output=True, text=True)

    def test_candidate_exact_offsets_and_map_erased_padding(self):
        maps = INSTALLER.parent / 'maps'
        sys.path.insert(0, str(maps))
        try:
            from build_nl import PROVINCES, build_pack
            source = self.root / 'features.jsonl'
            source.write_text(''.join(json.dumps({'id': f'place/{i}', 'kind': 'place',
                'province': province, 'name': f'Dorp {i}', 'geometry': [[4 + i / 100, 52]]}) + '\n'
                for i, province in enumerate(PROVINCES)), encoding='utf-8')
            build_pack(source, maps / 'nl_profile.json', self.root / 'map')
            self.inputs['map'] = (self.root / 'map').read_bytes()
        finally:
            sys.path.remove(str(maps))
        result = self.build()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        out = self.root / 'output'
        manifest = json.loads((out / 'manifest.json').read_text())
        self.assertIs(manifest['production'], False)
        self.assertEqual(manifest['install_mode'], 'full-v30')
        self.assertEqual({a['kind']: a['offset'] for a in manifest['artifacts']},
                         {'bootloader': 0, 'partition': 0x8000, 'ota_data': 0xe000,
                          'app0': 0x10000, 'map': 0x7d0000})
        image = (out / 'map.bin').read_bytes()
        self.assertEqual(len(image), 0x4c0000)
        self.assertEqual(image[:len(self.inputs['map'])], self.inputs['map'])
        self.assertEqual(image[len(self.inputs['map']):], b'\xff' * (len(image)-len(self.inputs['map'])))
        for item in manifest['artifacts']:
            self.assertEqual(item['sha256'], hashlib.sha256((out / item['path']).read_bytes()).hexdigest())
        verified = subprocess.run([sys.executable, str(VERIFIER), str(out / 'manifest.json')], capture_output=True, text=True)
        self.assertEqual(verified.returncode, 0, verified.stdout + verified.stderr)
        self.assertEqual(self.build('second').returncode, 0)
        self.assertEqual((out / 'manifest.json').read_bytes(), (self.root / 'second/manifest.json').read_bytes())

    def test_existing_output_never_overwritten(self):
        out = self.root / 'output'
        out.mkdir()
        (out / 'manifest.json').write_text('preserve')
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('output already exists', result.stdout)
        self.assertEqual((out / 'manifest.json').read_text(), 'preserve')
        self.assertEqual(list(out.iterdir()), [out / 'manifest.json'])

    def test_wrong_partition_never_creates_package(self):
        rows = list(ROWS)
        rows[4] = ('tiles', 1, 0x83, 0xc90000, 0x360000)
        (self.root / 'partition').write_bytes(partition_image(rows))
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('partition binary layout', result.stdout)
        self.assertFalse((self.root / 'output').exists())

    def test_wrong_firmware_never_creates_package(self):
        (self.root / 'app').write_bytes(esp_image('bootloader'))
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('app0', result.stdout)
        self.assertIn('descriptor', result.stdout)
        self.assertFalse((self.root / 'output').exists())

    def test_damaged_map_never_creates_package(self):
        (self.root / 'map').write_bytes(self.inputs['map'][:-1])
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('map pack CRC32 mismatch', result.stdout)
        self.assertFalse((self.root / 'output').exists())

    def test_overbudget_app_never_creates_package(self):
        (self.root / 'app').write_bytes(b'x' * (0x3e0000 - 0x40000 + 1))
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('app0 exceeds input budget', result.stdout)
        self.assertFalse((self.root / 'output').exists())


if __name__ == '__main__':
    unittest.main()
