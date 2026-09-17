#pragma once

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XR) && defined(MESHOFFGRID_ENABLE_XBEE_XR868) && \
    defined(MESHOFFGRID_XBEE_RX_PIN) && defined(MESHOFFGRID_XBEE_TX_PIN)

#include "XRAdaptiveCoordinator.h"
#include "XRDeferredPacketQueue.h"
#include "XRDeferredPacketStore.h"
#include "XRDeliveryEvents.h"
#include "XRRfCoexistence.h"
#include "concurrency/OSThread.h"
#include "mesh/RadioTxHook.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/xbee/XBeeXr868Link.h"

#include <HardwareSerial.h>
#include <array>
#include <cstddef>
#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace meshoffgrid::xr {

class XRXBeeTransport final : public concurrency::OSThread, public RadioTxHook, public XRDeliveryEventSink
{
  public:
    XRXBeeTransport();
    ~XRXBeeTransport() override;

    RadioTxHook::PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *packet) override;
    void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *packet) override;

    void onReliableDeliveryFailed(uint32_t destination, uint32_t packetId, uint32_t nowMs) override;
    void onReliableDeliveryAcked(uint32_t peer, uint32_t packetId, uint32_t nowMs) override;
    void onReliableDeliveryNaked(uint32_t peer, uint32_t packetId, uint32_t nowMs) override;


    bool ready() const { return initialized_; }
    uint8_t peerCount() const;
    uint8_t linkScoreFor(uint32_t nodeNum) const;
    uint64_t localXBeeAddress() const { return link_.moduleAddress64(); }
    size_t deferredCount() const { return deferred_.size(); }

  protected:
    int32_t runOnce() override;

  private:
#ifndef MESHOFFGRID_XBEE_BAUD
    static constexpr uint32_t XBEE_BAUD = 115200;
#else
    static constexpr uint32_t XBEE_BAUD = MESHOFFGRID_XBEE_BAUD;
