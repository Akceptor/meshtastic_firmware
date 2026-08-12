# Meshtastic Firmware — Board Support Handoff

Repo: https://github.com/Akceptor/meshtastic_firmware (branch: `develop-2.7.26`)
Base: upstream meshtastic/firmware @ same branch

---

## OPEN: CRSF Lua handset identification — not working on hardware

`CrsfHandsetModule` (`src/modules/CrsfHandsetModule.{h,cpp}`, `CRSF_UART_PIN 13` in
`variant.h`) was added so the stock ExpressLRS Lua script on an EdgeTX handset can see
"Meshtastic <version>" when this TX module is plugged into the JR bay. **It does not work
yet** — on real hardware the link never receives a single byte.

**Diagnostic in place:** System > CRSF Status menu (commit `d043c102f`) shows live counters
(RX bytes / frames / bad CRC / pings / replies sent) — check it live from the device menu
while plugged into the bay, since USB can't be attached at the same time as the bay
connector.

**Confirmed so far:**
- Module constructs and starts fine — boot log shows
  `CrsfHandset: listening on GPIO13 @ 400000 baud`.
- EdgeTX external RF module slot confirmed set to protocol "Crossfire" (so the radio should
  be driving CRSF onto the bay pin).
- With module plugged into the bay and Lua script running: **all counters read 0** — no
  bytes at all reach the UART. Not a parsing/protocol bug, something upstream of that.
- Fixed (real bug, keep): sync-byte check only accepted `0xC8`. Per ExpressLRS's own
  `CRSFHandset::alignBufferToSync()` (`ExpressLRS/src/lib/Handset/CRSFHandset.cpp:222`),
  frames addressed to an external module also legitimately start with `0xEE`
  (`CRSF_ADDRESS_CRSF_TRANSMITTER`). Now accepts both. Did not fix the all-zero symptom by
  itself, but is a genuine correctness fix worth keeping regardless.
- **Tried and reverted:** replaced `uart_set_mode(UART_MODE_RS485_HALF_DUPLEX)` with manual
  GPIO direction switching (tri-state via `gpio_set_direction()` between RX/TX), mirroring
  ExpressLRS's own `CRSFHandset::duplex_set_RX()/duplex_set_TX()` (classic ESP32 has no
  DE/RE pin, so their driver never trusts `UART_MODE_RS485_HALF_DUPLEX` alone — see
  `ExpressLRS/src/lib/Handset/CRSFHandset.cpp:364-410`). Theory: our TX driver stays
  permanently enabled and fights the handset's own driver on the shared wire. **Made things
  worse — device hung at the boot splash and never got past the Meshtastic logo while
  plugged into the bay.** Reverted back to `uart_set_mode(RS485_HALF_DUPLEX)` in commit
  `d043c102f`. Root cause of *that* regression was never isolated (didn't get to test
  whether it also hung standalone over USB, only tested plugged into the bay).

**Also observed, unexplained:** at one point (still on the `uart_set_mode` build, before the
GPIO revert), exiting the ExpressLRS Lua script caused the TX module itself to reboot. Not
re-confirmed since; worth checking again once the link is working, since it may point to a
brownout from electrical contention on the shared pin — consistent with the driver-fighting
theory above, just not yet proven.

