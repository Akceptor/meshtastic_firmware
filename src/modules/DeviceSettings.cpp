#include "DeviceSettings.h"

#include "mesh/CryptoEngine.h"
#include "mesh/Default.h"
#include "mesh/MeshRadio.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "meshUtils.h" // IF_SCREEN
#include "modules/AdminModule.h" // disableBluetooth
#include "main.h" // rebootAtMsec, shutdownAtMsec
#ifdef EMAX_900_TX_OLED
#include "mesh/SyncWordOverride.h"
#endif
#ifdef ARCH_ESP32
#include <esp_ota_ops.h>
#endif
#if HAS_SCREEN
#include "MessageStore.h" // messageStore.saveToFlash(), only meaningful with a persisted message history
#endif
#include <cmath>
#include <cstring>

namespace
{

const char *const regionNames[] = {"US",      "EU_433",  "EU_868",  "CN",      "JP",     "ANZ",    "KR",
                                    "TW",      "RU",      "IN",      "NZ_865",  "TH",     "LORA_24", "UA_433",
                                    "UA_868",  "MY_433",  "MY_919",  "SG_923",  "PH_433", "PH_868",  "PH_915",
                                    "ANZ_433", "KZ_433",  "KZ_863",  "NP_865",  "BR_902"};
constexpr meshtastic_Config_LoRaConfig_RegionCode regionValues[] = {
    meshtastic_Config_LoRaConfig_RegionCode_US,      meshtastic_Config_LoRaConfig_RegionCode_EU_433,
    meshtastic_Config_LoRaConfig_RegionCode_EU_868,  meshtastic_Config_LoRaConfig_RegionCode_CN,
    meshtastic_Config_LoRaConfig_RegionCode_JP,      meshtastic_Config_LoRaConfig_RegionCode_ANZ,
    meshtastic_Config_LoRaConfig_RegionCode_KR,      meshtastic_Config_LoRaConfig_RegionCode_TW,
    meshtastic_Config_LoRaConfig_RegionCode_RU,      meshtastic_Config_LoRaConfig_RegionCode_IN,
    meshtastic_Config_LoRaConfig_RegionCode_NZ_865,  meshtastic_Config_LoRaConfig_RegionCode_TH,
    meshtastic_Config_LoRaConfig_RegionCode_LORA_24, meshtastic_Config_LoRaConfig_RegionCode_UA_433,
    meshtastic_Config_LoRaConfig_RegionCode_UA_868,  meshtastic_Config_LoRaConfig_RegionCode_MY_433,
    meshtastic_Config_LoRaConfig_RegionCode_MY_919,  meshtastic_Config_LoRaConfig_RegionCode_SG_923,
    meshtastic_Config_LoRaConfig_RegionCode_PH_433,  meshtastic_Config_LoRaConfig_RegionCode_PH_868,
    meshtastic_Config_LoRaConfig_RegionCode_PH_915,  meshtastic_Config_LoRaConfig_RegionCode_ANZ_433,
    meshtastic_Config_LoRaConfig_RegionCode_KZ_433,  meshtastic_Config_LoRaConfig_RegionCode_KZ_863,
    meshtastic_Config_LoRaConfig_RegionCode_NP_865,  meshtastic_Config_LoRaConfig_RegionCode_BR_902};
constexpr size_t regionOptionCountVal = sizeof(regionValues) / sizeof(regionValues[0]);
static_assert(sizeof(regionNames) / sizeof(regionNames[0]) == regionOptionCountVal, "region name/value tables must match");

const char *const presetNames[] = {"LongTurbo", "LongModerate", "LongFast",  "MediumSlow",
                                    "MediumFast", "ShortSlow",   "ShortFast", "ShortTurbo"};
constexpr meshtastic_Config_LoRaConfig_ModemPreset presetValues[] = {
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO,   meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE,
    meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST,    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST,  meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW,
    meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,   meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO};
constexpr size_t presetOptionCountVal = sizeof(presetValues) / sizeof(presetValues[0]);
static_assert(sizeof(presetNames) / sizeof(presetNames[0]) == presetOptionCountVal, "preset name/value tables must match");

constexpr const char *roleNames[] = {"Client", "Client Mute", "Lost and Found", "Tracker"};
constexpr meshtastic_Config_DeviceConfig_Role roleValues[] = {
    meshtastic_Config_DeviceConfig_Role_CLIENT, meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE,
    meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND, meshtastic_Config_DeviceConfig_Role_TRACKER};
constexpr size_t roleOptionCountVal = sizeof(roleValues) / sizeof(roleValues[0]);
static_assert(sizeof(roleNames) / sizeof(roleNames[0]) == roleOptionCountVal, "role name/value tables must match");

// Same 12 dBm steps as ExpressLRS's DAC-controlled power_values rows (see the OLED txPowerPicker).
constexpr const char *txPowerNames[] = {"10dBm", "14dBm", "17dBm", "20dBm", "21dBm", "22dBm",
                                        "23dBm", "24dBm", "25dBm", "26dBm", "27dBm", "28dBm"};
constexpr int8_t txPowerDbmTable[] = {10, 14, 17, 20, 21, 22, 23, 24, 25, 26, 27, 28};
constexpr size_t txPowerOptionCountVal = sizeof(txPowerDbmTable) / sizeof(txPowerDbmTable[0]);
static_assert(sizeof(txPowerNames) / sizeof(txPowerNames[0]) == txPowerOptionCountVal, "tx power name/value tables must match");

} // namespace

