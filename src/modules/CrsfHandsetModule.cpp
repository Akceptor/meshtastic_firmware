#include "configuration.h"

// Only built for boards with a CRSF-capable UART pin wired to the radio bay (see variant.h).
#ifdef CRSF_UART_PIN

#include "CrsfHandsetModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "TextMessageModule.h"
#include "Throttle.h"
#include "mesh/MeshTypes.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/cannedmessages.pb.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_system.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>

// CannedMessageModuleConfig is a plain global (not declared in a header) owned by CannedMessageModule.cpp,
// which only builds under this same guard (see Modules.cpp).
#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_CANNEDMESSAGES
extern meshtastic_CannedMessageModuleConfig cannedMessageModuleConfig;
#endif

namespace
{
constexpr uint8_t CRSF_SYNC_BYTE = 0xC8;
constexpr uint8_t CRSF_FRAMETYPE_DEVICE_PING = 0x28;
constexpr uint8_t CRSF_FRAMETYPE_DEVICE_INFO = 0x29;
constexpr uint8_t CRSF_FRAMETYPE_PARAMETER_ENTRY = 0x2B;
constexpr uint8_t CRSF_FRAMETYPE_PARAMETER_READ = 0x2C;
constexpr uint8_t CRSF_FRAMETYPE_PARAMETER_WRITE = 0x2D;
constexpr uint8_t CRSF_PARAM_TYPE_SELECT = 9;
constexpr uint8_t CRSF_PARAM_TYPE_FOLDER = 11;
constexpr uint8_t CRSF_PARAM_TYPE_INFO = 12;
constexpr uint8_t CRSF_PARAM_TYPE_COMMAND = 13;
// Root order (id order == Lua display order within a parent): Message, Send, Messages folder,
// Nodes folder, Refresh, Version; message/node blocks and each folder's own Refresh sit off to the side.
constexpr uint8_t CRSF_FIELD_MESSAGE_SELECT = 1;
constexpr uint8_t CRSF_FIELD_SEND = 2;
constexpr uint8_t CRSF_FIELD_MESSAGES_FOLDER = 3;
constexpr uint8_t CRSF_FIELD_MSG_REFRESH = 4; // child of MESSAGES_FOLDER
constexpr uint8_t CRSF_FIELD_MSG_BASE = 5; // first message folder id; message k's folder = BASE + BLOCK_SIZE*k
constexpr uint8_t CRSF_MSG_ROW_COUNT = 10; // must match CrsfHandsetModule::MESH_MSG_ROW_COUNT (static_assert'd below)
constexpr uint8_t CRSF_MSG_BLOCK_SIZE = CRSF_MSG_ROW_COUNT + 1; // folder id + 10 fixed-id INFO rows
// Mirrors CrsfHandsetModule::MESH_MSG_SLOTS (private; can't reference it from this free-function scope,
// so it's duplicated here and cross-checked by static_assert in the constructor).
constexpr uint8_t CRSF_MSG_SLOT_COUNT = 5;
constexpr uint8_t CRSF_FIELD_NODES_FOLDER = CRSF_FIELD_MSG_BASE + CRSF_MSG_SLOT_COUNT * CRSF_MSG_BLOCK_SIZE; // 60
constexpr uint8_t CRSF_FIELD_NODES_REFRESH = CRSF_FIELD_NODES_FOLDER + 1; // 61, child of NODES_FOLDER
constexpr uint8_t CRSF_FIELD_ROOT_REFRESH = CRSF_FIELD_NODES_REFRESH + 1; // 62, root
constexpr uint8_t CRSF_FIELD_VERSION = CRSF_FIELD_ROOT_REFRESH + 1; // 63, root
constexpr uint8_t CRSF_FIELD_STATIC_COUNT = CRSF_FIELD_VERSION; // ids 1..63 always present, regardless of nodeCount
// Each node gets an 8-id block: the folder itself, then 7 fixed-id INFO rows (Name/Id/SNR/Heard/Hops/HW/Bat).
constexpr uint8_t CRSF_FIELD_NODES_BASE = CRSF_FIELD_STATIC_COUNT + 1; // first node folder's field id; node k's folder = BASE + 8*k
constexpr uint8_t CRSF_FIELD_NODE_BLOCK_SIZE = 8;
constexpr uint8_t CRSF_NODE_ROW_COUNT = 7;
// lcs* status codes from the ELRS Lua fieldCommand handlers.
constexpr uint8_t CRSF_LCS_IDLE = 0;
constexpr uint8_t CRSF_LCS_CLICK = 1;
constexpr uint8_t CRSF_LCS_EXECUTING = 2;
constexpr uint8_t CRSF_LCS_CANCEL = 5;
constexpr uint8_t CRSF_CMD_TIMEOUT_10MS = 50; // Lua polls every timeout*10ms while the popup is open
constexpr uint32_t CRSF_SEND_COOLDOWN_MS = 5000;

bool isRefreshField(uint8_t fieldId)
{
    return fieldId == CRSF_FIELD_MSG_REFRESH || fieldId == CRSF_FIELD_NODES_REFRESH ||
           fieldId == CRSF_FIELD_ROOT_REFRESH;
}

const char *cannedMessagesRaw()
{
#if HAS_SCREEN && !MESHTASTIC_EXCLUDE_CANNEDMESSAGES
    return cannedMessageModuleConfig.messages;
#else
    return "Hi|Bye|Yes|No|Ok"; // CannedMessageModule isn't built on this board; keep the same stock defaults
#endif
}
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
    // Cross-check the id-layout constants duplicated across the class (private, header) and the
    // anonymous namespace (free functions can't see private members) so a change to one can't silently
    // desync from the other.
    static_assert(MESH_MSG_SLOTS == CRSF_MSG_SLOT_COUNT, "CRSF_MSG_SLOT_COUNT must match MESH_MSG_SLOTS");
    static_assert(MESH_MSG_ROW_COUNT == CRSF_MSG_ROW_COUNT, "CRSF_MSG_ROW_COUNT must match MESH_MSG_ROW_COUNT");
    static_assert(CRSF_FIELD_NODES_FOLDER == CRSF_FIELD_MSG_BASE + MESH_MSG_SLOTS * CRSF_MSG_BLOCK_SIZE,
                  "message id blocks must exactly fill [MSG_BASE, NODES_FOLDER)");
    static_assert(CRSF_FIELD_NODES_BASE == CRSF_FIELD_STATIC_COUNT + 1,
                  "node blocks must start right after the static field range");
    static_assert((unsigned)CRSF_FIELD_STATIC_COUNT + (unsigned)CRSF_FIELD_NODE_BLOCK_SIZE * MESH_MAX_NODES + 1 <= 255,
                  "fieldCnt (static + node blocks + hidden parity field) must fit uint8_t");
    static_assert(MSG_OPTION_MAX_COUNT >= 1, "message SELECT needs at least the fixed 'Hi from ExpressLRS!' option");
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
    // TextMessageModule is constructed before us (see Modules.cpp), and notifies synchronously on the main thread.
    textMessageObserver.observe(textMessageModule);
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
    // The Lua uses this as its root-screen title (deviceName); the firmware version is its own INFO row.
    static const char name[] = "ELRS->Meshtastic";
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
    // ELRS Lua skips setting the device name when fieldCnt matches its initial 0, so expose our fields.
    const MeshSnapshot &snap = meshSnapshots[meshSnapshotIdx.load()];
    // Trailing hidden field toggles on/off with fieldGen's low bit, forcing the Lua to notice fldcnt
    // changed and reload every field (see the hidden-field branch in buildParameterEntryBody).
    const uint8_t hiddenFieldPresent = fieldGen.load() & 1;
    payload[i++] = (uint8_t)(CRSF_FIELD_STATIC_COUNT + CRSF_FIELD_NODE_BLOCK_SIZE * snap.nodeCount +
                              hiddenFieldPresent); // fieldCnt
    payload[i++] = 0; // parameterVersion
    sendFrame(CRSF_FRAMETYPE_DEVICE_INFO, destAddr, payload, i);
    crsfHandsetStats.infoSent++;
}

