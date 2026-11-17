import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


VERIFIER = Path(__file__).parents[1] / "installer" / "verify_signed_manifest.py"


class SignedManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.manifest = self.root / "manifest.json"
        self.signature = self.root / "manifest.sig"
        self.public = self.root / "publisher.pub"
        self.key = Ed25519PrivateKey.generate()
        public = self.key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self.public.write_bytes(public)
        self.pin = hashlib.sha256(public).hexdigest()
        entries = []
        for kind, offset, payload in (("bootloader", 0, b"boot"), ("partition", 0x8000, b"part"),
                                      ("ota_data", 0xe000, b"\xff" * 8192), ("app0", 0x10000, b"app")):
            path = self.root / (kind + ".bin")
            path.write_bytes(payload)
            entries.append(dict(kind=kind, offset=offset, path=path.name, size=len(payload), sha256=hashlib.sha256(payload).hexdigest()))
        self.data = dict(board="lilygo-t-deck-plus", chip="ESP32-S3", flash_size_bytes=0x1000000,
                         production=False, install_mode="initial", artifacts=entries)
        self.sign(json.dumps(self.data).encode())

    def tearDown(self):
        self.tmp.cleanup()

    def sign(self, payload):
        self.manifest.write_bytes(payload)
        self.signature.write_bytes(self.key.sign(payload))

    def check(self, *, pin=None):
        return subprocess.run([sys.executable, str(VERIFIER), str(self.manifest), str(self.signature),
                               str(self.public), "--public-key-sha256", self.pin if pin is None else pin],
                              capture_output=True, text=True)

    def test_valid_signature_and_artifacts_still_not_release_authorization(self):
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertIn("not release authorization", result.stdout)

    def test_tampered_manifest_rejected_even_when_valid_json(self):
        self.manifest.write_bytes(self.manifest.read_bytes() + b" ")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("signature", result.stdout.lower())

    def test_wrong_pin_rejected(self):
        result = self.check(pin="00" * 32)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("pin", result.stdout.lower())

    def test_wrong_key_rejected_even_if_its_pin_is_provided(self):
        other = Ed25519PrivateKey.generate().public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
        self.public.write_bytes(other)
        result = self.check(pin=hashlib.sha256(other).hexdigest())
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("signature", result.stdout.lower())

    def test_signed_duplicate_keys_rejected(self):
        self.sign(b'{"board":"wrong",' + json.dumps(self.data).encode()[1:])
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("duplicate", result.stdout.lower())

    def test_signed_artifact_mismatch_rejected(self):
        (self.root / "app0.bin").write_bytes(b"bad")
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("sha256 mismatch", result.stdout.lower())

    def test_signature_does_not_override_production_gate(self):
        self.data["production"] = True
        self.sign(json.dumps(self.data).encode())
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("production", result.stdout.lower())

    def test_truncated_signature_rejected(self):
        self.signature.write_bytes(self.signature.read_bytes()[:63])
        result = self.check()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("signature", result.stdout.lower())


if __name__ == "__main__":
    unittest.main()
