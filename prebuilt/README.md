# Prebuilt firmware

Binaries built from this branch. The suffix records any non-default build flags.

## Contents

### 2.7.26.8f1666d (current)

| File | Board | Notes |
|---|---|---|
| `firmware-emax_900_tx_oled-2.7.26.8f1666d-sync0x12.factory.bin` | Emax 900 OLED TX | Full image incl. bootloader + partitions. Flash to offset `0x0`. |
| `firmware-emax_900_tx_oled-2.7.26.8f1666d-sync0x12.ota.bin` | Emax 900 OLED TX | App only. For OTA, or serial flash to offset `0x10000`. |
| `firmware-emax_900_tx_oled-2.7.26.8f1666d-sync0x2b.factory.bin` | Emax 900 OLED TX | Full image, stock Meshtastic sync word. Flash to offset `0x0`. |
| `firmware-emax_900_tx_oled-2.7.26.8f1666d-sync0x2b.ota.bin` | Emax 900 OLED TX | App only, stock sync word. For OTA, or serial flash to offset `0x10000`. |

Built with:

```
# sync0x12 (LR11xx-compatible):
PLATFORMIO_BUILD_FLAGS="-DMESHTASTIC_LORA_SYNCWORD=0x12" pio run -e emax_900_tx_oled

# sync0x2b (stock Meshtastic default):
pio run -e emax_900_tx_oled
```

### 2.7.26.6451195 (archived)

| File | Board | Notes |
|---|---|---|
| `firmware-emax_900_tx_oled-2.7.26.6451195-sync0x12.factory.bin` | Emax 900 OLED TX | Full image incl. bootloader + partitions. Flash to offset `0x0`. |
| `firmware-emax_900_tx_oled-2.7.26.6451195-sync0x12.ota.bin` | Emax 900 OLED TX | App only. For OTA, or serial flash to offset `0x10000`. |

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
  write_flash 0x0 firmware-emax_900_tx_oled-2.7.26.6451195-sync0x12.factory.bin
```

App only (keeps config):

```
esptool.py --chip esp32 --port /dev/cu.usbserial-0001 --baud 460800 \
  write_flash 0x10000 firmware-emax_900_tx_oled-2.7.26.6451195-sync0x12.ota.bin
```

## PA calibration

The EMAX image carries a measured PA calibration table, not the ExpressLRS
`power_values` labels, which are ~4.4 dB optimistic on this board. See
`variants/esp32/emax_900_tx_oled/README.md`. The low end (below DAC 50) is extrapolated —
this PA cannot go below roughly 17 dBm, so the menu's 10 and 14 dBm entries clamp to that
floor.
