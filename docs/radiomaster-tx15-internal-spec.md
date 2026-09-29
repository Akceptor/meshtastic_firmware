# Radiomaster TX15 internal ELRS module — Meshtastic variant spec

Status: **planned, not implemented.** Target: Meshtastic in OTA slot 1 next to ExpressLRS in
slot 0 (ElrsDual dual-boot). Env/dir: `variants/esp32/radiomaster_tx15_internal/`. Step 1 is
Meshtastic only (phone over BLE); the ELRS Lua menu is step 2 (needs full-duplex UART0 in
`CrsfHandsetModule`).

Sources: ELRS layout `Targets/TX/Radiomaster TX15.json` (`radiomaster.tx_dual.tx15`, firmware
`Unified_ESP32_LR1121_TX`); ExpressLRS v4 (cross-checked v3) — citations below are into
`elrs-v4/src`. Template: `variants/esp32/unified_esp32_lr1121_rx/` (ESP32 + LR1121, hardware
verified).

## 1. RF switch

`radio_rfsw_ctrl` is copied verbatim into `SetDioAsRfSwitch`: [0] enable, [1] stby, [2] rx,
[3] tx, [4] txHP, [5] txHF, [6] gnss (unused), [7] wifi/HF-RX (`lib/LR1121Driver/LR1121.cpp:282-310`);
bit0=DIO5 … bit3=DIO8. RadioLib builds the same command (`LR11x0.cpp:1206-1244`), so DIO5..DIO8
reproduce ELRS exactly.

`[15,0,4,12,12,2,0,1]` → enable DIO5-8; STBY none; RX DIO7; **TX DIO7+DIO8**; TX_HP DIO7+DIO8;
TX_HF DIO6; GNSS none; WIFI/HF-RX DIO5. Not the ELRS default table the template uses.

```cpp
#include "RadioLib.h"

// From Radiomaster TX15.json radio_rfsw_ctrl [15,0,4,12,12,2,0,1] (ELRS LR1121.cpp:282-310).
// RadioLib has no HF-RX mode: MODE_RX (DIO7) is used on both bands, so 2.4GHz RX is mis-routed (ELRS uses DIO5).
static const uint32_t rfswitch_dio_pins[] = {RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_LR11X0_DIO7,
                                             RADIOLIB_LR11X0_DIO8, RADIOLIB_NC};

static const Module::RfSwitchMode_t rfswitch_table[] = {
    // mode                  DIO5  DIO6  DIO7  DIO8
    {LR11x0::MODE_STBY, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_RX, {LOW, LOW, HIGH, LOW}},
    {LR11x0::MODE_TX, {LOW, LOW, HIGH, HIGH}},
    {LR11x0::MODE_TX_HP, {LOW, LOW, HIGH, HIGH}},
    {LR11x0::MODE_TX_HF, {LOW, HIGH, LOW, LOW}},
    {LR11x0::MODE_GNSS, {LOW, LOW, LOW, LOW}},
    {LR11x0::MODE_WIFI, {HIGH, LOW, LOW, LOW}},
    END_OF_MODE_TABLE,
};
```

## 2. External PA and power

What ELRS does: `power_control 3` → `POWER_OUTPUT_DACWRITE` (`include/target/Unified_ESP32_TX.h:45`).
`setPower()` (`lib/POWERMGNT/POWERMGNT.cpp:266-290`) at each level sets the LR1121 sub-GHz output
to `power_values2[i]` dBm, writes `power_values[i]` raw to `dacWrite(26, …)` (line 276, same value
both bands), and the 2.4 GHz output to `power_values_dual[i]`. `radio_rfo_hf` → `OPT_USE_SX1276_RFO_HF`
(`Unified_ESP32_TX.h:20`), which for sub-GHz selects the **LP PA**: -17..+14 dBm
(`LR1121.cpp:350-352`), PaSel=LP, supply=VREG, duty 0x07, HPSel 0 (`LR1121.cpp:420-425`), ramp 48 µs
(`LR1121.cpp:405`). 2.4 GHz uses the HF PA.

| ELRS level | nominal dBm | DAC GPIO26 | LR1121 sub-GHz dBm (LP) | implied ext. gain | LR1121 2.4G dBm |
|---|---|---|---|---|---|
| 10 mW | 10 | 140 | -14 | 24 | -18 |
| 25 mW | 14 | 120 | -10 | 24 | -16 |
| 50 mW | 17 | 120 | -7 | 24 | -13 |
| 100 mW | 20 | 120 | -4 | 24 | -8 |
| 250 mW | 24 | 120 | 0 | 24 | -4 |
| 500 mW | 27 | 120 | +3 | 24 | 0 |
| 1000 mW | 30 | 95 | +10 | 20 | +10 |

