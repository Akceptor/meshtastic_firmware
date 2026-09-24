# Prebuilt firmware

Binaries built from this branch. The suffix records any non-default build flags.

## Contents

### 2.7.26.9f12caf (current)

| File | Board | Notes |
|---|---|---|
| `firmware-unified_esp32c3_lr1121_rx-2.7.26.9f12caf.factory.bin` | BAYCKRC C3 900/2400 Dual Band Nano RX | Standalone Meshtastic. Full image incl. bootloader + partitions. Flash to offset `0x0`. **Overwrites the ElrsDual bootloader — do not use for dual-boot.** |
| `firmware-unified_esp32c3_lr1121_rx-2.7.26.9f12caf.ota.bin` | BAYCKRC C3 900/2400 Dual Band Nano RX | App only. Flash to a slot offset (`0x10000` or `0x1F0000`) to sit alongside ExpressLRS, or use OTA. |

Stock Meshtastic sync word `0x2b` — this board is LR1121, so it talks to stock Meshtastic
nodes directly and needs no `sync0x12` override.

**TX power: 22 dBm max, and don't set 1-14 dBm from the app.** RadioLib picks the LR1121's
low-power PA at or below 14 dBm and its high-power PA above, so 14 vs 15 swaps amplifier
rather than stepping 1 dB — a request in the 1-14 range is roughly 8 dB down and transmits
very weakly while receive still works normally. Leave `tx_power` unset (defaults to the
region limit, clamped to 22) or set 22. This matches what ExpressLRS runs on this hardware.
EU_433's 10 dBm regulatory limit unavoidably forces the LP PA.

Built with:

```
pio run -e unified_esp32c3_lr1121_rx
```

## ESP32-C3 LR1121 RX — ElrsDual dual-boot

`board_build.partitions = variants/esp32c3/unified_esp32c3_lr1121_rx/partitions-dual.csv`
matches the layout [ElrsDual](https://github.com/Akceptor/ElrsDual)'s slot-switch bootloader
expects, which is ExpressLRS's own `min_spiffs` layout: `ota_0` @ `0x10000` and `ota_1` @
`0x1F0000`, both 1.875MB, LittleFS 128KB @ `0x3D0000`, and the bootloader's power-cycle
counter sector at `0x3F0000`. **3 rapid power cycles flip slots.**

Keep stock ExpressLRS in one slot and this image in the other:

```
esptool.py --chip esp32c3 --port /dev/cu.usbserial-0001 --baud 460800 \
  write_flash 0x1F0000 firmware-unified_esp32c3_lr1121_rx-2.7.26.9f12caf.ota.bin
```

The ElrsDual bootloader and partition table must already be on the board — flash only the
app image, never the `.factory.bin`, or the slot-switch bootloader is lost.

App image is 1.86MB against a 1.875MB slot: **~17KB of headroom.** Any module added to this
variant will likely need a matching `MESHTASTIC_EXCLUDE_*` to keep fitting.

### The LR1121 transceiver-firmware patch

ExpressLRS flashes Semtech's LR1121 *transceiver* firmware image (type `0xF3`, e.g.
`0xF30104`) into the radio and never restores the factory image. RadioLib's `findChip()`
only accepts the factory device byte `0x03`, so stock Meshtastic reports `LR11x0 init
result -2` (`CHIP_NOT_FOUND`) on every boot on any ex-ExpressLRS LR1121 board — the radio
never comes up at all. `extra_scripts/lr11x0_accept_trx_firmware.py` patches the downloaded
RadioLib copy at build time to also accept `0xF3`. Nothing else differs; the command set is
the same. There is no upstream fix as of RadioLib 7.6.0.

### 2.7.26.665b728

Dual-OTA layout (two 1.875MB app slots) for use with an external dual-boot bootloader
that keeps the original ExpressLRS firmware in the other slot. See "dual-boot" section
below.

| File | Board | Notes |
|---|---|---|
| `firmware-bayckrc_dual_band-2.7.26.665b728-sync0x12.factory.bin` | BAYCKRC Dual Band TX (gateway) | LR11xx-compatible sync word. Full image incl. bootloader + partitions. Flash to offset `0x0`. |
| `firmware-bayckrc_dual_band-2.7.26.665b728-sync0x12.ota.bin` | BAYCKRC Dual Band TX (gateway) | LR11xx-compatible sync word. App only. For OTA, or serial flash to offset `0x10000`. |
| `firmware-bayckrc_dual_band-2.7.26.665b728-sync0x2b.factory.bin` | BAYCKRC Dual Band TX (gateway) | Stock Meshtastic sync word. Full image incl. bootloader + partitions. Flash to offset `0x0`. |
| `firmware-bayckrc_dual_band-2.7.26.665b728-sync0x2b.ota.bin` | BAYCKRC Dual Band TX (gateway) | Stock Meshtastic sync word. App only. For OTA, or serial flash to offset `0x10000`. |

Built with:

```
# sync0x12 (LR11xx-compatible):
PLATFORMIO_BUILD_FLAGS="-DMESHTASTIC_LORA_SYNCWORD=0x12" pio run -e bayckrc_dual_band

# sync0x2b (stock Meshtastic default):
pio run -e bayckrc_dual_band
```

