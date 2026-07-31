# Emax 900 OLED TX

ESP32 + SX1276 + external PA + I2C OLED, 900/915 MHz.

## PA Power Calibration

PA is controlled via GPIO26 DAC (APC2 pin). Relationship is non-linear — empirically measured:

| DAC | dBm | Output power | Notes |
|-----|-----|--------------|-------|
| 25  | 20  | ~70 mW  | |
| 30  | 21  | ~100 mW | |
| 35  | 22  | ~140 mW | |
| 40  | 23  | ~200 mW | |
| 45  | 24  | ~260 mW | |
| 50  | 25  | ~350 mW | |
| **55** | **27** | **~460 mW** | **default — stable on USB power** |
| 60  | 28  | ~580 mW | requires powerbank/USB-C PD; collapses on USB 2.0 (500 mA limit) |

`RF95_MAX_POWER` is set to 10 (SX1276 output before external PA). Increasing it beyond 17 with low DAC values overdrives the PA into protection.

## Comparison: Radiomaster Bandit PA curve

Bandit uses the same DAC+APC2 approach but with a different PA — its curve is in `RF95Interface.cpp` (`getDACandDB()`):

| DAC | Output | Bandit variant |
|-----|--------|----------------|
| 165 | ~100 mW | Bandit full |
| 155 | ~250 mW | Bandit full |
| 142 | ~500 mW | Bandit full |
| 110 | ~1000 mW | Bandit full |
| 168 | ~100 mW | Bandit Nano |
| 148 | ~250 mW | Bandit Nano |
| 128 | ~500 mW | Bandit Nano |
| 90 | ~1000 mW | Bandit Nano |

EMAX hits 460 mW at DAC=55 vs Bandit's 500 mW at DAC=142. EMAX PA reaches peak gain at much lower APC2 voltages — different PA chip or different output stage wiring.