**Next steps, not yet tried:**
- Multimeter/scope on GPIO13 (or the bay connector's data pin) while the handset is powered
  with Crossfire selected, to confirm the handset is actually driving *something* on that
  wire electrically, independent of firmware. If it's flat, the fault is wiring/connector,
  not code.
- Continuity check: GPIO13 pad on the ESP32 to the JR-bay connector's signal pin, to rule
  out a bad trace/solder joint on this specific board.
- If electrical contention is confirmed as the real cause, the fix is still the manual
  direction-switching approach — but the boot hang needs debugging first (add the debug
  build without plugging into the bay, confirm standalone-over-USB boot is clean, then
  retest plugged in).
- Not yet considered: baud/level mismatch (5V vs 3.3V logic) between the handset's bay
  output and the ESP32 pad.

---

## TL;DR

**EMAX 900 OLED TX port works.** It builds, boots, drives the OLED and menus, receives and
transmits at every modem preset.

Two separate faults were found and fixed:

1. **The SX1276 was overdriving the external PA by 15 dB.** Fixed by matching the
   ExpressLRS drive level. Now yields a clean 260 mW at the antenna.
2. **SX127x transmissions were invisible to LR11xx receivers.** This is upstream issue
   [meshtastic/firmware#4775](https://github.com/meshtastic/firmware/issues/4775) —
   Meshtastic's non-standard 0x2b sync word — and is **not** a defect in this port.
   Worked around with a build-time sync word override. See "The sync word trap" below,
   because it will bite anyone testing an SX127x board against an LR11xx peer.

---

## Boards

### 1. Emax 900 OLED TX (`emax_900_tx_oled`)

**Hardware:** ESP32 + SX1276 (900 MHz) + I2C SSD1306 OLED + 5-way ADC joystick +
NeoPixel (GPIO27) + fan (GPIO32) + external PA controlled by DAC
**HW model ID:** 144 (`meshtastic_HardwareModel_EMAX_900_TX_OLED`)
**Pin source:** ExpressLRS Targets `TX/EMAX 900 OLED.json`
**Reference oscillator:** plain crystal, no TCXO (proven — setting `RegTcxo` bit 4 kills
the chip's clock and `startReceive()` returns `err=-16`)

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

**Status:** Working. TX and RX confirmed at ShortFast and MediumFast against an LR1121
peer (with matching sync word).

**Pending:** NeoPixel status LED `variant.cpp` not written for EMAX; currently relies on
default Meshtastic NeoPixel handling.

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

## The sync word trap (read this before debugging any SX127x board)

**Symptom:** an SX127x node transmits at correct power on the correct frequency, and an
LR11xx node hears nothing at all. The reverse direction works fine. The receiver's
`rxBad` counter stays at **0** — it is not failing CRC, it never detects a preamble.

**Cause:** Meshtastic uses sync word `0x2b`, which is not one of the two values Semtech
defines (0x12 private, 0x34 LoRaWAN). LR11xx radios fail to detect it from SX127x
transmitters. Tracked as meshtastic/firmware#4775, open since Sept 2024, still present in
2.7.x, deferred to 3.0.

**This cost a full debugging session.** It presents as an RF fault and survives every
RF-level check, because nothing is actually wrong with the transmitter. It also appeared
SF-dependent here (ShortFast worked, MediumFast and above did not), which made it look
even more like a modem or analogue problem. Do not trust that pattern.

**Workaround:** `-D MESHTASTIC_LORA_SYNCWORD=0x12` at build time
(`src/mesh/RadioLibInterface.h`). Defaults to 0x2b, so stock builds are unchanged.

```
PLATFORMIO_BUILD_FLAGS="-DMESHTASTIC_LORA_SYNCWORD=0x12" pio run -e <env> -t upload
```

**Every node in the mesh must be built with the same value.** A node built with 0x12 is
invisible to all stock Meshtastic devices. This is only appropriate for a closed mesh.

**Diagnostic shortcut:** if an SX127x board can't reach a peer, check the peer's radio
chip *first*. If it's LR11xx, test against an SX126x or SX127x node before investigating
anything else.

---

## How the EMAX PA works (verified against ExpressLRS source)

This is the part most likely to be re-derived wrongly.

1. `"power_control": 3` maps to `POWER_OUTPUT_DACWRITE`
   (`include/target/Unified_ESP32_TX.h:44`).
2. The JSON has **no `power_values2`**, so `POWERMGNT::setPower()` never calls
   `Radio.SetOutputPower()` (`lib/POWERMGNT/POWERMGNT.cpp:258-270`). The SX1276's own
   output level stays at whatever init set.
3. Init sets it once: `SetOutputPower(SX127X_MAX_OUTPUT_POWER)`, that constant being
   `0b01110000` (`lib/SX127xDriver/SX127x.cpp:83-92`). Masked with `SX127X_PA_POWER_MASK`
   (0x7F) and OR'd with `PA_SELECT_BOOST` gives **RegPaConfig = 0xF0**.
4. On PA_BOOST, `Pout = 17 - (15 - OutputPower)`. The OutputPower nibble is 0, so the chip
   transmits at **+2 dBm**. The MaxPower bits (0x70) only matter for RFO.
5. All remaining gain comes from the external PA, set by `dacWrite` on APC2 (GPIO26).

So: **SX1276 at +2 dBm, external PA does everything else.** The port originally ran the
chip at +17 dBm — a 15 dB overdrive of the PA input.

Also confirmed from ELRS:
- **PA_BOOST, not RFO** — `radio_rfo_hf` is absent from `EMAX 900 OLED.json`.
  (Radiomaster Bandit has it `true`, which is why that variant uses `USE_RF95_RFO`.)
- **Higher DAC = more power** on this board (Bandit is inverted — do not copy its table).
- **RXEN only, no TXEN**; RXEN low during TX (`lib/RFAMP/RFAMP_hal.cpp:31`).
- ELRS never writes `RegTcxo` — the module runs its crystal in default mode.

### PA calibration — measured, not from the ELRS labels

The ExpressLRS `power_values` labels turned out to be **~4.4 dB optimistic** on this
board. ELRS calls DAC 50 its "50 mW" step; it actually produces 147 mW. The table in
`getDACandDB()` is therefore built from measurements, taken with a power meter at 869 MHz
with the PA on a powerbank and the chip at +2 dBm:

| DAC | Measured | Actual dBm |
|---|---|---|
| 50 | 147 mW | 21.7 |
| 60 | 260 mW | 24.2 |
| 70 | 437 mW | 26.4 |
| 75 | 542 mW | 27.3 |

About **0.25 dB per DAC unit**, compressing to ~0.19 dB/unit above DAC 70.

Rows outside DAC 50-75 are extrapolated and want re-measuring:

- **Below DAC 50** unverified. DAC 30 is the ELRS minimum and still gives roughly 17 dBm,
  so this PA cannot go quiet. Requests under 17 dBm clamp to that floor, which means the
  menu's 10 and 14 dBm entries are not reachable on this board.
- **Above DAC 75** the PA is compressing, so the 30 dBm row is a guess.

An earlier revision of the variant README listed a completely different DAC table
(DAC 25 = 20 dBm etc). Those readings were taken while the SX1276 was overdriving the PA
at +17 dBm and no longer apply.

---

## Bugs fixed

### EMAX: SX1276 overdriving the external PA
`RF95_MAX_POWER` was 17, giving `RegPaConfig=0xFF` (+17 dBm) into a PA input that ELRS
drives at +2 dBm. Now `RF95_MAX_POWER 2` -> `RegPaConfig=0xF0`.

### EMAX: `tx_power == 0` fallback pinned the DAC
`config.lora.tx_power ? config.lora.tx_power : power` used `power` as the fallback, but
`power` is the SX1276 driver level (now always 2), not the requested EIRP. That would have
locked the PA to the default table row. Now falls back to the region power limit.

### EMAX: DAC value overflowed a signed type
`int8_t powerDAC` cannot hold the top DAC value of 225. Changed to `uint8_t`.

### EMAX: RFO vs PA_BOOST mismatch
`USE_RF95_RFO` routed output to the RFO pad; the external PA input is on PA_BOOST.
Removed — PA_BOOST is the default. Confirmed against the ELRS target JSON.

### EMAX: speculative LowFrequencyModeOn hack removed
A read-modify-write clearing `RegOpMode` bit 3 after `begin()` was added on the theory
that 869 MHz needs HF mode. Never validated, deviates from stock upstream (which works at
869 MHz on every other SX1276 board), and removing it changed nothing.

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

## Other changes

### TX power menu (`src/graphics/draw/MenuHandler.cpp`)
Added low-power steps 10 / 14 / 17 dBm, matching the ELRS DAC rows 30 / 40 / 50
(10 / 25 / 50 mW). Previously the menu only offered 20-28 dBm, so the bottom half of the
DAC table was unreachable from the device.

Replaced the switch plus 9-branch if-chain with a `txPowerDbm[]` lookup, so adding steps
is now a one-line edit. Also fixed the pre-select: when `tx_power` is 0 or off-list the
menu now highlights "Back" rather than falsely displaying 27 dBm.

This file is shared by all variants — the new steps appear on every board's menu. Harmless
(10-17 dBm is valid everywhere) but worth knowing before upstreaming.

Note the legacy mW labels are inconsistent with their own dBm values ("70 mW (20 dBm)" —
20 dBm is 100 mW). They appear to be empirical measurements against an older power table.
The three new labels are honest conversions.

### On-device menu vs app power setting
Both write the same field, `config.lora.tx_power`. The menu sets it directly then calls
`service->reloadConfig(SEGMENT_CONFIG)`; the app goes through `AdminModule.cpp:1414`.
Last writer wins, both take effect immediately via `reconfigure()`, which rewrites the
APC2 DAC at `RF95Interface.cpp`. Neither is authoritative over the other.

`getDACandDB()` interpolates between table rows and returns the default (DAC 50, ~50 mW)
for anything outside 10-33 dBm — so a sub-10 dBm request fails *upward*, not downward.
Region limits cap the top end well below 33.

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
| `src/mesh/RF95Interface.cpp` | EMAX DAC table, power fallback |
| `src/mesh/RadioLibInterface.h` | `MESHTASTIC_LORA_SYNCWORD` build override |
| `src/graphics/draw/MenuHandler.cpp` | TX power menu low steps |

The EMAX build trims unused modules via `MESHTASTIC_EXCLUDE_*` in its `platformio.ini` to
keep the OTA image small. Those exclusions work correctly and were never implicated in
any of the above.

---

## Architecture notes

- Variants live in `variants/esp32/<board>/` as `variant.h` + `platformio.ini`. The
  `esp32_base` `build_src_filter` auto-compiles
  `src/platform/extra_variants/<board>/variant.cpp` if present.
- `lateInitVariant()` runs after all subsystems init — safe place for NeoPixel and radio
  checks. `earlyInitVariant()` runs before; use only for strapping pins.
- RadioLib SPI init: `SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS)` in `main.cpp`,
  using the `LORA_*` defines from variant.h.
- `RadioInterface::limitPower()` supports a `TX_GAIN_LORA` offset for fixed-gain external
  PAs. This board uses the Bandit-style `getDACandDB()` path instead, because its PA gain
  is variable via APC2.
- `Packet TX: NNNms` in the log is `getPacketTime()` — **computed** from the firmware's SF
  and BW variables, not measured. It cannot detect a truncated transmission. To measure
  actual transmit duration, timestamp `configHardwareForSend()` against the following
  `startReceive()`.
