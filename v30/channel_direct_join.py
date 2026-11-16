"""V30 direct private-channel join contract.

This module is a side-effect-free adapter for the existing native channel
store/lookup/history path. It accepts either the legacy XXXX-XXXX locator or
the full 128-bit channel secret. The secret never appears in relay payloads,
URLs, logs, or invite strings.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import hmac
import re
from typing import Literal

_KEY_HEX_LEN = 32
_KEY_BYTES = 16
_INVITE_RE = re.compile(r"^[A-Z0-9]{4}-[A-Z0-9]{4}$")
_HEX_RE = re.compile(r"^[0-9a-fA-F]{32}$")
_CAPABILITY_DOMAIN = b"MeshOffGridNL/V30/channel-capability/v1"
_PROOF_DOMAIN = b"MeshOffGridNL/V30/direct-join-proof/v1"


class JoinInputError(ValueError):
    """Raised when a channel join input is malformed or unsafe."""


@dataclass(frozen=True)
class JoinAction:
    """Intent consumed by the existing native lookup/channel-history path."""

    kind: Literal["direct_key", "invite"]
    label: str = "Private"
    key_bytes: bytes | None = None
    invite_locator: str | None = None

    @property
    def channel_capability(self) -> bytes | None:
        if self.key_bytes is None:
            return None
        return hmac.new(self.key_bytes, _CAPABILITY_DOMAIN, hashlib.sha256).digest()

    def relay_payload(self, challenge: bytes | bytearray | memoryview) -> dict[str, str | int]:
        """Return the only payload allowed to leave the device.

        The full key is deliberately absent. The relay receives a
        challenge-bound proof or the legacy invite locator and must still use
        the existing signed/authenticated request path.
        """
        if self.kind == "invite":
            assert self.invite_locator is not None
            return {"kind": "invite", "locator": self.invite_locator}
        if not isinstance(challenge, (bytes, bytearray, memoryview)):
            raise TypeError("challenge must be bytes-like")
        if not challenge or len(challenge) > 128:
            raise JoinInputError("challenge must be 1..128 bytes")
        assert self.key_bytes is not None
        proof = hmac.new(
            self.key_bytes, _PROOF_DOMAIN + bytes(challenge), hashlib.sha256
        ).hexdigest()
        return {"kind": "direct-key-proof", "proof": proof, "key_bits": 128}


def normalize_channel_key(value: str) -> str:
    """Normalize a full existing key without accepting weaker passphrases."""
    if not isinstance(value, str):
        raise TypeError("channel key must be text")
    candidate = value.strip()
    if not _HEX_RE.fullmatch(candidate):
        raise JoinInputError("private channel key must be exactly 32 hexadecimal characters")
    return candidate.lower()


def channel_key_bytes(value: str) -> bytes:
    return bytes.fromhex(normalize_channel_key(value))


def normalize_invite_locator(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("invite locator must be text")
    candidate = value.strip().upper()
    if not _INVITE_RE.fullmatch(candidate):
        raise JoinInputError("invite locator must match XXXX-XXXX")
    return candidate


def parse_join_input(value: str, *, label: str = "Private") -> JoinAction:
    """Parse direct-key input or preserve the V28 short invite path."""
    if not isinstance(label, str):
        raise TypeError("label must be text")
    clean_label = label.strip() or "Private"
    if len(clean_label.encode("utf-8")) > 48:
        raise JoinInputError("label is too long")
    if not isinstance(value, str):
        raise TypeError("join input must be text")
    candidate = value.strip()
    if _INVITE_RE.fullmatch(candidate.upper()):
        return JoinAction("invite", clean_label, invite_locator=normalize_invite_locator(candidate))
    return JoinAction("direct_key", clean_label, key_bytes=channel_key_bytes(candidate))


def parse_direct_key(value: str) -> JoinAction:
    action = parse_join_input(value)
    if action.kind != "direct_key":
        raise JoinInputError("short invite locators are not direct keys")
    return action


def action_for_native_channel_path(action: JoinAction) -> dict[str, object]:
    """Describe the existing native path without creating a second history."""
    if action.kind == "invite":
        return {"path": "native-channel-lookup", "locator": action.invite_locator}
    assert action.key_bytes is not None
    return {
        "path": "native-channel-lookup",
        "channel_capability": action.channel_capability,
        "key_bytes": action.key_bytes,
    }


def contains_secret(text: str, action: JoinAction) -> bool:
    """Test helper for redacted diagnostics; callers should log only False."""
    if action.key_bytes is None:
        return False
    return action.key_bytes.hex() in text.lower()
