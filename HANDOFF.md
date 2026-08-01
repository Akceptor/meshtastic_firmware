# Meshtastic Firmware — Board Support Handoff

Repo: https://github.com/Akceptor/meshtastic_firmware (branch: `develop-2.7.26`)
Base: upstream meshtastic/firmware @ same branch

---

## TL;DR for whoever picks this up

The **EMAX 900 OLED TX** port boots, drives its display and menus, receives at every
modem preset, and transmits **only at SF7 (ShortFast)**. At SF8 and above the transmission
is not detected by any receiver, while reception in the opposite direction keeps working.

The PA drive level was genuinely wrong (15 dB overdrive) and is now fixed — the board
produces a clean 260 mW at the antenna on the correct frequency. **That fix did not
resolve the SF>=8 transmit failure.** Everything programmable on the chip has been
verified correct against a register dump. The remaining fault is not yet identified and
is most likely below the firmware layer. See "Open Bug" below for the full elimination
table so nobody repeats the work.

---

## Boards

### 1. Emax 900 OLED TX (`emax_900_tx_oled`)

**Hardware:** ESP32 + SX1276 (900 MHz) + I2C SSD1306 OLED + 5-way ADC joystick +
NeoPixel (GPIO27) + fan (GPIO32) + external PA controlled by DAC
**HW model ID:** 144 (`meshtastic_HardwareModel_EMAX_900_TX_OLED`)
**Pin source:** ExpressLRS Targets `TX/EMAX 900 OLED.json`
**Reference oscillator:** plain crystal, no TCXO (proven — see Open Bug)

| Function | GPIO |
|---|---|
| SX1276 SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 |
| SX1276 DIO0 / RESET | 4 / 14 |
| SX1276 DIO1 | not connected |
| I2C SDA / SCL | 22 / 21 |
| NeoPixel | 27 |
| Fan enable | 32 |
| PA control, APC2 DAC | 26 |
| RX enable switch | 12 |
| Joystick ADC | 33 |

Joystick ADC values (UP/DOWN/LEFT/RIGHT/OK/IDLE): 2010, 1230, 635, 2730, 0, 4095

**Status:** Builds, boots, OLED and menus fine, RX fine at all presets, TX works only at
SF7. Blocked — see Open Bug.

### 2. BAYCKRC 900/2400 Dual Band Nano TX (`bayckrc_dual_band`)

**Hardware:** ESP32 + LR1120 (dual-band sub-GHz + 2.4 GHz) + NeoPixel (GPIO12) +
fan (GPIO4), no display
**HW model ID:** 145 (`meshtastic_HardwareModel_BAYCKRC_DUAL_BAND`)
**Pin source:** ExpressLRS Targets `TX/BAYCKRC Dual Band.json` + Gemini overlay

| Function | GPIO |
|---|---|
| LR1120 SCK / MISO / MOSI / CS | 25 / 33 / 32 / 27 |
| LR1120 RESET / BUSY / IRQ | 26 / 36 / 37 |
| NeoPixel | 12 |
| Fan enable | 4 |
| Backpack serial RX/TX | 18 / 5 (occupied — do not reassign) |
| Backpack enable / boot | 14 / 23 (occupied — do not reassign) |

Second LR1120 (Gemini variant, **unused** — Meshtastic is single-radio): CS 15,
RESET 21, BUSY 39, IRQ 34. Documented in variant.h for possible future 433+868 bridge work.

Power notes: `power_control: 0` means direct dBm, no DAC. Sub-GHz range -16..+5 dBm.
`LR11X0_DIO3_TCXO_VOLTAGE 1.8`, `LR11X0_DIO_AS_RF_SWITCH`, `radio_dcdc: true`.

**Status:** Radio initialises (result=0). LED status implemented in `variant.cpp`.
**Pending:** confirm LED green on boot; add `lora.setRxGain(14)`; confirm sub-GHz TX/RX.

---

## How the EMAX PA actually works (verified against ExpressLRS source)

