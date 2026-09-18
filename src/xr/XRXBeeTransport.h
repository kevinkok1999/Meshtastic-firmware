#pragma once

#include "configuration.h"

#if defined(ARCH_ESP32) && defined(T_DECK) && defined(MESHOFFGRID_ENABLE_XBEE_XR868)

#include "concurrency/OSThread.h"
#include "mesh/RadioTxHook.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/xbee/XBeeXr868Link.h"

#include <Arduino.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#ifndef MESHOFFGRID_XBEE_UART_NUM
#define MESHOFFGRID_XBEE_UART_NUM 1
#endif
#ifndef MESHOFFGRID_XBEE_RX_PIN
#define MESHOFFGRID_XBEE_RX_PIN -1
#endif
#ifndef MESHOFFGRID_XBEE_TX_PIN
#define MESHOFFGRID_XBEE_TX_PIN -1
#endif
#ifndef MESHOFFGRID_XBEE_BAUD
#define MESHOFFGRID_XBEE_BAUD 115200
#endif

namespace meshoffgrid::xr {

class XRXBeeTransport final : public concurrency::OSThread, public RadioTxHook
{
  public:
    XRXBeeTransport();
    ~XRXBeeTransport() override;

    RadioTxHook::PreTxAction beforeTransmit(RadioInterface *, meshtastic_MeshPacket *) override { return PRETX_SEND; }
    void packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *packet) override;

    // Called by Reliable/NextHop routing only after normal LoRa + ESP-NOW delivery
    // has gone unacknowledged. XBee is therefore a real fallback/bridge route,
    // not a parallel duplicate transport.
    bool queueFallback(const meshtastic_MeshPacket &packet);

    bool ready() const { return initialized_ && online_; }
    uint8_t peerCount() const;
    uint32_t successfulRfFrames() const { return txSuccess_; }
    uint32_t failedRfFrames() const { return txFailures_; }

  protected:
    int32_t runOnce() override;

  private:
    static constexpr uint16_t WIRE_MAGIC = 0x5842; // "XB"
    static constexpr uint8_t WIRE_VERSION = 1;
    static constexpr uint8_t MAX_FRAGMENT_BYTES = 36; // 27-byte header + 36 = 63, safe below encrypted XR868 NP=65
    static constexpr uint8_t MAX_FRAGMENTS = 32;
    static constexpr uint8_t MAX_PEERS = 24;
    static constexpr uint8_t MAX_REASSEMBLY = 4;
    static constexpr uint8_t MAX_RECENT_INGRESS = 24;
    static constexpr uint8_t MAX_RECENT_FALLBACK = 24;
    static constexpr UBaseType_t TX_QUEUE_DEPTH = 6;
    static constexpr uint32_t SERVICE_INTERVAL_MS = 25;
    static constexpr uint32_t HELLO_INTERVAL_MS = 30u * 1000u;
    static constexpr uint32_t PEER_FRESH_MS = 5u * 60u * 1000u;
    static constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 12u * 1000u;
    static constexpr uint32_t INGRESS_SUPPRESS_MS = 2u * 60u * 1000u;
    static constexpr uint32_t FALLBACK_SUPPRESS_MS = 5u * 60u * 1000u;
    static constexpr uint32_t RETURN_ROUTE_MS = 2u * 60u * 1000u;
    static constexpr uint32_t PROBE_INTERVAL_MS = 10u * 1000u;
    static constexpr uint32_t FACTORY_PROVISION_AFTER_MS = 3u * 1000u;
    static constexpr uint32_t FACTORY_PROVISION_RETRY_MS = 60u * 1000u;
    static constexpr uint32_t FACTORY_GUARD_MS = 1100u;
    static constexpr uint32_t FACTORY_COMMAND_TIMEOUT_MS = 1200u;
    static constexpr uint32_t FACTORY_BAUD = 9600u;
    static constexpr uint32_t TX_STATUS_TIMEOUT_MS = 15u * 1000u;
    static constexpr uint8_t MAX_FRAGMENT_RETRIES = 1;
    static constexpr size_t MAX_PACKET_BYTES = meshtastic_MeshPacket_size;

    enum class FrameType : uint8_t { HELLO = 1, DATA = 2 };
    enum class ProvisionState : uint8_t {
        Idle,
        GuardBefore,
        GuardAfter,
        WaitEnter,
        WaitAp,
        WaitAo,
        WaitBd,
        WaitWr,
        ReopenDelay
    };

#pragma pack(push, 1)
    struct FrameHeader {
        uint16_t magic = WIRE_MAGIC;
        uint8_t version = WIRE_VERSION;
        FrameType type = FrameType::HELLO;
        uint32_t carrierNode = 0;
        uint32_t packetFrom = 0;
        uint32_t packetId = 0;
        uint16_t totalLength = 0;
        uint16_t fragmentOffset = 0;
        uint8_t fragmentIndex = 0;
        uint8_t fragmentCount = 0;
        uint8_t fragmentLength = 0;
        uint32_t checksum = 0;
    };
#pragma pack(pop)

    static_assert(sizeof(FrameHeader) + MAX_FRAGMENT_BYTES <= 65, "XR XBee carrier must fit encrypted XR868 payload");