uint8_t DeviceSettings::loraRegionOptionCount()
{
    return static_cast<uint8_t>(regionOptionCountVal);
}

const char *DeviceSettings::loraRegionOptionName(uint8_t idx)
{
    return regionNames[idx];
}

meshtastic_Config_LoRaConfig_RegionCode DeviceSettings::loraRegionOptionValue(uint8_t idx)
{
    return regionValues[idx];
}

// Shared "apply" body: called by both the OLED LoraRegionPicker and CrsfHandsetModule's Lua Region SELECT write.
void DeviceSettings::applyLoraRegion(meshtastic_Config_LoRaConfig_RegionCode region)
{
    if (config.lora.region == region) {
        return;
    }

    config.lora.region = region;
    auto changes = SEGMENT_CONFIG;

// FIXME: This should be a method consolidated with the same logic in the admin message as well
// This is needed as we wait til picking the LoRa region to generate keys for the first time.
#if !(MESHTASTIC_EXCLUDE_PKI_KEYGEN || MESHTASTIC_EXCLUDE_PKI)
    if (!owner.is_licensed) {
        bool keygenSuccess = false;
        if (config.security.private_key.size == 32) {
            // public key is derived from private, so this will always have the same result.
            if (crypto->regeneratePublicKey(config.security.public_key.bytes, config.security.private_key.bytes)) {
                keygenSuccess = true;
            }

        } else {
            LOG_INFO("Generate new PKI keys");
            crypto->generateKeyPair(config.security.public_key.bytes, config.security.private_key.bytes);
            keygenSuccess = true;
        }
        if (keygenSuccess) {
            config.security.public_key.size = 32;
            config.security.private_key.size = 32;
            owner.public_key.size = 32;
            memcpy(owner.public_key.bytes, config.security.public_key.bytes, 32);
        }
    }
#endif
    config.lora.tx_enabled = true;
    initRegion();
    if (myRegion->dutyCycle < 100) {
        config.lora.ignore_mqtt = true; // Ignore MQTT by default if region has a duty cycle limit
    }

    if (strncmp(moduleConfig.mqtt.root, default_mqtt_root, strlen(default_mqtt_root)) == 0) {
        //  Default broker is in use, so subscribe to the appropriate MQTT root topic for this region
        sprintf(moduleConfig.mqtt.root, "%s/%s", default_mqtt_root, myRegion->name);
        changes |= SEGMENT_MODULECONFIG;
    }

    service->reloadConfig(changes);
    rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
}

uint8_t DeviceSettings::modemPresetOptionCount()
{
    return static_cast<uint8_t>(presetOptionCountVal);
}

const char *DeviceSettings::modemPresetOptionName(uint8_t idx)
{
    return presetNames[idx];
}

meshtastic_Config_LoRaConfig_ModemPreset DeviceSettings::modemPresetOptionValue(uint8_t idx)
{
    return presetValues[idx];
}

void DeviceSettings::applyModemPreset(meshtastic_Config_LoRaConfig_ModemPreset preset)
{
    config.lora.modem_preset = preset;
    config.lora.channel_num = 0;        // Reset to default channel for the preset
    config.lora.override_frequency = 0; // Clear any custom frequency
    service->reloadConfig(SEGMENT_CONFIG);
    rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
}

uint32_t DeviceSettings::computeLoraNumChannels()
{
    // Mirrors RadioInterface::applyModemConfig()'s channel count calculation.
    if (!myRegion) {
        LOG_WARN("Region not set, cannot calculate number of channels");
        return 0;
    }
    meshtastic_Config_LoRaConfig &loraConfig = config.lora;
    double bw = loraConfig.use_preset ? modemPresetToBwKHz(loraConfig.modem_preset, myRegion->wideLora)
                                      : bwCodeToKHz(loraConfig.bandwidth);
    return (uint32_t)floor((myRegion->freqEnd - myRegion->freqStart) / (myRegion->spacing + (bw / 1000.0)));
}

void DeviceSettings::applyFrequencySlot(uint32_t slot)
{
    config.lora.channel_num = slot;
    service->reloadConfig(SEGMENT_CONFIG);
    rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
}

uint8_t DeviceSettings::txPowerOptionCount()
{
    return static_cast<uint8_t>(txPowerOptionCountVal);
}

const char *DeviceSettings::txPowerOptionName(uint8_t idx)
{
    return txPowerNames[idx];
}

int8_t DeviceSettings::txPowerOptionDbm(uint8_t idx)
{
    return txPowerDbmTable[idx];
}

