#pragma once

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
    void updateMeshSnapshot();
    void rebuildMessagesFromStore();

    RxState rxState = RxState::WaitSync;
    uint8_t rxBuf[64];
    uint8_t rxLen = 0;
    uint8_t rxExpected = 0;

    bool inverted = true;
    uint8_t baudIdx = 0;
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
    struct MeshSnapshot {
        uint8_t nodeCount = 0;
        MeshNodeSnapshot nodes[MESH_MAX_NODES];
        MeshMsgSnapshot messages[MESH_MSG_SLOTS];
        uint8_t msgOptionCount = 0;
        char msgOptionsStr[MSG_OPTIONS_STR_LEN] = "";
        char msgOptionTexts[MSG_OPTION_MAX_COUNT][MSG_OPTION_TEXT_LEN] = {};
    };
    MeshSnapshot meshSnapshots[2];
    std::atomic<uint8_t> meshSnapshotIdx{0};

    // Main-thread-only copy of the canned-message texts, rebuilt every updateMeshSnapshot() call;
    // sendSelectedMessage() reads from this (not the double-buffered snapshot) to avoid racing its flip.
    char mainMsgTexts[MSG_OPTION_MAX_COUNT][MSG_OPTION_TEXT_LEN] = {};
    uint8_t mainMsgTextCount = 0;
    void buildCannedMessageOptions(MeshSnapshot &snap);

    // Newest-first cache of MessageStore, main thread only; rebuilt when the store changes.
    MeshMsgSnapshot textMsgRing[MESH_MSG_SLOTS];
    uint8_t textMsgCount = 0;
    size_t lastStoreSize = SIZE_MAX;
    uint32_t lastStoreNewestTs = 0;
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
