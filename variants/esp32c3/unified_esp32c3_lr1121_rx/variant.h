// UNIFIED_ESP32C3_LR1121_RX — ExpressLRS unified RX target repurposed as a Meshtastic node.
// ESP32-C3 + LR1121 (dual-band sub-GHz + 2.4 GHz) + NeoPixel + button, no display.
// Pin source: ExpressLRS unified target layout (UNIFIED_ESP32C3_LR1121_RX).

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#undef EXT_NOTIFY_OUT

#define BUTTON_PIN 9

// NeoPixel (GRB, 1 LED)
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 8
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// LR1121 dual-band radio
#define USE_LR1121
#define LORA_SCK 6
#define LORA_MISO 5
#define LORA_MOSI 4
#define LORA_CS 7
#define LR1121_SPI_SCK_PIN LORA_SCK
#define LR1121_SPI_MISO_PIN LORA_MISO
#define LR1121_SPI_MOSI_PIN LORA_MOSI
#define LR1121_SPI_NSS_PIN LORA_CS
#define LR1121_NRESET_PIN 2
#define LR1121_BUSY_PIN 3
#define LR1121_IRQ_PIN 1
// RF switching via LR1121 internal DIOs (DIO5/DIO6) — same scheme as bayckrc_dual_band's
// LR1120; not confirmed against this board's schematic, re-check if RX is dead on one band.
#define LR11X0_DIO_AS_RF_SWITCH
// No tcxo field in the source ELRS layout -> assume plain crystal, no LR11X0_DIO3_TCXO_VOLTAGE.
// If RX proves flaky, this is the first thing to re-check (see HANDOFF.md's EMAX TCXO note).

// FC-facing UART (this is an RX target — talks to the flight controller, not a JR bay).
// Not wired into Meshtastic; left here for reference in case a future passthrough feature needs it.
// #define UART_RX_PIN 20
// #define UART_TX_PIN 21
