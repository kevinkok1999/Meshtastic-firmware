#pragma once

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XR)

#include "XRAdaptiveCoordinator.h"
#include "concurrency/OSThread.h"
#include "mesh/RadioTxHook.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace meshoffgrid::xr {

// ESP-NOW is a short-range, infrastructure-free sidecar transport. It never
// replaces the Meshtastic packet format: it carries the same encrypted
// MeshPacket that is about to go over LoRa. The Router therefore keeps normal
// duplicate suppression, ACK semantics, channels and chat identity.
class XREspNowTransport final : public concurrency::OSThread, public RadioTxHook {
  public:
    XREspNowTransport();
    ~XREspNowTransport() override;

    RadioTxHook::PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *packet) override;
    void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *packet) override;

    bool ready() const { return initialized_; }
    uint8_t peerCount() const;
    uint8_t linkScoreFor(uint32_t nodeNum) const;

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint16_t WIRE_MAGIC = 0x5852; // "XR"
    static constexpr uint8_t WIRE_VERSION = 1;
    static constexpr size_t ESP_NOW_SAFE_FRAME = 240;
    static constexpr size_t FRAGMENT_BYTES = 192;
    static constexpr size_t MAX_PACKET_BYTES = meshtastic_MeshPacket_size;
    static constexpr uint8_t MAX_FRAGMENTS = 3;
    static constexpr uint8_t MAX_PEERS = 16;
    static constexpr uint8_t MAX_REASSEMBLY = 4;
    static constexpr uint32_t PEER_FRESH_MS = 90u * 1000u;
    static constexpr uint32_t HELLO_INTERVAL_MS = 30u * 1000u;
    static constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 10u * 1000u;

    enum class FrameType : uint8_t { HELLO = 1, DATA = 2 };

#pragma pack(push, 1)
    struct FrameHeader {
        uint16_t magic = WIRE_MAGIC;
        uint8_t version = WIRE_VERSION;
        FrameType type = FrameType::HELLO;
        uint32_t fromNode = 0;
        uint32_t packetId = 0;
        uint16_t totalLength = 0;
        uint16_t fragmentOffset = 0;
        uint8_t fragmentIndex = 0;
        uint8_t fragmentCount = 0;
        uint16_t fragmentLength = 0;
        uint32_t checksum = 0;
    };
#pragma pack(pop)

    struct RxFrame {
        uint8_t mac[6]{};
        int8_t rssi = -127;
        uint16_t length = 0;
        uint8_t bytes[ESP_NOW_SAFE_FRAME]{};
    };

    struct TxPacket {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    };

    struct Peer {
        bool used = false;
        uint32_t nodeNum = 0;
        uint8_t mac[6]{};
        int16_t rssiEwma = -100;
        uint16_t sends = 0;
        uint16_t sendFailures = 0;
        uint32_t lastSeenMs = 0;
    };

    struct Reassembly {
        bool used = false;
        uint32_t fromNode = 0;
        uint32_t packetId = 0;
        uint8_t mac[6]{};
        uint16_t totalLength = 0;
        uint8_t fragmentCount = 0;
        uint8_t receivedMask = 0;
        uint32_t updatedMs = 0;
        std::array<uint8_t, MAX_PACKET_BYTES> data{};
    };

    std::atomic<bool> initialized_{false};
    bool initAttempted_ = false;
    uint32_t lastHelloMs_ = 0;
    QueueHandle_t rxQueue_ = nullptr;
    QueueHandle_t txQueue_ = nullptr;
    std::array<Peer, MAX_PEERS> peers_{};
    std::array<Reassembly, MAX_REASSEMBLY> reassembly_{};
    XRAdaptiveCoordinator coordinator_{XRAdaptivePolicy{}, "/prefs/xr_ai_espnow.bin", "/prefs/xr_ai_espnow.tmp"};

    bool initialize();
    void shutdown();
    void sendHello(uint32_t nowMs);
    void processRx(const RxFrame &frame, uint32_t nowMs);
    void processHello(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs);
    void processData(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs);
    void processTx(const TxPacket &queued, uint32_t nowMs);
    bool sendPacketToPeer(const Peer &peer, const meshtastic_MeshPacket &packet, uint32_t nowMs);
    bool sendFrame(const uint8_t *mac, FrameHeader header, const uint8_t *payload, size_t payloadLength);
    Peer *findPeer(uint32_t nodeNum);
    const Peer *findPeer(uint32_t nodeNum) const;
    Peer &rememberPeer(uint32_t nodeNum, const uint8_t mac[6], int8_t rssi, uint32_t nowMs);
    Reassembly &getReassembly(const FrameHeader &header, const uint8_t mac[6], uint32_t nowMs);
    void expireState(uint32_t nowMs);
    bool shouldMirror(const meshtastic_MeshPacket &packet, const Peer &peer, uint32_t nowMs);

    static uint32_t checksum32(const uint8_t *data, size_t length);
    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length);
    static XREspNowTransport *instance_;
};

extern XREspNowTransport *xrEspNowTransport;

} // namespace meshoffgrid::xr

#endif // ARCH_ESP32 && T_DECK
