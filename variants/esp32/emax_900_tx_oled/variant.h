// Emax OLED 900/915MHz TX Module
// ESP32 + SX1276 + I2C OLED + 5-way ADC + NeoPixel + fan
// Pin source: https://github.com/ExpressLRS/targets/blob/master/TX/EMAX%20900%20OLED.json

#define I2C_SDA 22
#define I2C_SCL 21

#define HAS_GPS 0
#undef GPS_RX_PIN
#undef GPS_TX_PIN

// SX1276 SPI
#define LORA_DIO0 4
#define LORA_DIO1 RADIOLIB_NC  // not connected on EMAX OLED
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RESET 14

// Fan
#define RF95_FAN_EN 32

// NeoPixel (GRB, 1 LED)
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 27
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// 5-way ADC joystick
// Values from ELRS JSON: [up, down, left, right, center, idle]
#define INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE
#define PIN_JOYSTICK 33
#define JOYSTICK_ADC_VALS /*UP*/ 2010, /*DOWN*/ 1230, /*LEFT*/ 635, /*RIGHT*/ 2730, /*OK*/ 0, /*IDLE*/ 4095

#define DISPLAY_FLIP_SCREEN

#undef EXT_NOTIFY_OUT

// RF95/SX1276 — PA_BOOST path to external PA (RFO unconnected, confirmed by no-burst test).
// MAX_POWER=17: at +20dBm SX1276 draws ~125mA, trips 100mA OCP, reboots. At +17dBm draws ~87mA.
// External PA (APC2 DAC) provides final output power.
#define USE_RF95
#define RF95_CS LORA_CS
#define RF95_RESET LORA_RESET
#define RF95_MAX_POWER 17

// PA via GPIO26 DAC — APC2 normal: higher DAC = more gain (ELRS power_values[0]=30 min, [7]=225 max)
// DAC value is calculated dynamically from TX power setting via getDACandDB() in RF95Interface.cpp
#define RF95_PA_EN 26
#define RF95_PA_DAC_EN

// RX enable switch
#define RF95_RXEN 12
