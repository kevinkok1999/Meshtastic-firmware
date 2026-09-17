#pragma once

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XR)

#include "XRAdaptiveCoordinator.h"
#include "XRDeferredPacketQueue.h"
#include "XRDeferredPacketStore.h"
#include "XRDeliveryEvents.h"
#include "concurrency/OSThread.h"
#include "mesh/RadioTxHook.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace meshoffgrid::xr {

// ESP-NOW is an infrastructure-free 2.4 GHz sidecar. It carries the same
// already-encrypted Meshtastic MeshPacket that would travel over LoRa. The
// normal Router therefore remains responsible for decryption, duplicate
// suppression, ACK/NAK semantics, channels and application identity.
//
// XR also keeps a bounded encrypted recovery spool. A local DM only enters that
// persistent spool after normal reliable LoRa delivery actually fails. When the
// destination later comes into ESP-NOW range, the same packet ID/ciphertext can
// be offered again automatically.
class XREspNowTransport final : public concurrency::OSThread, public RadioTxHook, public XRDeliveryEventSink
{
  public:
    XREspNowTransport();
    ~XREspNowTransport() override;

    RadioTxHook::PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *packet) override;
    void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *packet) override;

    void onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs) override;
    void onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs) override;
    void onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs) override;

    bool ready() const { return initialized_; }
    uint8_t peerCount() const;
    uint8_t linkScoreFor(uint32_t nodeNum) const;
    size_t deferredCount() const { return deferred_.size(); }

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
    static constexpr uint32_t OUTBOUND_CACHE_TTL_MS = 30u * 60u * 1000u;
    static constexpr size_t OUTBOUND_CACHE_SIZE = 16;

    enum class FrameType : uint8_t { HELLO = 1, DATA = 2 };
    enum class DeliveryEventType : uint8_t { FAILED = 0, ACKED, NAKED };

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

    struct DeliveryEvent {
        DeliveryEventType type = DeliveryEventType::FAILED;
        uint32_t peer = 0;
        uint32_t packetId = 0;
        uint32_t timeMs = 0;
    };

    struct CachedOutbound {
        bool used = false;
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        uint32_t cachedAtMs = 0;
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

    bool initialized_ = false;
    bool recoveryLoaded_ = false;
    uint32_t lastHelloMs_ = 0;
    QueueHandle_t rxQueue_ = nullptr;
    QueueHandle_t txQueue_ = nullptr;
    QueueHandle_t deliveryEventQueue_ = nullptr;

    std::array<CachedOutbound, OUTBOUND_CACHE_SIZE> outboundCache_{};
    std::array<Peer, MAX_PEERS> peers_{};
    std::array<Reassembly, MAX_REASSEMBLY> reassembly_{};

    XRDeferredPacketQueue deferred_{};
    XRDeferredPacketStore deferredStore_{"/prefs/xr_espnow_deferred.bin", "/prefs/xr_espnow_deferred.tmp"};
    XRAdaptiveCoordinator coordinator_{};

    bool initialize();
    void shutdown();
    void drainTxQueue(uint32_t nowMs, bool allowRadio);
    void processDeliveryEvents(uint32_t nowMs);
    void processDeferred(uint32_t nowMs);
    void sendHello(uint32_t nowMs);
    void processRx(const RxFrame &frame, uint32_t nowMs);
    void processHello(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs);
    void processData(const FrameHeader &header, const RxFrame &frame, uint32_t nowMs);
    void processTx(const TxPacket &queued, uint32_t nowMs, bool fromDeferred);
    bool sendPacketToPeer(const Peer &peer, const meshtastic_MeshPacket &packet, uint32_t nowMs);
    bool sendFrame(const uint8_t *mac, FrameHeader header, const uint8_t *payload, size_t payloadLength);

    void rememberOutbound(const meshtastic_MeshPacket &packet, uint32_t nowMs);
    CachedOutbound *findCachedOutbound(uint32_t destination, uint32_t packetId);
    void clearCachedOutbound(uint32_t destination, uint32_t packetId);
    void expireOutboundCache(uint32_t nowMs);

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

#endif // ARCH_ESP32 && T_DECK && MESHOFFGRID_ENABLE_XR
