#include "variant.h"
#include <Arduino.h>
// Same state ELRS devBackpack initialize() leaves it in: held off, not in bootloader.
void earlyInitVariant()
{
    pinMode(TX15_BACKPACK_BOOT, OUTPUT);
    digitalWrite(TX15_BACKPACK_BOOT, LOW);
    pinMode(TX15_BACKPACK_EN, OUTPUT);
    digitalWrite(TX15_BACKPACK_EN, LOW);
}
