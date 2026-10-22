// Reads the SX127x version register on both radio slots of the ESP32 SX12xx dual board.
// Pins match variants/esp32/esp32_sx12xx_dual_single/variant.h.
#include <Arduino.h>
#include <SPI.h>

namespace {

constexpr int PIN_SCK = 25;
constexpr int PIN_MISO = 33;
constexpr int PIN_MOSI = 32;

struct Radio {
  const char *name;
  int nss;
  int rst;
};

constexpr Radio RADIOS[] = {
    {"primary", 27, 26},
    {"second", 13, 21},
};

constexpr uint8_t REG_OP_MODE = 0x01;
constexpr uint8_t REG_VERSION = 0x42;

uint8_t readReg(int nss, uint8_t reg) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(nss, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t value = SPI.transfer(0x00);
  digitalWrite(nss, HIGH);
  SPI.endTransaction();
  return value;
}

void resetRadio(const Radio &r) {
  digitalWrite(r.rst, LOW);
  delay(2);
  digitalWrite(r.rst, HIGH);
  delay(10);
}

const char *verdict(uint8_t version) {
  switch (version) {
  case 0x12:
    return "SX127x found";
  case 0x00:
    return "no response, MISO low";
  case 0xFF:
    return "no response, MISO high";
  default:
    return "other chip, not SX127x";
  }
}

void probe(const Radio &r) {
  resetRadio(r);
  uint8_t version1 = readReg(r.nss, REG_VERSION);
  uint8_t version2 = readReg(r.nss, REG_VERSION);
  uint8_t opMode = readReg(r.nss, REG_OP_MODE);
  Serial.printf("%-7s NSS=%2d RST=%2d  RegVersion=0x%02X 0x%02X  RegOpMode=0x%02X  -> %s\n", r.name, r.nss, r.rst, version1,
                version2, opMode, verdict(version1));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  for (const Radio &r : RADIOS) {
    pinMode(r.nss, OUTPUT);
    digitalWrite(r.nss, HIGH);
    pinMode(r.rst, OUTPUT);
  }
  Serial.println("radio probe start");
}

void loop() {
  for (const Radio &r : RADIOS)
    probe(r);
  Serial.println();
  delay(3000);
}
