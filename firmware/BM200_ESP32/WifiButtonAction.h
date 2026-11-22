#pragma once
#include <stdint.h>

// Pure duration classifier shared by firmware and host-side boundary tests.
// The action is only evaluated after the BOOT button has been released.
enum class WifiBootAction : uint8_t { None, ToggleAP, ClearAPPassword };

constexpr WifiBootAction wifiBootActionForHold(uint32_t heldMs,
                                               uint32_t toggleMs,
                                               uint32_t clearMs,
                                               uint32_t ignoreMs) {
    return heldMs >= clearMs && heldMs < ignoreMs ? WifiBootAction::ClearAPPassword
         : heldMs >= toggleMs && heldMs < clearMs ? WifiBootAction::ToggleAP
         : WifiBootAction::None;
}