This took a while to establish and is the part most likely to be re-derived wrongly.

1. `"power_control": 3` in the target JSON maps to `POWER_OUTPUT_DACWRITE`
   (`include/target/Unified_ESP32_TX.h:44`).
2. The JSON has **no `power_values2`**, so `POWERMGNT::setPower()` never calls
   `Radio.SetOutputPower()` (`lib/POWERMGNT/POWERMGNT.cpp:258-270`). The SX1276's own
   output level therefore stays at whatever init set.
3. Init sets it once: `SetOutputPower(SX127X_MAX_OUTPUT_POWER)` where that constant is
   `0b01110000` (`lib/SX127xDriver/SX127x.cpp:83-92`). Masked with `SX127X_PA_POWER_MASK`
   (0x7F) and OR'd with `PA_SELECT_BOOST` gives **RegPaConfig = 0xF0**.
4. On PA_BOOST, `Pout = 17 - (15 - OutputPower)`. The OutputPower nibble is 0, so the
   chip transmits at **+2 dBm**. The MaxPower bits (0x70) only matter for RFO.
5. All remaining gain comes from the external PA, set by `dacWrite` on APC2 (GPIO26).

So: **SX1276 at +2 dBm, external PA does everything else.** The port originally ran the
chip at +17 dBm, a 15 dB overdrive of the PA input.

Also confirmed from ELRS:
- **PA_BOOST, not RFO** — `radio_rfo_hf` is absent from `EMAX 900 OLED.json`.
  (Radiomaster Bandit has it `true`, which is why that variant uses `USE_RF95_RFO`.)
- **Higher DAC = more power** on this board (Bandit is inverted — do not copy its table).
- **RXEN only, no TXEN**; RXEN low during TX (`lib/RFAMP/RFAMP_hal.cpp:31`).
- ELRS never writes `RegTcxo` — the module runs its crystal in default mode.

DAC calibration table now in `getDACandDB()`, taken from ELRS `power_values`
`[30,40,50,60,80,90,130,225]`, which map 1:1 onto `PowerLevels_e`:

| DAC | ELRS level | dBm |
|---|---|---|
| 30 | 10 mW | 10 |
| 40 | 25 mW | 14 |
| 50 | 50 mW | 17 |
| 60 | 100 mW | 20 |
| 80 | 250 mW | 24 |
| 90 | 500 mW | 27 |
| 130 | 1000 mW | 30 |
| 225 | 2000 mW | 33 |

---

## OPEN BUG: EMAX transmits only at SF7

### Symptom

| Direction | SF7 | SF8+ |
|---|---|---|
| peer -> EMAX | works | **works** (SF9, SF11, SF12 all confirmed) |
| EMAX -> peer | works | **fails** |

Confirmed against **two independent receivers**. The peer receives LongFast fine from a
third device, so the peer's receiver is not at fault.

Critical detail: the receiving side's **`rxBad` stays at 0**. It is not failing CRC — it
never detects a preamble at all. Nothing arrives that looks like the start of a packet.

### Verified correct — do not re-investigate

| Checked | Method | Result |
|---|---|---|
| PA drive level | ELRS source trace | fixed, `PaConfig=0xf0`, ELRS parity |
| RF output power | power meter | 260 mW at 869 MHz |
| PA bias current / supply sag | forced DAC=0, PA cold, peer at 30 cm | SF7 still worked, SF9 still failed |
| Power supply | external powerbank | not USB-port related |
| Carrier frequency | peer's `Corrected frequency offset` | **0.000000 Hz** |
| Link margin | peer log | `rxRSSI=-41, rxSNR=14.75` at 30 cm |
| SF register | `MC2=0x94` at TX | SF9, correct |
| Bandwidth / coding rate | `MC1=0x82` | BW250, CR4/5, explicit header |
| LDRO | `MC3=0x04` | bit 3 = 0, correct at SF9 (2.05 ms symbol vs 16 ms threshold) |
| Sync word | `Sync=0x2b` | correct |
| IQ inversion | `InvIQ=0x27 InvIQ2=0x1d` | both non-inverted defaults |
| Detection optimize | `DetOpt=0xc3 DetThr=0x0a` | correct for SF7-12 |
| Frequency register | `Frf=0xd9619a` | 869.5250 MHz |
| Transmit truncation | trace timestamps | 171 ms measured vs 120 ms computed — full duration |
| TxDone | `IrqFlags=0x08` | clean |
| Firmware interrupting TX | trace of all state changes | nothing touches the radio mid-send |
| LF/HF mode bit hack | removed it | no change; left removed (stock upstream) |
| TCXO | set `RegTcxo` bit 4 | radio died, `err=-16` -> **plain crystal, no TCXO** |
| Reference oscillator quality | logic | ruled out: same VCO receives SF12 fine |

