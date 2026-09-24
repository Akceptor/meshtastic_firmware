#pragma once

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
    void replyPopup(uint8_t destAddr, uint8_t fieldId, uint8_t value, bool open);
    void sendHello();
    void setDirection(bool transmit);
    void applyPolarityAndBaud();
    void updateMeshSnapshot();
    int onTextMessageReceived(const meshtastic_MeshPacket *mp);

    RxState rxState = RxState::WaitSync;
    uint8_t rxBuf[64];
    uint8_t rxLen = 0;
    uint8_t rxExpected = 0;

    bool inverted = true;
    uint8_t baudIdx = 0;
    uint32_t framesRxAtLastCheck = 0;
    uint32_t pingsLogged = 0;

    // Say Hello COMMAND field: written from the UART event task, consumed on the OSThread.
    std::atomic<bool> helloRequested{false};
    std::atomic<uint8_t> helloStatus{0}; // CRSF lcs* status codes: 0 idle, 2 executing
    std::atomic<const char *> helloInfo{""};
    std::atomic<uint32_t> lastHelloReqMs{0};

    // Entry payload assembled whole, then sliced into <=64B CRSF frames on request; UART task only,
    // so a member (not a uart_event_task stack local) is fine despite the size.
    static constexpr size_t CRSF_ENTRY_BODY_MAX = 256;
    static constexpr uint8_t CRSF_ENTRY_CHUNK_MAX = 50;
    uint8_t entryBody[CRSF_ENTRY_BODY_MAX];
    uint8_t entryBodyLen = 0;
    uint8_t popupBody[CRSF_ENTRY_BODY_MAX];
    uint8_t popupBodyLen = 0;
    uint8_t popupFieldId = 0;
    uint8_t popupChunk = 0;

    // Message popup state: written from the UART event task on PARAMETER_WRITE, read back into the
    // COMMAND field's status/info on the next PARAMETER_READ/WRITE reply. Nodes are plain INFO rows now,
    // no popup/confirm state needed for them.
    static constexpr uint8_t MESH_MAX_NODES = 10;
    static constexpr uint8_t MESH_MSG_SLOTS = 5;
    std::atomic<uint8_t> msgPopupStatus[MESH_MSG_SLOTS];

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
        char value[24] = "";  // preview shown as the field name
        char full[201] = "";  // full popup text; empty means "no message in this slot"
    };
    struct MeshSnapshot {
        uint8_t nodeCount = 0;
        MeshNodeSnapshot nodes[MESH_MAX_NODES];
        MeshMsgSnapshot messages[MESH_MSG_SLOTS];
    };
    MeshSnapshot meshSnapshots[2];
    std::atomic<uint8_t> meshSnapshotIdx{0};

    // Ring of received texts, newest first; written by onTextMessageReceived, read by updateMeshSnapshot.
    // Both run on the main thread (TextMessageModule notifies observers synchronously from packet handling,
    // same thread as the OSThread scheduler), so no locking is needed despite the shared state.
    MeshMsgSnapshot textMsgRing[MESH_MSG_SLOTS];
    uint8_t textMsgCount = 0;
    CallbackObserver<CrsfHandsetModule, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<CrsfHandsetModule, const meshtastic_MeshPacket *>(this, &CrsfHandsetModule::onTextMessageReceived);
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
