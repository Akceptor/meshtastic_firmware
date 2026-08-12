#pragma once

#ifdef RF95_FAN_EN

// Manual override for the PA fan, set from the on-device System > Fan Toggle menu.
// Auto = threshold-based on tx_power (see RF95_FAN_ON_THRESHOLD_DBM); ForceOn/ForceOff pin the
// fan regardless of power. Runtime-only, resets to Auto on reboot.
enum class FanMode : uint8_t { Auto, ForceOn, ForceOff };
extern FanMode fanMode;

#endif
