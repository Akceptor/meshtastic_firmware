// UNIFIED_ESP32_LR1121_RX — ExpressLRS unified RX target repurposed as a Meshtastic node.
// ESP32 + LR1121 (dual-band sub-GHz + 2.4 GHz) + NeoPixel + button, no display.
// Pin source: ExpressLRS unified target layout (UNIFIED_ESP32_LR1121_RX).

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#undef EXT_NOTIFY_OUT

#define BUTTON_PIN 0

// NeoPixel (GRB, 1 LED)
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 22
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// LR1121 dual-band radio
#define USE_LR1121
#define LORA_SCK 25
#define LORA_MISO 33
#define LORA_MOSI 32
#define LORA_CS 27
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_NRESET_PIN 26
#define LR1121_BUSY_PIN 36 // input-only
#define LR1121_IRQ_PIN 37  // input-only
#define LR11X0_DIO_AS_RF_SWITCH
// No tcxo field in the ELRS layout -> plain crystal, no LR11X0_DIO3_TCXO_VOLTAGE. First suspect if RX is flaky.