### Theories tried and disproved

- **PA overdrive / saturation** — real defect, fixed, did not resolve the bug.
- **Supply droop or thermal drift over long key-down** — fails with the PA cold too.
- **Frequency offset** — the EMAX measures +7155 Hz on receive, but the peer measures
  **0 Hz** on the EMAX's transmissions. The 7 kHz belongs to the peer's transmitter.
  Also, the EMAX itself decodes a 7 kHz-offset SF12 packet, so that magnitude is tolerable.
- **LDRO mismatch** — measured off, which is correct at SF9; it also lives in a register
  shared by TX and RX, so a mismatch would break the working receive path too.
- **TX truncated mid-preamble** — the 40 ms/41.5 ms coincidence between ShortFast's total
  airtime and MediumFast's preamble was compelling, but measurement showed a full 171 ms
  transmit with a clean TxDone.
- **Reference oscillator phase noise** — cannot be it. TX and RX share the VCO and
  reference, and receive works at SF12 with 16 ms symbols.

### Useful context

**ExpressLRS never uses SF above ~9 on this hardware** — short packets, low SF, high duty
cycle. So "ELRS works fine on this module" is *not* evidence that its transmit path is
sound at long symbol durations. A hardware limitation that only appears above SF7 would
never surface in the firmware this module was designed for.

### Suggested next steps

1. **SDR waterfall on 869.525 MHz** during an SF9 transmit. This is the highest-value
   test by far and the one thing register inspection cannot answer — it shows directly
   whether the chirp is present, clean and sweeping correctly. Everything else is guesswork
   until someone looks at the actual waveform.
2. **ShortSlow (SF8)** — never tested. Brackets the cliff between working and broken.
   One config change, no firmware.
3. **Power meter at SF9 vs SF7** — must read identical, since the PA cannot know the
   spreading factor. If it differs, something physical happens during longer keying.

---

## Diagnostic instrumentation currently in the tree

`src/mesh/RF95Interface.cpp` carries EMAX-only tracing, all guarded by
`#if defined(EMAX_900_TX_OLED)`. It is verbose and should be stripped before any
upstream PR, but is left in place because the bug is unresolved.

- `EMAX trace <millis>: setTransmitEnable(n)` / `setStandby` / `startReceive`
- `EMAX trace <millis>: TX start PaConfig=.. PaDac=..`
- `EMAX regs [TX|RX] ...` — full modem register dump via `dumpModemRegs()`

Comparing the `TX start` timestamp against the following `startReceive` timestamp gives
the **measured** transmit duration. Note `Packet TX: NNNms` in the standard log is
`getPacketTime()` — computed from the firmware's own SF/BW variables, not measured, so it
cannot detect a truncated transmission.

---

## Files created / modified

### New
| Path | Description |
|---|---|
| `variants/esp32/emax_900_tx_oled/variant.h` | Pin + radio config |
| `variants/esp32/emax_900_tx_oled/platformio.ini` | PlatformIO env |
| `variants/esp32/emax_900_tx_oled/partitions-dual.csv` | Partition layout |
| `variants/esp32/bayckrc_dual_band/variant.h` | Pin + radio config |
| `variants/esp32/bayckrc_dual_band/platformio.ini` | PlatformIO env |
| `variants/esp32/bayckrc_dual_band/rfswitch.h` | LR1120 RF switch table |
| `src/platform/extra_variants/bayckrc_dual_band/variant.cpp` | NeoPixel LED status |