    struct TxPacket {
        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
    };

    struct ActiveTx {
        bool active = false;
        std::array<uint8_t, MAX_PACKET_BYTES> encoded{};
        uint64_t destination64 = 0;
        uint32_t carrierNode = 0;
        uint32_t packetFrom = 0;
        uint32_t packetId = 0;
        uint32_t checksum = 0;
        uint16_t totalLength = 0;
        uint8_t fragmentBytes = 0;
        uint8_t fragmentCount = 0;
        uint8_t fragmentIndex = 0;
        uint8_t inFlightFrameId = 0;
        uint8_t retryCount = 0;
        uint32_t inFlightSinceMs = 0;
    };

    struct Peer {
        bool used = false;
        uint32_t nodeNum = 0;
        uint64_t address64 = 0;
        uint32_t lastSeenMs = 0;
        uint32_t lastIngressMs = 0;
    };

    struct Reassembly {
        bool used = false;
        uint64_t source64 = 0;
        uint32_t packetFrom = 0;
        uint32_t packetId = 0;
        uint16_t totalLength = 0;
        uint8_t fragmentCount = 0;
        uint32_t receivedMask = 0;
        uint32_t updatedMs = 0;
        std::array<uint8_t, MAX_PACKET_BYTES> data{};
    };

    struct RecentIngress {
        bool used = false;
        uint32_t packetFrom = 0;
        uint32_t packetId = 0;
        uint32_t seenMs = 0;
    };

    struct RecentFallback {
        bool used = false;
        uint32_t packetFrom = 0;
        uint32_t packetId = 0;
        uint32_t queuedMs = 0;
    };

    HardwareSerial serial_{MESHOFFGRID_XBEE_UART_NUM};
    meshoffgrid::xbee::XBeeXr868Link link_{};
    QueueHandle_t txQueue_ = nullptr;
    std::array<Peer, MAX_PEERS> peers_{};
    std::array<Reassembly, MAX_REASSEMBLY> reassembly_{};
    std::array<RecentIngress, MAX_RECENT_INGRESS> ingress_{};
    std::array<RecentFallback, MAX_RECENT_FALLBACK> fallbackHistory_{};
    ActiveTx activeTx_{};

    bool initialized_ = false;
    bool initAttempted_ = false;
    bool online_ = false;
    uint8_t npLimit_ = 65;
    uint32_t lastHelloMs_ = 0;
    uint32_t lastProbeMs_ = 0;
    uint32_t txSuccess_ = 0;
    uint32_t txFailures_ = 0;
    uint32_t txQueueDrops_ = 0;
    uint32_t txTimeouts_ = 0;
    ProvisionState provisionState_ = ProvisionState::Idle;
    uint32_t provisionDeadlineMs_ = 0;
    uint32_t lastProvisionAttemptMs_ = 0;
    uint8_t commandOkMatch_ = 0;

    bool initialize();
    void shutdown();
    void probeModule(uint32_t nowMs);
    void startFactoryProvisioning(uint32_t nowMs);
    void serviceFactoryProvisioning(uint32_t nowMs);
    void finishFactoryProvisioning(uint32_t nowMs, bool configured);
    bool consumeCommandOk();
    void sendFactoryCommand(const char *command, ProvisionState waitState, uint32_t nowMs);
    void sendHello(uint32_t nowMs);
    void processTx(const TxPacket &queued, uint32_t nowMs);
    bool sendPacket(const meshtastic_MeshPacket &packet, uint32_t nowMs);
    void servicePacketTx(uint32_t nowMs);
    uint8_t sendFrame(uint64_t destination64, FrameHeader header, const uint8_t *payload, size_t payloadLength);
    void processCarrier(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint32_t nowMs);
    void processData(uint64_t source64, const FrameHeader &header, const uint8_t *payload, uint32_t nowMs);

    Peer *findPeer(uint32_t nodeNum);
    const Peer *findPeer(uint32_t nodeNum) const;
    const Peer *selectFallbackPeer(uint32_t destination, uint32_t nowMs) const;
    void rememberPeer(uint32_t nodeNum, uint64_t address64, uint32_t nowMs);
    Reassembly &getReassembly(uint64_t source64, const FrameHeader &header, uint32_t nowMs);
    bool wasRecentIngress(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs) const;
    void markIngress(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs);
    bool fallbackWasQueued(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs) const;
    void markFallbackQueued(uint32_t packetFrom, uint32_t packetId, uint32_t nowMs);
    void expireState(uint32_t nowMs);

    static uint32_t checksum32(const uint8_t *data, size_t length);
    static void onReceive(uint64_t source64, const uint8_t *payload, size_t payloadLength, uint8_t options);
    static void onTxStatus(uint8_t frameId, uint8_t deliveryStatus, uint8_t retryCount, uint8_t discoveryStatus);
    static void onModemStatus(uint8_t status);
    static void onAtResponse(uint8_t frameId, char command0, char command1, uint8_t status, const uint8_t *value,
                             size_t valueLength);
    static XRXBeeTransport *instance_;
};

extern XRXBeeTransport *xrXBeeTransport;

} // namespace meshoffgrid::xr

#endif
