#include "EspNowEsp32Backend.h"

#if defined(ESP32)

#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <esp_timer.h>

namespace meshtastic::multilink {

EspNowEsp32Backend *EspNowEsp32Backend::instance_ = nullptr;

EspNowEsp32Backend::EspNowEsp32Backend() = default;

EspNowEsp32Backend::~EspNowEsp32Backend()
{
    if (instance_ != this)
        return;

    if (initialized_) {
        esp_now_unregister_send_cb();
        esp_now_unregister_recv_cb();
        esp_now_deinit();
    }

    instance_ = nullptr;
    initialized_ = false;
}

uint32_t EspNowEsp32Backend::monotonicMs()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool EspNowEsp32Backend::setPrimaryMasterKey(const uint8_t *pmk, size_t size)
{
    if (pmk == nullptr || size != KeySize)
        return false;

    std::copy_n(pmk, KeySize, pmk_.begin());
    pmkConfigured_ = true;

    if (initialized_ && esp_now_set_pmk(pmk_.data()) != ESP_OK) {
        pmkConfigured_ = false;
        std::fill(pmk_.begin(), pmk_.end(), 0);
        return false;
    }

    return true;
}

bool EspNowEsp32Backend::begin(const EspNowConfig &config)
{
    if (initialized_)
        return true;
    if (instance_ != nullptr && instance_ != this)
        return false;

    config_ = config;
    if (config_.mtu == 0 || config_.mtu > EspNowRxFrame::Capacity)
        return false;

    wifi_mode_t mode = WIFI_MODE_NULL;
    const esp_err_t modeResult = esp_wifi_get_mode(&mode);
    if (modeResult != ESP_OK || mode == WIFI_MODE_NULL) {
        if (!WiFi.mode(WIFI_STA))
            return false;
        interface_ = WIFI_IF_STA;
    } else if (mode == WIFI_MODE_AP) {
        interface_ = WIFI_IF_AP;
    } else {
        interface_ = WIFI_IF_STA;
    }

    // Only pin a channel when explicitly configured. When Wi-Fi is already
    // associated, callers should use that same channel rather than forcing a
    // channel change underneath the existing connection.
    if (config_.channel != 0) {
        uint8_t primary = 0;
        wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
        if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK && primary != config_.channel) {
            if (esp_wifi_set_channel(config_.channel, WIFI_SECOND_CHAN_NONE) != ESP_OK)
                return false;
        }
    }

    if (esp_now_init() != ESP_OK)
        return false;

    instance_ = this;

    if (pmkConfigured_ && esp_now_set_pmk(pmk_.data()) != ESP_OK) {
        esp_now_deinit();
        instance_ = nullptr;
        return false;
    }

    if (esp_now_register_send_cb(&EspNowEsp32Backend::onSend) != ESP_OK) {
        esp_now_deinit();
        instance_ = nullptr;
        return false;
    }

    if (esp_now_register_recv_cb(&EspNowEsp32Backend::onReceive) != ESP_OK) {
        esp_now_unregister_send_cb();
        esp_now_deinit();
        instance_ = nullptr;
        return false;
    }

    initialized_ = true;
    return true;
}

EspNowEsp32Backend::Peer *EspNowEsp32Backend::findPeer(uint32_t nodeId)
{
    for (auto &peer : peers_) {
        if (peer.used && peer.nodeId == nodeId)
            return &peer;
    }
    return nullptr;
}

const EspNowEsp32Backend::Peer *EspNowEsp32Backend::findPeer(uint32_t nodeId) const
{
    for (const auto &peer : peers_) {
        if (peer.used && peer.nodeId == nodeId)
            return &peer;
    }
    return nullptr;
}

EspNowEsp32Backend::Peer *EspNowEsp32Backend::findPeerByMac(const uint8_t mac[6])
{
    if (mac == nullptr)
        return nullptr;

    for (auto &peer : peers_) {
        if (peer.used && std::equal(peer.mac.begin(), peer.mac.end(), mac))
            return &peer;
    }
    return nullptr;
}

const EspNowEsp32Backend::Peer *EspNowEsp32Backend::findPeerByMac(const uint8_t mac[6]) const
{
    if (mac == nullptr)
        return nullptr;

    for (const auto &peer : peers_) {
        if (peer.used && std::equal(peer.mac.begin(), peer.mac.end(), mac))
            return &peer;
    }
    return nullptr;
}

bool EspNowEsp32Backend::registerPeer(uint32_t nodeId, const uint8_t mac[6], const uint8_t *lmk, size_t lmkSize)
{
    if (!initialized_ || nodeId == 0 || mac == nullptr || findPeer(nodeId) != nullptr)
        return false;

    const bool encrypted = lmk != nullptr;
    if (encrypted && (lmkSize != ESP_NOW_KEY_LEN || !pmkConfigured_))
        return false;
    if (!encrypted && lmkSize != 0)
        return false;

    Peer *slot = nullptr;
    for (auto &candidate : peers_) {
        if (!candidate.used) {
            slot = &candidate;
            break;
        }
    }
    if (slot == nullptr)
        return false;

    esp_now_peer_info_t info{};
    std::copy_n(mac, 6, info.peer_addr);
    info.channel = config_.channel;
    info.ifidx = interface_;
    info.encrypt = encrypted;
    if (encrypted)
        std::copy_n(lmk, ESP_NOW_KEY_LEN, info.lmk);

    if (esp_now_add_peer(&info) != ESP_OK)
        return false;

    slot->nodeId = nodeId;
    std::copy_n(mac, 6, slot->mac.begin());
    slot->encrypted = encrypted;
    slot->rssiDbm = -127;
    slot->txStartedMs = 0;
    slot->used = true;
    return true;
}

