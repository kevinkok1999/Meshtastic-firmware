#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)

#include "V29EmergencyPortal.h"
#include "V29EmergencyFabric.h"

#include <esp_random.h>
#include <string.h>

V29EmergencyPortal v29_emergency_portal;

namespace {
static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
}

void V29EmergencyPortal::begin() {
    _started = true;
}

void V29EmergencyPortal::generateCredentials() {
    const uint32_t suffix = esp_random();
    snprintf(_ssid, sizeof(_ssid), "MeshOffGridNL-%04lX",
             (unsigned long)(suffix & 0xffffUL));

    for (size_t i = 0; i < 10; ++i) {
        _password[i] = kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)];
    }
    _password[10] = '\0';

    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < 8; ++i) {
        _token[i] = hex[(esp_random() >> ((i & 3U) * 8U)) & 0x0fU];
    }
    _token[8] = '\0';
}

bool V29EmergencyPortal::start() {
    if (!_started) begin();
    if (_active) {
        _lastActivityMs = millis();
        return true;
    }

    generateCredentials();
    _previousMode = WiFi.getMode();

    const bool hadSta = (_previousMode & WIFI_MODE_STA) != 0;
    if (!WiFi.mode(hadSta ? WIFI_AP_STA : WIFI_AP)) {
        return false;
    }
    WiFi.persistent(false);

    if (!WiFi.softAP(_ssid, _password, 1, false, 4)) {
        WiFi.mode(_previousMode);
        return false;
    }

    const IPAddress ip = WiFi.softAPIP();
    snprintf(_ip, sizeof(_ip), "%u.%u.%u.%u",
             ip[0], ip[1], ip[2], ip[3]);

    _dns.start(DNS_PORT, "*", ip);
    _server.begin();

    _sessionStartedMs = millis();
    _lastActivityMs = _sessionStartedMs;
    _active = true;
    v29_emergency_fabric.setPowerMode(V29EmergencyFabric::PowerMode::Emergency);

    Serial.printf("[V29] local emergency portal active ssid=%s ip=%s\n", _ssid, _ip);
    return true;
}

void V29EmergencyPortal::stop() {
    if (!_active) return;

    _dns.stop();
    _server.stop();
    WiFi.softAPdisconnect(true);

    if (_previousMode == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_OFF);
    } else {
        WiFi.mode(_previousMode);
    }

    _active = false;
    _sessionStartedMs = 0;
    _lastActivityMs = 0;
    memset(_token, 0, sizeof(_token));
    Serial.println("[V29] local emergency portal stopped");
}

uint32_t V29EmergencyPortal::remainingSeconds() const {
    if (!_active) return 0;
    const uint32_t elapsedMs = (uint32_t)(millis() - _sessionStartedMs);
    if (elapsedMs >= SESSION_MAX_MS) return 0;
    return (SESSION_MAX_MS - elapsedMs) / 1000UL;
}

bool V29EmergencyPortal::tokenOk(const char* path) const {
    if (!path || !_token[0]) return false;
    const char* p = strstr(path, "t=");
    if (!p) return false;
    p += 2;
    return strncmp(p, _token, strlen(_token)) == 0;
}

void V29EmergencyPortal::sendResponseHeader(WiFiClient& c, const char* type) {
    c.print("HTTP/1.1 200 OK\r\n");
    c.print("Cache-Control: no-store\r\n");
    c.print("X-Content-Type-Options: nosniff\r\n");
    c.print("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'\r\n");
    c.print("Content-Type: ");
    c.print(type);
    c.print("\r\nConnection: close\r\n\r\n");
}

