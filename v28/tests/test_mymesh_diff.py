#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import sys

def die(msg: str) -> None:
    raise SystemExit("V28 MyMesh diff failure: " + msg)

def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        die(label + " anchor mismatch")
    return text.replace(old, new, 1)

def main() -> None:
    if len(sys.argv) != 3:
        die("usage: test_mymesh_diff.py <before> <after>")
    before = pathlib.Path(sys.argv[1]).read_text()
    after = pathlib.Path(sys.argv[2]).read_text()

    old_dm = """#if defined(MESH_OFFGRIDNL_V27)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif
"""
    new_dm = """#if defined(MESH_OFFGRIDNL_V27) && !defined(MESH_OFFGRIDNL_V28)
  if (attempt == 0 && recipient.type == ADV_TYPE_CHAT &&
      v11_global_bridge.tryGlobalFirstDM(recipient, timestamp, text)) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }
#endif
"""
    expected = replace_once(before, old_dm, new_dm, "DM route")

    old_group = """void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  uiTrackSentFp(txtFloodFp(pkt));
#if defined(MESH_OFFGRIDNL_V27)
  if (pkt && pkt->getPayloadType() == PAYLOAD_TYPE_GRP_TXT) {
    v11_global_bridge.mirrorChannelPacket(channel, pkt);
  }
#endif
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, floodPathHashSize());  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);   // the lower overload applies path_hash_mode
  }
}
"""
    new_group = """void MyMesh::sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis) {
  uiTrackSentFp(txtFloodFp(pkt));
  // TODO: have per-channel send_scope
  if (send_unscoped) {
    sendFlood(pkt, delay_millis, floodPathHashSize());  // app has explicitly requested un-scoped
  } else {
    TransportKey default_scope;
    memcpy(&default_scope.key, _prefs.default_scope_key, sizeof(default_scope.key));

    auto scope = send_scope.isNull() ? &default_scope : &send_scope;
    sendFloodScoped(*scope, pkt, delay_millis);   // the lower overload applies path_hash_mode
  }
#if defined(MESH_OFFGRIDNL_V27)
  // V28 policy: RF was attempted above. The encrypted Internet copy is route 2.
  if (pkt && pkt->getPayloadType() == PAYLOAD_TYPE_GRP_TXT) {
    v11_global_bridge.mirrorChannelPacket(channel, pkt);
  }
#endif
}
"""
    expected = replace_once(expected, old_group, new_group, "group route")

    old_mirror = """      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text,
                                       result != MSG_SEND_FAILED)
          : false;"""
    new_mirror = """      (attempt == 0 && recipient.type == ADV_TYPE_CHAT)
          ? v11_global_bridge.mirrorDM(recipient, timestamp, text, true)
          : false;"""
    expected = replace_once(expected, old_mirror, new_mirror, "independent Internet route")

    old_truth = """  if (result == MSG_SEND_FAILED && v11_global_ok) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }"""
    new_truth = """  if (result == MSG_SEND_FAILED && v11_global_ok &&
      v11_global_bridge.connected()) {
    expected_ack = 0;
    est_timeout = 0;
    if (out_packet_hash4) *out_packet_hash4 = 0;
    return MSG_SEND_SENT_DIRECT;
  }"""
    expected = replace_once(expected, old_truth, new_truth, "truthful Internet status")

    if expected != after:
        die("MyMesh changed outside the four approved V28 routing edits")

    print("V28 MyMesh exact-diff contract PASS (four bounded routing edits)")

if __name__ == "__main__":
    main()
