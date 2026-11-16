#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION) && defined(MESH_OFFGRIDNL_V29)

#include "V29EmergencyPortal.h"
#include "V29EmergencyFabric.h"
#include "V30PortalRequest.h"

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
    uint8_t bytes[16];
    esp_fill_random(bytes, sizeof(bytes));
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        _token[i*2] = hex[bytes[i] >> 4];
        _token[i*2+1] = hex[bytes[i] & 15];
    }
    _token[32] = '\0';
    memset(bytes, 0, sizeof(bytes));
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
    _lastActionMs = 0;
    _active = true;
    _previousPower = v29_emergency_fabric.powerMode();
    _approval.clear();
    _request.reset();
    _lastNotice[0] = 0;
    v29_emergency_fabric.setPowerMode(V29EmergencyFabric::PowerMode::Emergency);

    Serial.printf("[V29] local emergency portal active ssid=%s ip=%s\n", _ssid, _ip);
    return true;
}

void V29EmergencyPortal::stop() {
    if (!_active) return;

    _portalClient.stop();
    _request.reset();
    _approval.clear();
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
    _lastActionMs = 0;
    memset(_token, 0, sizeof(_token));
    memset(_password, 0, sizeof(_password));
    if (v29_emergency_fabric.powerMode() == V29EmergencyFabric::PowerMode::Emergency)
        v29_emergency_fabric.setPowerMode(_previousPower);
    Serial.println("[V29] local emergency portal stopped");
}

uint32_t V29EmergencyPortal::remainingSeconds() const {
    if (!_active) return 0;
    const uint32_t elapsedMs = (uint32_t)(millis() - _sessionStartedMs);
    if (elapsedMs >= SESSION_MAX_MS) return 0;
    return (SESSION_MAX_MS - elapsedMs) / 1000UL;
}

bool V29EmergencyPortal::actionAllowed(uint32_t now) {
    if (_lastActionMs != 0 && (uint32_t)(now - _lastActionMs) < ACTION_RATE_MS)
        return false;
    _lastActionMs = now;
    return true;
}

void V29EmergencyPortal::sendResponseHeader(WiFiClient& c, const char* type) {
    c.print("HTTP/1.1 200 OK\r\n");
    c.print("Cache-Control: no-store\r\n");
    c.print("X-Content-Type-Options: nosniff\r\n");
    c.print("Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'\r\n");
    c.print("Referrer-Policy: no-referrer\r\n");
    c.print("Content-Type: ");
    c.print(type);
    c.print("\r\nConnection: close\r\n\r\n");
}

void V29EmergencyPortal::sendPage(WiFiClient& c, const char* notice) {
    sendResponseHeader(c);
    c.print("<!doctype html><html lang='nl'><meta name='viewport' content='width=device-width,initial-scale=1'>");
    c.print("<title>MeshOffGridNL Noodmodus</title><style>body{font:17px system-ui;margin:0;background:#101214;color:#f3f5f6}"
            "main{max-width:520px;margin:auto;padding:22px}h1{font-size:25px}p{line-height:1.45}.card{background:#1b1f22;padding:16px;border-radius:16px;margin:12px 0}"
            "a,button{display:block;width:100%;box-sizing:border-box;text-decoration:none;text-align:center;padding:16px;margin:12px 0;border:0;border-radius:14px;background:#e7ecef;color:#111;font:inherit;font-weight:750}"
            "form{margin:0}.help{background:#ffd8d8}.small{font-size:14px;color:#b8c0c5}</style><main>");
    c.print("<h1>MeshOffGridNL Noodmodus</h1><p class='small'>Lokaal netwerk - geen internet nodig.</p>");
    if (notice && notice[0]) {
        c.print("<div class='card'><b>");
        c.print(notice);
        c.print("</b></div>");
    }
    const char* paths[] = {"/safe", "/help", "/moving", "/meeting"};
    const char* labels[] = {"Ik ben veilig","Ik heb hulp nodig","Ik ben onderweg","Bij verzamelpunt"};
    for (uint8_t i=0;i<4;++i) {
        c.print("<form method='post' action='"); c.print(paths[i]);
        c.print("'><input type='hidden' name='t' value='"); c.print(_token);
        c.print("'><button type='submit'>"); c.print(labels[i]); c.print("</button></form>");
    }
    c.print("<a href='/status'>Netwerkstatus / bevestiging</a>");
    c.print("<p>Bevestig elke noodactie op de T-Deck via de telefoonportaal-knop. Een aanvraag is nog geen verzonden bericht.</p>");
    c.print("<div class='card small'>Een hulpvraag gaat via het lokale mesh-netwerk. 112 wordt niet automatisch gebeld."
            " Als telefonie werkt en er direct gevaar is, gebruik 112.</div>");
    c.print("</main></html>");
}