void V29EmergencyPortal::sendPage(WiFiClient& c, const char* notice) {
    sendResponseHeader(c);
    c.print("<!doctype html><html lang='nl'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    c.print("<title>MeshOffGridNL Noodmodus</title><style>body{font:17px system-ui;margin:0;background:#101214;color:#f3f5f6}"
            "main{max-width:520px;margin:auto;padding:22px}h1{font-size:25px}p{line-height:1.45}.card{background:#1b1f22;padding:16px;border-radius:16px;margin:12px 0}"
            "a{display:block;text-decoration:none;text-align:center;padding:16px;margin:12px 0;border-radius:14px;background:#e7ecef;color:#111;font-weight:750}"
            ".help{background:#ffd8d8}.small{font-size:14px;color:#b8c0c5}</style><main>");
    c.print("<h1>MeshOffGridNL Noodmodus</h1><p class='small'>Lokaal netwerk - geen internet nodig.</p>");
    if (notice && notice[0]) {
        c.print("<div class='card'><b>");
        c.print(notice);
        c.print("</b></div>");
    }
    c.print("<a href='/safe?t="); c.print(_token); c.print("'>Ik ben veilig</a>");
    c.print("<a class='help' href='/help?t="); c.print(_token); c.print("'>Ik heb hulp nodig</a>");
    c.print("<a href='/status?t="); c.print(_token); c.print("'>Netwerkstatus</a>");
    c.print("<div class='card small'>Een hulpvraag gaat via het lokale mesh-netwerk. 112 wordt niet automatisch gebeld."
            " Als telefonie werkt en er direct gevaar is, gebruik 112.</div>");
    c.print("</main></html>");
}

void V29EmergencyPortal::sendNotFound(WiFiClient& c) {
    c.print("HTTP/1.1 302 Found\r\nLocation: http://");
    c.print(_ip);
    c.print("/\r\nConnection: close\r\n\r\n");
}

void V29EmergencyPortal::handleClient(WiFiClient& c) {
    char line[192] = {};
    size_t n = 0;
    const uint32_t deadline = millis() + 25UL;

    while (c.connected() && (int32_t)(millis() - deadline) < 0 && n + 1 < sizeof(line)) {
        while (c.available() && n + 1 < sizeof(line)) {
            const char ch = (char)c.read();
            if (ch == '\n') {
                line[n] = '\0';
                break;
            }
            if (ch != '\r') line[n++] = ch;
        }
        if (n && strchr(line, ' ')) break;
        delay(1);
    }

    if (strncmp(line, "GET ", 4) != 0) {
        c.print("HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n");
        return;
    }

    char* path = line + 4;
    char* end = strchr(path, ' ');
    if (!end) {
        sendNotFound(c);
        return;
    }
    *end = '\0';
    _lastActivityMs = millis();

    if (strcmp(path, "/") == 0 || strncmp(path, "/generate_204", 13) == 0 ||
        strncmp(path, "/hotspot-detect.html", 20) == 0 ||
        strncmp(path, "/connecttest.txt", 16) == 0) {
        sendPage(c);
        return;
    }

    if (!tokenOk(path)) {
        sendNotFound(c);
        return;
    }

    if (strncmp(path, "/safe?", 6) == 0) {
        const uint8_t sent = v29_emergency_fabric.sendCheckInToEmergencyContacts(
            V29EmergencyFabric::CheckInState::Safe);
        char notice[96];
        snprintf(notice, sizeof(notice),
                 sent ? "Veilig-melding is bewaard/verzonden naar %u noodcontact(en)."
                      : "Geen noodcontacten ingesteld op deze T-Deck.",
                 (unsigned)sent);
        sendPage(c, notice);
        return;
    }

    if (strncmp(path, "/help?", 6) == 0) {
        const uint8_t sent = v29_emergency_fabric.sendHelpToEmergencyContacts(0, 2);
        char notice[128];
        snprintf(notice, sizeof(notice),
                 sent ? "Lokale hulpvraag is bewaard/verzonden naar %u noodcontact(en). 112 is niet automatisch gebeld."
                      : "Geen noodcontacten ingesteld. 112 is niet automatisch gebeld.",
                 (unsigned)sent);
        sendPage(c, notice);
        return;
    }

    if (strncmp(path, "/status?", 8) == 0) {
        const auto st = v29_emergency_fabric.memoryStats();
        const uint8_t contacts = v29_emergency_fabric.emergencyContactCount();
        char notice[112];
        snprintf(notice, sizeof(notice),
                 "Noodnetwerk actief: %u noodcontact(en), %u bericht(en) lokaal bewaard.",
                 (unsigned)contacts, (unsigned)st.queueUsed);
        sendPage(c, notice);
        return;
    }

    sendNotFound(c);
}

void V29EmergencyPortal::loop() {
    if (!_active) return;

    const uint32_t now = millis();
    if ((uint32_t)(now - _sessionStartedMs) >= SESSION_MAX_MS ||
        (uint32_t)(now - _lastActivityMs) >= IDLE_STOP_MS) {
        stop();
        return;
    }

    _dns.processNextRequest();

    WiFiClient client = _server.available();
    if (!client) return;
    handleClient(client);
    client.stop();
}

#endif
