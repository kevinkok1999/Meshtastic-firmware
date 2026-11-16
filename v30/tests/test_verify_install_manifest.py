import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location("verify_install_manifest", Path(__file__).parents[1] / "installer" / "verify_install_manifest.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ManifestVerifierTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.files = {}
        for kind in ("bootloader", "partition", "ota_data", "app0", "app1"):
            data = ((kind.encode() * 32)[:128] if kind != "ota_data" else b"\\x00" * 0x2000)
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
                for kind, offset in MODULE.EXPECTED_OFFSETS.items()
                for path in [self.files[kind]]
            ],
        }

    def tearDown(self):
        self.tmp.cleanup()

    def test_valid_manifest(self):
        self.assertTrue(MODULE.validate_manifest(self.manifest, self.root))

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

    def test_production_requires_independent_signature(self):
        self.manifest["production"] = True
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)

    def test_map_and_metadata_limits(self):
        data = b"map" * 100
        p = self.root / "map.bin"; p.write_bytes(data)
        self.manifest["artifacts"].append({"kind": "map", "path": p.name, "offset": MODULE.MAP_OFFSET,
                                           "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        self.manifest["pack_metadata_size"] = MODULE.MAX_PACK_METADATA + 1
        with self.assertRaises(MODULE.ManifestError):
            MODULE.validate_manifest(self.manifest, self.root)


if __name__ == "__main__":
    unittest.main()

