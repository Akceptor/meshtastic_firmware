# Emax 900 OLED TX

ESP32 + SX1276 + external PA + I2C OLED, 900/915 MHz.
Pin source: ExpressLRS Targets [`TX/EMAX 900 OLED.json`](https://github.com/ExpressLRS/targets/blob/master/TX/EMAX%20900%20OLED.json).

## Build

```
pio run -e emax_900_tx_oled -t upload
```

### Sync word override (needed to talk to LR11xx nodes)

```
PLATFORMIO_BUILD_FLAGS="-DMESHTASTIC_LORA_SYNCWORD=0x12" pio run -e emax_900_tx_oled -t upload
```

Meshtastic uses sync word `0x2b`, which is not one of the two values Semtech defines
(`0x12` private, `0x34` LoRaWAN). **LR11xx receivers cannot detect `0x2b` from an SX127x
transmitter** — upstream issue
[meshtastic/firmware#4775](https://github.com/meshtastic/firmware/issues/4775), open since
September 2024 and deferred to 3.0.

The failure is nasty to diagnose: this board transmits at full power on the exact right
frequency and the LR11xx node hears nothing at all. Its `rxBad` counter stays at **0** —
it never detects a preamble, so there is no CRC failure to count. The reverse direction
works perfectly, which makes it look like a transmitter fault when nothing is wrong.

`MESHTASTIC_LORA_SYNCWORD` is defined in `src/mesh/RadioLibInterface.h` and defaults to
`0x2b`, so stock builds are unchanged.

> **Every node in the mesh must be built with the same value.** A node built with `0x12`
> is invisible to all stock Meshtastic devices. Only appropriate for a closed mesh.

## How the PA is driven

ExpressLRS keeps the SX1276 at `RegPaConfig=0xF0` — PA_BOOST with `OutputPower=0`, which
is **+2 dBm** — and lets the external PA supply all the gain via the APC2 DAC on GPIO26.

Chain, for anyone re-deriving it:

1. `"power_control": 3` selects `POWER_OUTPUT_DACWRITE` (`Unified_ESP32_TX.h:44`)
2. No `power_values2` in the target JSON, so `POWERMGNT::setPower()` never calls
   `Radio.SetOutputPower()` (`POWERMGNT.cpp:258-270`)
3. The chip therefore keeps the init default from `SX127x.cpp:83-92`
4. On PA_BOOST, `Pout = 17 - (15 - OutputPower)` = **+2 dBm**

Hence `RF95_MAX_POWER 2`. Driving the chip harder overdrives the PA input — the port
originally ran it at +17 dBm, a 15 dB overdrive.

Also from the ELRS target: **PA_BOOST not RFO** (`radio_rfo_hf` absent), **higher DAC =
more power** (the Radiomaster Bandit is inverted — do not copy its table), **RXEN only, no
TXEN**, and no TCXO (setting `RegTcxo` bit 4 kills the chip's clock, confirming a plain
crystal).

## PA power calibration

Measured with a power meter at 869 MHz, PA fed from a powerbank, chip at +2 dBm:

| DAC | Measured | Actual dBm |
|-----|----------|------------|
| 50  | 147 mW   | 21.7 |
| 60  | 260 mW   | 24.2 |
| 70  | 437 mW   | 26.4 |
| 75  | 542 mW   | 27.3 |

Roughly **0.25 dB per DAC unit**, compressing to ~0.19 dB/unit above DAC 70.

The ExpressLRS `power_values` labels are **~4.4 dB optimistic** on this board — ELRS calls
DAC 50 its "50 mW" step and it actually produces 147 mW. The table in `getDACandDB()` is
built from the measurements above, not from the ELRS labels.

Entries outside DAC 50-75 are extrapolated and should be re-measured:

- **Below DAC 50** — the low end is unverified. DAC 30 is the ELRS minimum and still yields
  roughly 17 dBm, so this PA cannot go quiet. Requests below 17 dBm clamp to that floor
  rather than going lower, and the menu's 10 and 14 dBm entries are not actually reachable
  on this board.
- **Above DAC 75** — the PA is compressing, so the 30 dBm row is a guess.

Note an earlier revision of this file listed a very different DAC table (DAC 25 = 20 dBm
and so on). Those readings were taken while the SX1276 was overdriving the PA at +17 dBm
and do not apply now that the drive level is correct.

## Hardware notes

- Fan on GPIO32, driven high at init
- NeoPixel on GPIO27, GRB, 1 LED
- 5-way joystick on GPIO33 (ADC), values 2010 / 1230 / 635 / 2730 / 0 / 4095
- DIO1 not connected; DIO0 carries both RxDone and TxDone
- `MESHTASTIC_EXCLUDE_*` flags in `platformio.ini` trim unused modules to keep the OTA
  image inside the dual-app partition layout
