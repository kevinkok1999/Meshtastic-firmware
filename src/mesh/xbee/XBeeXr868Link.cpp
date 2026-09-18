#include "XBeeXr868Link.h"

#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include "XBeeApiCodec.h"

namespace meshoffgrid::xbee {

bool XBeeXr868Link::begin(HardwareSerial &serial, uint32_t baud, int8_t rxPin, int8_t txPin)
{
    if (rxPin < 0 || txPin < 0 || rxPin == txPin)
        return false;

    serial_ = &serial;
    resetParser();
    validFrames_ = checksumErrors_ = oversizeFrames_ = 0;
    nextFrameId_ = 1;
    serial_->begin(baud, SERIAL_8N1, rxPin, txPin);
    return true;
}

void XBeeXr868Link::end()
{
    if (serial_)
        serial_->end();
    serial_ = nullptr;
    resetParser();
}

size_t XBeeXr868Link::writeFrame(const uint8_t *frame, size_t frameLength)
{
    if (!serial_ || !frame || !frameLength)
        return 0;
    return serial_->write(frame, frameLength);
}

void XBeeXr868Link::poll()
{
    if (!serial_)
        return;

    while (serial_->available() > 0) {
        const int value = serial_->read();
        if (value >= 0)
            consume(static_cast<uint8_t>(value));
    }
}

uint8_t XBeeXr868Link::allocateFrameId()
{
    const uint8_t id = nextFrameId_++;
    if (nextFrameId_ == 0)
        nextFrameId_ = 1;
    return id;
}

uint8_t XBeeXr868Link::sendAtCommand(char command0, char command1, const uint8_t *value, size_t valueLength)
{
    if (!serial_)
        return 0;

    uint8_t frame[64] = {};
    const uint8_t frameId = allocateFrameId();
    const size_t frameLength =
        XBeeApiCodec::buildAtCommand(frame, sizeof(frame), frameId, command0, command1, value, valueLength);
    if (!frameLength || writeFrame(frame, frameLength) != frameLength)
        return 0;
    return frameId;
}

uint8_t XBeeXr868Link::send(uint64_t destination64, const uint8_t *payload, size_t payloadLength, uint8_t transmitOptions,
                            uint8_t broadcastRadius)
{
    if (!serial_ || (payloadLength && !payload) || payloadLength > MAX_TX_PAYLOAD)
        return 0;

    uint8_t frame[1 + 2 + 14 + MAX_TX_PAYLOAD + 1] = {};
    const uint8_t frameId = allocateFrameId();
    const size_t frameLength =
        XBeeApiCodec::buildTransmitRequest(frame, sizeof(frame), frameId, destination64, payload, payloadLength,
                                           broadcastRadius, transmitOptions);
    if (!frameLength || writeFrame(frame, frameLength) != frameLength)
        return 0;
    return frameId;
}

void XBeeXr868Link::resetParser()
{
    parseState_ = ParseState::WaitStart;
    expectedLength_ = receivedLength_ = 0;
}

void XBeeXr868Link::consume(uint8_t byte)
{
    switch (parseState_) {
    case ParseState::WaitStart:
        if (byte == API_START_DELIMITER) {
            expectedLength_ = receivedLength_ = 0;
            parseState_ = ParseState::LengthMsb;
        }
        break;
    case ParseState::LengthMsb:
        expectedLength_ = static_cast<uint16_t>(byte) << 8;
        parseState_ = ParseState::LengthLsb;
        break;
    case ParseState::LengthLsb:
        expectedLength_ |= byte;
        if (expectedLength_ == 0)
            resetParser();
        else if (expectedLength_ > MAX_FRAME_DATA) {
            ++oversizeFrames_;
            resetParser();
        } else
            parseState_ = ParseState::FrameData;
        break;
    case ParseState::FrameData:
        frameData_[receivedLength_++] = byte;
        if (receivedLength_ >= expectedLength_)
            parseState_ = ParseState::Checksum;
        break;
    case ParseState::Checksum:
        if (XBeeApiCodec::checksumValid(frameData_, expectedLength_, byte)) {
            ++validFrames_;
            dispatchFrame(frameData_, expectedLength_);
        } else
            ++checksumErrors_;
        resetParser();
        break;
    }
}

uint64_t XBeeXr868Link::readU64BigEndian(const uint8_t *data)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i)
        value = (value << 8) | static_cast<uint64_t>(data[i]);
    return value;
}

void XBeeXr868Link::dispatchFrame(const uint8_t *frameData, size_t frameDataLength)
{
    if (!frameData || !frameDataLength)
        return;

    switch (frameData[0]) {
    case FRAME_RX_PACKET:
        if (frameDataLength >= 12 && receiveCallback_) {
            const uint64_t source64 = readU64BigEndian(&frameData[1]);
            receiveCallback_(source64, &frameData[12], frameDataLength - 12, frameData[11]);
        }
        break;
    case FRAME_TX_STATUS:
        if (frameDataLength >= 7 && txStatusCallback_)
            txStatusCallback_(frameData[1], frameData[5], frameData[4], frameData[6]);
        break;
    case FRAME_MODEM_STATUS:
        if (frameDataLength >= 2 && modemStatusCallback_)
            modemStatusCallback_(frameData[1]);
        break;
    case FRAME_AT_RESPONSE:
        if (frameDataLength >= 5 && atResponseCallback_)
            atResponseCallback_(frameData[1], static_cast<char>(frameData[2]), static_cast<char>(frameData[3]),
                                frameData[4], &frameData[5], frameDataLength - 5);
        break;
    default:
        break;
    }
}

} // namespace meshoffgrid::xbee

#endif
