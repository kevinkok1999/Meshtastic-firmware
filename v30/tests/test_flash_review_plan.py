import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from partition_fixture import partition_image

INSTALLER = Path(__file__).parents[1] / 'installer'
PLAN = INSTALLER / 'flash_review_plan.py'


class FlashReviewTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        maps = INSTALLER.parent / 'maps'
        sys.path.insert(0, str(maps))
        try:
            from build_nl import PROVINCES, build_pack
            source = self.root / 'features.jsonl'
            source.write_text(''.join(json.dumps({'id': f'place/{i}', 'kind': 'place',
                'province': province, 'name': f'Dorp {i}', 'geometry': [[4 + i / 100, 52]]}) + '\n'
                for i, province in enumerate(PROVINCES)), encoding='utf-8')
            build_pack(source, maps / 'nl_profile.json', self.root / 'map.meshmap')
        finally:
            sys.path.remove(str(maps))
        pack = (self.root / 'map.meshmap').read_bytes()
        self.map_image = pack.ljust(0x4c0000, b'\xff')
        files = [('bootloader', 0, b'boot'), ('partition', 0x8000, partition_image()),
                 ('ota_data', 0xe000, b'\xff'*8192), ('app0', 0x10000, b'app'),
                 ('map', 0x7d0000, self.map_image)]
        entries = []
        for kind, offset, data in files:
            path = self.root / (kind + '.bin')
            path.write_bytes(data)
            entries.append(dict(kind=kind, offset=offset, path=path.name,
                size=len(data), sha256=hashlib.sha256(data).hexdigest()))
        entries[-1].update(pack_size=len(pack), padding_byte=255)
        self.data = dict(schema=1, board='lilygo-t-deck-plus', chip='ESP32-S3',
            flash_size_bytes=0x1000000, production=False, install_mode='full-v30',
            pack_metadata_size=int.from_bytes(pack[6:10], 'little'), artifacts=entries)
        self.key = Ed25519PrivateKey.generate()
        pub = self.key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        (self.root / 'pub').write_bytes(pub)
        self.pin = hashlib.sha256(pub).hexdigest()
        self.sign()

    def sign(self):
        payload = json.dumps(self.data).encode()
        (self.root / 'manifest').write_bytes(payload)
        (self.root / 'sig').write_bytes(self.key.sign(payload))

    def check(self, pin=None):
        return subprocess.run([sys.executable, str(PLAN), str(self.root / 'manifest'),
            str(self.root / 'sig'), str(self.root / 'pub'), '--public-key-sha256', pin or self.pin],
            capture_output=True, text=True)

    def test_review_preserves_user_and_rollback_sectors(self):
        r = self.check()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        plan = json.loads(r.stdout)
        self.assertIs(plan['flash_authorized'], False)
        self.assertIs(plan['erase_all'], False)
        self.assertEqual([(p['kind'],p['offset'],p['erase_end']) for p in plan['segments']],
            [('bootloader',0,0x1000),('partition',0x8000,0x9000),('ota_data',0xe000,0x10000),
             ('app0',0x10000,0x11000),('map',0x7d0000,0xc90000)])
        self.assertEqual(plan['preserved_ranges'], [
            {'name':'nvs','start':0x9000,'end':0xe000},
            {'name':'app1','start':0x3f0000,'end':0x7d0000},
            {'name':'spiffs','start':0xc90000,'end':0xff0000},
            {'name':'coredump','start':0xff0000,'end':0x1000000}])

    def test_wrong_pin_or_tampered_file_emits_no_plan(self):
        r = self.check('00'*32)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(r.stdout, '')
        self.assertIn('pin', r.stderr)
        (self.root/'app0.bin').write_bytes(b'bad')
        r = self.check()
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(r.stdout, '')
        self.assertIn('sha256', r.stderr)

    def test_rollback_slot_is_not_accepted_even_with_safe_mode(self):
        self.data['safe_mode'] = True
        (self.root/'app1.bin').write_bytes(b'other')
        self.data['artifacts'].append(dict(kind='app1',offset=0x3f0000,path='app1.bin',size=5,
            sha256=hashlib.sha256(b'other').hexdigest()))
        self.sign()
        r = self.check()
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(r.stdout, '')
        self.assertIn('app1', r.stderr)

    def test_initial_manifest_without_map_cannot_be_full_v30_review(self):
        self.data['artifacts'].pop()
        self.data['install_mode'] = 'initial'
        self.sign()
        r = self.check()
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(r.stdout, '')
        self.assertIn('full-v30', r.stderr)


if __name__ == '__main__':
    unittest.main()
