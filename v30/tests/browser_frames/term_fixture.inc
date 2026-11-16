void WebSocketCompanionServer::drainTermInput(int idx, WebMirror& m) {
  WSClientState* c = &_clients[idx];
  WiFiClient* cl = &c->client;
  int guard = 0;
  while (cl->available() && guard++ < 2048) {
    switch (c->ws_state) {
      case WS_STATE_HEADER_0: { uint8_t b = (uint8_t)cl->read(); c->ws_opcode = b & 0x0F; c->ws_state = WS_STATE_HEADER_1; break; }
      case WS_STATE_HEADER_1: {
        uint8_t b = (uint8_t)cl->read(); uint8_t l7 = b & 0x7F; c->ws_payload_read = 0; c->comp_rx_len = 0;
        if (l7 == 126 || l7 == 127) { c->ws_payload_len = l7; c->ws_state = WS_STATE_LEN_EXT; }
        else                        { c->ws_payload_len = l7; c->ws_state = WS_STATE_MASK; }
        break;
      }
      case WS_STATE_LEN_EXT: {
        if (c->ws_payload_len == 126) { if (cl->available() < 2) return; uint8_t hi = (uint8_t)cl->read(), lo = (uint8_t)cl->read(); c->ws_payload_len = ((uint16_t)hi << 8) | lo; }
        else                          { if (cl->available() < 8) return; c->ws_payload_len = 0; for (int i = 0; i < 8; i++) c->ws_payload_len = (c->ws_payload_len << 8) | (uint8_t)cl->read(); }
        c->ws_state = (c->ws_payload_len == 0) ? WS_STATE_HEADER_0 : WS_STATE_MASK; break;
      }
      case WS_STATE_MASK: {
        if (cl->available() < 4) return;
        for (int i = 0; i < 4; i++) c->ws_mask[i] = (uint8_t)cl->read();
        c->ws_state = (c->ws_payload_len == 0) ? WS_STATE_HEADER_0 : WS_STATE_PAYLOAD; break;
      }
      default: {  // WS_STATE_PAYLOAD
        uint8_t b = (uint8_t)cl->read() ^ c->ws_mask[c->ws_payload_read % 4];
        if (c->comp_rx_len < MAX_FRAME_SIZE) c->comp_rx_buf[c->comp_rx_len++] = b;
        c->ws_payload_read++;
        if (c->ws_payload_read >= c->ws_payload_len) {
          if ((c->ws_opcode == 0x01 || c->ws_opcode == 0x02) && c->comp_rx_len > 0) {   // text/binary = a command line
            uint16_t n = c->comp_rx_len;
            while (n > 0 && (c->comp_rx_buf[n - 1] == '\r' || c->comp_rx_buf[n - 1] == '\n')) n--;
            if (n > 0) m.pushTermCmd((const char*)c->comp_rx_buf, n);
          } else if (c->ws_opcode == 0x08) { disconnectClient(idx); return; }
          c->ws_state = WS_STATE_HEADER_0;
        }
        break;
      }
    }
  }
}
