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

    void handleFrame(const uint8_t *frame, uint8_t len);
    void sendDeviceInfo(uint8_t destAddr);
    void setDirection(bool transmit);

    RxState rxState = RxState::WaitSync;
    uint8_t rxBuf[64];
    uint8_t rxLen = 0;
    uint8_t rxExpected = 0;
};
