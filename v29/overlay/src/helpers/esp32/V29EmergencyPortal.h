#pragma once

#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

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

private:
    static constexpr uint32_t SESSION_MAX_MS = 15UL * 60UL * 1000UL;
    static constexpr uint32_t IDLE_STOP_MS = 5UL * 60UL * 1000UL;
    static constexpr uint32_t ACTION_RATE_MS = 3000UL;
    static constexpr uint16_t HTTP_PORT = 80;
    static constexpr uint16_t DNS_PORT = 53;

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
    char _token[12] = {};
    char _ip[20] = {};

    void generateCredentials();
    bool tokenOk(const char* path) const;
    bool actionAllowed(uint32_t now);
    void handleClient(WiFiClient& client);
    void sendPage(WiFiClient& client, const char* notice = nullptr);
    void sendResponseHeader(WiFiClient& client, const char* type = "text/html; charset=utf-8");
    void sendNotFound(WiFiClient& client);
};

extern V29EmergencyPortal v29_emergency_portal;

#endif
