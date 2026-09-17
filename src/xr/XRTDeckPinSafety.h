#pragma once

#include <cstdint>

namespace meshoffgrid::xr {

constexpr bool isKnownTDeckReservedGpio(int pin)
{
    switch (pin) {
    // GPS UART on T-Deck Plus
    case 43:
    case 44:
    // Shared display / SD / LoRa SPI
    case 38:
    case 40:
    case 41:
    // SX1262 control
    case 9:
    case 13:
    case 17:
    case 45:
    // I2C / keyboard-touch related lines
    case 8:
    case 16:
    case 18:
    case 46:
        return true;
    default:
        return false;
    }
}

constexpr bool validDedicatedUartPair(int rxPin, int txPin)
{
    return rxPin >= 0 && txPin >= 0 && rxPin != txPin && !isKnownTDeckReservedGpio(rxPin) &&
           !isKnownTDeckReservedGpio(txPin);
}

} // namespace meshoffgrid::xr

#if defined(MESHOFFGRID_XBEE_HARDWARE_BUILD)
static_assert(MESHOFFGRID_XBEE_RX_PIN >= 0, "Real XBee hardware build requires a validated RX GPIO");
static_assert(MESHOFFGRID_XBEE_TX_PIN >= 0, "Real XBee hardware build requires a validated TX GPIO");
static_assert(MESHOFFGRID_XBEE_RX_PIN != MESHOFFGRID_XBEE_TX_PIN, "XBee RX and TX GPIOs must be different");
static_assert(!meshoffgrid::xr::isKnownTDeckReservedGpio(MESHOFFGRID_XBEE_RX_PIN),
              "Chosen XBee RX GPIO conflicts with a known T-Deck Plus peripheral");
static_assert(!meshoffgrid::xr::isKnownTDeckReservedGpio(MESHOFFGRID_XBEE_TX_PIN),
              "Chosen XBee TX GPIO conflicts with a known T-Deck Plus peripheral");
#endif
