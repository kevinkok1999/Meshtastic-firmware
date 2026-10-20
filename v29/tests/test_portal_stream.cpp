#include <stdint.h>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "../overlay/src/helpers/esp32/V29HttpRequestLine.h"

struct ChunkClient {
    std::vector<std::string> chunks;
    std::string buffer;
    size_t next = 0;
    size_t pos = 0;
    uint32_t ms = 0;
    explicit ChunkClient(std::vector<std::string> incoming) : chunks(incoming) { tick(); }
    void tick() {
        ms += 10;
        if (next < chunks.size()) {
            buffer += chunks[next];
            ++next;
        }
    }
    int available() const { return (int)(buffer.size() - pos); }
    int read() { return pos < buffer.size() ? (unsigned char)buffer[pos++] : -1; }
    bool connected() const { return true; }
};

void expect(const std::vector<std::string>& chunks,
            V29RequestLineResult expected, const char* fullLine) {
    ChunkClient client(chunks);
    char parsed[192] = {};
    auto result = v29ReadHttpRequestLine(client, parsed, sizeof(parsed),
        [&client]() -> uint32_t { return client.ms; },
        [&client](uint32_t) { client.tick(); }, 700U);
    if (result != expected ||
        (expected == V29RequestLineResult::Complete && std::strcmp(parsed, fullLine) != 0)) {
        std::cerr << "Request parser regression: result=" << (int)result
                  << " value='" << parsed << "'\n";
        std::exit(1);
    }
}

int main() {
    expect({"GET ", "/safe?t=0123456789ABCDEF ", "HTTP/1.1\r\n"},
           V29RequestLineResult::Complete, "GET /safe?t=0123456789ABCDEF HTTP/1.1");
    expect({"POST /help?t=0123456789ABCDEF HTTP/1.1\r", "\n"},
           V29RequestLineResult::Complete, "POST /help?t=0123456789ABCDEF HTTP/1.1");
    expect({"GET ", "/", " HTTP/1.0\n"},
           V29RequestLineResult::Complete, "GET / HTTP/1.0");
    expect({std::string(200, 'X') + "\n"},
           V29RequestLineResult::TooLong, "");
    expect({"GET /partial "}, V29RequestLineResult::Timeout, "");
    std::cout << "PASS: V29 fragmented/partial/oversized HTTP request-line parser\n";
    return 0;
}
