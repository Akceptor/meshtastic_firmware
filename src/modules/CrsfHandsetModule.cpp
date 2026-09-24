#include "configuration.h"

// Only built for boards with a CRSF-capable UART pin wired to the radio bay (see variant.h).
#ifdef CRSF_UART_PIN

#include "CrsfHandsetModule.h"
#include <cstdio>
#include <cstring>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

namespace
{
constexpr uint8_t CRSF_SYNC_BYTE = 0xC8;
constexpr uint8_t CRSF_FRAMETYPE_DEVICE_PING = 0x28;
constexpr uint8_t CRSF_FRAMETYPE_DEVICE_INFO = 0x29;
constexpr uint8_t CRSF_FRAMETYPE_PARAMETER_ENTRY = 0x2B;
constexpr uint8_t CRSF_FRAMETYPE_PARAMETER_READ = 0x2C;
constexpr uint8_t CRSF_PARAM_TYPE_INFO = 12;
constexpr uint8_t CRSF_ADDRESS_BROADCAST = 0x00;
constexpr uint8_t CRSF_ADDRESS_CRSF_TRANSMITTER = 0xEE;
// Same candidates as ExpressLRS TxToHandsetBauds.
constexpr uint32_t CRSF_BAUDS[] = {400000, 115200, 5250000, 3750000, 1870000, 921600, 2250000};
constexpr uint8_t CRSF_BAUD_COUNT = sizeof(CRSF_BAUDS) / sizeof(CRSF_BAUDS[0]);
HardwareSerial &crsfPort = Serial1;

uint8_t crsfCrc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0xD5) : (uint8_t)(crc << 1);
    }
    return crc;
}

// Pack "major.minor.patch..." into the 3-byte major/minor/patch form the CRSF Device Info
// softwareVer field uses (top byte unused/zero).
uint32_t packSoftwareVersion(const char *versionStr)
{
    unsigned major = 0, minor = 0, patch = 0;
    sscanf(versionStr, "%u.%u.%u", &major, &minor, &patch);
    return ((uint32_t)major << 16) | ((uint32_t)minor << 8) | (uint32_t)patch;
}
const char *resetReasonName(int r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "PWR";
    case ESP_RST_EXT:
        return "EXT";
    case ESP_RST_SW:
        return "SW";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "IWDT";
    case ESP_RST_TASK_WDT:
        return "TWDT";
    case ESP_RST_WDT:
        return "WDT";
    case ESP_RST_DEEPSLEEP:
        return "DSLP";
    case ESP_RST_BROWNOUT:
        return "BOD";
    default:
        return "?";
    }
}

// Persisted so a crash while in the JR bay (no USB log possible) is visible in the CRSF Status menu.
void recordResetReason()
{
    Preferences prefs;
    prefs.begin("crsfdiag", false);
    const int prev = prefs.getInt("rst", -1);
    const int now = (int)esp_reset_reason();
    prefs.putInt("rst", now);
    prefs.end();
    crsfHandsetStats.resetNow = resetReasonName(now);
    crsfHandsetStats.resetPrev = resetReasonName(prev);
}
} // namespace

CrsfHandsetStats crsfHandsetStats;

CrsfHandsetModule::CrsfHandsetModule() : concurrency::OSThread("CrsfHandset")
{
    recordResetReason();
    crsfPort.begin(CRSF_BAUDS[0], SERIAL_8N1, CRSF_UART_PIN, CRSF_UART_PIN, false);
    crsfPort.setTimeout(0);
    crsfHandsetStats.baud = CRSF_BAUDS[0];
    crsfHandsetStats.inverted = inverted;
    // UART_MODE_RS485_HALF_DUPLEX only toggles RTS on ESP32 and never tri-states TX, so switch by hand.
    setDirection(false);
    // Reply from the UART event task: an OSThread poll is too late for the handset's reply window.
    crsfPort.setRxTimeout(2);
    crsfPort.onReceive([this]() { onUartData(); }, true);
    crsfPort.onReceiveError([this](hardwareSerial_error_t e) { onUartError((int)e); });
    LOG_INFO("CrsfHandset: listening on GPIO%d @ %u baud", CRSF_UART_PIN, CRSF_BAUDS[0]);
}

