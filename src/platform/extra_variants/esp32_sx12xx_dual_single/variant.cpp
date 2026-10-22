#include "configuration.h"

#ifdef ESP32_SX12XX_DUAL_SINGLE

// The unused second radio shares SPI MISO; an unselected NSS must be held high or it can pull the bus low.
void earlyInitVariant()
{
    pinMode(13, OUTPUT);
    digitalWrite(13, HIGH);
}

#endif
