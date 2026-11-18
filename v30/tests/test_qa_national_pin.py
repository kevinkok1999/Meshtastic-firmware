import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[1] / 'installer'))
from qa_national_pin import verify_map, prepare_pin, MAP_SOURCE_COMMIT, FIRMWARE_SOURCE_COMMIT, PBF_SHA, BOUNDARY_SHA
from verify_install_manifest import ManifestError, MAP_SIZE
from esp_image_fixture import esp_image
from partition_fixture import partition_image
sys.path.insert(0, str(Path(__file__).parents[1] / 'maps'))
from build_nl import PROVINCES, build_pack


class NationalPinTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        features = self.root/'fixture.jsonl'
        features.write_text(''.join(json.dumps({'id':f'{kind}/{i}','kind':kind,'province':p,'name':f'Dorp {i}',
            'geometry':[[4+i/100,52]] if kind=='place' else [[4+i/100,52],[4+i/100,52.01]]})+'\n'
            for i,p in enumerate(PROVINCES) for kind in ('place','boundary','primary')))
        build_pack(features, Path(__file__).parents[1]/'maps/nl_profile.json', self.root/'candidate.meshmap')
        self.pack = (self.root/'candidate.meshmap').read_bytes()
        image = self.pack.ljust(MAP_SIZE, b'\xff')
        (self.root / 'candidate.meshmap').write_bytes(self.pack)
        (self.root / 'candidate.tiles.bin').write_bytes(image)
        digest = hashlib.sha256(self.pack).hexdigest()
        image_digest = hashlib.sha256(image).hexdigest()
        self.c = dict(production=False, pack_sha256=digest, image_sha256=image_digest,
                      pack_size=len(self.pack), image_size=MAP_SIZE,
                      boundaries_input_sha256=BOUNDARY_SHA, filtered_input_sha256='ab'*32,
                      packed_by_province_kind={p:{'boundary':1, 'place':1, 'primary':1} for p in PROVINCES})
        self.p = dict(production=False, source_commit=MAP_SOURCE_COMMIT, osm_original_sha256=PBF_SHA,
                      boundary_sha256=BOUNDARY_SHA, osm_filtered_sha256='ab'*32,
                      source_hash_review_mode='reviewed-exact-sha256', pack_sha256=digest,
                      padded_image_sha256=image_digest)
        self.write()

    def write(self):
        (self.root/'coverage.json').write_text(json.dumps(self.c))
        (self.root/'provenance.json').write_text(json.dumps(self.p))

    def test_exact_inventory_and_bytes(self):
        self.assertEqual(verify_map(self.root), hashlib.sha256(self.pack).hexdigest())

    def test_wrong_provenance(self):
        self.p['source_commit']='0'*40; self.write()
        with self.assertRaises(ManifestError): verify_map(self.root)

    def test_missing_province(self):
        self.c['packed_by_province_kind'].pop(PROVINCES[0]); self.write()
        with self.assertRaises(ManifestError): verify_map(self.root)

    def test_tampered_map(self):
        (self.root/'candidate.meshmap').write_bytes(b'X'+self.pack[1:])
        with self.assertRaises(ManifestError): verify_map(self.root)

    def test_complete_ephemeral_qa_pin(self):
        initial=self.root/'initial'; initial.mkdir()
        parts=[]
        for kind,offset,data in [('bootloader',0,esp_image('bootloader')),
            ('partition',0x8000,partition_image()),('ota_data',0xe000,b'\xff'*8192),
            ('app0',0x10000,esp_image('app0'))]:
            name=kind+'.bin'; (initial/name).write_bytes(data)
            parts.append(dict(kind=kind,offset=offset,path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
        manifest=dict(schema=1,board='lilygo-t-deck-plus',chip='ESP32-S3',flash_size_bytes=0x1000000,
                      production=False,install_mode='initial',source_commit=FIRMWARE_SOURCE_COMMIT,artifacts=parts)
        (initial/'manifest.json').write_text(json.dumps(manifest))
        self.assertEqual(prepare_pin(initial,self.root),hashlib.sha256(self.pack).hexdigest())


if __name__ == '__main__': unittest.main()
