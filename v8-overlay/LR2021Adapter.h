#pragma once

#include <cstddef>
#include <cstdint>

namespace ops::v8 {

class LR2021Adapter {
public:
    static constexpr bool compiledIn() {
#ifdef OPS_V8_ENABLE_LR2021
        return true;
#else
        return false;
#endif
    }

#ifdef OPS_V8_ENABLE_LR2021
    bool begin(int csPin, int irqPin, int resetPin, int busyPin);
    void end();
    bool send(const uint8_t* data, size_t len);
    bool receive(uint8_t* data, size_t capacity, size_t& received);
    bool ready() const { return _ready; }
    float lastRssi() const;
    float lastSnr() const;
private:
    class ModuleHolder;
    ModuleHolder* _holder = nullptr;
    bool _ready = false;
#else
    bool begin(int, int, int, int) { return false; }
    void end() {}
    bool send(const uint8_t*, size_t) { return false; }
    bool receive(uint8_t*, size_t, size_t& received) { received = 0; return false; }
    bool ready() const { return false; }
    float lastRssi() const { return 0.0f; }
    float lastSnr() const { return 0.0f; }
#endif
};

} // namespace ops::v8
