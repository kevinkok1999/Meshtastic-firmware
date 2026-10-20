#pragma once
#include <stddef.h>
#include <stdint.h>

// Pure C++ parser usable on ESP32 and in desktop tests. Read until the entire
// HTTP request line (LF) is received, not until the first space appears.
enum class V29RequestLineResult : uint8_t {
    Complete, Timeout, TooLong, Disconnected
};

template <typename Client, typename Clock, typename Sleep>
V29RequestLineResult v29ReadHttpRequestLine(
    Client& client, char* dest, size_t capacity,
    Clock nowMs, Sleep sleepMs, uint32_t timeoutMs) {
    if (!dest || capacity < 2) return V29RequestLineResult::TooLong;
    size_t used = 0;
    dest[0] = '\0';
    const uint32_t startedAt = nowMs();
    while ((uint32_t)(nowMs() - startedAt) < timeoutMs) {
        while (client.available() > 0) {
            const int value = client.read();
            if (value < 0) break;
            const char ch = (char)value;
            if (ch == '\n') {
                dest[used] = '\0';
                return V29RequestLineResult::Complete;
            }
            if (ch == '\r') continue;
            if (used + 1 >= capacity) return V29RequestLineResult::TooLong;
            dest[used++] = ch;
            dest[used] = '\0';
        }
        if (!client.connected()) return V29RequestLineResult::Disconnected;
        sleepMs(1);
    }
    return V29RequestLineResult::Timeout;
}