### Modified
| Path | Change |
|---|---|
| `src/mesh/generated/meshtastic/mesh.pb.h` | HW model enums 144, 145 |
| `src/platform/esp32/architecture.h` | `HW_VENDOR` mappings |
| `src/mesh/LR11x0Interface.cpp` | `getVersionInfo` result in separate `verRes` |
| `src/mesh/RF95Interface.cpp` | EMAX DAC table, power fallback, tracing |
| `src/mesh/RF95Interface.h` | `dumpModemRegs()` declaration |

The EMAX build also trims a lot of unused modules via `MESHTASTIC_EXCLUDE_*` in its
`platformio.ini` to keep the OTA image small. Those exclusions are unrelated to the open
bug and work correctly.

---

## Bugs fixed along the way

### EMAX: SX1276 overdriving the external PA
`RF95_MAX_POWER` was 17, giving `RegPaConfig=0xFF` (+17 dBm) into a PA input that ELRS
drives at +2 dBm. Now `RF95_MAX_POWER 2` -> `RegPaConfig=0xF0`. Yields a clean 260 mW.

### EMAX: `tx_power == 0` fallback pinned the DAC
`config.lora.tx_power ? config.lora.tx_power : power` used `power` as the fallback, but
`power` is the SX1276 driver level (now always 2), not the requested EIRP. That would have
locked the PA to the default table row forever. Now falls back to the region power limit.

### EMAX: DAC value overflowed a signed type
`int8_t powerDAC` cannot hold the top DAC value of 225. Changed to `uint8_t`.

### EMAX: RFO vs PA_BOOST mismatch
`USE_RF95_RFO` routed output to the RFO pad; the external PA input is on PA_BOOST.
Removed, PA_BOOST is the default. Confirmed against the ELRS target JSON.

### EMAX: speculative LowFrequencyModeOn hack removed
A read-modify-write clearing `RegOpMode` bit 3 after `begin()` was added on the theory
that 869 MHz needs HF mode. It was never validated, deviates from stock upstream (which
works at 869 MHz on every other SX1276 board), and removing it changed nothing. Gone.

### LR11x0Interface: getVersionInfo aborted init (BAYCKRC)
After a successful `lora.begin()`, `getVersionInfo()` fails on LR1120 because WiFi/GNSS
aren't initialised. The result was stored in `res`, so `setCRC`, `setDCDC` and
`startReceive` were all skipped and init returned false. Now uses a separate `verRes`.

### BAYCKRC: USE_LR1121 -> USE_LR1120
Board has an LR1120 (device ID 0x02). `findChip()` retried 10x then returned
`-2 CHIP_NOT_FOUND`.

### BAYCKRC: NeoPixel RMT conflict
`ENABLE_AMBIENTLIGHTING` claimed the RMT channel on GPIO12 before `lateInitVariant()`
could, so the second `pixel.begin()` failed silently. Omitted deliberately;
`variant.cpp` is the sole NeoPixel controller.

---

## Architecture notes

- Variants live in `variants/esp32/<board>/` as `variant.h` + `platformio.ini`. The
  `esp32_base` `build_src_filter` auto-compiles
  `src/platform/extra_variants/<board>/variant.cpp` if present.
- `lateInitVariant()` runs after all subsystems init — safe place for NeoPixel and radio
  checks. `earlyInitVariant()` runs before, use only for strapping pins.
- RadioLib SPI init: `SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS)` in `main.cpp`,
  using the `LORA_*` defines from variant.h.
- `RadioInterface::limitPower()` supports a `TX_GAIN_LORA` offset for fixed-gain external
  PAs. This board uses the Bandit-style `getDACandDB()` path instead, because its PA gain
  is variable via APC2.