// Mirrors ExpressLRS CRSFHandset::duplex_set_RX/TX, on UART1.
void CrsfHandsetModule::setDirection(bool transmit)
{
    const gpio_num_t pin = (gpio_num_t)CRSF_UART_PIN;
    if (transmit) {
        gpio_set_pull_mode(pin, GPIO_FLOATING);
        gpio_set_level(pin, inverted ? 0 : 1);
        gpio_set_direction(pin, GPIO_MODE_OUTPUT);
        // Idle level rather than ELRS's const-zero, so RX doesn't see a break while we transmit.
        gpio_matrix_in(GPIO_MATRIX_CONST_ONE_INPUT, U1RXD_IN_IDX, false);
        gpio_matrix_out(pin, U1TXD_OUT_IDX, inverted, false);
    } else {
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_matrix_in(pin, U1RXD_IN_IDX, inverted);
        if (inverted) {
            gpio_pulldown_en(pin);
            gpio_pullup_dis(pin);
        } else {
            gpio_pullup_en(pin);
            gpio_pulldown_dis(pin);
        }
    }
}

void CrsfHandsetModule::applyPolarityAndBaud()
{
    const uint32_t baud = CRSF_BAUDS[baudIdx];
    crsfPort.updateBaudRate(baud);
    setDirection(false);
    crsfHandsetStats.baud = baud;
    crsfHandsetStats.inverted = inverted;
    LOG_INFO("CrsfHandset: trying %u baud, %s", baud, inverted ? "INV" : "NRM");
}

void CrsfHandsetModule::sendFrame(uint8_t type, uint8_t destAddr, const uint8_t *payload, uint8_t payloadLen)
{
    uint8_t buf[64];
    if (payloadLen > sizeof(buf) - 6)
        return;
    uint8_t i = 0;
    buf[i++] = CRSF_SYNC_BYTE;
    buf[i++] = payloadLen + 4; // type, dest, orig, payload, crc
    buf[i++] = type;
    buf[i++] = destAddr;
    buf[i++] = CRSF_ADDRESS_CRSF_TRANSMITTER;
    memcpy(&buf[i], payload, payloadLen);
    i += payloadLen;
    buf[i] = crsfCrc8(&buf[2], i - 2);
    i++;

    setDirection(true);
    crsfPort.write(buf, i);
    crsfPort.flush();
    setDirection(false);
    while (crsfPort.available())
        crsfPort.read();
}

void CrsfHandsetModule::sendDeviceInfo(uint8_t destAddr)
{
    static const char name[] = "Meshtastic " xstr(APP_VERSION);
    const uint32_t softwareVer = packSoftwareVersion(optstr(APP_VERSION));

    uint8_t payload[sizeof(name) + 14];
    uint8_t i = 0;
    memcpy(&payload[i], name, sizeof(name));
    i += sizeof(name);
    memset(&payload[i], 0, 8); // serialNo, hardwareVer
    i += 8;
    payload[i++] = (uint8_t)(softwareVer >> 24);
    payload[i++] = (uint8_t)(softwareVer >> 16);
    payload[i++] = (uint8_t)(softwareVer >> 8);
    payload[i++] = (uint8_t)(softwareVer);
    // ELRS Lua skips setting the device name when fieldCnt matches its initial 0, so expose one field.
    payload[i++] = 1; // fieldCnt
    payload[i++] = 0; // parameterVersion
    sendFrame(CRSF_FRAMETYPE_DEVICE_INFO, destAddr, payload, i);
    crsfHandsetStats.infoSent++;
}

void CrsfHandsetModule::sendParameterEntry(uint8_t destAddr, uint8_t fieldId)
{
    static const char fieldName[] = "Version";
    static const char value[] = xstr(APP_VERSION);

    uint8_t payload[4 + sizeof(fieldName) + sizeof(value)];
    uint8_t i = 0;
    payload[i++] = fieldId;
    payload[i++] = 0; // chunks remaining
    payload[i++] = 0; // parent folder: root
    payload[i++] = CRSF_PARAM_TYPE_INFO;
    memcpy(&payload[i], fieldName, sizeof(fieldName));
    i += sizeof(fieldName);
    memcpy(&payload[i], value, sizeof(value));
    i += sizeof(value);
    sendFrame(CRSF_FRAMETYPE_PARAMETER_ENTRY, destAddr, payload, i);
}

