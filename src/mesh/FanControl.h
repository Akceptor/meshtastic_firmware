#pragma once

// fanMode is only defined by RF95Interface.cpp (USE_RF95); LR11x0/SX126x/SX128x boards that also
// set RF95_FAN_EN (e.g. bayckrc_dual_band, betafpv_2400_tx_micro) drive the pin with their own
// threshold-only auto logic and have no manual override, so there's nothing to declare for them.
#if defined(RF95_FAN_EN) && defined(USE_RF95)

// Manual override for the PA fan, set from the on-device System > Fan Toggle menu.
// Auto = threshold-based on tx_power (see RF95_FAN_ON_THRESHOLD_DBM); ForceOn/ForceOff pin the
// fan regardless of power. Runtime-only, resets to Auto on reboot.
enum class FanMode : uint8_t { Auto, ForceOn, ForceOff };
extern FanMode fanMode;

#endif