See "The sync word trap" further down for why `sync0x12` exists — it matters for this
board mainly if you want it to interoperate with SX127x boards like `emax_900_tx_oled`.

## BAYCKRC dual-band gateway — what "gateway" means here

This build runs both onboard LR1120 radios at once: the primary follows your normal
region/channel config, the second is locked at compile time to **433.125 MHz** and mirrors
the primary's modem preset/power automatically. Every outgoing packet goes out both radios;
incoming packets from either feed the same receive path. There's no phone-app UI for this —
the 433.125 MHz frequency is a compile-time constant (see
`variants/esp32/bayckrc_dual_band/variant.h`), not something you can change without
reflashing.

## BAYCKRC dual-boot with stock ExpressLRS

`board_build.partitions = variants/esp32/bayckrc_dual_band/partitions-dual.csv` splits
flash into two equal 1.875MB `ota_0`/`ota_1` app slots instead of the default single large
partition. The intent: keep the board's original ExpressLRS firmware in one slot and this
Meshtastic build in the other, switched between by an external dual-boot bootloader (not
part of this repo) — not just an in-place-OTA rollback slot like `emax_900_tx_oled`'s.

To fit, this build trims `MESHTASTIC_EXCLUDE_*` modules more aggressively than
`emax_900_tx_oled` (no GPS/sensors/audio, plus `INPUTBROKER`/`CANNEDMESSAGES` since this
board has no display or input hardware) and excludes unused RadioLib radio families
(`SX127X`/`SX128X`/`LR2021` — this board only uses `LR11X0`). App image is ~1.45MB,
comfortably under the 1.875MB slot.

### 2.7.26.1a13730

Adds the ExpressLRS Lua menu: plug the module into an EdgeTX handset's JR bay (external RF
= Crossfire, 400k baud) and run the stock ExpressLRS Lua script. It shows **ELRS->Meshtastic**
with a canned-message selector + Send (channel-0 broadcast), the last 5 received messages
(persisted, survive power loss after ~1 min), the node list with per-node details, and a
CRSF Status diagnostics screen on the device (System → press OK → CRSF Status). The sync word
can also be switched at runtime (LoRa screen → OK → Sync Word); the build flag only sets the
default for a fresh device.

| File | Board | Notes |
|---|---|---|
| `firmware-emax_900_tx_oled-2.7.26.1a13730-sync0x12.factory.bin` | Emax 900 OLED TX | Full image incl. bootloader + partitions. Flash to offset `0x0`. |
| `firmware-emax_900_tx_oled-2.7.26.1a13730-sync0x12.ota.bin` | Emax 900 OLED TX | App only. For OTA, or serial flash to offset `0x10000`. |
| `firmware-emax_900_tx_oled-2.7.26.1a13730-sync0x2b.factory.bin` | Emax 900 OLED TX | Full image, stock Meshtastic sync word. Flash to offset `0x0`. |
| `firmware-emax_900_tx_oled-2.7.26.1a13730-sync0x2b.ota.bin` | Emax 900 OLED TX | App only, stock sync word. For OTA, or serial flash to offset `0x10000`. |

Built with:

```
# sync0x12 (LR11xx-compatible):
PLATFORMIO_BUILD_FLAGS="-DMESHTASTIC_LORA_SYNCWORD=0x12" pio run -e emax_900_tx_oled

# sync0x2b (stock Meshtastic default):
pio run -e emax_900_tx_oled
```

## `sync0x12` — read this before flashing

These images use LoRa sync word **`0x12`**, not Meshtastic's default `0x2b`.

**A node running this firmware cannot talk to any stock Meshtastic device.** Every node in
the mesh must be built with the same override.

The reason is upstream issue
[meshtastic/firmware#4775](https://github.com/meshtastic/firmware/issues/4775): `0x2b` is
not one of the two sync words Semtech defines, and **LR11xx receivers cannot detect it from
an SX127x transmitter**. Since this board is SX127x, it is unheard by LR1121/LR1110/LR1120
nodes unless both ends use `0x12`. Fixed upstream in 3.0.

For a stock-compatible build, just omit the flag:

```
pio run -e emax_900_tx_oled
```

## Flashing

Factory image (erases config):

```
esptool.py --chip esp32 --port /dev/cu.usbserial-0001 --baud 460800 \
  write_flash 0x0 firmware-emax_900_tx_oled-2.7.26.1a13730-sync0x12.factory.bin
```

App only (keeps config):

```
esptool.py --chip esp32 --port /dev/cu.usbserial-0001 --baud 460800 \
  write_flash 0x10000 firmware-emax_900_tx_oled-2.7.26.1a13730-sync0x12.ota.bin
```

## PA calibration

The EMAX image carries a measured PA calibration table, not the ExpressLRS
`power_values` labels, which are ~4.4 dB optimistic on this board. See
`variants/esp32/emax_900_tx_oled/README.md`. The low end (below DAC 50) is extrapolated —
this PA cannot go below roughly 17 dBm, so the menu's 10 and 14 dBm entries clamp to that
floor.