// Appends a null-terminated INFO field: name then value, each including their own NUL.
static void appendInfoField(uint8_t *payload, uint8_t &i, const char *name, const char *value)
{
    size_t n = strlen(name) + 1;
    memcpy(&payload[i], name, n);
    i += n;
    n = strlen(value) + 1;
    memcpy(&payload[i], value, n);
    i += n;
}

// Builds the full parameter-entry body (everything after fieldId+chunksRemain) into entryBody.
// Returns its length. UART task only.
uint8_t CrsfHandsetModule::buildParameterEntryBody(uint8_t fieldId)
{
    const MeshSnapshot &snap = meshSnapshots[meshSnapshotIdx.load()];
    const uint8_t nodesEnd = CRSF_FIELD_NODES_BASE + CRSF_FIELD_NODE_BLOCK_SIZE * snap.nodeCount;
    const uint8_t hiddenFieldId = nodesEnd; // one past the last real id (ids are 1-indexed and contiguous)
    // Node k's folder id and row ids within [CRSF_FIELD_NODES_BASE, nodesEnd); offset 0 = folder, 1..7 = rows.
    const bool inNodeRange = fieldId >= CRSF_FIELD_NODES_BASE && fieldId < nodesEnd;
    const uint8_t nodeBlockOffset = inNodeRange ? (fieldId - CRSF_FIELD_NODES_BASE) % CRSF_FIELD_NODE_BLOCK_SIZE : 0;
    const uint8_t nodeFolderId = fieldId - nodeBlockOffset;
    const uint8_t nodeIdx = inNodeRange ? (fieldId - CRSF_FIELD_NODES_BASE) / CRSF_FIELD_NODE_BLOCK_SIZE : 0;
    // Message k's folder id and row ids within [CRSF_FIELD_MSG_BASE, CRSF_FIELD_NODES_FOLDER); offset 0 =
    // folder, 1..10 = rows. Mirrors the node block math above.
    const bool inMsgRange = fieldId >= CRSF_FIELD_MSG_BASE && fieldId < CRSF_FIELD_NODES_FOLDER;
    const uint8_t msgBlockOffset = inMsgRange ? (fieldId - CRSF_FIELD_MSG_BASE) % CRSF_MSG_BLOCK_SIZE : 0;
    const uint8_t msgFolderId = fieldId - msgBlockOffset;
    const uint8_t msgIdx = inMsgRange ? (fieldId - CRSF_FIELD_MSG_BASE) / CRSF_MSG_BLOCK_SIZE : 0;

    uint8_t *payload = entryBody;
    uint8_t i = 0;
    uint8_t parent = 0;
    if (fieldId == CRSF_FIELD_MSG_REFRESH)
        parent = CRSF_FIELD_MESSAGES_FOLDER;
    else if (fieldId == CRSF_FIELD_NODES_REFRESH)
        parent = CRSF_FIELD_NODES_FOLDER;
    else if (inMsgRange)
        parent = (msgBlockOffset == 0) ? CRSF_FIELD_MESSAGES_FOLDER : msgFolderId;
    else if (inNodeRange)
        parent = (nodeBlockOffset == 0) ? CRSF_FIELD_NODES_FOLDER : nodeFolderId;
    payload[i++] = parent;

    if (fieldId == CRSF_FIELD_MESSAGE_SELECT) {
        static const char fieldName[] = "Message";
        const uint8_t maxIdx = snap.msgOptionCount > 0 ? snap.msgOptionCount - 1 : 0;
        const uint8_t value = std::min<uint8_t>(msgSelectIndex.load(), maxIdx);
        payload[i++] = CRSF_PARAM_TYPE_SELECT;
        memcpy(&payload[i], fieldName, sizeof(fieldName));
        i += sizeof(fieldName);
        const size_t optsLen = strlen(snap.msgOptionsStr) + 1;
        memcpy(&payload[i], snap.msgOptionsStr, optsLen);
        i += optsLen;
        payload[i++] = value;
        payload[i++] = 0; // min
        payload[i++] = maxIdx; // max
        payload[i++] = 0; // default
        payload[i++] = 0; // units ""
    } else if (fieldId == CRSF_FIELD_SEND) {
        static const char fieldName[] = "Send";
        const char *info = sendInfo.load();
        const size_t infoLen = strlen(info) + 1;
        payload[i++] = CRSF_PARAM_TYPE_COMMAND;
        memcpy(&payload[i], fieldName, sizeof(fieldName));
        i += sizeof(fieldName);
        payload[i++] = sendStatus.load();
        payload[i++] = CRSF_CMD_TIMEOUT_10MS;
        memcpy(&payload[i], info, infoLen);
        i += infoLen;
    } else if (fieldId == CRSF_FIELD_VERSION) {
        static const char fieldName[] = "Version";
        static const char value[] = xstr(APP_VERSION);
        payload[i++] = CRSF_PARAM_TYPE_INFO;
        appendInfoField(payload, i, fieldName, value);
    } else if (fieldId == CRSF_FIELD_MESSAGES_FOLDER) {
        payload[i++] = CRSF_PARAM_TYPE_FOLDER;
        static const char fieldName[] = "Messages";
        memcpy(&payload[i], fieldName, sizeof(fieldName));
        i += sizeof(fieldName);
        // Child id list terminated by 0xFF, matching ELRS's own folder encoding (Lua ignores it for us).
        payload[i++] = CRSF_FIELD_MSG_REFRESH;
        for (uint8_t n = 0; n < MESH_MSG_SLOTS; n++)
            payload[i++] = CRSF_FIELD_MSG_BASE + CRSF_MSG_BLOCK_SIZE * n;
        payload[i++] = 0xFF;
    } else if (isRefreshField(fieldId)) {
        // Always idle: the write handler replies immediately (see handleFrame), never leaving it executing.
        static const char fieldName[] = "Refresh";
        static const char info[] = "";
        payload[i++] = CRSF_PARAM_TYPE_COMMAND;
        memcpy(&payload[i], fieldName, sizeof(fieldName));
        i += sizeof(fieldName);
        payload[i++] = CRSF_LCS_IDLE;
        payload[i++] = CRSF_CMD_TIMEOUT_10MS;
        memcpy(&payload[i], info, sizeof(info));
        i += sizeof(info);
    } else if (inMsgRange && msgBlockOffset == 0) {
        // Message folder: opening it is a pure Lua-local state change (fieldFolderOpen), no round trip needed.
        const MeshMsgSnapshot &msg = snap.messages[msgIdx];
        const char *label = msg.label[0] ? msg.label : "-";
        payload[i++] = CRSF_PARAM_TYPE_FOLDER;
        memcpy(&payload[i], label, strlen(label) + 1);
        i += strlen(label) + 1;
        for (uint8_t r = 1; r <= CRSF_MSG_ROW_COUNT; r++)
            payload[i++] = msgFolderId + r;
        payload[i++] = 0xFF;
    } else if (inMsgRange) {
        // Row 1..10 = word-wrapped message lines; hidden (type|0x80) when the message has no such line.
        // The row's NAME (not value) carries the text so it spans the full line width (see fieldStringDisplay).
        const char *line = snap.messages[msgIdx].lines[msgBlockOffset - 1];
        payload[i++] = CRSF_PARAM_TYPE_INFO | (line[0] == '\0' ? 0x80 : 0);
        appendInfoField(payload, i, line, "");
    } else if (fieldId == CRSF_FIELD_NODES_FOLDER) {
        payload[i++] = CRSF_PARAM_TYPE_FOLDER;
        static const char fieldName[] = "Nodes";
        memcpy(&payload[i], fieldName, sizeof(fieldName));
        i += sizeof(fieldName);
        // Child id list terminated by 0xFF, matching ELRS's own folder encoding (Lua ignores it for us).
        payload[i++] = CRSF_FIELD_NODES_REFRESH;
        for (uint8_t n = 0; n < snap.nodeCount; n++)
            payload[i++] = CRSF_FIELD_NODES_BASE + CRSF_FIELD_NODE_BLOCK_SIZE * n;
        payload[i++] = 0xFF;
    } else if (fieldId == hiddenFieldId && (fieldGen.load() & 1)) {
        // Dummy field, never shown: only exists to move fieldCnt so the Lua reloads every field's name.
        payload[i++] = CRSF_PARAM_TYPE_INFO | 0x80;
        appendInfoField(payload, i, "", "");
    } else if (inNodeRange && nodeBlockOffset == 0) {
        // Node folder: opening it is a pure Lua-local state change (fieldFolderOpen), no round trip needed.
        const MeshNodeSnapshot &node = snap.nodes[nodeIdx];
        payload[i++] = CRSF_PARAM_TYPE_FOLDER;
        memcpy(&payload[i], node.value, strlen(node.value) + 1);
        i += strlen(node.value) + 1;
        for (uint8_t r = 1; r <= CRSF_NODE_ROW_COUNT; r++)
            payload[i++] = nodeFolderId + r;
        payload[i++] = 0xFF;
    } else if (inNodeRange) {
        // Row 1..7 = Name/Id/SNR/Heard/Hops/HW/Bat; hidden (type|0x80) when the node has no such data.
        static const char *const rowNames[CRSF_NODE_ROW_COUNT] = {"Name", "Id", "SNR", "Heard", "Hops", "HW", "Bat"};
        const MeshNodeSnapshot &node = snap.nodes[nodeIdx];
        const char *const rowValues[CRSF_NODE_ROW_COUNT] = {node.rowName, node.rowId,   node.rowSnr,
                                                             node.rowHeard, node.rowHops, node.rowHw, node.rowBat};
        const char *value = rowValues[nodeBlockOffset - 1];
        payload[i++] = CRSF_PARAM_TYPE_INFO | (value[0] == '\0' ? 0x80 : 0);
        appendInfoField(payload, i, rowNames[nodeBlockOffset - 1], value);
    } else {
        // Beyond our current layout (e.g. stale fieldCnt on the handset): never leave the Lua waiting.
        payload[0] = 0; // parent: root
        payload[i++] = CRSF_PARAM_TYPE_INFO;
        appendInfoField(payload, i, "-", "");
    }
    entryBodyLen = i;
    return i;
}

