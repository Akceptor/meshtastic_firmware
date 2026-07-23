// BAYCKRC 900/2400 Dual Band 1W Nano TX
// ESP32 + LR1121 + NeoPixel + fan, no display
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

// Fan
#define FAN_EN_PIN 4

// LR1121 dual-band radio
#define USE_LR1121
#define LR1121_SPI_MISO_PIN 33
#define LR1121_SPI_MOSI_PIN 32
#define LR1121_SPI_SCK_PIN 25
#define LR1121_SPI_NSS_PIN 27
#define LR1121_NRESET_PIN 26
#define LR1121_BUSY_PIN 36
#define LR1121_IRQ_PIN 37
// RF switching via LR1121 internal DIOs (DIO5/DIO6)
#define LR11X0_DIO_AS_RF_SWITCH
// DCDC always enabled by firmware for LR11x0
// radio_rfo_hf=true: HF (2.4GHz) uses RFO path — reflected in rfswitch_table MODE_TX_HF
