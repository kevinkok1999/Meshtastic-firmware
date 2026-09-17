#pragma once

#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xbee {

class XBeeXr868Link
{
  public:
    static constexpr size_t MAX_FRAME_DATA = 512;
    static constexpr size_t MAX_TX_PAYLOAD = MAX_FRAME_DATA - 14;

    using ReceiveCallback = void (*)(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t receiveOptions);
    using TxStatusCallback = void (*)(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus);
    using ModemStatusCallback = void (*)(uint8_t status);

    bool begin(HardwareSerial &serial, uint32_t baud, int8_t rxPin, int8_t txPin);
    void end();

    /**
     * Poll the serial stream. Call frequently from the normal device loop/thread.
     * This method never waits for RF delivery.
     */
    void poll();

    /**
     * Send an API Transmit Request (0x10).
     *
     * Returns the allocated non-zero frame ID when queued to UART, or 0 on local failure.
     * Delivery success/failure arrives later through TxStatusCallback.
     */
    uint8_t send(uint64_t destination64, const uint8_t *payload, size_t payloadLength, uint8_t transmitOptions = 0,
                 uint8_t broadcastRadius = 0);

    void onReceive(ReceiveCallback callback) { receiveCallback_ = callback; }
    void onTxStatus(TxStatusCallback callback) { txStatusCallback_ = callback; }
    void onModemStatus(ModemStatusCallback callback) { modemStatusCallback_ = callback; }

    bool isReady() const { return serial_ != nullptr; }

    uint32_t validFrames() const { return validFrames_; }
    uint32_t checksumErrors() const { return checksumErrors_; }
    uint32_t oversizeFrames() const { return oversizeFrames_; }

  private:
    enum class ParseState : uint8_t {
        WaitStart,
        LengthMsb,
        LengthLsb,
        FrameData,
        Checksum,
    };

    HardwareSerial *serial_ = nullptr;

    ParseState parseState_ = ParseState::WaitStart;
    uint16_t expectedLength_ = 0;
    uint16_t receivedLength_ = 0;
    uint8_t frameData_[MAX_FRAME_DATA] = {};

    uint8_t nextFrameId_ = 1;

    ReceiveCallback receiveCallback_ = nullptr;
    TxStatusCallback txStatusCallback_ = nullptr;
    ModemStatusCallback modemStatusCallback_ = nullptr;

    uint32_t validFrames_ = 0;
    uint32_t checksumErrors_ = 0;
    uint32_t oversizeFrames_ = 0;

    void resetParser();
    void consume(uint8_t byte);
    void dispatchFrame(const uint8_t *frameData, size_t frameDataLength);
    uint8_t allocateFrameId();

    static uint64_t readU64BigEndian(const uint8_t *data);
};

} // namespace meshoffgrid::xbee

#endif // ARCH_ESP32 && MESHOFFGRID_ENABLE_XBEE_XR868
