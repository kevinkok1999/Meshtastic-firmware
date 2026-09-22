#include "MeshOffGridDirectLink.h"

#if defined(MESHOFFGRID_V14) && defined(ARCH_ESP32)

#include "MeshOffGridDirectCodec.h"
#include "MeshTypes.h"
#include "RadioTxHook.h"
#include "Router.h"
#include "concurrency/Periodic.h"
#include "configuration.h"

#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <pb_decode.h>
#include <pb_encode.h>

namespace meshoffgrid
{
namespace
{

constexpr uint8_t kOffGridChannel = 6;
constexpr uint32_t kHelloIntervalMs = 5000;
constexpr uint32_t kPeerTimeoutMs = 60000;
constexpr uint32_t kReassemblyTimeoutMs = 3000;
constexpr size_t kPeerCapacity = 12;
constexpr size_t kTxQueueDepth = 4;
constexpr size_t kRxQueueDepth = 8;
constexpr size_t kReassemblySlots = 3;
constexpr size_t kSentHistory = 12;
constexpr uint8_t kMaxFragmentRetries = 2;

constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

struct RxEvent {
    uint8_t mac[6]{};
    int8_t rssi = 0;
    uint16_t length = 0;
    uint8_t bytes[kDirectMaxFrameBytes]{};
};

struct TxJob {
    uint8_t mac[6]{};
    uint32_t senderNode = 0;
    uint32_t packetId = 0;
    uint16_t length = 0;
    uint8_t bytes[kDirectMaxMessageBytes]{};
};

struct DirectPeer {
    bool used = false;
    NodeNum node = 0;
    uint8_t mac[6]{};
    int8_t rssi = 0;
    uint32_t lastSeenMs = 0;
    uint32_t txSuccess = 0;
    uint32_t txFailure = 0;
};

struct ReassemblySlot {
    bool used = false;
    uint8_t mac[6]{};
    int8_t rssi = 0;
    uint32_t lastUpdateMs = 0;
    DirectReassemblyBuffer buffer;
};

struct SentKey {
    NodeNum from = 0;
    PacketId id = 0;
};

class DirectLink final : public RadioTxHook
{
  public:
    DirectLink()
    {
        rxQueue_ = xQueueCreateStatic(kRxQueueDepth, sizeof(RxEvent), rxQueueStorage_, &rxQueueStruct_);
        txQueue_ = xQueueCreateStatic(kTxQueueDepth, sizeof(TxJob), txQueueStorage_, &txQueueStruct_);
    }

    bool begin()
    {
        if (ready_)
            return true;
        if (!rxQueue_ || !txQueue_) {
            LOG_ERROR("V14 DirectLink queue allocation failed");
            return false;
        }

        if (WiFi.getMode() == WIFI_OFF) {
            if (!WiFi.mode(WIFI_STA)) {
                LOG_ERROR("V14 DirectLink could not enable WiFi STA");
                return false;
            }
        }

        const esp_err_t countryResult = esp_wifi_set_country_code("NL", true);
        if (countryResult != ESP_OK)
            LOG_WARN("V14 DirectLink could not apply NL WiFi channel policy: %d", countryResult);

        if (!WiFi.isConnected()) {
            const esp_err_t channelResult = esp_wifi_set_channel(kOffGridChannel, WIFI_SECOND_CHAN_NONE);
            if (channelResult != ESP_OK)
                LOG_WARN("V14 DirectLink could not set off-grid channel: %d", channelResult);
        }

        const esp_err_t initResult = esp_now_init();
        if (initResult != ESP_OK) {
            LOG_ERROR("V14 DirectLink esp_now_init failed: %d", initResult);
            return false;
        }

        instance_ = this;
        if (esp_now_register_recv_cb(&DirectLink::onReceiveStatic) != ESP_OK ||
            esp_now_register_send_cb(&DirectLink::onSendStatic) != ESP_OK) {
            LOG_ERROR("V14 DirectLink callback registration failed");
            esp_now_deinit();
            instance_ = nullptr;
            return false;
        }

        if (!ensureEspNowPeer(kBroadcastMac)) {
            LOG_ERROR("V14 DirectLink broadcast peer setup failed");
            esp_now_deinit();
            instance_ = nullptr;
            return false;
        }

        ready_ = true;
        lastHelloMs_ = millis() - kHelloIntervalMs;
        LOG_INFO("V14 DirectLink ready on WiFi channel %u", currentChannel());
        return true;
    }

