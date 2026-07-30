# Emax 900 OLED TX

ESP32 + SX1276 + external PA + I2C OLED, 900/915 MHz.

## PA Power Calibration

PA is controlled via GPIO26 DAC (APC2 pin). Relationship is non-linear — empirically measured:

| `RF95_PA_LEVEL` (DAC) | Output power | Notes |
|-----------------------|--------------|-------|
| 45 | ~260 mW | |
| 50 | ~350 mW | |
| **55** | **~460 mW** | **default — stable on USB power** |
| 60 | ~580 mW | requires powerbank/USB-C PD; collapses on USB 2.0 (500 mA limit) |

`RF95_MAX_POWER` is set to 10 (SX1276 output before external PA). Increasing it beyond 17 with low DAC values overdrives the PA into protection.
