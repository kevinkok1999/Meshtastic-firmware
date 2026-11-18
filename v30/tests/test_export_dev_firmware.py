#!/usr/bin/env python3
"""Host contract tests for offline export. No real hardware or serial access."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

from partition_fixture import partition_image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'installer'))
from export_dev_firmware import ExportError, export


GOOD_INFO = 'esptool.py v4.5.1\nESP32S3 image header\nChip ID: 9\nChecksum: 0xef (valid)\nValidation hash: ' + 'a' * 64 + ' (valid)\n'


def fixture_image(chip=9):
    header = struct.pack('<BBBBI', 0xE9, 1, 0, 0x4F, 0x40378000)
    extended = b'\xff\0\0\0' + struct.pack('<H', chip) + b'\0' * 9 + b'\x01'
    segment = b'\x40\x41\x42\x43'
    body = header + extended + struct.pack('<II', 0x40378000, len(segment)) + segment
    checksum = 0xEF
    for value in segment:
        checksum ^= value
    body += b'\0' * (15 - len(body) % 16) + bytes([checksum])
    return body + hashlib.sha256(body).digest()


class ExportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.project = root / 'project'
        self.build = self.project / '.pio/build/LilyGo_TDeck_companion_radio_touch'
        self.build.mkdir(parents=True)
        self.framework = root / 'framework-arduinoespressif32'
        self.ota = self.framework / 'tools/partitions/boot_app0.bin'
        self.ota.parent.mkdir(parents=True)
        self.ota.write_bytes(b'\xff' * 8192)
        self.tool = root / 'tool-esptoolpy/esptool.py'
        self.tool.parent.mkdir()
        self.tool.write_text('# test fixture only')
        (self.build / 'bootloader.bin').write_bytes(fixture_image())
        (self.build / 'firmware.bin').write_bytes(fixture_image())
        (self.build / 'partitions.bin').write_bytes(partition_image())
        self.meta = dict(environment='LilyGo_TDeck_companion_radio_touch', board='t-deck',
                         chip='esp32s3', flash_size='16MB', app_offset='0x10000',
                         app_path=str(self.build / 'firmware.bin'),
                         esptool=str(self.tool), framework=str(self.framework),
                         extra_images=[['0x0', str(self.build / 'bootloader.bin')],
                                       ['0x8000', str(self.build / 'partitions.bin')],
                                       ['0xe000', str(self.ota)]])
        self.metadata = root / 'pio-metadata.json'
        self.output = root / 'export'

    def run_export(self):
        self.metadata.write_text(json.dumps(self.meta))
        with patch('export_dev_firmware._image_info', return_value=GOOD_INFO):
            return export(self.metadata, self.build, self.output, 'a' * 40)

    def test_actual_parts_and_initial_manifest(self):
        manifest = self.run_export()
        self.assertFalse(manifest['production'])
        self.assertTrue(manifest['development_only'])
        self.assertEqual(manifest['install_mode'], 'initial')
        self.assertEqual(manifest['signature_status'], 'unsigned')
        self.assertEqual({a['offset'] for a in manifest['artifacts']}, {0, 0x8000, 0xe000, 0x10000})
        self.assertEqual({a['kind'] for a in manifest['artifacts']}, {'bootloader', 'partition', 'ota_data', 'app0'})
        for artifact in manifest['artifacts']:
            self.assertEqual(hashlib.sha256((self.output / artifact['path']).read_bytes()).hexdigest(), artifact['sha256'])
        self.assertTrue((self.output / 'firmware.image-info.txt').is_file())

    def test_reject_wrong_offset_path_or_chip(self):
        self.meta['extra_images'][2][0] = '0xf000'
        with self.assertRaises(ExportError): self.run_export()
        self.meta['extra_images'][2][0] = '0xe000'
        self.meta['extra_images'][2][1] = str(self.build / 'bootloader.bin')
        with self.assertRaises(ExportError): self.run_export()
        self.meta['extra_images'][2][1] = str(self.ota)
        self.meta['chip'] = 'esp32'
        with self.assertRaises(ExportError): self.run_export()

    def test_reject_wrong_image_chip_and_corrupt_footer(self):
        fw = self.build / 'firmware.bin'
        fw.write_bytes(fixture_image(0))
        with self.assertRaises(ExportError): self.run_export()
        fw.write_bytes(fixture_image()[:-1] + b'\x00')
        with self.assertRaises(ExportError): self.run_export()

    def test_reject_esptool_invalid_checksum_even_with_successful_exit(self):
        self.metadata.write_text(json.dumps(self.meta))
        with patch('export_dev_firmware._image_info', return_value=GOOD_INFO.replace('Checksum: 0xef (valid)', 'Checksum: 0xef (invalid - calculated aa)')):
            with self.assertRaises(ExportError):
                export(self.metadata, self.build, self.output, 'a' * 40)


if __name__ == '__main__':
    unittest.main()