void CrsfHandsetModule::sendParameterEntry(uint8_t destAddr, uint8_t fieldId, uint8_t chunkIndex)
{
    sendEntryChunk(destAddr, fieldId, entryBody, buildParameterEntryBody(fieldId), chunkIndex);
}

void CrsfHandsetModule::sendEntryChunk(uint8_t destAddr, uint8_t fieldId, const uint8_t *body, uint8_t bodyLen,
                                       uint8_t chunkIndex)
{
    uint8_t numChunks = (bodyLen + CRSF_ENTRY_CHUNK_MAX - 1) / CRSF_ENTRY_CHUNK_MAX;
    if (numChunks == 0)
        numChunks = 1;
    if (chunkIndex >= numChunks) // handset asked for a chunk we no longer have (stale request): clamp
        chunkIndex = numChunks - 1;
    const uint8_t offset = chunkIndex * CRSF_ENTRY_CHUNK_MAX;
    const uint8_t sliceLen = std::min<uint8_t>(CRSF_ENTRY_CHUNK_MAX, bodyLen - offset);
    const uint8_t chunksRemaining = numChunks - 1 - chunkIndex;

    uint8_t frame[64];
    frame[0] = fieldId;
    frame[1] = chunksRemaining;
    memcpy(&frame[2], body + offset, sliceLen);
    sendFrame(CRSF_FRAMETYPE_PARAMETER_ENTRY, destAddr, frame, 2 + sliceLen);
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
        // Flip parity on every ping so the Lua's fldcnt check always mismatches and reloads all names.
        fieldGen++;
        if (destAddr == CRSF_ADDRESS_BROADCAST || destAddr == CRSF_ADDRESS_CRSF_TRANSMITTER)
            sendDeviceInfo(origAddr);
    } else if (type == CRSF_FRAMETYPE_PARAMETER_READ && destAddr == CRSF_ADDRESS_CRSF_TRANSMITTER && len >= 8) {
        crsfHandsetStats.paramReads++;
        if (frame[5] >= 1) // sendParameterEntry itself handles ids past the current layout
            sendParameterEntry(origAddr, frame[5], frame[6]);
    } else if (type == CRSF_FRAMETYPE_PARAMETER_WRITE && destAddr == CRSF_ADDRESS_CRSF_TRANSMITTER && len >= 8) {
        const uint8_t fieldId = frame[5];
        const uint8_t value = frame[6];
        if (isRefreshField(fieldId)) {
            // Reply first (always idle) so the Lua's popup closes; only a click then bumps parity and
            // pushes Device Info unsolicited, so the Lua reallocates and re-reads every field.
            sendParameterEntry(origAddr, fieldId, 0);
            if (value == CRSF_LCS_CLICK) {
                fieldGen++;
                sendDeviceInfo(origAddr);
            }
        } else if (fieldId == CRSF_FIELD_MESSAGE_SELECT) {
            const MeshSnapshot &snap = meshSnapshots[meshSnapshotIdx.load()];
            const uint8_t maxIdx = snap.msgOptionCount > 0 ? snap.msgOptionCount - 1 : 0;
            msgSelectIndex = std::min(value, maxIdx);
        } else if (fieldId == CRSF_FIELD_SEND) {
            if (value == CRSF_LCS_CLICK && sendStatus.load() == CRSF_LCS_IDLE) {
                if (Throttle::isWithinTimespanMs(lastSendReqMs.load(), CRSF_SEND_COOLDOWN_MS)) {
                    sendInfo = "Wait"; // stays idle so Lua's popup shows "Wait Stopped!" and closes
                } else {
                    lastSendReqMs = millis();
                    sendInfo = "Sending...";
                    sendStatus = CRSF_LCS_EXECUTING;
                    sendRequested = true;
                }
            } else if (value == CRSF_LCS_CANCEL) {
                sendStatus = CRSF_LCS_IDLE;
                sendInfo = "";
            }
            // CRSF_LCS_QUERY (and anything else) just falls through to report current state below.
            sendParameterEntry(origAddr, fieldId, 0);
        }
        // Message and node folders/rows never get a PARAMETER_WRITE: opening a folder is a pure
        // Lua-local state change (fieldFolderOpen), and rows aren't editable.
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

// Mesh sends only happen here (main thread); handleFrame() just flips the atomic request flag. Reads
// mainMsgTexts (main-thread-only) rather than the double-buffered snapshot, so it can't race its flip.
void CrsfHandsetModule::sendSelectedMessage()
{
    const uint8_t maxIdx = mainMsgTextCount > 0 ? mainMsgTextCount - 1 : 0;
    const uint8_t idx = std::min<uint8_t>(msgSelectIndex.load(), maxIdx);
    const char *text = mainMsgTextCount > 0 ? mainMsgTexts[idx] : "";
    meshtastic_MeshPacket *p = router->allocForSending();
    if (p) {
        p->to = NODENUM_BROADCAST;
        p->channel = 0;
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        p->decoded.payload.size = strlen(text);
        memcpy(p->decoded.payload.bytes, text, p->decoded.payload.size);
        service->sendToMesh(p, RX_SRC_LOCAL, true);
        crsfHandsetStats.helloSent++;
        sendInfo = "Sent";
    } else {
        sendInfo = "Failed";
    }
    sendStatus = CRSF_LCS_IDLE;
}

// TextMessageModule notifies synchronously from packet handling, on the main thread, so this can write
// textMsgRing directly (see header comment); excludes our own sendSelectedMessage() broadcasts.
static void shortNameOf(NodeNum num, char *out, size_t outLen)
{
    const meshtastic_NodeInfoLite *ni = nodeDB->getMeshNode(num);
    if (ni && ni->has_user && ni->user.short_name[0])
        snprintf(out, outLen, "%s", ni->user.short_name);
    else
        snprintf(out, outLen, "%04x", (unsigned)(num & 0xFFFF));
}

// Truncates combined ("sender: text") to <=labelLen-1 chars, ending in "..." if it didn't fit.
static void buildMsgLabel(char *label, size_t labelLen, const char *combined)
{
    const size_t maxChars = labelLen - 1;
    const int written = snprintf(label, labelLen, "%s", combined);
    if (written < 0)
        return;
    if ((size_t)written > maxChars) {
        const size_t cut = maxChars - 3; // room for "..."
        label[cut] = '\0';
        strcat(label, "...");
    }
}

// Greedy word-wrap of `text` into up to maxLines rows of <=21 chars (lines[][22]); breaks at spaces,
// hard-breaks words longer than a line, and ends the last line with "..." if text didn't all fit.
static void wrapMessageLines(char lines[][22], uint8_t maxLines, const char *text)
{
    constexpr size_t kMaxChars = 22 - 1;
    for (uint8_t j = 0; j < maxLines; j++)
        lines[j][0] = '\0';
    if (maxLines == 0)
        return;

    uint8_t line = 0;
    size_t col = 0;
    size_t pos = 0;
    const size_t textLen = strlen(text);
    bool stop = false;

    while (pos < textLen && !stop) {
        const size_t wordStart = pos;
        while (pos < textLen && text[pos] != ' ')
            pos++;
        const size_t wordLen = pos - wordStart;
        while (pos < textLen && text[pos] == ' ')
            pos++; // collapse runs of spaces into the single separator we add ourselves

        size_t wordOff = 0;
        while (wordOff < wordLen && !stop) {
            // A fresh word that can't fit (with its separating space) on the current line wraps first;
            // a word already in progress (wordOff>0, from a prior hard-break) just fills to the margin.
            if ((wordOff == 0 && col > 0 && col + 1 + std::min(wordLen, kMaxChars) > kMaxChars) || col == kMaxChars) {
                line++;
                col = 0;
                if (line >= maxLines) {
                    stop = true;
                    break;
                }
            }
            if (col > 0 && wordOff == 0)
                lines[line][col++] = ' ';
            const size_t avail = kMaxChars - col;
            const size_t take = std::min(avail, wordLen - wordOff);
            memcpy(&lines[line][col], text + wordStart + wordOff, take);
            col += take;
            wordOff += take;
            lines[line][col] = '\0';
        }
    }

    if (stop || pos < textLen) {
        const uint8_t last = maxLines - 1;
        size_t len = strlen(lines[last]);
        if (len > kMaxChars - 3)
            len = kMaxChars - 3;
        lines[last][len] = '\0';
        strcat(lines[last], "...");
    }
}

int CrsfHandsetModule::onTextMessageReceived(const meshtastic_MeshPacket *mp)
{
    if (mp->from == nodeDB->getNodeNum())
        return 0;

    for (uint8_t j = MESH_MSG_SLOTS - 1; j > 0; j--)
        textMsgRing[j] = textMsgRing[j - 1];
    MeshMsgSnapshot &entry = textMsgRing[0];

    char sender[8];
    shortNameOf(mp->from, sender, sizeof(sender));

    char textBuf[201];
    size_t n = mp->decoded.payload.size;
    if (n > sizeof(textBuf) - 1)
        n = sizeof(textBuf) - 1;
    memcpy(textBuf, mp->decoded.payload.bytes, n);
    textBuf[n] = '\0';

    char combined[216];
    snprintf(combined, sizeof(combined), "%s: %s", sender, textBuf);

    buildMsgLabel(entry.label, sizeof(entry.label), combined);
    wrapMessageLines(entry.lines, MESH_MSG_ROW_COUNT, combined);

    if (textMsgCount < MESH_MSG_SLOTS)
        textMsgCount++;
    return 0;
}

static void formatAge(char *out, size_t outLen, uint32_t agoSecs)
{
    if (agoSecs < 60)
        snprintf(out, outLen, "%us", (unsigned)agoSecs);
    else if (agoSecs < 3600)
        snprintf(out, outLen, "%um", (unsigned)(agoSecs / 60));
    else if (agoSecs < 86400)
        snprintf(out, outLen, "%uh", (unsigned)(agoSecs / 3600));
    else
        snprintf(out, outLen, "%ud", (unsigned)(agoSecs / 86400));
}

static void formatNodeValue(char *out, size_t outLen, const char *name, float snr, uint32_t agoSecs)
{
    char age[8];
    formatAge(age, sizeof(age), agoSecs);
    snprintf(out, outLen, "%s %ddB %s", name, (int)lroundf(snr), age);
}

// Fills a node's fixed 7 row strings; empty means "hide this row" (see buildParameterEntryBody).
// Takes raw buffers rather than the (private, nested) MeshNodeSnapshot type so it can live outside the class.
static void buildNodeRows(char *rowName, size_t rowNameLen, char *rowId, size_t rowIdLen, char *rowSnr, size_t rowSnrLen,
                           char *rowHeard, size_t rowHeardLen, char *rowHops, size_t rowHopsLen, char *rowHw,
                           size_t rowHwLen, char *rowBat, size_t rowBatLen, const meshtastic_NodeInfoLite *ni)
{
    rowName[0] = rowId[0] = rowSnr[0] = rowHeard[0] = rowHops[0] = rowHw[0] = rowBat[0] = '\0';
    if (!ni)
        return;
    if (ni->has_user && ni->user.long_name[0])
        snprintf(rowName, rowNameLen, "%s", ni->user.long_name);
    snprintf(rowId, rowIdLen, "!%08x", (unsigned)ni->num);
    snprintf(rowSnr, rowSnrLen, "%.1fdB", (double)ni->snr);
    char age[8];
    formatAge(age, sizeof(age), sinceLastSeen(ni));
    snprintf(rowHeard, rowHeardLen, "%s ago", age);
    if (ni->has_hops_away)
        snprintf(rowHops, rowHopsLen, "%u", (unsigned)ni->hops_away);
    if (ni->has_user)
        snprintf(rowHw, rowHwLen, "%u", (unsigned)ni->user.hw_model);
    if (ni->has_device_metrics) {
        const auto &dm = ni->device_metrics;
        if (dm.has_battery_level && dm.has_voltage)
            snprintf(rowBat, rowBatLen, "%u%% %.2fV", (unsigned)dm.battery_level, (double)dm.voltage);
        else if (dm.has_battery_level)
            snprintf(rowBat, rowBatLen, "%u%%", (unsigned)dm.battery_level);
        else if (dm.has_voltage)
            snprintf(rowBat, rowBatLen, "%.2fV", (double)dm.voltage);
    }
}

// Rebuilds mainMsgTexts/mainMsgTextCount (main-thread-owned) from the fixed first option plus the
// '|'-split canned messages, then publishes the same data into snap as a ';'-joined options string.
void CrsfHandsetModule::buildCannedMessageOptions(MeshSnapshot &snap)
{
    static const char kFirstOption[] = "Hi from ExpressLRS!";
    mainMsgTextCount = 0;
    snprintf(mainMsgTexts[mainMsgTextCount++], MSG_OPTION_TEXT_LEN, "%s", kFirstOption);

    const char *p = cannedMessagesRaw();
    while (*p && mainMsgTextCount < MSG_OPTION_MAX_COUNT) {
        const char *sep = strchr(p, '|');
        const size_t len = sep ? (size_t)(sep - p) : strlen(p);
        if (len > 0) {
            char *dst = mainMsgTexts[mainMsgTextCount];
            const size_t n = std::min(len, MSG_OPTION_TEXT_LEN - 1);
            memcpy(dst, p, n);
            dst[n] = '\0';
            for (char *c = dst; *c; c++) // ';' is the CRSF option separator, so it can't appear in an option
                if (*c == ';')
                    *c = ',';
            mainMsgTextCount++;
        }
        if (!sep)
            break;
        p = sep + 1;
    }

    snap.msgOptionsStr[0] = '\0';
    size_t used = 0;
    uint8_t count = 0;
    for (uint8_t j = 0; j < mainMsgTextCount; j++) {
        const size_t entryLen = strlen(mainMsgTexts[j]);
        const size_t needed = entryLen + (count > 0 ? 1 : 0); // +1 for the ';' separator
        if (used + needed >= MSG_OPTIONS_STR_LEN - 1) // stop rather than truncate mid-option
            break;
        if (count > 0)
            snap.msgOptionsStr[used++] = ';';
        memcpy(&snap.msgOptionsStr[used], mainMsgTexts[j], entryLen);
        used += entryLen;
        snap.msgOptionsStr[used] = '\0';
        snprintf(snap.msgOptionTexts[j], MSG_OPTION_TEXT_LEN, "%s", mainMsgTexts[j]);
        count++;
    }
    snap.msgOptionCount = count;
}

// Builds the inactive snapshot buffer from NodeDB/devicestate, then flips the reader's index.
// Main thread only: NodeDB access and mesh state are not safe from the UART event task.
void CrsfHandsetModule::updateMeshSnapshot()
{
    const uint8_t writeIdx = 1 - meshSnapshotIdx.load();
    MeshSnapshot &snap = meshSnapshots[writeIdx];

    buildCannedMessageOptions(snap);

    // textMsgRing is main-thread-owned (see header); a plain copy is enough to publish it to the UART task.
    for (uint8_t j = 0; j < MESH_MSG_SLOTS; j++)
        snap.messages[j] = (j < textMsgCount) ? textMsgRing[j] : MeshMsgSnapshot{};

    // Keep the MESH_MAX_NODES most-recently-heard peers, sorted newest-first, via insertion into a
    // fixed-size top-N list (cheaper than sorting the whole NodeDB for a screen that shows 10 rows).
    NodeNum topNums[MESH_MAX_NODES];
    uint32_t topHeard[MESH_MAX_NODES];
    uint8_t count = 0;
    const NodeNum myNode = nodeDB->getNodeNum();
    for (size_t idx = 0; idx < nodeDB->getNumMeshNodes(); idx++) {
        const meshtastic_NodeInfoLite *ni = nodeDB->getMeshNodeByIndex(idx);
        if (!ni || ni->num == myNode)
            continue;
        const uint32_t heard = ni->last_heard;
        if (count < MESH_MAX_NODES) {
            int pos = count++;
            while (pos > 0 && topHeard[pos - 1] < heard) {
                topHeard[pos] = topHeard[pos - 1];
                topNums[pos] = topNums[pos - 1];
                pos--;
            }
            topHeard[pos] = heard;
            topNums[pos] = ni->num;
        } else if (heard > topHeard[count - 1]) {
            int pos = count - 1;
            while (pos > 0 && topHeard[pos - 1] < heard) {
                topHeard[pos] = topHeard[pos - 1];
                topNums[pos] = topNums[pos - 1];
                pos--;
            }
            topHeard[pos] = heard;
            topNums[pos] = ni->num;
        }
    }

    snap.nodeCount = count;
    for (uint8_t j = 0; j < count; j++) {
        const meshtastic_NodeInfoLite *ni = nodeDB->getMeshNode(topNums[j]);
        char name[8];
        shortNameOf(topNums[j], name, sizeof(name));
        formatNodeValue(snap.nodes[j].value, sizeof(snap.nodes[j].value), name, ni ? ni->snr : 0.0f,
                        ni ? sinceLastSeen(ni) : 0);
        MeshNodeSnapshot &row = snap.nodes[j];
        buildNodeRows(row.rowName, sizeof(row.rowName), row.rowId, sizeof(row.rowId), row.rowSnr, sizeof(row.rowSnr),
                      row.rowHeard, sizeof(row.rowHeard), row.rowHops, sizeof(row.rowHops), row.rowHw, sizeof(row.rowHw),
                      row.rowBat, sizeof(row.rowBat), ni);
    }
    for (uint8_t j = count; j < MESH_MAX_NODES; j++)
        snap.nodes[j] = MeshNodeSnapshot{};

    meshSnapshotIdx = writeIdx;
}

// Like ELRS UARTwdt: the constructor's setup alone never received a valid frame in the bay.
int32_t CrsfHandsetModule::runOnce()
{
    updateMeshSnapshot();

    if (sendRequested.exchange(false))
        sendSelectedMessage();

    if (crsfHandsetStats.pingsRx != pingsLogged) {
        pingsLogged = crsfHandsetStats.pingsRx;
        LOG_INFO("CrsfHandset: pings=%lu info=%lu paramReads=%lu hello=%lu", (unsigned long)crsfHandsetStats.pingsRx,
                 (unsigned long)crsfHandsetStats.infoSent, (unsigned long)crsfHandsetStats.paramReads,
                 (unsigned long)crsfHandsetStats.helloSent);
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