    int32_t tick()
    {
        if (!ready_)
            return 1000;

        processSendCompletion();

        RxEvent event;
        uint8_t processed = 0;
        while (processed < kRxQueueDepth && xQueueReceive(rxQueue_, &event, 0) == pdTRUE) {
            processRx(event);
            ++processed;
        }

        expireState();

        if (!sendInFlight_)
            advanceTx();

        const uint32_t now = millis();
        if (!sendInFlight_ && !hasCurrentTx_ && static_cast<uint32_t>(now - lastHelloMs_) >= kHelloIntervalMs)
            sendHello(now);

        return 20;
    }

    PreTxAction beforeTransmit(RadioInterface *, meshtastic_MeshPacket *packet) override
    {
        if (!ready_ || !packet || !router || packet->which_payload_variant != meshtastic_MeshPacket_encrypted_tag ||
            !isFromUs(packet))
            return PRETX_SEND;

        uint8_t targetMac[6]{};
        if (isBroadcast(packet->to) || packet->to == NODENUM_BROADCAST_NO_LORA) {
            memcpy(targetMac, kBroadcastMac, sizeof(targetMac));
        } else if (!peerMac(packet->to, targetMac)) {
            return PRETX_SEND;
        }

        if (wasQueued(packet->from, packet->id))
            return PRETX_SEND;

        TxJob job;
        memcpy(job.mac, targetMac, sizeof(job.mac));
        job.senderNode = packet->from;
        job.packetId = packet->id;
        const size_t encoded = pb_encode_to_bytes(job.bytes, sizeof(job.bytes), &meshtastic_MeshPacket_msg, packet);
        if (encoded == 0 || encoded > sizeof(job.bytes))
            return PRETX_SEND;

        job.length = static_cast<uint16_t>(encoded);
        if (xQueueSend(txQueue_, &job, 0) == pdTRUE) {
            rememberQueued(packet->from, packet->id);
            LOG_DEBUG("V14 DirectLink queued packet 0x%08x (%u bytes)", packet->id, job.length);
        } else {
            ++txQueueDrops_;
            LOG_WARN("V14 DirectLink TX queue full");
        }

        // BondLink safety: until two-device HIL passes, the normal LoRa path remains active.
        return PRETX_SEND;
    }

    size_t peerCount() const
    {
        portENTER_CRITICAL(&peerMux_);
        size_t count = 0;
        for (const auto &peer : peers_)
            count += peer.used ? 1 : 0;
        portEXIT_CRITICAL(&peerMux_);
        return count;
    }

    bool ready() const { return ready_; }

  private:
    static DirectLink *instance_;

    static void onReceiveStatic(const esp_now_recv_info_t *info, const uint8_t *data, int length)
    {
        if (!instance_ || !info || !info->src_addr || !data || length <= 0 ||
            static_cast<size_t>(length) > kDirectMaxFrameBytes)
            return;

        RxEvent event;
        memcpy(event.mac, info->src_addr, sizeof(event.mac));
        event.rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
        event.length = static_cast<uint16_t>(length);
        memcpy(event.bytes, data, event.length);
        if (xQueueSend(instance_->rxQueue_, &event, 0) != pdTRUE)
            ++instance_->rxQueueDrops_;
    }

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
    static void onSendStatic(const esp_now_send_info_t *, esp_now_send_status_t status)
#else
    static void onSendStatic(const uint8_t *, esp_now_send_status_t status)
#endif
    {
        if (!instance_)
            return;
        instance_->lastSendOk_ = status == ESP_NOW_SEND_SUCCESS;
        instance_->sendCompletionPending_ = true;
        instance_->sendInFlight_ = false;
    }

    bool ensureEspNowPeer(const uint8_t mac[6])
    {
        if (esp_now_is_peer_exist(mac))
            return true;

        esp_now_peer_info_t peer{};
        memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
        peer.channel = 0; // Follow the current STA/SoftAP channel.
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false; // Payload is already end-to-end encrypted by Meshtastic.
        return esp_now_add_peer(&peer) == ESP_OK;
    }