void V29EmergencyPortal::sendNotFound(WiFiClient& c) {
    c.print("HTTP/1.1 302 Found\r\nLocation: http://");
    c.print(_ip);
    c.print("/\r\nConnection: close\r\n\r\n");
}

void V29EmergencyPortal::sendForbidden(WiFiClient& c) {
    c.print("HTTP/1.1 403 Forbidden\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
}

void V29EmergencyPortal::handleRequest(WiFiClient& c, v30::PortalRequest::Action action) {
    using A = v30::PortalRequest;
    if (c.localIP() != WiFi.softAPIP() || action == A::Deny) {
        sendForbidden(c); return;
    }
    if (action == A::Redirect) { sendNotFound(c); return; }
    _lastActivityMs = millis();
    if (action == A::Page) { sendPage(c); return; }
    if (action == A::Status) {
        if (_approval.pending()) sendPage(c, "Wacht op bevestiging op de T-Deck.");
        else sendPage(c, _lastNotice[0] ? _lastNotice : "Geen actie in behandeling.");
        return;
    }
    if (!actionAllowed(millis()) || !_approval.request(action,millis())) {
        sendPage(c, "Er wacht al een actie of probeer later opnieuw."); return;
    }
    _lastNotice[0] = 0;
    sendPage(c, "Aanvraag ontvangen, bevestig op de T-Deck. Nog niet verzonden.");
}

void V29EmergencyPortal::executeApproved(v30::PortalRequest::Action action) {
    using A = v30::PortalRequest;
    uint8_t n=0;
    if (action == A::Help)
        n=v29_emergency_fabric.sendHelpToEmergencyContacts(0,2);
    else if (action == A::Safe)
        n=v29_emergency_fabric.sendCheckInToEmergencyContacts(V29EmergencyFabric::CheckInState::Safe);
    else if (action == A::Moving)
        n=v29_emergency_fabric.sendCheckInToEmergencyContacts(V29EmergencyFabric::CheckInState::Moving);
    else if (action == A::Meeting)
        n=v29_emergency_fabric.sendCheckInToEmergencyContacts(V29EmergencyFabric::CheckInState::AtMeetingPoint);
    else return;
    if(n) snprintf(_lastNotice,sizeof(_lastNotice),
        "Lokaal aangeboden aan %u noodcontact(en). Ontvangst onbevestigd. 112 is niet automatisch gebeld.",(unsigned)n);
    else snprintf(_lastNotice,sizeof(_lastNotice),
        "Geen noodcontacten bereikt. 112 is niet automatisch gebeld.");
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

    // Dispatch only actions that the native approval path has accepted.
    const auto approved = _approval.take(now);
    if (approved != v30::PortalRequest::Deny) executeApproved(approved);
    if (!_portalClient) {
        _portalClient = _server.available();
        if (!_portalClient) return;
        _request.reset();
        _requestStartedMs = now;
        if (_portalClient.localIP() != WiFi.softAPIP()) {
            _portalClient.stop();
            return;
        }
    }
    if ((uint32_t)(now-_requestStartedMs)>=2000u) {
        _portalClient.stop(); _request.reset(); return;
    }
    unsigned budget=128;
    while (budget-- && _portalClient.available() &&
           _request.state()==v30::PortalRequest::Reading) {
        int b=_portalClient.read();
        if (b>=0) _request.feed((char)b);
    }
    if (_request.state()!=v30::PortalRequest::Reading) {
        handleRequest(_portalClient,_request.authorize(_ip,_token));
        _portalClient.stop(); _request.reset();
    }
}

#endif