void CrsfHandsetModule::handleFrame(const uint8_t *frame, uint8_t len)
{
    // frame = [sync, len, type, dest, orig, ...payload..., crc]
    const uint8_t type = frame[2];
    const uint8_t crc = crsfCrc8(&frame[2], len - 3);
    if (crc != frame[len - 1]) {
        crsfHandsetStats.badCrc++;
        return;
    }

    crsfHandsetStats.framesRx++;
    if (len < 6)
        return;
    const uint8_t destAddr = frame[3];
    const uint8_t origAddr = frame[4];

    if (type == CRSF_FRAMETYPE_DEVICE_PING) {
        crsfHandsetStats.pingsRx++;
        if (destAddr == CRSF_ADDRESS_BROADCAST || destAddr == CRSF_ADDRESS_CRSF_TRANSMITTER)
            sendDeviceInfo(origAddr);
    } else if (type == CRSF_FRAMETYPE_PARAMETER_READ && destAddr == CRSF_ADDRESS_CRSF_TRANSMITTER && len >= 8) {
        crsfHandsetStats.paramReads++;
        if (frame[5] == 1)
            sendParameterEntry(origAddr, frame[5]);
    }
}

// UART event task only (2KB stack): no logging here.
void CrsfHandsetModule::onUartData()
{
    while (crsfPort.available()) {
        const uint8_t b = crsfPort.read();
        crsfHandsetStats.bytesRx++;
        if (b == CRSF_SYNC_BYTE || b == CRSF_ADDRESS_CRSF_TRANSMITTER)
            crsfHandsetStats.syncCandidates++;
        memmove(crsfHandsetStats.lastBytes, crsfHandsetStats.lastBytes + 1, sizeof(crsfHandsetStats.lastBytes) - 1);
        crsfHandsetStats.lastBytes[sizeof(crsfHandsetStats.lastBytes) - 1] = b;
        switch (rxState) {
        case RxState::WaitSync:
            // Handset frames to an external module start with 0xEE, not 0xC8 (ELRS alignBufferToSync).
            if (b == CRSF_SYNC_BYTE || b == CRSF_ADDRESS_CRSF_TRANSMITTER) {
                rxBuf[0] = b;
                rxLen = 1;
                rxState = RxState::WaitLen;
            }
            break;
        case RxState::WaitLen:
            if (b < 2 || b > sizeof(rxBuf) - 2) {
                rxState = RxState::WaitSync;
                break;
            }
            rxBuf[1] = b;
            rxLen = 2;
            rxExpected = b;
            rxState = RxState::WaitData;
            break;
        case RxState::WaitData:
            rxBuf[rxLen++] = b;
            if (rxLen == 2 + rxExpected) {
                handleFrame(rxBuf, rxLen);
                rxState = RxState::WaitSync;
            }
            break;
        }
    }
}

void CrsfHandsetModule::onUartError(int error)
{
    (void)error;
    crsfHandsetStats.uartErrors++;
}

// Like ELRS UARTwdt: the constructor's setup alone never received a valid frame in the bay.
int32_t CrsfHandsetModule::runOnce()
{
    if (crsfHandsetStats.pingsRx != pingsLogged) {
        pingsLogged = crsfHandsetStats.pingsRx;
        LOG_INFO("CrsfHandset: pings=%lu info=%lu paramReads=%lu", (unsigned long)crsfHandsetStats.pingsRx,
                 (unsigned long)crsfHandsetStats.infoSent, (unsigned long)crsfHandsetStats.paramReads);
    }
    if (crsfHandsetStats.framesRx != framesRxAtLastCheck) {
        framesRxAtLastCheck = crsfHandsetStats.framesRx;
        return 1000;
    }

    inverted = !inverted;
    if (inverted)
        baudIdx = (baudIdx + 1) % CRSF_BAUD_COUNT;
    applyPolarityAndBaud();
    return 1000;
}

#endif
