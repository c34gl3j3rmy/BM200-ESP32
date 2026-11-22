#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "../firmware/BM200_ESP32/WifiButtonAction.h"

int main() {
    constexpr uint32_t toggle = 5000;
    constexpr uint32_t clear = 30000;
    constexpr uint32_t ignore = 60000;
    auto classify = [](uint32_t ms) {
        return wifiBootActionForHold(ms, toggle, clear, ignore);
    };
    assert(classify(0) == WifiBootAction::None);
    assert(classify(4999) == WifiBootAction::None);
    assert(classify(5000) == WifiBootAction::ToggleAP);
    assert(classify(10000) == WifiBootAction::ToggleAP);
    assert(classify(29999) == WifiBootAction::ToggleAP);
    assert(classify(30000) == WifiBootAction::ClearAPPassword);
    assert(classify(45000) == WifiBootAction::ClearAPPassword);
    assert(classify(59999) == WifiBootAction::ClearAPPassword);
    assert(classify(60000) == WifiBootAction::None);
    assert(classify(100000) == WifiBootAction::None);
    // millis() rolls over; unsigned subtraction still gives the duration.
    const uint32_t pressedAt = UINT32_MAX - 30000U;
    const uint32_t releasedAt = 15000U;
    assert(classify(releasedAt - pressedAt) == WifiBootAction::ClearAPPassword);
    puts("BOOT boundaries 5/30/60 s and millis rollover: OK");
}