Nominal ELRS labels, not measured (on the EMAX they were ~4.4 dB optimistic). **DAC polarity is
not provable from the code** (raw `dacWrite`, no inversion); the DAC is not the main gain control
(constant 120 from 25 to 500 mW while drive tracks the label 1:1). So the mapping never
interpolates the DAC — only ELRS's exact DAC/drive pairs, or the same DAC with less drive.

**The template's `LR1110_MAX_POWER=22` is wrong here.** RadioLib picks the HP PA above 14 dBm
(`LR1120.cpp:83-103`); this board is wired for the LP path into an external PA ELRS never drives
above +10 dBm. Use `LR1110_MAX_POWER 10` (always LP), then re-apply ELRS's exact LP config via the
qualified base call `lora.LR11x0::setOutputPower(p, RADIOLIB_LR11X0_PA_SEL_LP,
RADIOLIB_LR11X0_PA_SUPPLY_INTERNAL, 0x07, 0x00, RADIOLIB_LRXXXX_PA_RAMP_48U - 0x03)` (duty 0x07 vs
RadioLib's 0x04; `-0x03` is RadioLib's LR11x0 ramp convention, LR1120.cpp:102 → 0x02 = ELRS).

**Meshtastic mapping** (requested EIRP R = `power` after `applyModemConfig()`, region-capped):
1. R = min(R, `LR11X0_PA_MAX_EIRP_DBM`), default **20** (100 mW, ELRS default) until measured.
2. P = highest table point with P.eirp ≤ R.
3. If the next point has the same DAC: lr = P.lr + (R−P.eirp)·Δlr/Δeirp; else lr = P.lr (snap down).
4. Below the first point: DAC 140, lr = -14 − (10−R), floor -17.
5. `dacWrite(26, P.dac)`; `power = lr` (≤ +10).

| R (dBm) | DAC | LR1121 dBm |
|---|---|---|
| ≤10 | 140 | -14 (→ -17 for very low requests) |
| 11-13 | 140 | -14 (snap) |
| 14-27 | 120 | R − 24 |
| 28-29 | 120 | +3 (snap) |
| ≥30 | 95 | +10 |

LORA_24 would use the `power_values_dual` column with the same DACs — untested, and 2.4 GHz RX is
mis-routed; unsupported for step 1.

**Hook** — `src/mesh/LR11x0Interface.cpp`, generic, gated on `LR11X0_PA_DAC_PIN` (mirrors the
EMAX `getDACandDB()` pattern in `RF95Interface.cpp`):

```cpp
#ifdef LR11X0_PA_DAC_PIN
#ifndef LR11X0_PA_MAX_EIRP_DBM
#define LR11X0_PA_MAX_EIRP_DBM 20
#endif
struct Lr11x0PaPoint { int8_t eirp; uint8_t dac; int8_t lrDbm; };
static const Lr11x0PaPoint lr11x0PaTable[] = {LR11X0_PA_TABLE};
static const Lr11x0PaPoint lr11x0PaTableHf[] = {LR11X0_PA_TABLE_HF};
// Never interpolate DAC: its polarity/effect is unverified, only drive is scaled within a constant-DAC span.
static Lr11x0PaPoint lr11x0PaFor(int8_t eirp, bool hf)
{
    const Lr11x0PaPoint *t = hf ? lr11x0PaTableHf : lr11x0PaTable;
    const size_t n = (hf ? sizeof(lr11x0PaTableHf) : sizeof(lr11x0PaTable)) / sizeof(Lr11x0PaPoint);
    if (eirp > LR11X0_PA_MAX_EIRP_DBM) eirp = LR11X0_PA_MAX_EIRP_DBM;
    if (eirp < t[0].eirp) {
        int lr = t[0].lrDbm - (t[0].eirp - eirp);
        return {eirp, t[0].dac, (int8_t)(lr < -17 ? -17 : lr)};
    }
    size_t i = 0;
    while (i + 1 < n && t[i + 1].eirp <= eirp) i++;
    int lr = t[i].lrDbm;
    if (i + 1 < n && t[i + 1].dac == t[i].dac)
        lr += (eirp - t[i].eirp) * (t[i + 1].lrDbm - t[i].lrDbm) / (t[i + 1].eirp - t[i].eirp);
    return {eirp, t[i].dac, (int8_t)lr};
}
#endif
```

- `init()`: after `RadioLibInterface::init();` save `int8_t requestedEirp = power;`. After the
  `limitPower`/LORA_24 clamp and before `lora.begin`: map, `power = pa.lrDbm`,
  `dacWrite(LR11X0_PA_DAC_PIN, pa.dac)`, LOG_INFO requested/eirp/DAC/lr. After
  `setRegulatorDCDC()` succeeds, on sub-GHz only, the qualified LP `setOutputPower` call. Fan:
  `updateFanState(pa.eirp)` (`power` is now drive, -14..10, and would never trip the fan).
