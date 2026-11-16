void WebSocketCompanionServer::drainTermInput(int idx, WebMirror& m) {
  WSClientState* c = &_clients[idx];
  WiFiClient* cl = &c->client;
  int guard = 0;
  while (cl->available() && guard++ < 2048) {
    switch (c->ws_state) {
      case WS_STATE_HEADER_0: {
        const uint8_t b = static_cast<uint8_t>(cl->read());
        c->ws_opcode = b & 0x0f;
        // V30: no fragmented/reserved/unknown application frames.
        if ((b & 0x80) == 0 || (b & 0x70) != 0 ||
            (c->ws_opcode != 0x01 && c->ws_opcode != 0x02 && c->ws_opcode != 0x08)) {
          disconnectClient(idx); return;
        }
        c->ws_state = WS_STATE_HEADER_1;
        break;
      }
      case WS_STATE_HEADER_1: {
        const uint8_t b = static_cast<uint8_t>(cl->read());
        const uint8_t len7 = b & 0x7f;
        if ((b & 0x80) == 0 || len7 == 127 ||
            (c->ws_opcode == 0x08 && (len7 == 126 || len7 == 1))) {
          disconnectClient(idx); return;
        }
        c->ws_payload_read = 0;
        c->comp_rx_len = 0;
        c->ws_payload_len = len7;
        c->ws_state = len7 == 126 ? WS_STATE_LEN_EXT : WS_STATE_MASK;
        break;
      }
      case WS_STATE_LEN_EXT: {
        if (cl->available() < 2) return;
        const uint8_t hi = static_cast<uint8_t>(cl->read());
        const uint8_t lo = static_cast<uint8_t>(cl->read());
        c->ws_payload_len = (static_cast<uint16_t>(hi) << 8) | lo;
        if (c->ws_payload_len < 126 || c->ws_payload_len > MAX_FRAME_SIZE) {
          disconnectClient(idx); return;
        }
        c->ws_state = WS_STATE_MASK;
        break;
      }
      case WS_STATE_MASK: {
        if (c->ws_payload_len > MAX_FRAME_SIZE) { disconnectClient(idx); return; }
        if (cl->available() < 4) return;
        for (int i=0; i<4; ++i) c->ws_mask[i] = static_cast<uint8_t>(cl->read());
        if (c->ws_payload_len == 0) {
          if (c->ws_opcode == 0x08) { disconnectClient(idx); return; }
          c->ws_state = WS_STATE_HEADER_0;
        } else {
          c->ws_state = WS_STATE_PAYLOAD;
        }
        break;
      }
      case WS_STATE_PAYLOAD: {
        if (c->ws_payload_read >= c->ws_payload_len || c->comp_rx_len >= MAX_FRAME_SIZE) {
          disconnectClient(idx); return;
        }
        const uint8_t b = static_cast<uint8_t>(cl->read()) ^ c->ws_mask[c->ws_payload_read % 4];
        c->comp_rx_buf[c->comp_rx_len++] = b;
        ++c->ws_payload_read;
        if (c->ws_payload_read != c->ws_payload_len) break;
        if (c->ws_opcode == 0x08) { disconnectClient(idx); return; }
        uint16_t n = c->comp_rx_len;
        while (n > 0 && (c->comp_rx_buf[n-1] == '\r' || c->comp_rx_buf[n-1] == '\n')) --n;
        // A binary-NUL or embedded line break must not turn a bounded WS message
        // into ambiguous or multiple commands in the legacy text dispatcher.
        for (uint16_t i=0; i<n; ++i) {
          if (c->comp_rx_buf[i] == 0 || c->comp_rx_buf[i] == '\r' || c->comp_rx_buf[i] == '\n') {
            disconnectClient(idx); return;
          }
        }
        c->ws_state = WS_STATE_HEADER_0;
        if (n > 0) m.pushTermCmd(reinterpret_cast<const char*>(c->comp_rx_buf), n);
        break;
      }
      default:
        disconnectClient(idx); return;
    }
  }
}