#endif

    static constexpr uint16_t WIRE_MAGIC = 0x5842; // "XB"
    static constexpr uint8_t WIRE_VERSION = 1;
    static constexpr uint8_t MAX_PEERS = 24;
    static constexpr uint8_t MAX_REASSEMBLY = 4;
    static constexpr uint8_t MAX_FRAGMENTS = 16;
    static constexpr uint32_t PEER_FRESH_MS = 180u * 1000u;
    static constexpr uint32_t HELLO_INTERVAL_MS = 45u * 1000u;
    static constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 20u * 1000u;
    static constexpr uint32_t TX_STATUS_TIMEOUT_MS = 4000u;
    static constexpr uint32_t SERVICE_INTERVAL_MS = 20u;
    static constexpr UBaseType_t DELIVERY_EVENT_QUEUE_DEPTH = 8;

    enum class FrameType : uint8_t { HELLO = 1, DATA = 2 };
    enum class DeliveryEventType : uint8_t { Failed = 1, Acked = 2, Naked = 3 };

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
        uint8_t fragmentLength = 0;
        uint32_t checksum = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(FrameHeader) < meshoffgrid::xbee::XBeeXr868Link::MAX_TX_PAYLOAD_HARD,
                  "XBee carrier header must fit inside one XR 868 RF payload");

    struct TxPacket {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        bool ackExpected = false;
    };

    struct PendingMirror {
        bool valid = false;
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        bool ackExpected = false;
    };

    struct DeliveryEvent {
        DeliveryEventType type = DeliveryEventType::Failed;
        uint32_t peer = 0;
        uint32_t packetId = 0;
        uint32_t whenMs = 0;
    };

    struct CachedOutbound {
        bool used = false;
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        uint32_t cachedAtMs = 0;
    };

    struct Peer {
        bool used = false;
        uint32_t nodeNum = 0;
        uint64_t address64 = 0;
        uint32_t lastSeenMs = 0;
        uint16_t packetsStarted = 0;
        uint16_t fragmentsOk = 0;
        uint16_t fragmentsFailed = 0;
    };

    struct Reassembly {
        bool used = false;
        uint32_t fromNode = 0;
        uint32_t packetId = 0;
        uint64_t source64 = 0;
        uint16_t totalLength = 0;
        uint8_t fragmentCount = 0;
        uint16_t receivedMask = 0;
        uint32_t checksum = 0;
        uint32_t updatedMs = 0;
        std::array<uint8_t, meshtastic_MeshPacket_size> data{};
    };

    struct ActiveTx {
        bool used = false;
        uint32_t nodeNum = 0;
        uint64_t destination64 = 0;
        uint32_t packetId = 0;
        uint16_t totalLength = 0;
        uint32_t checksum = 0;
        uint8_t fragmentCount = 0;
        uint8_t nextFragment = 0;
        uint8_t waitingFrameId = 0;
        uint32_t waitingSinceMs = 0;
        uint16_t fragmentPayloadBytes = 0;
        bool fromDeferredQueue = false;
        std::array<uint8_t, meshtastic_MeshPacket_size> encoded{};
    };

    HardwareSerial serial_{2};
    meshoffgrid::xbee::XBeeXr868Link link_{};
    QueueHandle_t txQueue_ = nullptr;
    QueueHandle_t deliveryEventQueue_ = nullptr;
    bool initialized_ = false;
    bool initAttempted_ = false;
    uint32_t lastHelloMs_ = 0;
    uint32_t lastInfoQueryMs_ = 0;

    PendingMirror mirrorCandidate_{};
    static constexpr size_t OUTBOUND_CACHE_SIZE = 16;
    static constexpr uint32_t OUTBOUND_CACHE_TTL_MS = 30u * 60u * 1000u;
    std::array<CachedOutbound, OUTBOUND_CACHE_SIZE> outboundCache_{};
    std::array<Peer, MAX_PEERS> peers_{};
    std::array<Reassembly, MAX_REASSEMBLY> reassembly_{};
    ActiveTx activeTx_{};
    XRDeferredPacketQueue deferred_{};
    XRDeferredPacketStore deferredStore_{};

    XRRfCoexistence coexistence_{};
    XRAdaptiveCoordinator coordinator_{XRAdaptivePolicy{}, "/prefs/xr_ai_xbee.bin", "/prefs/xr_ai_xbee.tmp"};

    bool initialize();
    void shutdown();
    void drainDeliveryEvents(uint32_t nowMs);
    void handleDeliveryEvent(const DeliveryEvent &event, uint32_t nowMs);
    bool enqueueDeliveryEvent(DeliveryEventType type, uint32_t peer, uint32_t packetId, uint32_t whenMs);
    void sendHello(uint32_t nowMs);
    void serviceOutgoing(uint32_t nowMs);
    bool prepareActiveTx(const meshtastic_MeshPacket &packet, uint32_t nowMs, bool fromDeferredQueue = false);
    void rememberOutbound(const meshtastic_MeshPacket &packet, uint32_t nowMs);
    CachedOutbound *findCachedOutbound(uint32_t destination, uint32_t packetId);
    void clearCachedOutbound(uint32_t destination, uint32_t packetId);
    void expireOutboundCache(uint32_t nowMs);
    void sendNextFragment(uint32_t nowMs);
    void finishActiveTx(bool success, uint32_t nowMs);

    void processRx(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t receiveOptions, uint32_t nowMs);
    void processHello(const FrameHeader &header, uint64_t source64, uint32_t nowMs);
    void processData(const FrameHeader &header, uint64_t source64, const uint8_t *payload, size_t payloadLength,
                     uint32_t nowMs);

    Peer *findPeer(uint32_t nodeNum);
    const Peer *findPeer(uint32_t nodeNum) const;
    Peer *findPeerByAddress(uint64_t address64);
    Peer &rememberPeer(uint32_t nodeNum, uint64_t address64, uint32_t nowMs);
    Reassembly &getReassembly(const FrameHeader &header, uint64_t source64, uint32_t nowMs);
    void expireState(uint32_t nowMs);

    bool eligibleForMirror(const meshtastic_MeshPacket &packet) const;
    uint16_t fragmentBudget() const;

    static uint32_t checksum32(const uint8_t *data, size_t length);

    static void onReceiveStatic(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t receiveOptions);
    static void onTxStatusStatic(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus);
    static void onModemStatusStatic(uint8_t status);
    static void onAtResponseStatic(uint8_t frameId, char command0, char command1, uint8_t status, const uint8_t *value,
                                   size_t valueLength);

    void onTxStatus(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus);
    void onModemStatus(uint8_t status);
    void onAtResponse(uint8_t frameId, char command0, char command1, uint8_t status, const uint8_t *value,
                      size_t valueLength);

    static XRXBeeTransport *instance_;
};

extern XRXBeeTransport *xrXBeeTransport;

} // namespace meshoffgrid::xr

#endif