    uint8_t currentChannel() const
    {
        uint8_t primary = 0;
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        if (esp_wifi_get_channel(&primary, &secondary) != ESP_OK)
            return 0;
        return primary;
    }

    bool peerMac(NodeNum node, uint8_t out[6])
    {
        const uint32_t now = millis();
        bool found = false;
        portENTER_CRITICAL(&peerMux_);
        for (const auto &peer : peers_) {
            if (peer.used && peer.node == node && static_cast<uint32_t>(now - peer.lastSeenMs) < kPeerTimeoutMs) {
                memcpy(out, peer.mac, 6);
                found = true;
                break;
            }
        }
        portEXIT_CRITICAL(&peerMux_);
        return found;
    }

    void updatePeer(NodeNum node, const uint8_t mac[6], int8_t rssi)
    {
        if (!node || !memcmp(mac, kBroadcastMac, 6))
            return;

        const uint32_t now = millis();
        int slot = -1;
        int oldest = 0;
        uint32_t oldestAge = 0;
        uint8_t oldMac[6]{};
        bool replacing = false;

        portENTER_CRITICAL(&peerMux_);
        for (size_t i = 0; i < kPeerCapacity; ++i) {
            if (peers_[i].used && (peers_[i].node == node || memcmp(peers_[i].mac, mac, 6) == 0)) {
                slot = static_cast<int>(i);
                break;
            }
            if (!peers_[i].used && slot < 0)
                slot = static_cast<int>(i);

            if (peers_[i].used) {
                const uint32_t age = now - peers_[i].lastSeenMs;
                if (age >= oldestAge) {
                    oldestAge = age;
                    oldest = static_cast<int>(i);
                }
            }
        }

        if (slot < 0) {
            slot = oldest;
            memcpy(oldMac, peers_[slot].mac, sizeof(oldMac));
            replacing = true;
        }

        peers_[slot].used = true;
        peers_[slot].node = node;
        memcpy(peers_[slot].mac, mac, 6);
        peers_[slot].rssi = rssi;
        peers_[slot].lastSeenMs = now;
        portEXIT_CRITICAL(&peerMux_);

        if (replacing && memcmp(oldMac, mac, 6) != 0)
            esp_now_del_peer(oldMac);
        if (!ensureEspNowPeer(mac))
            LOG_WARN("V14 DirectLink could not add peer 0x%08x", node);
    }

    void expireState()
    {
        const uint32_t now = millis();
        uint8_t staleMacs[kPeerCapacity][6]{};
        size_t staleCount = 0;

        portENTER_CRITICAL(&peerMux_);
        for (auto &peer : peers_) {
            if (peer.used && static_cast<uint32_t>(now - peer.lastSeenMs) >= kPeerTimeoutMs) {
                memcpy(staleMacs[staleCount++], peer.mac, 6);
                peer = {};
            }
        }
        portEXIT_CRITICAL(&peerMux_);

        for (size_t i = 0; i < staleCount; ++i)
            esp_now_del_peer(staleMacs[i]);

        for (auto &slot : reassembly_) {
            if (slot.used && static_cast<uint32_t>(now - slot.lastUpdateMs) >= kReassemblyTimeoutMs) {
                slot.used = false;
                slot.buffer.clear();
                ++reassemblyTimeouts_;
            }
        }
    }

    ReassemblySlot &slotFor(const DirectFrame &frame, const uint8_t mac[6], int8_t rssi)
    {
        const uint32_t now = millis();
        ReassemblySlot *freeSlot = nullptr;
        ReassemblySlot *oldest = &reassembly_[0];
        uint32_t oldestAge = 0;

        for (auto &slot : reassembly_) {
            if (slot.used && slot.buffer.senderNode() == frame.header.senderNode &&
                slot.buffer.packetId() == frame.header.packetId && memcmp(slot.mac, mac, 6) == 0) {
                slot.lastUpdateMs = now;
                slot.rssi = rssi;
                return slot;
            }
            if (!slot.used && !freeSlot)
                freeSlot = &slot;
            if (slot.used) {
                const uint32_t age = now - slot.lastUpdateMs;
                if (age >= oldestAge) {
                    oldestAge = age;
                    oldest = &slot;
                }
            }
        }

        ReassemblySlot *selected = freeSlot ? freeSlot : oldest;
        selected->used = true;
        memcpy(selected->mac, mac, 6);
        selected->rssi = rssi;
        selected->lastUpdateMs = now;
        selected->buffer.reset(frame.header.senderNode, frame.header.packetId, frame.header.totalLength,
                               frame.header.fragmentCount);
        return *selected;
    }

