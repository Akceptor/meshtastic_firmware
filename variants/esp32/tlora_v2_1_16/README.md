# LILYGO T-LoRa V2.1-1.6

ESP32 (often ESP32-PICO-D4 module in real units) + SX1276 (RF95) + I2C OLED.
Silkscreen on the board reads **"T3_V1.6.1"** — LilyGO's internal board-family name is
"T3", version "1.6.1". If a TCXO is fitted (silkscreen near the crystal), use the
`-tcxo` env instead.

> **Board ID gotcha:** don't confuse this with `tlora-v1`/`tlora_v1_3` (older TTGO LoRa32
> V1, plain ESP-WROOM-32 module, different pinout entirely). Flashing `tlora-v1` firmware
> onto a real T3 V1.6.1 board boots into an immediate `TG1WDT_SYS_RESET` reboot loop
> (screen stays blank) because the OLED reset/VEXT/LoRa pins don't match — the serial
> banner's `Chip is ESP32-PICO-D4` is the tell. Confirm the silkscreen text before picking
> an env.

## Build & flash

```
pio run -e tlora-v2-1-1_6 -t upload
```

TCXO-fitted units:

```
pio run -e tlora-v2-1-1_6-tcxo -t upload
```

## Dual-OTA partition layout

`board_build.partitions = variants/esp32/tlora_v2_1_16/partitions-dual.csv` gives two
equal 1.875MB app slots (`ota_0`/`ota_1`) instead of the default lopsided table (2.4MB +
640KB, where the second slot is too small to hold a full image). This is a genuine
ESP-IDF OTA layout — `esp_ota_set_boot_partition`/Arduino `Update` lib pick the inactive
slot automatically, so an in-place OTA update always leaves the previous working image
in the other slot as a rollback fallback.

`MESHTASTIC_EXCLUDE_*` flags trim unused modules (GPS, sensors, audio, unused mesh
modules) so the app fits comfortably under 1.875MB — stock build is ~2.0MB and would not
fit either OTA slot.
