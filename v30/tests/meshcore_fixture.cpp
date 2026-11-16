// Excerpt copied from ALLFATHER-BV/meshcomod src/Mesh.cpp
// immutable source commit: edd7ed47f42acf6c64c9fcac551050fdbd531a09
case PAYLOAD_TYPE_RAW_CUSTOM: {
  if (pkt->isRouteDirect() && !_tables->wasSeen(pkt)) {
    _tables->markSeen(pkt);
    onRawDataRecv(pkt);
  }
  break;
}