    void processRx(const RxEvent &event)
    {
        DirectFrame frame;
        if (!parseDirectFrame(event.bytes, event.length, frame)) {
            ++invalidFrames_;
            return;
        }

        updatePeer(frame.header.senderNode, event.mac, event.rssi);
        const auto type = static_cast<DirectFrameType>(frame.header.type);
        if (type == DirectFrameType::Hello)
            return;

        ReassemblySlot &slot = slotFor(frame, event.mac, event.rssi);
        if (!slot.buffer.accept(frame)) {
            ++invalidFrames_;
            return;
        }
        if (!slot.buffer.complete())
            return;

        meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;
        const bool decoded =
            pb_decode_from_bytes(slot.buffer.data(), slot.buffer.length(), &meshtastic_MeshPacket_msg, &packet);
        if (!decoded || packet.id != slot.buffer.packetId() || packet.from != slot.buffer.senderNode() ||
            packet.which_payload_variant != meshtastic_MeshPacket_encrypted_tag || packet.hop_limit > HOP_MAX ||
            packet.hop_start > HOP_MAX || isFromUs(&packet) || !router) {
            ++invalidMessages_;
            slot.used = false;
            slot.buffer.clear();
            return;
        }

        packet.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;
        packet.pki_encrypted = false;
        packet.public_key.size = 0;
        packet.rx_snr = 0;
        packet.rx_rssi = 0;
        packet.has_rx_rssi = false;

        UniquePacketPoolPacket pooled = packetPool.allocUniqueCopy(packet, 0);
        if (pooled) {
            router->enqueueReceivedMessage(pooled.release());
            ++rxMessages_;
        } else {
            ++rxPoolDrops_;
        }

        slot.used = false;
        slot.buffer.clear();
    }

    void sendHello(uint32_t now)
    {
        if (!router)
            return;

        DirectFrame frame;
        size_t length = 0;
        if (!buildDirectHello(router->getNodeNum(), frame, length))
            return;

        const esp_err_t result = esp_now_send(kBroadcastMac, reinterpret_cast<const uint8_t *>(&frame), length);
        if (result == ESP_OK) {
            sendInFlight_ = true;
            lastHelloMs_ = now;
        } else {
            ++txImmediateFailures_;
        }
    }

    void processSendCompletion()
    {
        if (!sendCompletionPending_)
            return;

        sendCompletionPending_ = false;
        if (!hasCurrentTx_)
            return;

        if (lastSendOk_) {
            ++txFragmentsOk_;
            retryCount_ = 0;
            ++fragmentIndex_;
            if (fragmentIndex_ >= directFragmentCount(currentTx_.length)) {
                ++txMessagesOk_;
                hasCurrentTx_ = false;
                updatePeerTxResult(currentTx_.mac, true);
            }
        } else if (retryCount_ < kMaxFragmentRetries) {
            ++retryCount_;
        } else {
            ++txMessagesFailed_;
            updatePeerTxResult(currentTx_.mac, false);
            hasCurrentTx_ = false;
            retryCount_ = 0;
        }
    }

    void advanceTx()
    {
        if (!hasCurrentTx_) {
            if (xQueueReceive(txQueue_, &currentTx_, 0) != pdTRUE)
                return;
            hasCurrentTx_ = true;
            fragmentIndex_ = 0;
            retryCount_ = 0;
        }

        DirectFrame frame;
        size_t frameLength = 0;
        if (!buildDirectDataFrame(currentTx_.senderNode, currentTx_.packetId, currentTx_.bytes, currentTx_.length,
                                  fragmentIndex_, frame, frameLength)) {
            ++txMessagesFailed_;
            hasCurrentTx_ = false;
            return;
        }

        const esp_err_t result =
            esp_now_send(currentTx_.mac, reinterpret_cast<const uint8_t *>(&frame), frameLength);
        if (result == ESP_OK) {
            sendInFlight_ = true;
        } else {
            ++txImmediateFailures_;
            if (retryCount_ < kMaxFragmentRetries)
                ++retryCount_;
            else {
                ++txMessagesFailed_;
                updatePeerTxResult(currentTx_.mac, false);
                hasCurrentTx_ = false;
                retryCount_ = 0;
            }
        }
    }

