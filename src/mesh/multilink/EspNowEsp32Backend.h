#pragma once

#include "EspNowLink.h"

#if defined(ESP32)

#include <array>
#include <cstddef>
#include <cstdint>
#include <esp_idf_version.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace meshtastic::multilink {

class EspNowEsp32Backend final : public EspNowBackend
{
  public:
    static constexpr size_t MaxPeers = 16;
    static constexpr size_t ReportQueueSize = 12;
    static constexpr size_t RxQueueSize = 8;
    static constexpr size_t KeySize = 16;

    EspNowEsp32Backend();
    ~EspNowEsp32Backend() override;

    // Set before registering encrypted peers. No default PMK is installed.
    bool setPrimaryMasterKey(const uint8_t *pmk, size_t size);

    bool registerPeer(uint32_t nodeId, const uint8_t mac[6], const uint8_t *lmk = nullptr, size_t lmkSize = 0);
    bool removePeer(uint32_t nodeId);

    bool begin(const EspNowConfig &config) override;
    bool available() const override { return initialized_; }
    bool hasPeer(uint32_t nodeId) const override;
    bool send(uint32_t nodeId, const uint8_t *data, size_t size) override;
    int16_t peerRssiDbm(uint32_t nodeId) const override;
    bool popDeliveryReport(EspNowDeliveryReport &report) override;
    bool popReceived(EspNowRxFrame &frame) override;
    void poll(uint32_t nowMs) override;

  private:
    struct Peer {
        uint32_t nodeId = 0;
        std::array<uint8_t, 6> mac{};
        int16_t rssiDbm = -127;
        uint32_t txStartedMs = 0;
        bool encrypted = false;
        bool used = false;
    };

    struct RxSlot {
        EspNowRxFrame frame{};
        bool used = false;
    };

    static EspNowEsp32Backend *instance_;

    EspNowConfig config_{};
    std::array<Peer, MaxPeers> peers_{};
    std::array<EspNowDeliveryReport, ReportQueueSize> reports_{};
    std::array<RxSlot, RxQueueSize> rxQueue_{};
    std::array<uint8_t, KeySize> pmk_{};

    size_t reportRead_ = 0;
    size_t reportWrite_ = 0;
    size_t rxRead_ = 0;
    size_t rxWrite_ = 0;
    wifi_interface_t interface_ = WIFI_IF_STA;
    bool initialized_ = false;
    bool pmkConfigured_ = false;
    volatile bool txInFlight_ = false;
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    Peer *findPeer(uint32_t nodeId);
    const Peer *findPeer(uint32_t nodeId) const;
    Peer *findPeerByMac(const uint8_t mac[6]);
    const Peer *findPeerByMac(const uint8_t mac[6]) const;

    void pushDelivery(const EspNowDeliveryReport &report);
    void pushReceived(const uint8_t srcMac[6], const uint8_t *data, int length, int16_t rssiDbm);
    static uint32_t monotonicMs();

    static void onSend(const uint8_t *mac, esp_now_send_status_t status);
#if ESP_IDF_VERSION_MAJOR >= 5
    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length);
#else
    static void onReceive(const uint8_t *mac, const uint8_t *data, int length);
#endif
};

} // namespace meshtastic::multilink

#endif // ESP32
