#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include "XBeeXr868Link.h"
#include "XBeeApiCodec.h"

#include <algorithm>

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
    maxTxPayload_ = MAX_TX_PAYLOAD_HARD;
    serialHigh_ = 0;
    serialLow_ = 0;
    moduleAddress64_ = 0;

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
    if (serial_ == nullptr || (payload == nullptr && payloadLength != 0) || payloadLength > maxTxPayload_) {
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
    return written == frameLength ? frameId : 0;
}

uint8_t XBeeXr868Link::sendAt(const char command[2], const uint8_t *parameter, size_t parameterLength)
{
    if (serial_ == nullptr || command == nullptr || (parameter == nullptr && parameterLength != 0)) {
        return 0;
    }

    uint8_t frame[1 + 2 + 64 + 1] = {};
    const uint8_t frameId = allocateFrameId();
    const size_t frameLength =
        XBeeApiCodec::buildAtCommand(frame, sizeof(frame), frameId, command, parameter, parameterLength);

    if (frameLength == 0) {
        return 0;
    }

    const size_t written = serial_->write(frame, frameLength);
    return written == frameLength ? frameId : 0;
}

void XBeeXr868Link::queryModuleInfo()
{
    static constexpr char kNp[2] = {'N', 'P'};
    static constexpr char kSh[2] = {'S', 'H'};
    static constexpr char kSl[2] = {'S', 'L'};
    static constexpr char kAp[2] = {'A', 'P'};
    static constexpr char kAo[2] = {'A', 'O'};
    static constexpr char kHv[2] = {'H', 'V'};
    static constexpr char kVr[2] = {'V', 'R'};

    (void)sendAt(kNp);
    (void)sendAt(kSh);
    (void)sendAt(kSl);
    (void)sendAt(kAp);
    (void)sendAt(kAo);
    (void)sendAt(kHv);
    (void)sendAt(kVr);
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

uint32_t XBeeXr868Link::readU32BigEndianVariable(const uint8_t *data, size_t length)
{
    if (data == nullptr || length == 0) {
        return 0;
    }

    const size_t start = length > 4 ? length - 4 : 0;
    uint32_t value = 0;
    for (size_t i = start; i < length; ++i) {
        value = (value << 8) | static_cast<uint32_t>(data[i]);
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

    case FRAME_AT_RESPONSE:
        // type(1) + frameId(1) + command(2) + status(1) + value(N)
        if (frameDataLength >= 5) {
            const uint8_t frameId = frameData[1];
            const char command0 = static_cast<char>(frameData[2]);
            const char command1 = static_cast<char>(frameData[3]);
            const uint8_t status = frameData[4];
            const uint8_t *value = frameDataLength > 5 ? &frameData[5] : nullptr;
            const size_t valueLength = frameDataLength > 5 ? frameDataLength - 5 : 0;

            if (status == 0) {
                if (command0 == 'N' && command1 == 'P' && valueLength > 0) {
                    const uint32_t np = readU32BigEndianVariable(value, valueLength);
                    if (np > 0) {
                        maxTxPayload_ = std::min<size_t>(MAX_TX_PAYLOAD_HARD, np);
                    }
                } else if (command0 == 'S' && command1 == 'H' && valueLength > 0) {
                    serialHigh_ = readU32BigEndianVariable(value, valueLength);
                    moduleAddress64_ = (static_cast<uint64_t>(serialHigh_) << 32) | serialLow_;
                } else if (command0 == 'S' && command1 == 'L' && valueLength > 0) {
                    serialLow_ = readU32BigEndianVariable(value, valueLength);
                    moduleAddress64_ = (static_cast<uint64_t>(serialHigh_) << 32) | serialLow_;
                }
            }

            if (atResponseCallback_ != nullptr) {
                atResponseCallback_(frameId, command0, command1, status, value, valueLength);
            }
        }
        break;

    case FRAME_MODEM_STATUS:
        if (frameDataLength >= 2 && modemStatusCallback_ != nullptr) {
            modemStatusCallback_(frameData[1]);
        }
        break;

    default:
        break;
    }
}

} // namespace meshoffgrid::xbee

#endif // ARCH_ESP32 && MESHOFFGRID_ENABLE_XBEE_XR868
