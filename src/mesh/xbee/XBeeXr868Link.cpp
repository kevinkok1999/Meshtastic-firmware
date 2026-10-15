#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include "XBeeXr868Link.h"
#include "XBeeApiCodec.h"

namespace meshoffgrid::xbee {

bool XBeeXr868Link::begin(HardwareSerial &serial, uint32_t baud, int8_t rxPin, int8_t txPin)
{
    if (rxPin < 0 || txPin < 0 || rxPin == txPin) {
        return false;
    }

    serial_ = &serial;
    resetParser();

    validFrames_ = 0;
    checksumErrors_ = 0;
    oversizeFrames_ = 0;
    nextFrameId_ = 1;

    serial_->begin(baud, SERIAL_8N1, rxPin, txPin);
    return true;
}

void XBeeXr868Link::end()
{
    if (serial_ != nullptr) {
        serial_->end();
    }
    serial_ = nullptr;
    resetParser();
}

void XBeeXr868Link::poll()
{
    if (serial_ == nullptr) {
        return;
    }

    while (serial_->available() > 0) {
        const int value = serial_->read();
        if (value >= 0) {
            consume(static_cast<uint8_t>(value));
        }
    }
}

uint8_t XBeeXr868Link::allocateFrameId()
{
    const uint8_t id = nextFrameId_;
    ++nextFrameId_;
    if (nextFrameId_ == 0) {
        nextFrameId_ = 1;
    }
    return id;
}

uint8_t XBeeXr868Link::send(uint64_t destination64, const uint8_t *payload, size_t payloadLength, uint8_t transmitOptions,
                            uint8_t broadcastRadius)
{
    if (serial_ == nullptr || (payload == nullptr && payloadLength != 0) || payloadLength > MAX_TX_PAYLOAD) {
        return 0;
    }

    uint8_t frame[1 + 2 + MAX_FRAME_DATA + 1] = {};
    const uint8_t frameId = allocateFrameId();

    const size_t frameLength =
        XBeeApiCodec::buildTransmitRequest(frame, sizeof(frame), frameId, destination64, payload, payloadLength,
                                           broadcastRadius, transmitOptions);

    if (frameLength == 0) {
        return 0;
    }

    const size_t written = serial_->write(frame, frameLength);
    if (written != frameLength) {
        return 0;
    }

    return frameId;
}

void XBeeXr868Link::resetParser()
{
    parseState_ = ParseState::WaitStart;
    expectedLength_ = 0;
    receivedLength_ = 0;
}

void XBeeXr868Link::consume(uint8_t byte)
{
    switch (parseState_) {
    case ParseState::WaitStart:
        if (byte == API_START_DELIMITER) {
            expectedLength_ = 0;
            receivedLength_ = 0;
            parseState_ = ParseState::LengthMsb;
        }
        break;

    case ParseState::LengthMsb:
        expectedLength_ = static_cast<uint16_t>(byte) << 8;
        parseState_ = ParseState::LengthLsb;
        break;

    case ParseState::LengthLsb:
        expectedLength_ |= byte;

        if (expectedLength_ == 0) {
            resetParser();
        } else if (expectedLength_ > MAX_FRAME_DATA) {
            ++oversizeFrames_;
            resetParser();
        } else {
            parseState_ = ParseState::FrameData;
        }
        break;

    case ParseState::FrameData:
        frameData_[receivedLength_++] = byte;
        if (receivedLength_ >= expectedLength_) {
            parseState_ = ParseState::Checksum;
        }
        break;

    case ParseState::Checksum:
        if (XBeeApiCodec::checksumValid(frameData_, expectedLength_, byte)) {
            ++validFrames_;
            dispatchFrame(frameData_, expectedLength_);
        } else {
            ++checksumErrors_;
        }
        resetParser();
        break;
    }
}

uint64_t XBeeXr868Link::readU64BigEndian(const uint8_t *data)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<uint64_t>(data[i]);
    }
    return value;
}

void XBeeXr868Link::dispatchFrame(const uint8_t *frameData, size_t frameDataLength)
{
    if (frameData == nullptr || frameDataLength == 0) {
        return;
    }

    switch (frameData[0]) {
    case FRAME_RX_PACKET:
        // 0x90 frame data:
        // type(1) + source64(8) + reserved16(2) + options(1) + payload(N)
        if (frameDataLength >= 12 && receiveCallback_ != nullptr) {
            const uint64_t source64 = readU64BigEndian(&frameData[1]);
            const uint8_t options = frameData[11];
            const uint8_t *payload = &frameData[12];
            const size_t payloadLength = frameDataLength - 12;
            receiveCallback_(source64, payload, payloadLength, options);
        }
        break;

    case FRAME_TX_STATUS:
        // 0x8B frame data:
        // type(1) + frameId(1) + reserved16(2) + retryCount(1)
        // + deliveryStatus(1) + discoveryStatus(1)
        if (frameDataLength >= 7 && txStatusCallback_ != nullptr) {
            const uint8_t frameId = frameData[1];
            const uint8_t retryCount = frameData[4];
            const uint8_t deliveryStatus = frameData[5];
            const uint8_t discoveryStatus = frameData[6];
            txStatusCallback_(frameId, deliveryStatus, retryCount, discoveryStatus);
        }
        break;

    case FRAME_MODEM_STATUS:
        if (frameDataLength >= 2 && modemStatusCallback_ != nullptr) {
            modemStatusCallback_(frameData[1]);
        }
        break;

    default:
        // Unknown/unused API frames are intentionally ignored here.
        // Future features (AT responses, route information, node discovery)
        // should get explicit handlers instead of being mixed into the parser.
        break;
    }
}

} // namespace meshoffgrid::xbee

#endif // ARCH_ESP32 && MESHOFFGRID_ENABLE_XBEE_XR868
