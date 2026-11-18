import hashlib
import importlib.util
import json
import struct
import sys
import zlib
import tempfile
import unittest
from pathlib import Path
from partition_fixture import partition_image, ROWS
from esp_image_fixture import esp_image

SPEC = importlib.util.spec_from_file_location("verify_install_manifest", Path(__file__).parents[1] / "installer" / "verify_install_manifest.py")
sys.path.insert(0, str(Path(__file__).parents[1] / 'maps'))
from build_nl import PROVINCES, build_pack

MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ManifestVerifierTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.files = {}
        for kind in ("bootloader", "partition", "ota_data", "app0"):
            data = (partition_image() if kind == "partition" else
                    esp_image(kind) if kind != "ota_data" else bytes([0]) * 0x2000)
            path = self.root / f"{kind}.bin"
            path.write_bytes(data)
            self.files[kind] = path
        self.manifest = {
            "schema": 1, "board": "lilygo-t-deck-plus", "chip": "ESP32-S3",
            "flash_size_bytes": 0x1000000, "production": False,
            "pack_metadata_size": 0,
            "artifacts": [
                {"kind": kind, "path": path.name, "offset": offset,
                 "size": path.stat().st_size,
                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                for kind, offset in ((k, MODULE.EXPECTED_OFFSETS[k]) for k in ("bootloader", "partition", "ota_data", "app0"))
                for path in [self.files[kind]]
            ],
        }

    def tearDown(self):
        self.tmp.cleanup()

    def test_valid_manifest(self):
        self.assertTrue(MODULE.validate_manifest(self.manifest, self.root))

    def replace_firmware(self, kind, image):
        self.files[kind].write_bytes(image)
        artifact = next(x for x in self.manifest["artifacts"] if x["kind"] == kind)
        artifact["size"] = len(image)
        artifact["sha256"] = hashlib.sha256(image).hexdigest()

    def test_rehashed_wrong_esp_image_magic_chip_or_count_rejected(self):
        for offset, value, error in ((0, 0, 'magic'), (12, 0, 'chip'),
                                     (1, 0, 'segment count'), (1, 17, 'segment count'),
                                     (23, 2, 'digest flag')):
            image = bytearray(esp_image('bootloader'))
            image[offset] = value
            with self.subTest(offset=offset, value=value):
                self.replace_firmware('bootloader', image)
                with self.assertRaisesRegex(MODULE.ManifestError, 'bootloader.*' + error):
                    MODULE.validate_manifest(self.manifest, self.root)

    def test_rehashed_truncated_segment_or_invalid_alignment_rejected(self):
        for size in (0xfffffffc, 3):
            image = bytearray(esp_image('app0'))
            struct.pack_into('<I', image, 28, size)
            with self.subTest(size=size):
                self.replace_firmware('app0', image)
                with self.assertRaisesRegex(MODULE.ManifestError, 'app0.*segment'):
                    MODULE.validate_manifest(self.manifest, self.root)

    def test_wrong_mapped_flash_offset_rejected(self):
        image = bytearray(esp_image('app0'))
        struct.pack_into('<I', image, 24, 0x3c000024)
        image[-32:] = hashlib.sha256(image[:-32]).digest()
        self.replace_firmware('app0', image)
        with self.assertRaisesRegex(MODULE.ManifestError, 'app0.*alignment'):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rehashed_firmware_with_invalid_entry_or_load_address_rejected(self):
        for position, addr, error in ((4, 0, 'entry point'), (24, 0xeeeeeeee, 'load address')):
            image = bytearray(esp_image('bootloader'))
            struct.pack_into('<I', image, position, addr)
            image[-32:] = hashlib.sha256(image[:-32]).digest()
            self.replace_firmware('bootloader', image)
            with self.assertRaisesRegex(MODULE.ManifestError, 'bootloader.*' + error):
                MODULE.validate_manifest(self.manifest, self.root)

    def test_application_descriptor_and_bootloader_role_not_interchangeable(self):
        for kind, image in (('app0', esp_image('bootloader')), ('bootloader', esp_image('app0'))):
            with self.subTest(kind=kind):
                self.replace_firmware(kind, image)
                with self.assertRaisesRegex(MODULE.ManifestError, kind + '.*descriptor'):
                    MODULE.validate_manifest(self.manifest, self.root)
                self.replace_firmware(kind, esp_image(kind))

    def test_rehashed_checksum_digest_padding_and_trailing_bytes_rejected(self):
        valid = esp_image('bootloader')
        for offset, error in ((-33, 'checksum'), (-1, 'digest'), (-34, 'padding')):
            image = bytearray(valid)
            image[offset] ^= 1
            with self.subTest(error=error):
                self.replace_firmware('bootloader', image)
                with self.assertRaisesRegex(MODULE.ManifestError, 'bootloader.*' + error):
                    MODULE.validate_manifest(self.manifest, self.root)
        for image in (valid + b'\xff', valid[:-1]):
            self.replace_firmware('bootloader', image)
            with self.assertRaisesRegex(MODULE.ManifestError, 'bootloader.*digest.*length'):
                MODULE.validate_manifest(self.manifest, self.root)

    def test_no_appended_digest_still_requires_checksum_and_exact_end(self):
        image = esp_image('bootloader', append_digest=False)
        self.replace_firmware('bootloader', image)
        self.assertTrue(MODULE.validate_manifest(self.manifest, self.root))
        self.replace_firmware('bootloader', image[:-1] + bytes([image[-1] ^ 1]))
        with self.assertRaisesRegex(MODULE.ManifestError, 'bootloader.*checksum'):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_valid_app1_safe_mode_validation_and_corruption(self):
        image = esp_image('app1')
        path = self.root / 'app1.bin'
        path.write_bytes(image)
        self.files['app1'] = path
        self.manifest['artifacts'].append(dict(kind='app1', path=path.name,
            offset=MODULE.EXPECTED_OFFSETS['app1'], size=len(image),
            sha256=hashlib.sha256(image).hexdigest()))
        self.manifest['safe_mode'] = True
        self.assertTrue(MODULE.validate_manifest(self.manifest, self.root))
        self.replace_firmware('app1', image[:-1])
        with self.assertRaisesRegex(MODULE.ManifestError, 'app1.*digest'):
            MODULE.validate_manifest(self.manifest, self.root)

    def replace_partition(self, image):
        self.files["partition"].write_bytes(image)
        artifact = next(x for x in self.manifest["artifacts"] if x["kind"] == "partition")
        artifact["size"] = len(image)
        artifact["sha256"] = hashlib.sha256(image).hexdigest()

    def test_valid_checksum_cannot_move_tiles_into_preserved_user_region(self):
        rows = list(ROWS)
        rows[4] = ("tiles", 1, 0x83, 0xC90000, 0x360000)
        self.replace_partition(partition_image(rows))
        with self.assertRaisesRegex(MODULE.ManifestError, "partition.*layout"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_partition_checksum_must_match(self):
        image = bytearray(partition_image())
        image[7 * 32 + 16] ^= 1
        self.replace_partition(image)
        with self.assertRaisesRegex(MODULE.ManifestError, "partition.*checksum"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_partition_rejects_non_erased_trailing_bytes(self):
        image = bytearray(partition_image())
        image[-1] = 0
        self.replace_partition(image)
        with self.assertRaisesRegex(MODULE.ManifestError, "partition.*padding"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_partition_rejects_missing_user_data_partition(self):
        self.replace_partition(partition_image([row for row in ROWS if row[0] != "spiffs"]))
        with self.assertRaisesRegex(MODULE.ManifestError, "partition.*layout"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rejects_path_traversal(self):
        self.manifest["artifacts"][0]["path"] = "../outside.bin"
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rejects_hash_and_size_mismatch(self):
        self.manifest["artifacts"][0]["sha256"] = "0" * 64
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rejects_overlap_and_wrong_offsets(self):
        self.manifest["artifacts"][1]["offset"] = MODULE.EXPECTED_OFFSETS["bootloader"]
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rejects_app_without_headroom(self):
        path = self.files["app0"]
        self.manifest["artifacts"][3]["size"] = MODULE.APP_PARTITION_SIZE - MODULE.APP_HEADROOM + 1
        self.manifest["artifacts"][3]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rejects_untrusted_production_even_with_self_asserted_signature(self):
        self.manifest["production"] = True
        self.manifest["signature_verified"] = True
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_production_gate_requires_literal_false(self):
        for value in (None, 'false', 'true', 0, 1):
            self.manifest['production'] = value
            with self.subTest(value=value), self.assertRaisesRegex(MODULE.ManifestError, 'production'):
                MODULE.validate_manifest(self.manifest, self.root)
        del self.manifest['production']
        with self.assertRaisesRegex(MODULE.ManifestError, 'production'):
            MODULE.validate_manifest(self.manifest, self.root)

    def add_map(self, pack=None):
        if pack is None:
            source = self.root / "source.jsonl"
            source.write_text("\n".join(json.dumps({
                "id": f"place/{i}", "kind": "place", "province": province,
                "name": f"Dorp {i}", "geometry": [[4 + i / 100, 52]],
            }) for i, province in enumerate(PROVINCES)) + "\n", encoding="utf-8")
            out = self.root / "fixture.meshmap"
            report = build_pack(source, Path(__file__).parents[1] / "maps" / "nl_profile.json", out)
            pack = out.read_bytes()
            self.assertEqual(len(pack), report.bytes_written)
        self.pack = bytes(pack)
        self.manifest["pack_metadata_size"] = struct.unpack_from("<I", self.pack, 6)[0]
        artifact = {
            "kind": "map", "path": "map.bin", "offset": MODULE.MAP_OFFSET,
            "size": MODULE.MAP_SIZE, "pack_size": len(self.pack), "padding_byte": 255,
            "sha256": hashlib.sha256(self.pack + b"\xff" * (MODULE.MAP_SIZE - len(self.pack))).hexdigest(),
        }
        (self.root / "map.bin").write_bytes(self.pack + b"\xff" * (MODULE.MAP_SIZE - len(self.pack)))
        self.manifest["artifacts"].append(artifact)
        return artifact

    def replace_map(self, image):
        (self.root / "map.bin").write_bytes(image)
        self.manifest["artifacts"][-1]["sha256"] = hashlib.sha256(image).hexdigest()

    def repack(self, prefix):
        return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xffffffff)

    def replace_map_body(self, body):
        artifact = self.add_map()
        metadata_len = struct.unpack_from("<I", self.pack, 6)[0]
        logical = self.repack(self.pack[:10 + metadata_len] + body)
        artifact["pack_size"] = len(logical)
        self.replace_map(logical + b"\xff" * (MODULE.MAP_SIZE - len(logical)))

    def test_valid_crc_cannot_hide_invalid_compressed_geometry(self):
        self.replace_map_body(b"not a zlib stream")
        with self.assertRaisesRegex(MODULE.ManifestError, "map.*zlib"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_valid_crc_cannot_hide_trailing_zlib_stream(self):
        self.replace_map_body(zlib.compress(b"x") + zlib.compress(b"y"))
        with self.assertRaisesRegex(MODULE.ManifestError, "map.*zlib"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_compressed_payload_cannot_exceed_native_decoded_budget(self):
        self.replace_map_body(zlib.compress(b"x" * (4 * 1024 * 1024 + 1)))
        with self.assertRaisesRegex(MODULE.ManifestError, "map.*decoded"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_generated_pack_valid_in_full_partition_image(self):
        self.add_map()
        self.manifest["install_mode"] = "full-v30"
        self.assertTrue(MODULE.validate_manifest(self.manifest, self.root))
        self.assertEqual((self.root / "map.bin").stat().st_size, 0x4C0000)

    def test_metadata_bound_matches_native_reader(self):
        self.assertEqual(MODULE.MAX_PACK_METADATA, 65536)

    def test_map_and_metadata_limits(self):
        self.add_map()
        self.manifest["pack_metadata_size"] = 65537
        with self.assertRaisesRegex(MODULE.ManifestError, "metadata"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_declared_metadata_must_match_header(self):
        self.add_map()
        self.manifest["pack_metadata_size"] += 1
        with self.assertRaisesRegex(MODULE.ManifestError, "metadata.*mismatch"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_pack_length_required_and_bounded(self):
        artifact = self.add_map()
        for value in (0, 13, 0x380001, MODULE.MAP_SIZE):
            artifact["pack_size"] = value
            with self.subTest(value=value), self.assertRaisesRegex(MODULE.ManifestError, "pack_size"):
                MODULE.validate_manifest(self.manifest, self.root)
        artifact.pop("pack_size")
        with self.assertRaisesRegex(MODULE.ManifestError, "pack_size"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_pack_length_cannot_include_padding_or_truncate_crc(self):
        artifact = self.add_map()
        for delta in (-1, 1):
            artifact["pack_size"] = len(self.pack) + delta
            with self.subTest(delta=delta), self.assertRaises(MODULE.ManifestError):
                MODULE.validate_manifest(self.manifest, self.root)

    def test_map_crc_checked_even_after_artifact_hash_updated(self):
        self.add_map()
        image = bytearray((self.root / "map.bin").read_bytes())
        image[len(self.pack) - 5] ^= 1
        self.replace_map(image)
        with self.assertRaisesRegex(MODULE.ManifestError, "CRC32"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_magic_and_version_checked_even_with_valid_crc(self):
        for offset, value, error in ((0, 0, "magic"), (4, 2, "version")):
            with self.subTest(offset=offset):
                self.manifest["artifacts"] = self.manifest["artifacts"][:4]
                self.add_map()
                prefix = bytearray(self.pack[:-4])
                prefix[offset] = value
                pack = self.repack(prefix)
                self.replace_map(pack + b"\xff" * (MODULE.MAP_SIZE - len(pack)))
                with self.assertRaisesRegex(MODULE.ManifestError, error):
                    MODULE.validate_manifest(self.manifest, self.root)

    def test_metadata_header_cannot_run_beyond_pack(self):
        self.add_map()
        prefix = bytearray(self.pack[:-4])
        struct.pack_into("<I", prefix, 6, len(self.pack))
        self.manifest["pack_metadata_size"] = len(self.pack)
        pack = self.repack(prefix)
        self.replace_map(pack + b"\xff" * (MODULE.MAP_SIZE - len(pack)))
        with self.assertRaisesRegex(MODULE.ManifestError, "metadata"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_malformed_metadata_rejected_with_valid_crc(self):
        self.add_map()
        prefix = bytearray(self.pack[:-4])
        prefix[10] = ord("!")
        pack = self.repack(prefix)
        self.replace_map(pack + b"\xff" * (MODULE.MAP_SIZE - len(pack)))
        with self.assertRaisesRegex(MODULE.ManifestError, "metadata"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_only_explicit_ff_padding_allowed(self):
        artifact = self.add_map()
        for value in (0, "255", None):
            artifact["padding_byte"] = value
            with self.subTest(value=value), self.assertRaisesRegex(MODULE.ManifestError, "padding"):
                MODULE.validate_manifest(self.manifest, self.root)
        artifact["padding_byte"] = 255
        image = bytearray((self.root / "map.bin").read_bytes())
        image[-1] = 0
        self.replace_map(image)
        with self.assertRaisesRegex(MODULE.ManifestError, "padding"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_actual_metadata_above_reader_limit_rejected(self):
        meta = b'{"version":1,"names":[],"features":0,"extra":"' + b"x" * 65536 + b'"}'
        prefix = b"MMN1" + struct.pack("<HI", 1, len(meta)) + meta + zlib.compress(b"")
        self.add_map(self.repack(prefix))
        with self.assertRaisesRegex(MODULE.ManifestError, "metadata"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_metadata_version_must_match_binary_header(self):
        meta = b'{"version":2,"names":[],"features":0}'
        prefix = b"MMN1" + struct.pack("<HI", 1, len(meta)) + meta + zlib.compress(b"")
        self.add_map(self.repack(prefix))
        with self.assertRaisesRegex(MODULE.ManifestError, "metadata version"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_map_cannot_move_into_preserved_partition(self):
        artifact = self.add_map()
        artifact["offset"] = MODULE.SPIFFS_OFFSET
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_raw_unpadded_pack_is_not_partition_image(self):
        artifact = self.add_map()
        artifact["size"] = len(self.pack)
        self.replace_map(self.pack)
        with self.assertRaisesRegex(MODULE.ManifestError, "4.75 MiB"):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_rejects_unknown_and_user_partition_roles(self):
        for kind in ("nvs", "spiffs", "identity", "user"):
            self.manifest["artifacts"][0]["kind"] = kind
            with self.subTest(kind=kind), self.assertRaisesRegex(MODULE.ManifestError, "allowlisted"):
                MODULE.validate_manifest(self.manifest, self.root)

    def test_app1_requires_explicit_safe_mode(self):
        image = esp_image('app1')
        p = self.root / "app1.bin"; p.write_bytes(image)
        self.manifest["artifacts"].append({"kind": "app1", "path": p.name, "offset": MODULE.EXPECTED_OFFSETS["app1"],
                                           "size": len(image), "sha256": hashlib.sha256(image).hexdigest()})
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_full_v30_requires_map_and_explicit_metadata(self):
        self.manifest["install_mode"] = "full-v30"
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

if __name__ == "__main__":
    unittest.main()