void DeviceSettings::applyTxPower(int8_t dbm)
{
    config.lora.tx_power = dbm;
    service->reloadConfig(SEGMENT_CONFIG);
}

uint8_t DeviceSettings::deviceRoleOptionCount()
{
    return static_cast<uint8_t>(roleOptionCountVal);
}

const char *DeviceSettings::deviceRoleOptionName(uint8_t idx)
{
    return roleNames[idx];
}

meshtastic_Config_DeviceConfig_Role DeviceSettings::deviceRoleOptionValue(uint8_t idx)
{
    return roleValues[idx];
}

void DeviceSettings::applyDeviceRole(meshtastic_Config_DeviceConfig_Role role)
{
    config.device.role = role;
    service->reloadConfig(SEGMENT_CONFIG);
    rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
}

// Replicates SystemCommandsModule's INPUT_BROKER_MSG_BLUETOOTH_TOGGLE handler directly rather than
// going through the input broker, so it also works on boards built with MESHTASTIC_EXCLUDE_INPUTBROKER.
void DeviceSettings::setBluetoothEnabled(bool enable)
{
    if (enable == config.bluetooth.enabled)
        return;
    config.bluetooth.enabled = enable;
    LOG_INFO("User toggled Bluetooth");
    nodeDB->saveToDisk();
#if defined(ARDUINO_ARCH_NRF52)
    if (!config.bluetooth.enabled) {
        disableBluetooth();
        IF_SCREEN(screen->showSimpleBanner("Bluetooth OFF\nRebooting", 3000));
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 2000;
    } else {
        IF_SCREEN(screen->showSimpleBanner("Bluetooth ON\nRebooting", 3000));
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
    }
#else
    if (!config.bluetooth.enabled) {
        disableBluetooth();
        IF_SCREEN(screen->showSimpleBanner("Bluetooth OFF", 3000));
    } else {
        IF_SCREEN(screen->showSimpleBanner("Bluetooth ON\nRebooting", 3000));
        rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
    }
#endif
}

#if HAS_WIFI
void DeviceSettings::setWifiEnabled(bool enable)
{
    config.network.wifi_enabled = enable;
    config.bluetooth.enabled = !enable;
    service->reloadConfig(SEGMENT_CONFIG);
    rebootAtMsec = (millis() + DEFAULT_REBOOT_SECONDS * 1000);
}
#endif

void DeviceSettings::requestReboot()
{
    IF_SCREEN(screen->showSimpleBanner("Rebooting...", 0));
    nodeDB->saveToDisk();
#if HAS_SCREEN
    messageStore.saveToFlash();
#endif
    rebootAtMsec = millis() + DEFAULT_REBOOT_SECONDS * 1000;
}

// Mirrors SystemCommandsModule's INPUT_BROKER_SHUTDOWN handler; done directly (not via the input
// broker) so it also works on boards built with MESHTASTIC_EXCLUDE_INPUTBROKER.
void DeviceSettings::requestShutdown()
{
    shutdownAtMsec = millis();
}

#ifdef EMAX_900_TX_OLED
void DeviceSettings::setSyncWord(uint8_t word)
{
    saveEmaxSyncWord(word);
    // Nothing about the lora config actually changed; this just re-triggers reconfigure()
    // so RF95Interface re-applies the new sync word to the radio immediately.
    service->reloadConfig(SEGMENT_CONFIG);
}
#endif

#if defined(RF95_FAN_EN) && defined(USE_RF95)
void DeviceSettings::setFanMode(FanMode mode)
{
    fanMode = mode;
    // Nothing about the lora config actually changed; this just re-triggers reconfigure()
    // so RF95Interface re-evaluates the fan pin against the new mode immediately.
    service->reloadConfig(SEGMENT_CONFIG);
}
#endif

bool DeviceSettings::hasSecondOtaPartition()
{
#ifdef ARCH_ESP32
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
        return false;
    esp_partition_subtype_t otherSub = (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
                                            ? ESP_PARTITION_SUBTYPE_APP_OTA_1
                                            : ESP_PARTITION_SUBTYPE_APP_OTA_0;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, otherSub, NULL) != nullptr;
#else
    return false;
#endif
}

// Also persists nodeDB/messageStore before the reboot. Works on any ESP32 dual-OTA layout, not just
// the EMAX/BAYCK boards this was written for — the partition switch itself has nothing board-specific.
bool DeviceSettings::switchToOtherFirmwareSlot()
{
#ifdef ARCH_ESP32
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
        return false;
    esp_partition_subtype_t targetSub = (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
                                            ? ESP_PARTITION_SUBTYPE_APP_OTA_1
                                            : ESP_PARTITION_SUBTYPE_APP_OTA_0;
    const esp_partition_t *target = esp_partition_find_first(ESP_PARTITION_TYPE_APP, targetSub, NULL);
    if (!target || esp_ota_set_boot_partition(target) != ESP_OK) {
        return false;
    }
    nodeDB->saveToDisk();
#if HAS_SCREEN
    messageStore.saveToFlash();
#endif
    rebootAtMsec = millis() + 2000;
    return true;
#else
    return false;
#endif
}
