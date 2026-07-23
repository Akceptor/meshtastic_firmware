// BAYCKRC 900/2400 Dual Band 1W Nano TX
// ESP32 + LR1120 + NeoPixel + fan, no display
// Pin source: https://github.com/ExpressLRS/targets/blob/master/TX/BAYCKRC%20Dual%20Band.json

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#undef EXT_NOTIFY_OUT

// NeoPixel (GRB, 1 LED)
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 12
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)
// ENABLE_AMBIENTLIGHTING intentionally omitted — variant.cpp owns the NeoPixel (blue→green status)

// Fan
#define FAN_EN_PIN 4

// LR1120 dual-band radio (device ID 0x02; LR1121=0x03 — confirmed by findChip() failure pattern)
#define USE_LR1120
#define LR11X0_DIO3_TCXO_VOLTAGE 1.8
#define LORA_SCK 25
#define LORA_MISO 33
#define LORA_MOSI 32
#define LORA_CS 27
#define LR1120_SPI_SCK_PIN LORA_SCK
#define LR1120_SPI_MISO_PIN LORA_MISO
#define LR1120_SPI_MOSI_PIN LORA_MOSI
#define LR1120_SPI_NSS_PIN LORA_CS
#define LR1120_NRESET_PIN 26
#define LR1120_BUSY_PIN 36
#define LR1120_IRQ_PIN 37
// RF switching via LR1120 internal DIOs (DIO5/DIO6)
#define LR11X0_DIO_AS_RF_SWITCH
// DCDC always enabled by firmware for LR11x0
// radio_rfo_hf=true: HF (2.4GHz) uses RFO path — reflected in rfswitch_table MODE_TX_HF
