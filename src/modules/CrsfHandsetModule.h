#pragma once

#include "configuration.h" // HAS_WIFI, RF95_FAN_EN, EMAX_900_TX_OLED
#include "Observer.h"
#include "concurrency/OSThread.h"
#include "mesh/MeshTypes.h"
#include <Arduino.h>
#include <atomic>

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
    uint8_t buildParameterEntryBody(uint8_t fieldId);
    void sendParameterEntry(uint8_t destAddr, uint8_t fieldId, uint8_t chunkIndex);
    void sendEntryChunk(uint8_t destAddr, uint8_t fieldId, const uint8_t *body, uint8_t bodyLen, uint8_t chunkIndex);
    void sendSelectedMessage();
    void setDirection(bool transmit);
    void applyPolarityAndBaud();
#ifdef CRSF_UART_RX_PIN
    uint32_t autobaud();
#endif
    void updateMeshSnapshot();
#if HAS_SCREEN
    void rebuildMessagesFromStore();
#else
    // No MessageStore without a screen (see MessageStore.h): fed instead by a TextMessageModule
    // observer, RAM-only (see textMessageObserver below).
    int onTextMessageReceived(const meshtastic_MeshPacket *mp);
#endif
    void buildSettingsSnapshot(uint8_t writeIdx); // fills meshSnapshots[writeIdx].settings
    void applyPendingSettingsWrite(uint8_t fieldId, uint8_t value);

    RxState rxState = RxState::WaitSync;
    uint8_t rxBuf[64];
    uint8_t rxLen = 0;
    uint8_t rxExpected = 0;

    // Half-duplex (CRSF_UART_PIN) starts inverted, matching ELRS's own half-duplex default; full-duplex
    // (CRSF_UART_RX_PIN/TX_PIN) starts non-inverted (ELRS CRSFHandset::Begin: UARTinverted = halfDuplex).
#ifdef CRSF_UART_RX_PIN
    bool inverted = false;
#else
    bool inverted = true;
#endif
    uint8_t baudIdx = 0;
#ifdef CRSF_UART_RX_PIN
    enum class AutobaudState { Init, Measured, Inverted };
    AutobaudState autobaudState = AutobaudState::Init;
