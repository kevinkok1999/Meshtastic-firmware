#include "XBeeXr868Link.h"

namespace ops::v8 {

bool XBeeXr868Link::begin(HardwareSerial& serial, uint32_t baud, int8_t rxPin, int8_t txPin) {
    if (rxPin < 0 || txPin < 0 || rxPin == txPin) return false;
    _serial = &serial;
    resetParser();
    _nextFrameId = 1;
    _serial->begin(baud, SERIAL_8N1, rxPin, txPin);
    return true;
}

void XBeeXr868Link::end() {
    if (_serial) _serial->end();
    _serial = nullptr;
    resetParser();
}

void XBeeXr868Link::poll() {
    if (!_serial) return;
    while (_serial->available() > 0) {
        const int v = _serial->read();
        if (v >= 0) consume(static_cast<uint8_t>(v));
    }
}

uint8_t XBeeXr868Link::allocateFrameId() {
    const uint8_t id = _nextFrameId++;
    if (_nextFrameId == 0) _nextFrameId = 1;
    return id;
}

uint8_t XBeeXr868Link::send(uint64_t destination64, const uint8_t* payload, size_t payloadLength,
                            uint8_t transmitOptions, uint8_t broadcastRadius) {
    if (!_serial || (!payload && payloadLength != 0) || payloadLength > MAX_TX_PAYLOAD) return 0;
    uint8_t frame[1 + 2 + MAX_FRAME_DATA + 1] = {};
    const uint8_t id = allocateFrameId();
    const size_t n = XBeeApiCodec::buildTransmitRequest(frame, sizeof(frame), id, destination64,
                                                         payload, payloadLength, broadcastRadius,
                                                         transmitOptions);
    if (!n || _serial->write(frame, n) != n) return 0;
    return id;
}

void XBeeXr868Link::resetParser() {
    _state = ParseState::WaitStart;
    _expected = 0;
    _received = 0;
}

void XBeeXr868Link::consume(uint8_t byte) {
    switch (_state) {
        case ParseState::WaitStart:
            if (byte == XBEE_API_START) _state = ParseState::LengthMsb;
            break;
        case ParseState::LengthMsb:
            _expected = static_cast<uint16_t>(byte) << 8;
            _state = ParseState::LengthLsb;
            break;
        case ParseState::LengthLsb:
            _expected |= byte;
            if (_expected == 0 || _expected > MAX_FRAME_DATA) resetParser();
            else _state = ParseState::FrameData;
            break;
        case ParseState::FrameData:
            _data[_received++] = byte;
            if (_received >= _expected) _state = ParseState::Checksum;
            break;
        case ParseState::Checksum:
            if (XBeeApiCodec::checksumValid(_data, _expected, byte)) dispatch();
            resetParser();
            break;
    }
}

uint64_t XBeeXr868Link::readU64(const uint8_t* p) {
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

void XBeeXr868Link::dispatch() {
    if (_expected == 0) return;
    switch (_data[0]) {
        case XBEE_FRAME_RX_PACKET:
            if (_expected >= 12 && _receive) {
                _receive(readU64(&_data[1]), &_data[12], _expected - 12, _data[11]);
            }
            break;
        case XBEE_FRAME_TX_STATUS:
            if (_expected >= 7 && _txStatus) {
                _txStatus(_data[1], _data[5], _data[4], _data[6]);
            }
            break;
        default:
            break;
    }
}

} // namespace ops::v8
