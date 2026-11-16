"""V30 direct private-channel join contract."""
from __future__ import annotations
from dataclasses import dataclass, field
import hashlib, hmac, re
from typing import Literal
_INVITE_RE=re.compile(r"^[A-HJ-NP-Z2-9]{8}$"); _HEX_RE=re.compile(r"^[0-9a-fA-F]{32}$")
_NATIVE_CAPABILITY_DOMAIN=b"MOG27-CH-INBOX"
class JoinInputError(ValueError): pass
@dataclass(frozen=True, repr=False)
class JoinAction:
    kind: Literal["direct_key","invite"]; label: str="Private"
    key_bytes: bytes|None=field(default=None,repr=False); invite_locator: str|None=None
    def __repr__(self): return f"JoinAction(kind={self.kind!r}, label={self.label!r}, invite_locator={self.invite_locator!r})"
    @property
    def channel_capability(self): return None if self.key_bytes is None else hmac.new(self.key_bytes,_NATIVE_CAPABILITY_DOMAIN,hashlib.sha256).digest()[:16]
    def relay_payload(self, challenge=None):
        if self.kind=="invite": return {"kind":"invite","locator":self.invite_locator}
        raise JoinInputError("direct key is local-only; use the existing native channel path")
@dataclass(frozen=True, repr=False)
class NativeChannelRequest:
    path: Literal["native-channel-lookup"]="native-channel-lookup"
    capability: bytes|None=field(default=None,repr=False); key_bytes: bytes|None=field(default=None,repr=False); locator: str|None=None
    def __repr__(self): return f"NativeChannelRequest(path={self.path!r}, locator={self.locator!r})"
def normalize_channel_key(value: str)->str:
    if not isinstance(value,str): raise TypeError("channel key must be text")
    c=value.strip()
    if not _HEX_RE.fullmatch(c): raise JoinInputError("private channel key must be exactly 32 hexadecimal characters")
    return c.lower()
def channel_key_bytes(value: str)->bytes: return bytes.fromhex(normalize_channel_key(value))
def normalize_invite_locator(value: str)->str:
    if not isinstance(value,str): raise TypeError("invite locator must be text")
    c="".join(value.strip().upper().split()).replace("-","")
    if not _INVITE_RE.fullmatch(c): raise JoinInputError("invite locator must be 8 chars from the V28 alphabet")
    return c[:4]+"-"+c[4:]
def parse_join_input(value: str, *, label: str="Private")->JoinAction:
    if not isinstance(label,str): raise TypeError("label must be text")
    label=label.strip() or "Private"
    if len(label.encode())>48: raise JoinInputError("label is too long")
    if not isinstance(value,str): raise TypeError("join input must be text")
    c="".join(value.strip().upper().split()).replace("-","")
    if _INVITE_RE.fullmatch(c): return JoinAction("invite",label,invite_locator=normalize_invite_locator(value))
    return JoinAction("direct_key",label,key_bytes=channel_key_bytes(value))
def parse_direct_key(value: str)->JoinAction:
    a=parse_join_input(value)
    if a.kind!="direct_key": raise JoinInputError("short invite locators are not direct keys")
    return a
def action_for_native_channel_path(action: JoinAction)->NativeChannelRequest:
    if action.kind=="invite": return NativeChannelRequest(locator=action.invite_locator)
    return NativeChannelRequest(capability=action.channel_capability,key_bytes=action.key_bytes)
def contains_secret(text: str, action: JoinAction)->bool:
    return action.key_bytes is not None and action.key_bytes.hex() in text.lower()
