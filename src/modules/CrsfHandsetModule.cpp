#include "configuration.h"

// Only built for boards with a CRSF-capable UART pin wired to the radio bay (see variant.h).
#ifdef CRSF_UART_PIN

#include "CrsfHandsetModule.h"
#include <cstdio>
#include <cstring>
#include <driver/uart.h>

namespace
{
constexpr uint8_t CRSF_SYNC_BYTE = 0xC8;
constexpr uint8_t CRSF_FRAMETYPE_DEVICE_PING = 0x28;
constexpr uint8_t CRSF_FRAMETYPE_DEVICE_INFO = 0x29;
constexpr uint8_t CRSF_ADDRESS_BROADCAST = 0x00;
constexpr uint8_t CRSF_ADDRESS_CRSF_TRANSMITTER = 0xEE;
constexpr uint32_t CRSF_BAUD = 400000;
constexpr uart_port_t CRSF_UART_NUM = UART_NUM_1;
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
} // namespace

CrsfHandsetStats crsfHandsetStats;

CrsfHandsetModule::CrsfHandsetModule() : concurrency::OSThread("CrsfHandset")
{
    crsfPort.begin(CRSF_BAUD, SERIAL_8N1, CRSF_UART_PIN, CRSF_UART_PIN, false);
    // Single-wire half duplex: the JR-bay signal pin is shared for RX and TX. The UART hardware
    // handles the direction switching so we don't have to bit-bang pinMode around every send.
    uart_set_mode(CRSF_UART_NUM, UART_MODE_RS485_HALF_DUPLEX);
    LOG_INFO("CrsfHandset: listening on GPIO%d @ %u baud", CRSF_UART_PIN, CRSF_BAUD);
}

void CrsfHandsetModule::sendDeviceInfo(uint8_t destAddr)
{
    static const char name[] = "Meshtastic " xstr(APP_VERSION);
    constexpr uint8_t nameLen = sizeof(name); // includes trailing '\0'
    const uint32_t softwareVer = packSoftwareVersion(optstr(APP_VERSION));

    uint8_t buf[8 + nameLen + 14]; // sync, len, type, dest, orig, name, serial, hwVer, swVer, fieldCnt, paramVer, crc
    uint8_t i = 0;
    buf[i++] = CRSF_SYNC_BYTE;
    i++; // frame_size filled in below
    const uint8_t typeIdx = i;
    buf[i++] = CRSF_FRAMETYPE_DEVICE_INFO;
    buf[i++] = destAddr;                        // reply to whoever pinged us
    buf[i++] = CRSF_ADDRESS_CRSF_TRANSMITTER;    // our address
    memcpy(&buf[i], name, nameLen);
    i += nameLen;
    memset(&buf[i], 0, 4); // serialNo
    i += 4;
    memset(&buf[i], 0, 4); // hardwareVer
    i += 4;
    buf[i++] = (uint8_t)(softwareVer >> 24);
    buf[i++] = (uint8_t)(softwareVer >> 16);
    buf[i++] = (uint8_t)(softwareVer >> 8);
    buf[i++] = (uint8_t)(softwareVer);
    buf[i++] = 0; // fieldCnt: no configurable parameters
    buf[i++] = 0; // parameterVersion

    buf[1] = i - typeIdx + 1; // bytes after frame_size, including the CRC we're about to add
    buf[i] = crsfCrc8(&buf[typeIdx], i - typeIdx);
    i++;

    crsfPort.write(buf, i);
    crsfHandsetStats.infoSent++;
}

void CrsfHandsetModule::handleFrame(const uint8_t *frame, uint8_t len)
{
    // frame = [sync, len, type, dest, orig, ...payload..., crc]
    const uint8_t type = frame[2];
    const uint8_t crc = crsfCrc8(&frame[2], len - 3);
    if (crc != frame[len - 1]) {
        crsfHandsetStats.badCrc++;
        LOG_DEBUG("CrsfHandset: bad CRC on type=0x%02x len=%d (got 0x%02x want 0x%02x)", type, len, frame[len - 1], crc);
        return;
    }

    crsfHandsetStats.framesRx++;
    LOG_DEBUG("CrsfHandset: frame type=0x%02x dest=0x%02x orig=0x%02x", type, frame[3], frame[4]);

    if (type == CRSF_FRAMETYPE_DEVICE_PING) {
        crsfHandsetStats.pingsRx++;
        const uint8_t destAddr = frame[3];
        const uint8_t origAddr = frame[4];
        if (destAddr == CRSF_ADDRESS_BROADCAST || destAddr == CRSF_ADDRESS_CRSF_TRANSMITTER) {
            LOG_INFO("CrsfHandset: got Device Ping, replying with Device Info");
            sendDeviceInfo(origAddr);
        }
    }
}

int32_t CrsfHandsetModule::runOnce()
{
    while (crsfPort.available()) {
        const uint8_t b = crsfPort.read();
        crsfHandsetStats.bytesRx++;
        switch (rxState) {
        case RxState::WaitSync:
            // Frames from the handset to an external module are addressed to us (0xEE), not the
            // flight-controller sync byte (0xC8) — see ExpressLRS CRSFHandset::alignBufferToSync().
            if (b == CRSF_SYNC_BYTE || b == CRSF_ADDRESS_CRSF_TRANSMITTER) {
                rxBuf[0] = b;
                rxLen = 1;
                rxState = RxState::WaitLen;
            }
            break;
        case RxState::WaitLen:
            // len = bytes remaining after this field (type + dest + orig + payload + crc)
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
    return 5;
}

#endif
