#pragma once
#if HAS_SCREEN
#include "configuration.h"
#ifdef RF95_FAN_EN
#include "FanControl.h"
#endif
class OLEDDisplay;
namespace graphics
{

class menuHandler
{
  public:
    enum screenMenus {
        MenuNone,
        LoraMenu,
        LoraPicker,
        DeviceRolePicker,
        RadioPresetPicker,
        FrequencySlot,
        NoTimeoutLoraPicker,
        TzPicker,
        TwelveHourPicker,
        ClockFacePicker,
        ClockMenu,
        PositionBaseMenu,
        NodeBaseMenu,
        GpsToggleMenu,
        GpsFormatMenu,
        GpsSmartPositionMenu,
        GpsUpdateIntervalMenu,
        GpsPositionBroadcastMenu,
        CompassPointNorthMenu,
        ResetNodeDbMenu,
        BuzzerModeMenuPicker,
        MuiPicker,
        TftColorMenuPicker,
        BrightnessPicker,
        RebootMenu,
        ShutdownMenu,
        NodePickerMenu,
        ManageNodeMenu,
        RemoveFavorite,
        TestMenu,
        NumberTest,
        WifiToggleMenu,
        BluetoothToggleMenu,
        FanToggleMenu,
        CrsfStatusMenu,
        ScreenOptionsMenu,
        PowerMenu,
        SystemBaseMenu,
        KeyVerificationInit,
        KeyVerificationFinalPrompt,
        TraceRouteMenu,
        ThrottleMessage,
        MessageResponseMenu,
        MessageViewModeMenu,
        ReplyMenu,
        DeleteMessagesMenu,
        NodeNameLengthMenu,
        FrameToggles,
        DisplayUnits,
        MessageBubblesMenu,
        TxPowerPicker,
        SyncWordMenu,
        ChangeSlotMenu
    };
    static screenMenus menuQueue;
    static uint32_t pickedNodeNum; // node selected by NodePicker for ManageNodeMenu

    static void OnboardMessage();
    static void LoraRegionPicker(uint32_t duration = 30000);
    static void loraMenu();
    static void deviceRolePicker();
    static void radioPresetPicker();
    static void FrequencySlotPicker();
    static void handleMenuSwitch(OLEDDisplay *display);
    static void showConfirmationBanner(const char *message, std::function<void()> onConfirm);
    static void clockMenu();
    static void TZPicker();
    static void twelveHourPicker();
    static void clockFacePicker();
    static void messageResponseMenu();
    static void messageViewModeMenu();
    static void replyMenu();
    static void deleteMessagesMenu();
    static void homeBaseMenu();
    static void textMessageBaseMenu();
    static void systemBaseMenu();
    static void favoriteBaseMenu();
    static void positionBaseMenu();
    static void compassNorthMenu();
    static void GPSToggleMenu();
    static void GPSFormatMenu();
    static void GPSSmartPositionMenu();
    static void GPSUpdateIntervalMenu();
    static void GPSPositionBroadcastMenu();
    static void BuzzerModeMenu();
    static void switchToMUIMenu();
    static void TFTColorPickerMenu(OLEDDisplay *display);
    static void nodeListMenu();
    static void resetNodeDBMenu();
    static void BrightnessPickerMenu();
    static void rebootMenu();
    static void shutdownMenu();
    static void NodePicker();
    static void manageNodeMenu();
    static void addFavoriteMenu();
    static void removeFavoriteMenu();
    static void traceRouteMenu();
    static void testMenu();
    static void numberTest();
    static void wifiBaseMenu();
    static void wifiToggleMenu();
    static void fanToggleMenu();
    static void crsfStatusMenu();
    static void screenOptionsMenu();
    static void powerMenu();
    static void nodeNameLengthMenu();
    static void frameTogglesMenu();
    static void displayUnitsMenu();
    static void messageBubblesMenu();
    static void textMessageMenu();
    static void txPowerPicker();
    static void syncWordMenu();
    static void changeSlotMenu();

    // --- Apply bodies + option tables shared with CrsfHandsetModule's Lua Settings folder, so the
    // Lua menu drives the exact same logic as the on-device pickers above. Main thread only. ---
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
    static bool switchToOtherFirmwareSlot();
#endif
#ifdef RF95_FAN_EN
    static void setFanMode(FanMode mode);
#endif

  private:
    static void saveUIConfig();
    static void keyVerificationInitMenu();
    static void keyVerificationFinalPrompt();
    static void bluetoothToggleMenu();
};

/* Generic Menu Options designations  */
enum class OptionsAction { Back, Select };

template <typename T> struct MenuOption {
    const char *label;
    OptionsAction action;
    bool hasValue;
    T value;

    MenuOption(const char *labelIn, OptionsAction actionIn, T valueIn)
        : label(labelIn), action(actionIn), hasValue(true), value(valueIn)
    {
    }

    MenuOption(const char *labelIn, OptionsAction actionIn) : label(labelIn), action(actionIn), hasValue(false), value() {}
};

struct ScreenColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    bool useVariant;

    explicit ScreenColor(uint8_t rIn = 0, uint8_t gIn = 0, uint8_t bIn = 0, bool variantIn = false)
        : r(rIn), g(gIn), b(bIn), useVariant(variantIn)
    {
    }
};

using RadioPresetOption = MenuOption<meshtastic_Config_LoRaConfig_ModemPreset>;
using LoraRegionOption = MenuOption<meshtastic_Config_LoRaConfig_RegionCode>;
using TimezoneOption = MenuOption<const char *>;
using CompassOption = MenuOption<meshtastic_CompassMode>;
using ScreenColorOption = MenuOption<ScreenColor>;
using GPSToggleOption = MenuOption<meshtastic_Config_PositionConfig_GpsMode>;
using GPSFormatOption = MenuOption<meshtastic_DeviceUIConfig_GpsCoordinateFormat>;
using NodeNameOption = MenuOption<bool>;
using PositionMenuOption = MenuOption<int>;
using ManageNodeOption = MenuOption<int>;
using ClockFaceOption = MenuOption<bool>;

} // namespace graphics
#endif