bool EspNowEsp32Backend::removePeer(uint32_t nodeId)
{
    Peer *peer = findPeer(nodeId);
    if (peer == nullptr)
        return false;

    if (initialized_ && esp_now_del_peer(peer->mac.data()) != ESP_OK)
        return false;

    *peer = Peer{};
    return true;
}

bool EspNowEsp32Backend::hasPeer(uint32_t nodeId) const
{
    return findPeer(nodeId) != nullptr;
}

int16_t EspNowEsp32Backend::peerRssiDbm(uint32_t nodeId) const
{
    const Peer *peer = findPeer(nodeId);
    return peer != nullptr ? peer->rssiDbm : -127;
}

bool EspNowEsp32Backend::send(uint32_t nodeId, const uint8_t *data, size_t size)
{
    if (!initialized_ || data == nullptr || size == 0 || size > config_.mtu || size > ESP_NOW_MAX_DATA_LEN)
        return false;

    Peer *peer = findPeer(nodeId);
    if (peer == nullptr)
        return false;

    portENTER_CRITICAL(&mux_);
    if (txInFlight_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    txInFlight_ = true;
    peer->txStartedMs = monotonicMs();
    portEXIT_CRITICAL(&mux_);

    const esp_err_t result = esp_now_send(peer->mac.data(), data, size);
    if (result != ESP_OK) {
        portENTER_CRITICAL(&mux_);
        txInFlight_ = false;
        portEXIT_CRITICAL(&mux_);
        return false;
    }

    return true;
}

void EspNowEsp32Backend::pushDelivery(const EspNowDeliveryReport &report)
{
    portENTER_CRITICAL(&mux_);
    const size_t next = (reportWrite_ + 1) % reports_.size();
    if (next == reportRead_)
        reportRead_ = (reportRead_ + 1) % reports_.size();
    reports_[reportWrite_] = report;
    reportWrite_ = next;
    portEXIT_CRITICAL(&mux_);
}

bool EspNowEsp32Backend::popDeliveryReport(EspNowDeliveryReport &report)
{
    portENTER_CRITICAL(&mux_);
    if (reportRead_ == reportWrite_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    report = reports_[reportRead_];
    reportRead_ = (reportRead_ + 1) % reports_.size();
    portEXIT_CRITICAL(&mux_);
    return true;
}

void EspNowEsp32Backend::pushReceived(const uint8_t srcMac[6], const uint8_t *data, int length, int16_t rssiDbm)
{
    if (srcMac == nullptr || data == nullptr || length <= 0 || static_cast<size_t>(length) > EspNowRxFrame::Capacity)
        return;

    Peer *peer = findPeerByMac(srcMac);
    if (peer == nullptr && config_.requireKnownPeer)
        return;

    EspNowRxFrame frame;
    frame.size = static_cast<uint16_t>(length);
    std::copy_n(data, frame.size, frame.data.begin());
    frame.nodeId = peer != nullptr ? peer->nodeId : 0;
    frame.rssiDbm = rssiDbm;
    frame.transportAuthenticated = peer != nullptr && peer->encrypted;

    if (peer != nullptr && rssiDbm > -127)
        peer->rssiDbm = rssiDbm;

    portENTER_CRITICAL(&mux_);
    const size_t next = (rxWrite_ + 1) % rxQueue_.size();
    if (next == rxRead_)
        rxRead_ = (rxRead_ + 1) % rxQueue_.size();
    rxQueue_[rxWrite_].frame = frame;
    rxQueue_[rxWrite_].used = true;
    rxWrite_ = next;
    portEXIT_CRITICAL(&mux_);
}

bool EspNowEsp32Backend::popReceived(EspNowRxFrame &frame)
{
    portENTER_CRITICAL(&mux_);
    if (rxRead_ == rxWrite_) {
        portEXIT_CRITICAL(&mux_);
        return false;
    }
    frame = rxQueue_[rxRead_].frame;
    rxQueue_[rxRead_].used = false;
    rxRead_ = (rxRead_ + 1) % rxQueue_.size();
    portEXIT_CRITICAL(&mux_);
    return true;
}

void EspNowEsp32Backend::poll(uint32_t nowMs)
{
    (void)nowMs;
}

void EspNowEsp32Backend::onSend(const uint8_t *mac, esp_now_send_status_t status)
{
    if (instance_ == nullptr)
        return;

    Peer *peer = instance_->findPeerByMac(mac);
    EspNowDeliveryReport report;
    if (peer != nullptr) {
        report.nodeId = peer->nodeId;
        const uint32_t now = monotonicMs();
        report.latencyMs = peer->txStartedMs != 0 ? static_cast<uint16_t>(std::min<uint32_t>(now - peer->txStartedMs, 65535U)) : 0;
    }
    report.success = status == ESP_NOW_SEND_SUCCESS;

    instance_->pushDelivery(report);
    portENTER_CRITICAL(&instance_->mux_);
    instance_->txInFlight_ = false;
    portEXIT_CRITICAL(&instance_->mux_);
}

#if ESP_IDF_VERSION_MAJOR >= 5
void EspNowEsp32Backend::onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length)
{
    if (instance_ == nullptr || info == nullptr)
        return;

    const int16_t rssi = info->rx_ctrl != nullptr ? info->rx_ctrl->rssi : -127;
    instance_->pushReceived(info->src_addr, data, length, rssi);
}
#else
void EspNowEsp32Backend::onReceive(const uint8_t *mac, const uint8_t *data, int length)
{
    if (instance_ == nullptr)
        return;

    instance_->pushReceived(mac, data, length, -127);
}
#endif

} // namespace meshtastic::multilink

#endif // ESP32
