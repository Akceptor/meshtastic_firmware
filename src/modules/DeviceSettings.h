#pragma once

#include "configuration.h" // HAS_WIFI, RF95_FAN_EN, EMAX_900_TX_OLED
#include "mesh/generated/meshtastic/config.pb.h"
#if defined(RF95_FAN_EN) && defined(USE_RF95)
#include "mesh/FanControl.h"
#endif
#include <cstdint>

// Screen-independent settings shared by the OLED pickers (graphics::menuHandler) and the CRSF Lua
// Settings folder (CrsfHandsetModule), so both UIs drive the exact same apply logic. Always compiled
// (no HAS_SCREEN dependency) — anything screen-related inside DeviceSettings.cpp is IF_SCREEN/#if
// HAS_SCREEN guarded. Main thread only: these touch config/NodeDB and may reboot.
class DeviceSettings
{
  public:
    static uint8_t loraRegionOptionCount();
    static const char *loraRegionOptionName(uint8_t idx); // short name, e.g. "EU_868"
    static meshtastic_Config_LoRaConfig_RegionCode loraRegionOptionValue(uint8_t idx);
    static uint8_t modemPresetOptionCount();
    static const char *modemPresetOptionName(uint8_t idx);
    static meshtastic_Config_LoRaConfig_ModemPreset modemPresetOptionValue(uint8_t idx);
    static uint8_t deviceRoleOptionCount();
    static const char *deviceRoleOptionName(uint8_t idx);
    static meshtastic_Config_DeviceConfig_Role deviceRoleOptionValue(uint8_t idx);
    static uint8_t txPowerOptionCount();
    static const char *txPowerOptionName(uint8_t idx); // e.g. "14dBm"
    static int8_t txPowerOptionDbm(uint8_t idx);
    static uint32_t computeLoraNumChannels(); // channel count for the current region/preset

    static void applyLoraRegion(meshtastic_Config_LoRaConfig_RegionCode region);
    static void applyModemPreset(meshtastic_Config_LoRaConfig_ModemPreset preset);
    static void applyFrequencySlot(uint32_t slot);
    static void applyTxPower(int8_t dbm);
    static void applyDeviceRole(meshtastic_Config_DeviceConfig_Role role);
    static void setBluetoothEnabled(bool enable);
#if HAS_WIFI
    static void setWifiEnabled(bool enable);
#endif
    static void requestReboot();
    static void requestShutdown();
#ifdef EMAX_900_TX_OLED
    static void setSyncWord(uint8_t word);
#endif
#if defined(RF95_FAN_EN) && defined(USE_RF95)
    static void setFanMode(FanMode mode);
#endif

    // True if the running image has a second OTA app partition to switch into (dual-boot ESP32
    // layouts only). Cheap enough to call directly, but callers that need a stable value across a
    // UART/interrupt boundary should snapshot it once (see CrsfHandsetModule::bootElrsAvailable).
    static bool hasSecondOtaPartition();
    // Switches the boot partition to the other OTA slot and reboots into it (e.g. back into ELRS).
    // Persists nodeDB/messageStore first. Returns false if there's no second slot or the switch failed.
    static bool switchToOtherFirmwareSlot();
};
