#include "LR2021Adapter.h"

#ifdef OPS_V8_ENABLE_LR2021

#include <RadioLib.h>

namespace ops::v8 {

class LR2021Adapter::ModuleHolder {
public:
    ModuleHolder(int cs, int irq, int rst, int busy)
        : module(cs, irq, rst, busy), radio(&module) {}
    Module module;
    LR2021 radio;
};

bool LR2021Adapter::begin(int csPin, int irqPin, int resetPin, int busyPin) {
    if (csPin < 0 || irqPin < 0 || resetPin < 0 || busyPin < 0) return false;
    end();
    _holder = new ModuleHolder(csPin, irqPin, resetPin, busyPin);
    _holder->radio.tcxoVoltage = 0.0f;
    const int16_t state = _holder->radio.begin(868.618f, 62.5f, 8, 8,
                                                RADIOLIB_LR2021_LORA_SYNC_WORD_PRIVATE,
                                                10, 8, 0.0f);
    _ready = state == RADIOLIB_ERR_NONE;
    if (!_ready) end();
    return _ready;
}

void LR2021Adapter::end() {
    if (_holder) {
        if (_ready) _holder->radio.standby();
        delete _holder;
        _holder = nullptr;
    }
    _ready = false;
}

bool LR2021Adapter::send(const uint8_t* data, size_t len) {
    if (!_ready || !data || len == 0 || len > 255) return false;
    return _holder->radio.transmit(data, len) == RADIOLIB_ERR_NONE;
}

bool LR2021Adapter::receive(uint8_t* data, size_t capacity, size_t& received) {
    received = 0;
    if (!_ready || !data || capacity == 0) return false;
    const size_t len = _holder->radio.getPacketLength();
    if (len == 0 || len > capacity) return false;
    if (_holder->radio.receive(data, len) != RADIOLIB_ERR_NONE) return false;
    received = len;
    return true;
}

float LR2021Adapter::lastRssi() const {
    return (_ready && _holder) ? _holder->radio.getRSSI() : 0.0f;
}

float LR2021Adapter::lastSnr() const {
    return (_ready && _holder) ? _holder->radio.getSNR() : 0.0f;
}

} // namespace ops::v8

#endif
