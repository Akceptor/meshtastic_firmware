#pragma once

#include "concurrency/OSThread.h"
#include <Arduino.h>

/**
 * Minimal CRSF responder so the stock ExpressLRS Lua script on an EdgeTX handset can identify this
 * TX module: answers a Device Ping (0x28) with a Device Info (0x29) frame carrying our name and
 * firmware version, and zero exposed parameters. No parameter menu, telemetry, or RC data handling.
 */
class CrsfHandsetModule : private concurrency::OSThread
{
  public:
    CrsfHandsetModule();

  protected:
    int32_t runOnce() override;

  private:
    enum class RxState { WaitSync, WaitLen, WaitData };

    void onUartData();
    void onUartError(int error);
    void handleFrame(const uint8_t *frame, uint8_t len);
    void sendFrame(uint8_t type, uint8_t destAddr, const uint8_t *payload, uint8_t payloadLen);
    void sendDeviceInfo(uint8_t destAddr);
    void sendParameterEntry(uint8_t destAddr, uint8_t fieldId);
    void setDirection(bool transmit);
    void applyPolarityAndBaud();

    RxState rxState = RxState::WaitSync;
    uint8_t rxBuf[64];
    uint8_t rxLen = 0;
    uint8_t rxExpected = 0;

    bool inverted = true;
    uint8_t baudIdx = 0;
    uint32_t framesRxAtLastCheck = 0;
    uint32_t pingsLogged = 0;
};

// Shown in System > CRSF Status; USB can't be attached while the module is in the JR bay.
struct CrsfHandsetStats {
    uint32_t bytesRx = 0;
    uint32_t framesRx = 0;
    uint32_t badCrc = 0;
    uint32_t pingsRx = 0;
    uint32_t infoSent = 0;
    uint32_t paramReads = 0;
    uint32_t uartErrors = 0;
    uint32_t syncCandidates = 0;
    uint32_t baud = 0;
    bool inverted = true;
    uint8_t lastBytes[8] = {0};
    const char *resetNow = "?";
    const char *resetPrev = "?";
};
extern CrsfHandsetStats crsfHandsetStats;