- `reconfigure()`: capture `requestedEirp` after `RadioLibInterface::reconfigure()`; replace the
  clamp + `lora.setOutputPower(power)` + fan block (under the ifdef) with the same mapping,
  `dacWrite`, qualified LP `setOutputPower`, `updateFanState(pa.eirp)`.
- `sleep()`: leave the DAC alone (polarity unknown); fan is already turned off there.

## 3. Fan, LED, buttons, backpack

- **Fan GPIO2**: existing `RF95_FAN_EN` / `RF95_FAN_ON_THRESHOLD_DBM` path in LR11x0Interface
  (added for bayckrc). Threshold 24 = ELRS default 250 mW (`lib/CONFIG/config.cpp:724`). Feed it
  requested EIRP. GPIO2 is a strap pin, only driven after boot.
- **LED**: NeoPixel GPIO22, GRB, 1 LED.
- **Screen/buttons**: `HAS_SCREEN 0`, **no `BUTTON_PIN`** — GPIO0 is BOOT0 / passthrough detect
  (`lib/Backpack/devBackpack.cpp:34,138`).
- **Backpack**: ELRS `initialize()` drives EN LOW + BOOT LOW (`devBackpack.cpp:405-416`), `event()`
  later raises EN (473-479); BOOT HIGH + EN pulse = backpack bootloader (73-78). Meshtastic
  mirrors the pre-`event()` state in `earlyInitVariant()`: **EN 19 LOW, BOOT 23 LOW**. Don't
  touch backpack UART 18/5.

## 4. UART0 (GPIO3/1) = CRSF link to the handset

Full duplex (separate pins) → no electrical contention. EdgeTX (CRSF at 400k–1.87M) just sees
garbage/no module; nothing is harmed (the ROM boot log goes there under ELRS too). Meshtastic's
StreamAPI drops garbage (needs 0x94 0xC3 + valid protobuf); worst case a spurious API-mode switch
mutes logs. The internal module is likely powered only while Internal RF = CRSF/ELRS in EdgeTX.
EdgeTX `serialpassthrough rfmod 0 115200` bridges handset USB to this UART.

Step 1: keep the console on UART0 at 115200 (only USB-less log/API path, via passthrough). Don't
define `CRSF_UART_PIN` (the half-duplex CrsfHandsetModule doesn't apply). Set
`MESHTASTIC_EXCLUDE_SERIAL=1`. Step 2 (Lua) must move/mute the console.

## 5. Partitions, hardware model, build

- Partitions: copy the template's ElrsDual-compatible `partitions-dual.csv` (ota_0 0x10000,
  ota_1 0x1F0000, 0x1E0000 each; LittleFS 0x3D0000 128 KB; counter sector 0x3F0000), 4 MB,
  `board = esp32doit-devkit-v1`. ELRS shares the LittleFS partition for `/options.json` /
  `/hardware.json` and falls back to its appended layout if absent
  (`lib/OPTIONS/hardware.cpp:220-236`, `options.cpp:264-283`). Flash only `.ota.bin` to 0x1F0000.
