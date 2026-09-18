#pragma once

#if defined(ARCH_ESP32) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include <Arduino.h>
#include <cstddef>
#include <cstdint>

namespace meshoffgrid::xbee {

class XBeeXr868Link
{
  public:
    // Digi XR 868 supports up to 256 bytes of application payload in API mode.
    // NP is still queried at runtime and may lower this limit for the current
    // radio configuration, so the transport never exceeds the module's answer.
    static constexpr size_t MAX_TX_PAYLOAD_HARD = 256;
    static constexpr size_t MAX_FRAME_DATA = 512;

    using ReceiveCallback = void (*)(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t receiveOptions);
    using TxStatusCallback = void (*)(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus);
    using ModemStatusCallback = void (*)(uint8_t status);
    using AtResponseCallback =
        void (*)(uint8_t frameId, char command0, char command1, uint8_t status, const uint8_t *value, size_t valueLength);

    bool begin(HardwareSerial &serial, uint32_t baud, int8_t rxPin, int8_t txPin);
    void end();

    // Call frequently from a normal task/thread. Never call from an ISR.
    void poll();

    // Returns a non-zero frame ID when the API frame was queued to UART.
    // Delivery success/failure arrives asynchronously through TxStatusCallback.
    uint8_t send(uint64_t destination64, const uint8_t *payload, size_t payloadLength, uint8_t transmitOptions = 0,
                 uint8_t broadcastRadius = 0);

    // Local AT query/write using API frame 0x08. parameterLength=0 performs a read.
    uint8_t sendAt(const char command[2], const uint8_t *parameter = nullptr, size_t parameterLength = 0);

    // Non-blocking discovery of the values needed by the higher-level transport.
    // Responses arrive through 0x88 and update NP/SH/SL automatically.
    void queryModuleInfo();

    void onReceive(ReceiveCallback callback) { receiveCallback_ = callback; }
    void onTxStatus(TxStatusCallback callback) { txStatusCallback_ = callback; }
    void onModemStatus(ModemStatusCallback callback) { modemStatusCallback_ = callback; }
    void onAtResponse(AtResponseCallback callback) { atResponseCallback_ = callback; }

    bool isReady() const { return serial_ != nullptr; }
    size_t maxTxPayload() const { return maxTxPayload_; }
    uint64_t moduleAddress64() const { return moduleAddress64_; }

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
    size_t maxTxPayload_ = MAX_TX_PAYLOAD_HARD;
    uint32_t serialHigh_ = 0;
    uint32_t serialLow_ = 0;
    uint64_t moduleAddress64_ = 0;

    ReceiveCallback receiveCallback_ = nullptr;
    TxStatusCallback txStatusCallback_ = nullptr;
    ModemStatusCallback modemStatusCallback_ = nullptr;
    AtResponseCallback atResponseCallback_ = nullptr;

    uint32_t validFrames_ = 0;
    uint32_t checksumErrors_ = 0;
    uint32_t oversizeFrames_ = 0;

    void resetParser();
    void consume(uint8_t byte);
    void dispatchFrame(const uint8_t *frameData, size_t frameDataLength);
    uint8_t allocateFrameId();

    static uint64_t readU64BigEndian(const uint8_t *data);
    static uint32_t readU32BigEndianVariable(const uint8_t *data, size_t length);
};

} // namespace meshoffgrid::xbee

#endif // ARCH_ESP32 && MESHOFFGRID_ENABLE_XBEE_XR868