    void updatePeerTxResult(const uint8_t mac[6], bool success)
    {
        if (!memcmp(mac, kBroadcastMac, 6))
            return;

        portENTER_CRITICAL(&peerMux_);
        for (auto &peer : peers_) {
            if (peer.used && memcmp(peer.mac, mac, 6) == 0) {
                if (success)
                    ++peer.txSuccess;
                else
                    ++peer.txFailure;
                break;
            }
        }
        portEXIT_CRITICAL(&peerMux_);
    }

    bool wasQueued(NodeNum from, PacketId id) const
    {
        for (const auto &entry : sentHistory_)
            if (entry.from == from && entry.id == id)
                return true;
        return false;
    }

    void rememberQueued(NodeNum from, PacketId id)
    {
        sentHistory_[sentHistoryNext_] = {from, id};
        sentHistoryNext_ = (sentHistoryNext_ + 1) % kSentHistory;
    }

    QueueHandle_t rxQueue_ = nullptr;
    StaticQueue_t rxQueueStruct_{};
    alignas(4) uint8_t rxQueueStorage_[kRxQueueDepth * sizeof(RxEvent)]{};

    QueueHandle_t txQueue_ = nullptr;
    StaticQueue_t txQueueStruct_{};
    alignas(4) uint8_t txQueueStorage_[kTxQueueDepth * sizeof(TxJob)]{};

    mutable portMUX_TYPE peerMux_ = portMUX_INITIALIZER_UNLOCKED;
    DirectPeer peers_[kPeerCapacity]{};
    ReassemblySlot reassembly_[kReassemblySlots]{};
    SentKey sentHistory_[kSentHistory]{};
    size_t sentHistoryNext_ = 0;

    TxJob currentTx_{};
    bool hasCurrentTx_ = false;
    uint8_t fragmentIndex_ = 0;
    uint8_t retryCount_ = 0;

    volatile bool sendInFlight_ = false;
    volatile bool sendCompletionPending_ = false;
    volatile bool lastSendOk_ = false;
    bool ready_ = false;
    uint32_t lastHelloMs_ = 0;

    volatile uint32_t rxQueueDrops_ = 0;
    uint32_t txQueueDrops_ = 0;
    uint32_t invalidFrames_ = 0;
    uint32_t invalidMessages_ = 0;
    uint32_t reassemblyTimeouts_ = 0;
    uint32_t rxMessages_ = 0;
    uint32_t rxPoolDrops_ = 0;
    uint32_t txFragmentsOk_ = 0;
    uint32_t txMessagesOk_ = 0;
    uint32_t txMessagesFailed_ = 0;
    uint32_t txImmediateFailures_ = 0;
};

DirectLink *DirectLink::instance_ = nullptr;
DirectLink *directLink = nullptr;
concurrency::Periodic *directLinkThread = nullptr;

} // namespace

void initV14DirectLink()
{
    if (directLink)
        return;

    directLink = new DirectLink();
    if (!directLink->begin()) {
        LOG_WARN("V14 DirectLink disabled after initialization failure");
        delete directLink;
        directLink = nullptr;
        return;
    }

    directLinkThread = new concurrency::Periodic("V14Direct", []() { return directLink ? directLink->tick() : 1000; });
}

bool isV14DirectLinkReady()
{
    return directLink && directLink->ready();
}

size_t v14DirectLinkPeerCount()
{
    return directLink ? directLink->peerCount() : 0;
}

} // namespace meshoffgrid

#else

namespace meshoffgrid
{

void initV14DirectLink() {}
bool isV14DirectLinkReady() { return false; }
size_t v14DirectLinkPeerCount() { return 0; }

} // namespace meshoffgrid

#endif
