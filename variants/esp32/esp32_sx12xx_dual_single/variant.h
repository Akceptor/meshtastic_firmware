// ESP32 + dual SX127x radio footprint, second radio unused (single-radio build).
// Pin source: hardware.json export (ELRS-style radio_*/radio_*_2 schema).

#define HAS_SCREEN 0
#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

#undef EXT_NOTIFY_OUT

// Primary SX1276/RF95 radio.
#define USE_RF95
#define LORA_SCK 25
#define LORA_MISO 33
#define LORA_MOSI 32
#define LORA_CS 27
#define LORA_RESET 26
#define LORA_DIO0 36
#define LORA_DIO1 37
#define RF95_CS LORA_CS
#define RF95_RESET LORA_RESET
#define RF95_DIO1 LORA_DIO1

// Second radio present on hardware (radio_*_2 in hardware.json) but unused in this build.
// #define LORA_CS_2 13
// #define LORA_RESET_2 21
// #define LORA_DIO0_2 39
// #define LORA_DIO1_2 34

// NeoPixel status LED (GRB, 1 LED).
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 22
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP
