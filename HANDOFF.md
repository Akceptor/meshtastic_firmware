# Meshtastic Firmware — Board Support Handoff

Repo: https://github.com/Akceptor/meshtastic_firmware (branch: `develop-2.7.26`)
Base: upstream meshtastic/firmware @ same branch

---

## PLANNED: Ra-01/Ra-01H dual-band bridge — port of bayckrc_dual_band's simulcast pattern

Goal: ESP32-WROOM-32U + Ra-01 (SX1278, 433MHz) + Ra-01H (SX1276, 868MHz), receive on
either band, retransmit the same packet on both. This is the same shape as
`bayckrc_dual_band`'s dual-LR1120 simulcast (see below) — one nodeDB/router, two radio
interfaces, `Router` mirrors every TX and `PacketHistory`'s existing dedup collapses a
packet heard on both radios for free. Deliberately **not** the two-independent-meshes
UART-bridge approach (stock `Serial` module in `PROTO` mode) — that re-originates every
crossing packet as a new local message (fresh `from`/`id`/`hop_limit`), which loses sender
attribution and has no loop protection across the bridge. The `bayckrc_dual_band` pattern
avoids all of that because both radios belong to the same `Router`/nodeDB.

**What already exists and can be reused as-is:**
- `RadioLibInterface::instance2` + secondary ISR trampolines (`RadioLibInterface.h/.cpp`) —
  generic, not LR11x0-specific. Confirmed by reading the header.
- `Router::iface2` / `addSecondInterface()` and the TX-mirror clone-and-send in
  `Router.cpp` (`rawSend()` and `send()`) — generic, chip-agnostic.
- The `#ifdef BAYCKRC_DUAL_BAND` block in `main.cpp` (~line 998-1080) that constructs the
  second interface, fails closed if it doesn't init, and calls `addSecondInterface()` — copy
  this pattern wholesale for the new variant's own `#ifdef`.

**What's missing and is the actual work:**
- `RF95Interface.h/.cpp` (the SX127x driver) does **not** have the `isSecondary` /
  `fixedFreqOverride` constructor params that `LR11x0Interface`/`LR1120Interface` got for
  the bayckrc port. Need to thread those through the same way, plus the constructor's
  `isSecondary` flag into the base `RadioLibInterface` constructor.
- New variant dir `variants/esp32/<name>/` with `variant.h` + `platformio.ini`. Pins already
  worked out (shared VSPI bus, per-radio NSS/RST/IRQ):

  ```
                      ESP32-WROOM-32U DevKit
                      ┌───────────────────────┐
     3V3 ────┬────────┤ 3V3               GND ├────┬──── GND
             │        │                       │    │
             │   ┌────┤ 18 (SCK)   VSPI       │    │
             │   │ ┌──┤ 19 (MISO)             │    │
             │   │ │ ┌┤ 23 (MOSI)             │    │
             │   │ │ ││                       │    │
             │   │ │ ││ 5   ──► NSS_A         │    │
             │   │ │ ││ 14  ──► RST_A         │    │
             │   │ │ ││ 26  ◄── DIO0_A        │    │
             │   │ │ ││ 33  ◄── DIO1_A        │    │
             │   │ │ ││                       │    │
             │   │ │ ││ 4   ──► NSS_B         │    │
             │   │ │ ││ 27  ──► RST_B         │    │
             │   │ │ ││ 25  ◄── DIO0_B        │    │
             │   │ │ ││ 32  ◄── DIO1_B        │    │
                      └───────────────────────┘
             │   │ │ │
        ┌────┴───┴─┴─┴───────┐          ┌───────────────────┐
        │  Ra-01  (SX1278)   │          │  Ra-01H (SX1276)  │
        │  433 MHz           │          │  868/915 MHz      │
        │ ANT ─── 433 whip   │          │ ANT ─── 868 whip  │
        └────────────────────┘          └───────────────────┘
  ```

  | Signal | ESP32 | Ra-01 (433) | Ra-01H (868) |
  |---|---|---|---|
  | SCK    | 18  | SCK  | SCK  |
  | MISO   | 19  | MISO | MISO |
  | MOSI   | 23  | MOSI | MOSI |
  | NSS_A  | 5   | NSS  | —    |
  | RST_A  | 14  | RST  | —    |
  | DIO0_A | 26  | DIO0 | —    |
  | DIO1_A | 33  | DIO1 | —    |
  | NSS_B  | 4   | —    | NSS  |
  | RST_B  | 27  | —    | RST  |
  | DIO0_B | 25  | —    | DIO0 |
  | DIO1_B | 32  | —    | DIO1 |
  | 3V3    | 3V3 | 3V3  | 3V3  |
  | GND    | GND | GND ×3 | GND ×3 |

  GPIO 6-11 (SPI flash), 0/2/12/15 (strapping) avoided. 10k pull-up on each NSS so
  neither radio listens to the bus while the ESP32 boots and the pins float. Clock the
  shared bus at 8MHz — SX127x tops out at 10MHz. Full derivation, power/decoupling and
  RF-safety detail in `docs/dual-band-meshtastic-bridge.md`.
- **No `rfswitch.h` needed** — that mechanism is LR11x0-specific (internal DIO-driven RF
  switch table). SX127x has no equivalent; RadioLib drives PA_BOOST directly.
- **No sync-word workaround needed** for this pair specifically — both radios are SX127x,
  so the LR11xx-can't-hear-SX127x sync-word trap (`meshtastic/firmware#4775`, see below)
  only matters if one of these two radios ever talks to an LR1120/LR1121 node.
- **No TCXO config needed** — Ra-01/Ra-01H are plain XTAL, same as EMAX's SX1276.
- Verify PA config separately for each module (Ra-01 vs Ra-01H may have different power
  ceilings/PA path) — this is exactly the class of bug the EMAX port hit (SX1276 overdriving
  the PA by 15dB, see "How the EMAX PA works" below). Do not assume Ra-01H's config carries
  over unmeasured.

**RF/power caveats specific to this pair, not present on bayckrc's single dual-band chip:**
`bayckrc_dual_band`'s two LR1120s never risk desensing each other on transmit, because the
*same chip* handles both bands and RadioLib/RF-switch logic already sequences it. Two
*separate* transceivers transmitting near-simultaneously is a real risk here:
- SX127x RX input tolerates roughly 0dBm before damage/desense; a nearby +20dBm TX (either
  radio) can degrade or destroy the other radio's front end even across different bands.
- Antenna separation 30-50cm minimum, perpendicular, ground plane between modules. Never
  power up either module without its antenna attached.
- TX must be serialized in firmware (mutex around the shared SPI bus + both radios' keyup) —
  same as the mirror-send code already does sequentially, but confirm on hardware that the
  timing gap between the two `iface2`/`iface` sends is enough that both PAs are never keyed
  at once; the shared-AMS1117 brownout risk is real at ~120mA peak per module.
- 433MHz at 20dBm exceeds the 10mW ERP limit in most of ITU Region 1 — check local
  allocation before setting TX power that high.

Full wiring diagram, net list, power/decoupling notes, and RF safety details are in
`docs/dual-band-meshtastic-bridge.md` (written before this bayckrc precedent was found —
its custom RadioLib-bridge code sketch there is now superseded by "just port
bayckrc_dual_band", but the wiring/power/RF sections still apply unchanged).

Status: design/planning only. Nothing built or flashed yet.

---

## FIXED: unified_esp32c3_lr1121_rx crashed generating PKI keys on first region set

Setting a region for the first time (fresh flash) triggers `CryptoEngine::generateKeyPair()`
(first-time PKI key generation). On this board it hard-faulted, so the region setting was lost
on the reboot that followed — looked like "region doesn't save" from the app side, but was
actually a crash, not a persistence bug.

**Three faults found chasing this, in order:**

1. `mixWithLoRaEntropy()` (`src/mesh/HardwareRNG.cpp`) calls `radio->randomBytes()`, which for
   LR11x0 goes through RadioLib's `LR11x0::randomByte()` → `Module::SPIcommand` →
   `Module::SPItransferStream` → **Load access fault**. This is a real, LR11x0-family-wide
   risk (not board-specific — `bayckrc_dual_band` shares the same code path and just hadn't
   hit it yet, since PKI generation only fires once, on first real region set). **Fixed**:
   `LR11x0Interface::randomBytes()` now overrides the base and always returns `false`,
   skipping modem-sourced entropy for this whole radio family. ESP32's own
   `esp_fill_random()` already covers the primary entropy source — this is a defensive
   removal of a broken "nice to have" mixing step, not a loss of real security.

2. With that fixed, region-set still crashed, but **not** in Curve25519 math — bisected on
   hardware with temporary `LOG_ERROR` tracepoints through `generateKeyPair()` →
   `HardwareRNG::fill()` → `mixWithLoRaEntropy()`. Crash was the virtual call
   `radio->randomBytes(scratch, toCopy)` itself, before it ever reached the (now-safe)
   `LR11x0Interface::randomBytes()` body. `Guru Meditation Error: Instruction access fault`,
   `MEPC: 0x00000000` (jumped to a null address).

   Diagnosed via ELF inspection (`riscv32-esp-elf-nm -C` on the built `.elf`): the
   heap-allocated `LR1121Interface` object's vtable pointer was corrupted at the time of the
   call. Its real vtable (`vtable for LR11x0Interface<LR1121>`) lives at `0x3c177190`, but the
   pointer actually stored in the live object's first word decoded to `0x3c16a794` — 8 bytes
   into `vtable for MQTT`. Reading the `randomBytes` slot at that (wrong) vtable location
   returned a null function pointer, hence the jump to `0x0`. Ruled out stack overflow (tested
   a doubled `CONFIG_ARDUINO_LOOP_STACK_SIZE`, no change) and a generic RISC-V/Curve25519
   codegen bug (a stock ESP32-C3 fork running plain SX1262 does first-time PKI keygen fine —
   https://github.com/benb0jangles/seeed-xiao-esp32c3-meshtastic).

3. **Root cause, confirmed and fixed**: this board's LR1121 failed hardware `init()` on
   *every* boot (see the FIXED section below — `RADIOLIB_LR11X0_CMD_GET_VERSION` reads back
   `0xf3`, ExpressLRS's transceiver-firmware type, which RadioLib rejected). `RadioLibInterface`'s constructor
   (`src/mesh/RadioLibInterface.cpp`) unconditionally sets the static `instance` (or
   `instance2`) pointer to `this` — before `init()` even runs. When `init()` fails,
   `RadioInterface.cpp`'s `initHardware()` does `rIf = nullptr;` on the owning `unique_ptr`,
   destroying the `LR1121Interface` object — but nothing cleared `RadioLibInterface::instance`.
   That left it dangling on every single boot. The freed heap slot was later reused by an
   unrelated allocation (something MQTT-related), and the entropy-mixing code's read through
   the dangling pointer landed on that new object's vtable instead — explaining the "MQTT
   vtable" address from fault #2 exactly. **Fix**: added `~RadioLibInterface()` in
   `src/mesh/RadioLibInterface.h` that nulls `instance`/`instance2` when the owning object is
   destroyed. Confirmed on hardware: region now survives a reboot (`Wanted region 3, using
   EU_868` persists across power cycles), no more crash.

---

## FIXED: unified_esp32c3_lr1121_rx — LR1121 failed hardware init on every boot

`LR11x0 init result -2` (`RADIOLIB_ERR_CHIP_NOT_FOUND`) on **every** boot. Root cause:
**ExpressLRS flashes Semtech's LR1121 *transceiver* firmware image into the radio's internal
flash, and RadioLib only recognises the factory image.**

- ExpressLRS ships `lr1121_transceiver_F30104.h` — Semtech's own *"Firmware transceiver
  version 0xF30104 for LR1121"* — and `LR1121Driver::CheckVersion()`
  (`ExpressLRS/src/lib/LR1121Driver/LR1121.cpp:71-105`) flashes it unless the chip already
  reports type `0xF3` **and** version `0x0104`. `#define LR1121_FIRMWARE_TYPE 0xF3` is right
  at the top of that file. It never restores the factory image.
- RadioLib's `findChip()` (`modules/LR11x0/LR11x0.cpp`) accepts only device byte `0x03`
  (`RADIOLIB_LR11X0_DEVICE_LR1121`, the factory image) or `0xDF` (bootloader mode). `0xf3`
  matches neither, so `begin()` returns `-2` and `initHardware()` destroys the radio object.

So the chip was always healthy and the SPI bus always worked — the stable `0xf3` across all
10 retries was a *correct* reply that RadioLib refused. Wiring was never the problem, which
is why every wiring-level check came back clean. **This affects any ex-ExpressLRS LR1121
board, not just this one.**

**Fix**: `extra_scripts/lr11x0_accept_trx_firmware.py` patches the downloaded RadioLib copy
at build time so `findChip()` also accepts `0xF3` when the expected chip is an LR1121. The
transceiver image's command set is identical (LR1121 has no WiFi/GNSS anyway, and RadioLib
already skips those reads for this chip type), so nothing else needs to change. RadioLib is
consumed as an upstream release zip via `lib_deps`, hence a build-time patch rather than a
source diff; the script is idempotent and fails loudly if RadioLib ever changes that line.
No upstream fix as of RadioLib 7.6.0. Confirmed working on hardware.

**Also fixed alongside it**: `rfswitch.h` had RX on DIO5 and TX on DIO6, copied from
`bayckrc_dual_band`'s LR1120. ExpressLRS's own default for this product
(`LR1121Driver::SetDioAsRfSwitch()`, `LR1121.cpp:299-308` — the BAYCKRC Nano RX has no
`radio_rfsw_ctrl` override in `Targets/targets.json`) enables DIO5-8 with **RX on DIO7, TX
and TX_HP on DIO8, TX_HF on DIO6, and HF-RX on DIO5**. No product in `targets.json` uses the
old mapping. Note RadioLib has no separate HF-receive mode — it uses `MODE_RX` for both
bands — so 2.4GHz RX still routes through the sub-GHz DIO7 path and would need a different
table.

**Dead ends this cost, recorded so nobody repeats them:** MISO pull-up (ExpressLRS calls
`gpio_pullup_en()` on MISO, we didn't — made no difference), reset-pulse width (RadioLib 10ms
vs ExpressLRS 1ms — irrelevant), and a planned logic-analyzer session on the SPI lines that
would have shown a perfectly valid transaction. An earlier "it hangs the board" observation
during that hunt was also a false alarm: **this board does not auto-restart after an esptool
flash without a manual reset**, which looks identical to a hang.

---


## CRSF Lua handset identification — WORKING

`CrsfHandsetModule` (`src/modules/CrsfHandsetModule.{h,cpp}`, `CRSF_UART_PIN 13` in
`variant.h`) answers the stock ExpressLRS Lua script on an EdgeTX handset so it shows
"Meshtastic <version>" when this TX module sits in the JR bay. Hardware/wiring is fine — the
same module works with ExpressLRS firmware. **Verified on hardware:** Lua shows
"Meshtastic 2.7.26…" with a "Version" line.

**Diagnostics:** System screen → press OK → CRSF Status. Snapshot on open (reopen to
refresh). 4 lines: `RX E F B` (bytes, uart errors, good frames, bad CRC) / `Ping Sent Par`
(pings, Device Info replies, parameter reads) / `<baud> INV|NRM <rst now>`<`<rst prev>` /
last 8 raw bytes hex. Reset reason is persisted in NVS (`crsfdiag/rst`) so a crash in the bay
(no USB log possible) is readable on the next boot. Codes: PWR, EXT, SW, PANIC, IWDT, TWDT,
BOD (brownout), DSLP.

**Root causes found and fixed (verified on hardware unless noted):**
1. **Our TX held the wire.** `begin()` with rx==tx pin attaches UART1 TX push-pull to GPIO13
   permanently; `uart_set_mode(UART_MODE_RS485_HALF_DUPLEX)` only toggles RTS on ESP32, it
   never tri-states TX. Handset could not drive the line → 0 bytes. Fix: manual direction
   switching via GPIO matrix, mirroring ELRS `CRSFHandset::duplex_set_RX/TX`
   (`ExpressLRS/src/lib/Handset/CRSFHandset.cpp:364-410`) but with UART1 signals
   (`U1RXD_IN_IDX`/`U1TXD_OUT_IDX`). While transmitting, RX is fed
   `GPIO_MATRIX_CONST_ONE_INPUT` (idle) instead of ELRS's const-zero, to avoid a break.
2. **Polarity:** JR-bay half-duplex CRSF is **inverted** (matrix inversion + pulldown in RX).
   Locks at **400000 baud, INV** with EdgeTX set to 400k.
3. **Polarity/baud watchdog:** `runOnce()` every 1s — if no new good frames, flip polarity,
   and on each flip back to INV step through ELRS's `TxToHandsetBauds` list. Needed in
   practice: the constructor-only 400k/INV setup received bytes but never parsed a frame;
   the watchdog's later re-apply locked immediately. **Unexplained** — suspect something
   re-configures GPIO13/UART1 after our constructor; not investigated.
4. **Boot hang at splash in the bay** (also the cause of the earlier reverted GPIO attempt):
   per-frame `LOG_DEBUG` at ~250 frames/s saturated the 115200 console and blocked boot.
   Fix: no per-frame logging; counters only.
5. **Reply latency:** parsing moved from 5 ms OSThread polling to
   `Serial1.onReceive(cb, true)` + `setRxTimeout(2)` so replies land inside the CRSF period.
6. **PANIC reboots** (reset code PANIC captured via NVS) when pings arrived: the onReceive
   callback runs on arduino-esp32's `uart_event_task` with a **2048-byte stack**
   (`HardwareSerial.cpp:111`), and `LOG_INFO` from the reply path overflows it. Fix: no
   logging in the callback (runOnce logs ping counts), plus
   `-DARDUINO_SERIAL_EVENT_TASK_STACK_SIZE=4096` in the emax `platformio.ini`.
   No PANIC seen since, but no long soak yet.
7. **Lua shows no text although ping/reply works** (Ping 2 / Sent 2 — the script stops
   pinging once its device list is non-empty, so it did accept Device Info). Cause in
   `ExpressLRS/src/lua/elrs.lua:27,385`: `fields_count` starts at 0 and `changeDeviceId()`
   returns early when `fldcnt` equals it, so `deviceName` is never set. Fix: advertise
   `fieldCnt=1` and answer PARAMETER_READ (0x2C) for field 1 with PARAMETER_ENTRY (0x2B), an
   INFO (type 12) field "Version" = `APP_VERSION`. Frames now built by shared `sendFrame()`.
   Verified: Ping 1 / Sent 1 / Par 1, name + Version displayed.

**Side finding — dark screen + LED off + joystick dead:** on this board a 3 s long-press of OK
(5-way ADC ≈0) calls `shutdown()` → deep sleep with no ADC wake
(`src/input/ExpressLRSFiveWay.cpp:186`). One early bay failure looked exactly like that, but a
diagnostic build suppressing it never saw it fire; reverted. Suspect it if the symptom returns.

**Also unexplained:** the 7-line CRSF Status banner (more than `MAX_LINES = 5` in
`NotificationRenderer.h`) coincided with reboots; trimmed to 4 lines. Code review found no
overflow, so this may just have been the ping-path PANIC above.

**Bench gotchas:**
- Opening the CP2102 serial port on macOS (pyserial, even with dtr/rts=False) **resets the
  ESP32** via EN. Open the logger first, then do the on-device steps.
- USB and JR bay can't be attached at once, so bay behaviour is only observable via the
  CRSF Status screen / persisted reset reason.

**Lua UI (verified on hardware):** title `ELRS->Meshtastic`; root = Message (canned SELECT:
"Hi from ExpressLRS!" + `cannedMessageModuleConfig.messages`), [Send] (channel-0 broadcast, 5 s
cooldown), > Messages (last 5 received, each a folder: header = "ABCD: preview...", rows = word-wrapped
text), > Nodes (10 newest, each a folder with Name/Id/SNR/Heard/Hops/HW/Bat rows), [Refresh] (also
inside both folders), Version. Field-id layout is in named constants at the top of
`CrsfHandsetModule.cpp`.

ELRS Lua (`ExpressLRS/src/lua/elrs.lua`) constraints this is built around — re-derive nothing:
- Field **names are cached** per field object; only a Device Info with a different `fldcnt` makes
  it reallocate (`changeDeviceId`). Refresh buttons and pings flip a hidden trailing field and push
  an unsolicited Device Info; side effect: the Lua lands back at root.
- Folders don't reload on open. COMMAND popups show a **title only** (`popupConfirmation(t, e)`),
  and a status-0 reply just closes the popup — so details are rows in folders, not popups.
- STRING editing is not implemented in the Lua → no free-text input; canned SELECT instead.
- The UART callback (uart_event_task) only reads a double-buffered snapshot built on the main
  thread; no NodeDB, heap or logging there.

**Open items:**
- Long soak in the bay with Lua open to confirm the PANIC is gone for good.
- Find what undoes the constructor's GPIO13/UART1 setup (root cause 3); the watchdog masks it.
- `trunk fmt` not run (trunk not installed on this machine) — run before upstreaming.

---

## TL;DR

**EMAX 900 OLED TX port works.** It builds, boots, drives the OLED and menus, receives and
transmits at every modem preset.

Two separate faults were found and fixed:

1. **The SX1276 was overdriving the external PA by 15 dB.** Fixed by matching the
   ExpressLRS drive level. Now yields a clean 260 mW at the antenna.
2. **SX127x transmissions were invisible to LR11xx receivers.** This is upstream issue
   [meshtastic/firmware#4775](https://github.com/meshtastic/firmware/issues/4775) —
   Meshtastic's non-standard 0x2b sync word — and is **not** a defect in this port.
   Worked around with a build-time sync word override. See "The sync word trap" below,
   because it will bite anyone testing an SX127x board against an LR11xx peer.

---

## Boards

### 1. Emax 900 OLED TX (`emax_900_tx_oled`)

**Hardware:** ESP32 + SX1276 (900 MHz) + I2C SSD1306 OLED + 5-way ADC joystick +
NeoPixel (GPIO27) + fan (GPIO32) + external PA controlled by DAC
**HW model ID:** 144 (`meshtastic_HardwareModel_EMAX_900_TX_OLED`)
**Pin source:** ExpressLRS Targets `TX/EMAX 900 OLED.json`
**Reference oscillator:** plain crystal, no TCXO (proven — setting `RegTcxo` bit 4 kills
the chip's clock and `startReceive()` returns `err=-16`)

| Function | GPIO |
|---|---|
| SX1276 SCK / MISO / MOSI / CS | 18 / 19 / 23 / 5 |
| SX1276 DIO0 / RESET | 4 / 14 |
| SX1276 DIO1 | not connected |
| I2C SDA / SCL | 22 / 21 |
| NeoPixel | 27 |
| Fan enable | 32 |
| PA control, APC2 DAC | 26 |
| RX enable switch | 12 |
| Joystick ADC | 33 |

Joystick ADC values (UP/DOWN/LEFT/RIGHT/OK/IDLE): 2010, 1230, 635, 2730, 0, 4095

**Status:** Working. TX and RX confirmed at ShortFast and MediumFast against an LR1121
peer (with matching sync word).

**Pending:** NeoPixel status LED `variant.cpp` not written for EMAX; currently relies on
default Meshtastic NeoPixel handling.

### 2. BAYCKRC 900/2400 Dual Band Nano TX (`bayckrc_dual_band`)

**Hardware:** ESP32 + LR1120 (dual-band sub-GHz + 2.4 GHz) + NeoPixel (GPIO12) +
fan (GPIO4), no display
**HW model ID:** 145 (`meshtastic_HardwareModel_BAYCKRC_DUAL_BAND`)
**Pin source:** ExpressLRS Targets `TX/BAYCKRC Dual Band.json` + Gemini overlay

| Function | GPIO |
|---|---|
| LR1120 SCK / MISO / MOSI / CS | 25 / 33 / 32 / 27 |
| LR1120 RESET / BUSY / IRQ | 26 / 36 / 37 |
| NeoPixel | 12 |
| Fan enable | 4 |
| Backpack serial RX/TX | 18 / 5 (occupied — do not reassign) |
| Backpack enable / boot | 14 / 23 (occupied — do not reassign) |

Second LR1120 (Gemini variant): CS 15, RESET 21, BUSY 39, IRQ 34. Now used — see
"Dual-radio simulcast" below.

Power notes: `power_control: 0` means direct dBm, no DAC. Sub-GHz range -16..+5 dBm.
`LR11X0_DIO3_TCXO_VOLTAGE 1.8`, `LR11X0_DIO_AS_RF_SWITCH`, `radio_dcdc: true`.

**Status:** Working. Both radios initialise, TX and RX confirmed on hardware for both the
primary band and the second radio's fixed 433.125MHz.

### Dual-radio simulcast (Gemini variant, second LR1120)

The Gemini board actually populates both onboard LR1120s. The second one is locked at
**compile time** to 433.125MHz (`BAYCKRC_SECOND_RADIO_FREQ_MHZ` in `variant.h`) — no
runtime/app config, since the phone app's protocol has no concept of a second radio.

- Every outgoing packet is mirrored to both radios (`Router::send()`/`rawSend()` clone
  the packet via `packetPool.allocCopy()` and send the clone out `iface2` before the
  original goes out `iface`, since `iface->send()` consumes/frees it).
- Incoming packets from either radio feed the same receive path; `PacketHistory`'s
  existing packet-ID+sender dedup collapses a packet heard on both radios for free — no
  special-case RX code needed.
- The second radio mirrors whatever modem preset/power the primary computes from
  `config.lora` automatically (both read the same global config) — only its frequency is
  overridden. Changing "Long Fast" or TX power in the app applies to both radios with zero
  extra code.
- Startup is fail-closed: if the second radio's `init()` fails, neither radio is added to
  `Router` (matches the existing red-LED failure state in `variant.cpp`).
- `RadioLibInterface::instance` is a single static pointer read by many unrelated
  consumers app-wide (telemetry, `HardwareRNG`, `ButtonThread`, `Screen`, `DeviceTelemetry`,
  etc.) as "the" primary radio. It keeps that exact meaning — only the primary radio sets
  it. A second static slot, `instance2`, was added instead, with a parallel set of ISR
  trampolines (`isrRxLevel0Secondary`/`isrTxLevel0Secondary`). RadioLib's own
  `setIrqAction()` takes a plain no-arg function pointer (no `attachInterruptArg`-style
  context available at that layer), which is why this is two static slots rather than a
  generic per-object dispatch — sufficient since only ever two radios are in play here.
- Confirmed on hardware: TX on both bands, RX on both bands (received-on-433,
  retransmitted-on-868 confirmed with two bench units).

### 3. Unified ESP32-C3 LR1121 RX (`unified_esp32c3_lr1121_rx`)

**Hardware:** ESP32-C3 (single-core RISC-V) + single LR1121 (dual-band sub-GHz + 2.4GHz) +
NeoPixel + user button, no display. An ExpressLRS unified RX target repurposed as a
Meshtastic node — single radio, no dual-radio simulcast (unlike bayckrc_dual_band).
**HW model ID:** 146 (custom, this branch's own numbering — see BAYCKRC/EMAX notes on why
these aren't registered in `mesh.pb.h`/`architecture.h` on this branch)

| Function | GPIO |
|---|---|
| LR1121 SCK / MISO / MOSI / NSS | 6 / 5 / 4 / 7 |
| LR1121 RESET / BUSY / IRQ | 2 / 3 / 1 |
| NeoPixel | 8 |
| User button | 9 |
| FC-facing UART (not wired into Meshtastic) | RX 20 / TX 21 |

**Status:** Working. BLE/app/config/telemetry all work, the PKI-keygen-on-region-set crash
is fixed, and the radio now initialises — it needed the RadioLib transceiver-firmware patch
plus a corrected RF switch table (both in the FIXED section near the top). Confirmed on
hardware.

Uses the ElrsDual dual-OTA partition layout
(`variants/esp32c3/unified_esp32c3_lr1121_rx/partitions-dual.csv`) so it can share the board
with stock ExpressLRS — see `prebuilt/README.md`. The app image is ~1.86MB against a 1.875MB
slot, so **~17KB of headroom**: any module added here will likely need a matching
`MESHTASTIC_EXCLUDE_*`. Also note Meshtastic's own OTA updater writes to the *inactive* OTA
slot, which in a dual-boot setup is where ExpressLRS lives — an in-app firmware update would
destroy it.

**Still-unconfirmed assumption carried over from BAYCKRC's LR1120 config:**
- No TCXO (`LR11X0_DIO3_TCXO_VOLTAGE` omitted) — the source ELRS layout JSON had no `tcxo`
  field, so assumed plain crystal. Radio works without it, so this is settled in practice.

---

## The sync word trap (read this before debugging any SX127x board)

**Symptom:** an SX127x node transmits at correct power on the correct frequency, and an
LR11xx node hears nothing at all. The reverse direction works fine. The receiver's
`rxBad` counter stays at **0** — it is not failing CRC, it never detects a preamble.

**Cause:** Meshtastic uses sync word `0x2b`, which is not one of the two values Semtech
defines (0x12 private, 0x34 LoRaWAN). LR11xx radios fail to detect it from SX127x
transmitters. Tracked as meshtastic/firmware#4775, open since Sept 2024, still present in
2.7.x, deferred to 3.0.

**This cost a full debugging session.** It presents as an RF fault and survives every
RF-level check, because nothing is actually wrong with the transmitter. It also appeared
SF-dependent here (ShortFast worked, MediumFast and above did not), which made it look
even more like a modem or analogue problem. Do not trust that pattern.

**Workaround:** `-D MESHTASTIC_LORA_SYNCWORD=0x12` at build time
(`src/mesh/RadioLibInterface.h`). Defaults to 0x2b, so stock builds are unchanged.

```
PLATFORMIO_BUILD_FLAGS="-DMESHTASTIC_LORA_SYNCWORD=0x12" pio run -e <env> -t upload
```

**Every node in the mesh must be built with the same value.** A node built with 0x12 is
invisible to all stock Meshtastic devices. This is only appropriate for a closed mesh.

**Diagnostic shortcut:** if an SX127x board can't reach a peer, check the peer's radio
chip *first*. If it's LR11xx, test against an SX126x or SX127x node before investigating
anything else.

---

## How the EMAX PA works (verified against ExpressLRS source)

This is the part most likely to be re-derived wrongly.

1. `"power_control": 3` maps to `POWER_OUTPUT_DACWRITE`
   (`include/target/Unified_ESP32_TX.h:44`).
2. The JSON has **no `power_values2`**, so `POWERMGNT::setPower()` never calls
   `Radio.SetOutputPower()` (`lib/POWERMGNT/POWERMGNT.cpp:258-270`). The SX1276's own
   output level stays at whatever init set.
3. Init sets it once: `SetOutputPower(SX127X_MAX_OUTPUT_POWER)`, that constant being
   `0b01110000` (`lib/SX127xDriver/SX127x.cpp:83-92`). Masked with `SX127X_PA_POWER_MASK`
   (0x7F) and OR'd with `PA_SELECT_BOOST` gives **RegPaConfig = 0xF0**.
4. On PA_BOOST, `Pout = 17 - (15 - OutputPower)`. The OutputPower nibble is 0, so the chip
   transmits at **+2 dBm**. The MaxPower bits (0x70) only matter for RFO.
5. All remaining gain comes from the external PA, set by `dacWrite` on APC2 (GPIO26).

So: **SX1276 at +2 dBm, external PA does everything else.** The port originally ran the
chip at +17 dBm — a 15 dB overdrive of the PA input.

Also confirmed from ELRS:
- **PA_BOOST, not RFO** — `radio_rfo_hf` is absent from `EMAX 900 OLED.json`.
  (Radiomaster Bandit has it `true`, which is why that variant uses `USE_RF95_RFO`.)
- **Higher DAC = more power** on this board (Bandit is inverted — do not copy its table).
- **RXEN only, no TXEN**; RXEN low during TX (`lib/RFAMP/RFAMP_hal.cpp:31`).
- ELRS never writes `RegTcxo` — the module runs its crystal in default mode.

### PA calibration — measured, not from the ELRS labels

The ExpressLRS `power_values` labels turned out to be **~4.4 dB optimistic** on this
board. ELRS calls DAC 50 its "50 mW" step; it actually produces 147 mW. The table in
`getDACandDB()` is therefore built from measurements, taken with a power meter at 869 MHz
with the PA on a powerbank and the chip at +2 dBm:

| DAC | Measured | Actual dBm |
|---|---|---|
| 50 | 147 mW | 21.7 |
| 60 | 260 mW | 24.2 |
| 70 | 437 mW | 26.4 |
| 75 | 542 mW | 27.3 |

About **0.25 dB per DAC unit**, compressing to ~0.19 dB/unit above DAC 70.

Rows outside DAC 50-75 are extrapolated and want re-measuring:

- **Below DAC 50** unverified. DAC 30 is the ELRS minimum and still gives roughly 17 dBm,
  so this PA cannot go quiet. Requests under 17 dBm clamp to that floor, which means the
  menu's 10 and 14 dBm entries are not reachable on this board.
- **Above DAC 75** the PA is compressing, so the 30 dBm row is a guess.

An earlier revision of the variant README listed a completely different DAC table
(DAC 25 = 20 dBm etc). Those readings were taken while the SX1276 was overdriving the PA
at +17 dBm and no longer apply.

---

## Bugs fixed

### EMAX: SX1276 overdriving the external PA
`RF95_MAX_POWER` was 17, giving `RegPaConfig=0xFF` (+17 dBm) into a PA input that ELRS
drives at +2 dBm. Now `RF95_MAX_POWER 2` -> `RegPaConfig=0xF0`.

### EMAX: `tx_power == 0` fallback pinned the DAC
`config.lora.tx_power ? config.lora.tx_power : power` used `power` as the fallback, but
`power` is the SX1276 driver level (now always 2), not the requested EIRP. That would have
locked the PA to the default table row. Now falls back to the region power limit.

### EMAX: DAC value overflowed a signed type
`int8_t powerDAC` cannot hold the top DAC value of 225. Changed to `uint8_t`.

### EMAX: RFO vs PA_BOOST mismatch
`USE_RF95_RFO` routed output to the RFO pad; the external PA input is on PA_BOOST.
Removed — PA_BOOST is the default. Confirmed against the ELRS target JSON.

### EMAX: speculative LowFrequencyModeOn hack removed
A read-modify-write clearing `RegOpMode` bit 3 after `begin()` was added on the theory
that 869 MHz needs HF mode. Never validated, deviates from stock upstream (which works at
869 MHz on every other SX1276 board), and removing it changed nothing.

### LR11x0Interface: getVersionInfo aborted init (BAYCKRC)
After a successful `lora.begin()`, `getVersionInfo()` fails on LR1120 because WiFi/GNSS
aren't initialised. The result was stored in `res`, so `setCRC`, `setDCDC` and
`startReceive` were all skipped and init returned false. Now uses a separate `verRes`.

### BAYCKRC: USE_LR1121 -> USE_LR1120
Board has an LR1120 (device ID 0x02). `findChip()` retried 10x then returned
`-2 CHIP_NOT_FOUND`.

### BAYCKRC: NeoPixel RMT conflict
`ENABLE_AMBIENTLIGHTING` claimed the RMT channel on GPIO12 before `lateInitVariant()`
could, so the second `pixel.begin()` failed silently. Omitted deliberately;
`variant.cpp` is the sole NeoPixel controller.

### BAYCKRC: stale/corrupt NVS caused an intermittent BLE crash loop, looked like a receive bug
During dual-radio bench testing, RX looked flaky/inconsistent ("some messages pass"). Serial
log showed `__stack_chk_fail` inside `NimbleBluetooth::setup() -> populate_db_from_nvs`,
hard-rebooting via `panic_abort` — device had **344 recorded reboots**. This is NimBLE GATT
DB init on the main `loopTask` stack, nothing to do with the radio/Router changes. The
intermittent reboot (not every boot) was cutting test sessions short mid-flight, which looked
exactly like unreliable RX. Erasing flash (`pio run -t erase`) and reflashing clean resolved
it — treat as stale/corrupt NVS from repeated flash cycles, not a code bug. **If this
resurfaces**, it's worth actually root-causing (not just erase-and-move-on) — the crash is
in vendored NimBLE code, not ours, so the next step would be bisecting whether it's config
size, a corrupted particular NVS namespace, or a genuine NimBLE bug independent of NVS state.

### LR11x0Interface: randomBytes() hard-faulted inside RadioLib's SPI code
Found bringing up `unified_esp32c3_lr1121_rx`, but affects every LR11x0-family board
(`bayckrc_dual_band` included — it just hadn't hit this path yet). `HardwareRNG`'s
`mixWithLoRaEntropy()` calls `RadioLibInterface::randomBytes()`, which for LR11x0 goes
through RadioLib's `LR11x0::randomByte()` → `Module::SPIcommand` → `Module::SPItransferStream`
→ **Load access fault**, triggered by `CryptoEngine::generateKeyPair()`'s entropy mixing on
first-time PKI key generation. `randomBytes()` is now `virtual`; `LR11x0Interface` overrides
it to always return `false`, skipping modem-sourced entropy entirely for this radio family.
ESP32's `esp_fill_random()` already covers the primary entropy source, so this is a
defensive removal of a broken supplementary step, not a security regression. See the OPEN
section at the top of this doc for the second, still-unresolved crash this uncovered.

### main.cpp: `inputBroker` only reached transitively via GPS.h, broke on headless+GPS-excluded boards
`inputBroker` (declared in `input/InputBroker.h`) was never included directly by
`main.cpp` — it arrived by accident via `GPS.h`'s `#include "input/RotaryEncoderInterruptImpl1.h"`
/ `UpDownInterruptImpl1.h`, which only exist in GPS.h's non-excluded branch. Any board
excluding GPS on a headless (`HAS_SCREEN 0`) variant loses that side-channel and fails to
build (`'inputBroker' was not declared in this scope`). `bayckrc_dual_band` worked around
this by excluding `InputBroker` outright (it has no button), which is why it didn't surface
there. `unified_esp32c3_lr1121_rx` has a real button and needs `InputBroker` enabled, so the
actual fix landed instead: `main.cpp` now includes `input/InputBroker.h` directly,
unconditionally.

---

## Other changes

### TX power menu (`src/graphics/draw/MenuHandler.cpp`)
Added low-power steps 10 / 14 / 17 dBm, matching the ELRS DAC rows 30 / 40 / 50
(10 / 25 / 50 mW). Previously the menu only offered 20-28 dBm, so the bottom half of the
DAC table was unreachable from the device.

Replaced the switch plus 9-branch if-chain with a `txPowerDbm[]` lookup, so adding steps
is now a one-line edit. Also fixed the pre-select: when `tx_power` is 0 or off-list the
menu now highlights "Back" rather than falsely displaying 27 dBm.

This file is shared by all variants — the new steps appear on every board's menu. Harmless
(10-17 dBm is valid everywhere) but worth knowing before upstreaming.

Note the legacy mW labels are inconsistent with their own dBm values ("70 mW (20 dBm)" —
20 dBm is 100 mW). They appear to be empirical measurements against an older power table.
The three new labels are honest conversions.

### On-device menu vs app power setting
Both write the same field, `config.lora.tx_power`. The menu sets it directly then calls
`service->reloadConfig(SEGMENT_CONFIG)`; the app goes through `AdminModule.cpp:1414`.
Last writer wins, both take effect immediately via `reconfigure()`, which rewrites the
APC2 DAC at `RF95Interface.cpp`. Neither is authoritative over the other.

`getDACandDB()` interpolates between table rows and returns the default (DAC 50, ~50 mW)
for anything outside 10-33 dBm — so a sub-10 dBm request fails *upward*, not downward.
Region limits cap the top end well below 33.

---

## Files created / modified

### New
| Path | Description |
|---|---|
| `variants/esp32/emax_900_tx_oled/variant.h` | Pin + radio config |
| `variants/esp32/emax_900_tx_oled/platformio.ini` | PlatformIO env |
| `variants/esp32/emax_900_tx_oled/partitions-dual.csv` | Partition layout |
| `variants/esp32/bayckrc_dual_band/variant.h` | Pin + radio config |
| `variants/esp32/bayckrc_dual_band/platformio.ini` | PlatformIO env |
| `variants/esp32/bayckrc_dual_band/rfswitch.h` | LR1120 RF switch table |
| `src/platform/extra_variants/bayckrc_dual_band/variant.cpp` | NeoPixel LED status |

### Modified
| Path | Change |
|---|---|
| `src/mesh/generated/meshtastic/mesh.pb.h` | HW model enums 144, 145 |
| `src/platform/esp32/architecture.h` | `HW_VENDOR` mappings |
| `src/mesh/LR11x0Interface.cpp` | `getVersionInfo` result in separate `verRes` |
| `src/mesh/RF95Interface.cpp` | EMAX DAC table, power fallback |
| `src/mesh/RadioLibInterface.h` | `MESHTASTIC_LORA_SYNCWORD` build override; `instance2` + secondary ISR trampolines for dual-radio |
| `src/mesh/RadioLibInterface.cpp` | Secondary ISR trampolines, `isSecondaryRadio` constructor flag |
| `src/mesh/LR11x0Interface.h/.cpp` | `getVersionInfo` result in separate `verRes`; `isSecondary`/`fixedFreqOverride` params, `getFreq()` override |
| `src/mesh/LR1120Interface.h/.cpp` | Threaded `isSecondary`/`fixedFreqOverride` params to base |
| `src/mesh/Router.h/.cpp` | Optional `iface2` + `addSecondInterface()`; mirrors outgoing packets to both radios |
| `src/main.cpp` | Constructs second LR1120Interface under `BAYCKRC_DUAL_BAND`, fail-closed startup, polls `instance2` in `loop()` |
| `src/graphics/draw/MenuHandler.cpp` | TX power menu low steps |

The EMAX build trims unused modules via `MESHTASTIC_EXCLUDE_*` in its `platformio.ini` to
keep the OTA image small. Those exclusions work correctly and were never implicated in
any of the above.

---

## Architecture notes

- Variants live in `variants/esp32/<board>/` as `variant.h` + `platformio.ini`. The
  `esp32_base` `build_src_filter` auto-compiles
  `src/platform/extra_variants/<board>/variant.cpp` if present.
- `lateInitVariant()` runs after all subsystems init — safe place for NeoPixel and radio
  checks. `earlyInitVariant()` runs before; use only for strapping pins.
- RadioLib SPI init: `SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS)` in `main.cpp`,
  using the `LORA_*` defines from variant.h.
- `RadioInterface::limitPower()` supports a `TX_GAIN_LORA` offset for fixed-gain external
  PAs. This board uses the Bandit-style `getDACandDB()` path instead, because its PA gain
  is variable via APC2.
- `Packet TX: NNNms` in the log is `getPacketTime()` — **computed** from the firmware's SF
  and BW variables, not measured. It cannot detect a truncated transmission. To measure
  actual transmit duration, timestamp `configHardwareForSend()` against the following
  `startReceive()`.