- Hardware model **148**, slug `RADIOMASTER_TX15_INTERNAL` (fork numbers private variants above
  upstream's 138: 144 emax, 145 bayckrc, 146 c3, 147 unified_esp32). Runtime stays `PRIVATE_HW`.
- Size: template `.ota.bin` 1,838,512 / 1,966,080 B; added code is small; EMAX-style excludes add margin.

`platformio.ini`:
```ini
[env:radiomaster_tx15_internal]
custom_meshtastic_hw_model = 148
custom_meshtastic_hw_model_slug = RADIOMASTER_TX15_INTERNAL
custom_meshtastic_architecture = esp32
custom_meshtastic_actively_supported = false
custom_meshtastic_support_level = 3
custom_meshtastic_display_name = Radiomaster TX15 Internal ELRS
custom_meshtastic_tags = Radiomaster, ExpressLRS

extends = esp32_base
board = esp32doit-devkit-v1
board_build.partitions = variants/esp32/radiomaster_tx15_internal/partitions-dual.csv
extra_scripts =
  ${esp32_base.extra_scripts}
  pre:extra_scripts/lr11x0_accept_trx_firmware.py
build_src_filter =
  ${esp32_base.build_src_filter} +<../variants/esp32/radiomaster_tx15_internal>
build_flags =
  ${esp32_base.build_flags}
  -DRADIOMASTER_TX15_INTERNAL
  -DMESSAGE_AUTOSAVE_INTERVAL_SEC=60
  -I variants/esp32/radiomaster_tx15_internal
  -D RADIOLIB_EXCLUDE_SX127X=1
  -D RADIOLIB_EXCLUDE_SX128X=1
  -D RADIOLIB_EXCLUDE_LR2021=1
  -D MESHTASTIC_EXCLUDE_GPS=1
  -D MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR_EXTERNAL=1
  -D MESHTASTIC_EXCLUDE_DETECTIONSENSOR=1
  -D MESHTASTIC_EXCLUDE_ACCELEROMETER=1
  -D MESHTASTIC_EXCLUDE_MAGNETOMETER=1
  -D MESHTASTIC_EXCLUDE_AUDIO=1
  -D MESHTASTIC_EXCLUDE_SERIAL=1
  -D MESHTASTIC_EXCLUDE_ATAK=1
  -D MESHTASTIC_EXCLUDE_PAXCOUNTER=1
  -D MESHTASTIC_EXCLUDE_REMOTEHARDWARE=1
  -D MESHTASTIC_EXCLUDE_STOREFORWARD=1
  -D MESHTASTIC_EXCLUDE_RANGETEST=1
  -D MESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION=1
monitor_speed = 115200
upload_protocol = esptool
upload_speed = 460800
```

`variant.h`:
```cpp
// Radiomaster TX15 internal ExpressLRS module (Unified_ESP32_LR1121_TX, "Radiomaster TX15.json").
#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#undef EXT_NOTIFY_OUT
// No BUTTON_PIN: GPIO0 is BOOT0, used by EdgeTX/ELRS for passthrough.

#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 22
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

#define USE_LR1121
#define LORA_SCK 25
#define LORA_MISO 33
#define LORA_MOSI 32
#define LORA_CS 27
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_NRESET_PIN 15 // not 26 as on the unified RX template: 26 is the PA DAC here
#define LR1121_BUSY_PIN 36
#define LR1121_IRQ_PIN 37
#define LR11X0_DIO_AS_RF_SWITCH
// ELRS LR1121 driver has no TCXO handling -> crystal, no LR11X0_DIO3_TCXO_VOLTAGE.

// radio_rfo_hf: LR1121 LP PA feeds an external PA; ELRS never drives it above +10 dBm. <=14 keeps RadioLib on the LP PA.
#define LR1110_MAX_POWER 10
#define LR1120_MAX_POWER 10
#define LR11X0_PA_DAC_PIN 26
// {nominal EIRP dBm, DAC, LR1121 dBm} = ELRS power_values/power_values2(/_dual). ELRS labels are nominal, not measured.
#define LR11X0_PA_TABLE {10, 140, -14}, {14, 120, -10}, {17, 120, -7}, {20, 120, -4}, {24, 120, 0}, {27, 120, 3}, {30, 95, 10}
#define LR11X0_PA_TABLE_HF {10, 140, -18}, {14, 120, -16}, {17, 120, -13}, {20, 120, -8}, {24, 120, -4}, {27, 120, 0}, {30, 95, 10}
// Raise only after power-meter verification.
#define LR11X0_PA_MAX_EIRP_DBM 20

#define RF95_FAN_EN 2
#define RF95_FAN_ON_THRESHOLD_DBM 24

#define TX15_BACKPACK_EN 19
#define TX15_BACKPACK_BOOT 23
```

`variant.cpp`:
```cpp
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
```

## 6. Verify first on hardware

1. LR1121 init: `LR11x0 init result 0`, device 0xF3 (trx-firmware patch); RX from a known node.
2. **Power, before any sustained TX**: power meter + attenuator on the internal antenna port;
   tx_power 10/14/17/20; confirm output tracks drive 1:1 at DAC 120; at fixed drive step DAC
   120 → 140 to learn polarity. Only then raise `LR11X0_PA_MAX_EIRP_DBM` and replace the nominal
   table with measured points.
3. RSSI: `power_lna_gain 15` = ~15 dB external LNA → Meshtastic RSSI/noise floor read ~15 dB high.
4. Fan on at tx_power ≥ 24, off below.
5. Handset: module stays powered with Internal RF = CRSF; EdgeTX unaffected; 3 power cycles flip
   back to ELRS.
6. **Never use Meshtastic's in-app OTA**: it writes the inactive slot = ELRS's slot 0.
7. Thermal: heavy duty cycle at high EIRP inside the handset.

## Open questions

- Is the ElrsDual bootloader + partition table on the TX15 already (counter at 0x3F0000)?
- Module flash really 4 MB? ESP32-D0WD or PICO?
- Does EdgeTX control GPIO0/EN on the internal module; does `serialpassthrough rfmod 0 115200`
  work for the console and for esptool-flashing slot 1?
- Is module power cut when Internal RF = OFF?
- Actual DAC effect/polarity (needs measurement).
- Is LORA_24 in scope (RX mis-routed, HF table untested)?
