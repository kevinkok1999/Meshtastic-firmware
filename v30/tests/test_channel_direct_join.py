import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parents[1]))
from channel_direct_join import (
    JoinInputError,
    action_for_native_channel_path,
    normalize_channel_key,
    normalize_invite_locator,
    parse_join_input,
)


class DirectJoinContractTests(unittest.TestCase):
    KEY = "A1" * 16

    def test_key_is_case_insensitive_and_capability_is_label_independent(self):
        one = parse_join_input(self.KEY.lower(), label="Alpha")
        two = parse_join_input("  " + self.KEY.lower() + "  ", label="Different label")
        self.assertEqual(one.key_bytes, two.key_bytes)
        self.assertEqual(one.channel_capability, two.channel_capability)
        self.assertEqual(one.kind, "direct_key")

    def test_wrong_length_non_hex_and_weak_passphrase_rejected(self):
        for value in ("A1" * 15, "A1" * 17, "not-a-passphrase", "A1" * 15 + "Z0"):
            with self.subTest(value=value):
                with self.assertRaises(JoinInputError):
                    normalize_channel_key(value)

    def test_legacy_invite_path_is_preserved(self):
        action = parse_join_input(" abcd-1234 ", label="Existing")
        self.assertEqual(action.kind, "invite")
        self.assertEqual(action.invite_locator, "ABCD-1234")
        self.assertEqual(normalize_invite_locator("abcd-1234"), "ABCD-1234")
        self.assertEqual(action_for_native_channel_path(action)["path"], "native-channel-lookup")

    def test_direct_payload_contains_proof_only(self):
        action = parse_join_input(self.KEY)
        payload = action.relay_payload(b"server-challenge")
        encoded = json.dumps(payload)
        self.assertEqual(payload["kind"], "direct-key-proof")
        self.assertNotIn(self.KEY.lower(), encoded.lower())
        self.assertNotIn(self.KEY.lower(), "https://relay.invalid/join/" + encoded.lower())

    def test_invite_payload_does_not_contain_key(self):
        action = parse_join_input("ABCD-1234")
        payload = action.relay_payload(b"ignored")
        self.assertEqual(payload, {"kind": "invite", "locator": "ABCD-1234"})
        self.assertNotIn(self.KEY.lower(), json.dumps(payload).lower())

    def test_no_second_history_or_message_engine(self):
        action = parse_join_input(self.KEY)
        native = action_for_native_channel_path(action)
        self.assertEqual(native["path"], "native-channel-lookup")
        self.assertEqual(native["key_bytes"], bytes.fromhex(self.KEY))

    def test_challenge_bound_proof_changes(self):
        action = parse_join_input(self.KEY)
        self.assertNotEqual(
            action.relay_payload(b"one")["proof"],
            action.relay_payload(b"two")["proof"],
        )


if __name__ == "__main__":
    unittest.main()
