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

// Full-duplex CRSF link to the EdgeTX handset on UART0's default pins, via Serial1/matrix (CrsfHandsetModule).
#define CRSF_UART_RX_PIN 3
#define CRSF_UART_TX_PIN 1