#endif
    uint32_t framesRxAtLastCheck = 0;
    uint32_t pingsLogged = 0;

    // Send COMMAND field: written from the UART event task, consumed on the OSThread.
    std::atomic<bool> sendRequested{false};
    std::atomic<uint8_t> sendStatus{0}; // CRSF lcs* status codes: 0 idle, 2 executing
    std::atomic<const char *> sendInfo{""};
    std::atomic<uint32_t> lastSendReqMs{0};

    // Message SELECT field: index into the canned-message option list, written from the UART event
    // task; clamped to the current option count both on write and on read.
    std::atomic<uint8_t> msgSelectIndex{0};

    // Settings > (Region/Preset/Slot/TxPower/Role/Bluetooth/WiFi/SyncWord/Fan) writes: the UART task
    // just records the latest one here (single slot; Lua edits one field at a time), and runOnce()
    // applies it via the DeviceSettings:: functions and refreshes the snapshot. 0 = none pending.
    std::atomic<uint8_t> pendingSettingsField{0};
    std::atomic<uint8_t> pendingSettingsValue{0};

    // Settings > Reboot/Shutdown/"Boot ELRS" COMMAND fields: lcs* status only (0 idle, 3 askConfirm);
    // the fixed confirmation text is baked into buildParameterEntryBody, not stored here.
    std::atomic<uint8_t> rebootCmdStatus{0};
    std::atomic<bool> rebootRequested{false};
    std::atomic<uint8_t> shutdownCmdStatus{0};
    std::atomic<bool> shutdownRequested{false};
    std::atomic<uint8_t> bootElrsCmdStatus{0};
    std::atomic<bool> bootElrsRequested{false};
    // Whether a second OTA app partition exists to switch into; checked once at construction (main
    // thread — the partition table doesn't change at runtime) and read from the UART task to decide
    // whether "Boot ELRS" is shown (hidden bit), same as any other board with a single app partition.
    std::atomic<bool> bootElrsAvailable{false};

    // Entry payload assembled whole, then sliced into <=64B CRSF frames on request; UART task only,
    // so a member (not a uart_event_task stack local) is fine despite the size.
    static constexpr size_t CRSF_ENTRY_BODY_MAX = 256;
    static constexpr uint8_t CRSF_ENTRY_CHUNK_MAX = 50;
    uint8_t entryBody[CRSF_ENTRY_BODY_MAX];
    uint8_t entryBodyLen = 0;

    // Messages and nodes are plain FOLDER/INFO rows (no popup/confirm state needed): opening a folder is
    // a pure Lua-local state change (fieldFolderOpen), no round trip to the firmware.
    static constexpr uint8_t MESH_MAX_NODES = 10;
    static constexpr uint8_t MESH_MSG_SLOTS = 5;
    static constexpr uint8_t MESH_MSG_ROW_COUNT = 10; // must match CRSF_MSG_ROW_COUNT in the .cpp (static_assert'd)

    // Message SELECT option list: "Hi from ExpressLRS!" plus up to 11 '|'-split canned messages.
    static constexpr uint8_t MSG_OPTION_MAX_COUNT = 12;
    static constexpr size_t MSG_OPTION_TEXT_LEN = 48;   // per-option send/display text buffer
    static constexpr size_t MSG_OPTIONS_STR_LEN = 208;  // ';'-joined options string, ~200 char budget + NUL

    // Toggled on the UART event task on every Device Ping and every Refresh write; flips a hidden
    // trailing field's presence so Device Info's fieldCnt changes, forcing the Lua to notice the
    // fldcnt mismatch and reload every field (re-reading all cached names).
    std::atomic<uint8_t> fieldGen{0};

    // Mesh state fed to the Lua menu (Messages + Nodes folders): built on the OSThread, read from the UART task.
    struct MeshNodeSnapshot {
        char value[24] = "";   // preview shown as the node folder's name
        char rowName[40] = ""; // "Name" row (long_name); empty means hidden
        char rowId[12] = "";   // "Id" row, "!%08x"; empty means "no node in this slot"
        char rowSnr[12] = "";  // "SNR" row, e.g. "-7.2dB"
        char rowHeard[12] = ""; // "Heard" row, e.g. "2m ago"
        char rowHops[8] = "";  // "Hops" row; empty means hidden
        char rowHw[8] = "";    // "HW" row; empty means hidden
        char rowBat[20] = "";  // "Bat" row, e.g. "87% 4.05V"; empty means hidden
    };
    struct MeshMsgSnapshot {
        char label[24] = "";                          // preview shown as the message folder's name
        char lines[MESH_MSG_ROW_COUNT][22] = {};       // word-wrapped rows (<=21 chars); empty means hidden row
    };
    // Current values for the Settings folder; indices are into the matching DeviceSettings::*OptionName/
    // *OptionValue tables. Built on the main thread (config/NodeDB reads are not safe from the UART task).
    struct SettingsSnapshot {
        uint8_t regionIdx = 0;
        uint8_t presetIdx = 0;
        uint8_t slotValue = 0; // config.lora.channel_num, clamped to uint8_t
        uint8_t slotMax = 0;   // computeLoraNumChannels(), clamped to uint8_t
        uint8_t txPowerIdx = 0;
        uint8_t roleIdx = 0;
        uint8_t bluetoothOn = 0; // 0/1
#if HAS_WIFI
        uint8_t wifiOn = 0; // 0/1
#endif
#ifdef EMAX_900_TX_OLED
        uint8_t syncWordIdx = 0; // 0 = 0x2b (standard), 1 = 0x12 (LR11xx compat)
#endif
#if defined(RF95_FAN_EN) && defined(USE_RF95)
        uint8_t fanIdx = 0; // 0 = Auto, 1 = On, 2 = Off
#endif
    };
    struct MeshSnapshot {
        uint8_t nodeCount = 0;
        MeshNodeSnapshot nodes[MESH_MAX_NODES];
        MeshMsgSnapshot messages[MESH_MSG_SLOTS];
        uint8_t msgOptionCount = 0;
        char msgOptionsStr[MSG_OPTIONS_STR_LEN] = "";
        char msgOptionTexts[MSG_OPTION_MAX_COUNT][MSG_OPTION_TEXT_LEN] = {};
        SettingsSnapshot settings;
    };
    MeshSnapshot meshSnapshots[2];
    std::atomic<uint8_t> meshSnapshotIdx{0};

    // Main-thread-only copy of the canned-message texts, rebuilt every updateMeshSnapshot() call;
    // sendSelectedMessage() reads from this (not the double-buffered snapshot) to avoid racing its flip.
    char mainMsgTexts[MSG_OPTION_MAX_COUNT][MSG_OPTION_TEXT_LEN] = {};
    uint8_t mainMsgTextCount = 0;
    void buildCannedMessageOptions(MeshSnapshot &snap);

    // Newest-first ring of received texts, main thread only.
    MeshMsgSnapshot textMsgRing[MESH_MSG_SLOTS];
    uint8_t textMsgCount = 0;
#if HAS_SCREEN
    // Cache of MessageStore (persisted to flash); rebuilt when the store changes.
    size_t lastStoreSize = SIZE_MAX;
    uint32_t lastStoreNewestTs = 0;
#else
    // TextMessageModule notifies observers synchronously from packet handling, on the main thread
    // (same thread as the OSThread scheduler), so onTextMessageReceived can write textMsgRing directly.
    // RAM-only: without MessageStore this history doesn't survive a reboot.
    CallbackObserver<CrsfHandsetModule, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<CrsfHandsetModule, const meshtastic_MeshPacket *>(this, &CrsfHandsetModule::onTextMessageReceived);
#endif
};

// Shown in System > CRSF Status; USB can't be attached while the module is in the JR bay.
struct CrsfHandsetStats {
    uint32_t bytesRx = 0;
    uint32_t framesRx = 0;
    uint32_t badCrc = 0;
    uint32_t pingsRx = 0;
    uint32_t infoSent = 0;
    uint32_t paramReads = 0;
    uint32_t helloSent = 0;
    uint32_t uartErrors = 0;
    uint32_t syncCandidates = 0;
    uint32_t baud = 0;
    bool inverted = true;
    uint8_t lastBytes[8] = {0};
    const char *resetNow = "?";
    const char *resetPrev = "?";
};
extern CrsfHandsetStats crsfHandsetStats;
