#pragma once

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>
#include "V29EmergencyFabric.h"
#include "V30PortalApproval.h"

class V29EmergencyPortal {
public:
    void begin();
    bool start();
    void stop();
    void loop();

    bool active() const { return _active; }
    const char* ssid() const { return _ssid; }
    const char* password() const { return _password; }
    const char* ipText() const { return _ip; }
    uint32_t remainingSeconds() const;
    uint32_t pendingApproval() const { return _approval.pending(); }
    bool approvePending(uint32_t id) { return _active && _approval.approve(id); }

private:
    static constexpr uint32_t SESSION_MAX_MS = 15UL * 60UL * 1000UL;
    static constexpr uint32_t IDLE_STOP_MS = 5UL * 60UL * 1000UL;
    static constexpr uint32_t ACTION_RATE_MS = 3000UL;
    // Enum constants avoid ODR/linker storage on the C++11 ESP32 toolchain.
    enum : uint16_t {
        HTTP_PORT = 80,
        DNS_PORT = 53,
    };

    bool _started = false;
    bool _active = false;
    wifi_mode_t _previousMode = WIFI_MODE_NULL;
    WiFiServer _server{HTTP_PORT};
    DNSServer _dns;
    uint32_t _sessionStartedMs = 0;
    uint32_t _lastActivityMs = 0;
    uint32_t _lastActionMs = 0;
    char _ssid[32] = {};
    char _password[16] = {};
    char _token[33] = {};
    WiFiClient _portalClient;
    v30::PortalRequest _request;
    v30::PortalApproval _approval;
    uint32_t _requestStartedMs = 0;
    V29EmergencyFabric::PowerMode _previousPower = V29EmergencyFabric::PowerMode::Normal;
    char _lastNotice[144] = {};
    char _ip[20] = {};

    void generateCredentials();
    void executeApproved(v30::PortalRequest::Action action);
    void sendForbidden(WiFiClient& client);
    bool actionAllowed(uint32_t now);
    void handleRequest(WiFiClient& client, v30::PortalRequest::Action action);
    void sendPage(WiFiClient& client, const char* notice = nullptr);
    void sendResponseHeader(WiFiClient& client, const char* type = "text/html; charset=utf-8");
    void sendNotFound(WiFiClient& client);
};

extern V29EmergencyPortal v29_emergency_portal;

#endif
