# Dual-Band Meshtastic Bridge — ESP32-WROOM-32U + Ra-01 / Ra-01H

Handoff notes. Status: design only, nothing built or tested.

**Goal:** receive Meshtastic traffic on 433 MHz and 868 MHz, retransmit each side's
packets onto the other band. Bidirectional cross-band repeater.

**Hardware on hand:** 1x ESP32-WROOM-32U devkit, 1x Ra-01 (SX1278, 410–525 MHz),
1x Ra-01H (SX1276, 803–930 MHz).

**Prior art:** we already did 2-radio RadioLib instances for the ELRS backpack
(2x LR1121). Topology carries over; chip config and the application layer do not.
See "Deltas from the ELRS dual-LR1121 work" below.

---

## 1. Key insight — this is not a Meshtastic fork

Meshtastic's 16-byte PHY header is **plaintext**. Only the payload protobuf is
encrypted. So the bridge can read packet id, source, hop limit and destination
without any channel keys, and forward the encrypted blob untouched.

That means: bare RadioLib sketch. No NodeDB, no MeshService, no BLE, no channel
config, no fork of `RadioLibInterface` (which is a singleton and would have to be
refactored — avoided entirely).

Consequence: the bridge is channel-agnostic and bridges all channels at once.

---

## 2. Wiring

Shared VSPI bus, per-radio NSS + RST + IRQ. Same pattern as the ELRS dual-radio boards.

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

### Net list

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

DIO2–DIO5 unconnected. DIO1 only needed for the RxTimeout IRQ — wire it anyway.

### Ra-01 / Ra-01H module pinout (16-pin, both identical)

Left column: `ANT, GND, 3V3, RST, DIO0, DIO1, DIO2, DIO3`
Right column: `GND, DIO4, DIO5, SCK, MISO, MOSI, NSS, GND`

### Pin-choice rationale

- GPIO 6–11: SPI flash. Never use.
- GPIO 0, 2, 12, 15: strapping pins. Avoided.
- GPIO 34–39: input-only. Usable for DIO0/DIO1 (radio drives them) if 25/26/32/33
  are needed elsewhere; current assignment leaves ADC1 free.
- Add a 10k pull-up on each NSS so neither radio listens to the bus while the
  ESP32 boots and the pins float.
- Clock the shared bus at **8 MHz** — SX127x tops out at 10 MHz (LR1121 did 16).

### Power

Ra-01H at +20 dBm draws ~120 mA peak.

- 100 µF electrolytic + 10 µF ceramic on the 3V3 rail near each module,
  plus 100 nF right at each module's VCC pin.
- Devkit AMS1117 off USB 5 V handles *one* radio transmitting. Two at once
  browns out the rail and corrupts in-flight SPI transactions.
- Preferred: external 3.3 V buck (AP63203 / MP1584) from 5 V, 1 A, feeding both
  modules. Leave the devkit regulator for the ESP32 alone. Tie grounds.
- Firmware must serialize TX anyway (see §5).

---

## 3. Architecture

```
   433 mesh                  BRIDGE                   868 mesh
      │                                                   │
      │  RX ──► parse hdr ──► dedup(from,id) ──┐          │
      │                                         │          │
   radioA                                   queueB ──► TX radioB
                                                │
   radioA TX ◄── queueA ◄──┬── dedup(from,id) ◄─┴── RX radioB
                           │
                    hop_limit-- , drop if 0
```

Symmetric. **One seen-set shared by both directions** — that is what prevents loops.

Note the design difference from the ELRS work: there, dual radio meant one link
over two paths (diversity / band-hop). Here it is two *independent* meshes with
packets forwarded across, deduplicated by packet id.

---

## 4. Code sketch

### Header

```cpp
// Meshtastic PHY header, plaintext, little-endian, 16 bytes
struct __attribute__((packed)) MeshHeader {
    uint32_t to;
    uint32_t from;
    uint32_t id;
    uint8_t  flags;       // [2:0] hop_limit, [3] want_ack, [4] via_mqtt, [7:5] hop_start
    uint8_t  channel;     // channel-name hash
    uint8_t  next_hop;    // low byte of next-hop nodenum, 0 = flood
    uint8_t  relay_node;  // low byte of relaying nodenum
};
static_assert(sizeof(MeshHeader) == 16);

#define HOP_LIMIT(f)       ((f) & 0x07)
#define SET_HOP_LIMIT(f,h) ((f) = ((f) & ~0x07) | ((h) & 0x07))
```

Everything after byte 16 is the encrypted protobuf. Never touch it.

### Radio init

```cpp
SPIClass spi(VSPI);
SX1278 radioA = new Module(5, 26, 14, 33, spi);   // 433: NSS DIO0 RST DIO1
SX1276 radioB = new Module(4, 25, 27, 32, spi);   // 868

// LongFast-equivalent on both bands. Presets need not match across bands,
// but matching keeps airtime symmetric and queues shallow.
// Last arg = tcxoVoltage: MUST be 0.0, Ra-01 modules are XTAL not TCXO.
radioA.begin(433.175, 250.0, 11, 5, 0x2B, 20, 8, 0.0);
radioB.begin(869.525, 250.0, 11, 5, 0x2B, 20, 8, 0.0);
radioA.setCRC(true);      radioB.setCRC(true);
radioA.explicitHeader();  radioB.explicitHeader();
// PA_BOOST, not RFO — RFO pin is not bonded on these modules
radioA.setOutputPower(pwr, true);
radioB.setOutputPower(pwr, true);
```

`syncWord 0x2B` is Meshtastic's.

### RX path + dedup

```cpp
const uint8_t MY_RELAY_ID = 0xB1;   // low byte of this bridge's nodenum

struct Seen { uint32_t from, id; uint32_t t; };
Seen seen[128];
uint8_t seenIdx = 0;

bool alreadySeen(uint32_t from, uint32_t id) {
    uint32_t now = millis();
    for (auto &s : seen)
        if (s.from == from && s.id == id && (now - s.t) < 300000UL) return true;
    seen[seenIdx] = {from, id, now};
    seenIdx = (seenIdx + 1) % 128;
    return false;
}

struct Pkt { uint8_t buf[256]; uint8_t len; };
QueueHandle_t qA, qB;   // depth 8 each

void onRx(PhysicalLayer *src, QueueHandle_t dst) {
    Pkt p;
    if (src->readData(p.buf, 0) != RADIOLIB_ERR_NONE) return;
    p.len = src->getPacketLength();
    if (p.len < sizeof(MeshHeader)) return;

    auto *h = (MeshHeader *)p.buf;
    if (alreadySeen(h->from, h->id)) return;

    uint8_t hops = HOP_LIMIT(h->flags);
    if (hops == 0) return;                  // exhausted, do not cross
    SET_HOP_LIMIT(h->flags, hops - 1);
    h->relay_node = MY_RELAY_ID;
    h->next_hop = 0;                        // force flood on the far side

    xQueueSend(dst, &p, 0);
}
```

### TX path — one task, strictly serialized across both radios

```cpp
void txTask(void *) {
    Pkt p;
    for (;;) {
        for (auto [q, radio] : {make_pair(qA, &radioA), make_pair(qB, &radioB)}) {
            if (xQueueReceive(q, &p, 0) != pdTRUE) continue;
            xSemaphoreTake(busMutex, portMAX_DELAY);
            radio->standby();
            while (radio->scanChannel() == RADIOLIB_LORA_DETECTED)
                delay(random(10, 60));       // listen-before-talk
            radio->transmit(p.buf, p.len);
            radio->startReceive();
            xSemaphoreGive(busMutex);
            delay(20);                       // PA settle, never both keyed
        }
        vTaskDelay(1);
    }
}
```

---

## 5. Gotchas

1. **Loops.** Without the shared seen-set: packet crosses A→B, B's mesh
   rebroadcasts, bridge sees it on B, crosses B→A, forever. Dedup on `(from, id)`
   across *both* directions from a single table. Non-negotiable.
2. **Half duplex.** Each radio is deaf while transmitting. Bridge TXing on B for
   800 ms misses everything on B. Unavoidable — keep queues shallow, preset fast.
3. **hop_limit.** Decrement on cross, drop at 0. Otherwise a 3-hop packet gets 3
   fresh hops of life on the far mesh at every crossing.
4. **Duty cycle.** EU 869.4–869.65 is the 10 % / 27 dBm sub-band — fine. Landing
   on 868.0–868.6 puts you at 1 %, and a busy 433 mesh will blow through that in
   minutes. Add a duty-cycle accumulator on radioB if not on 869.5.
5. **Both PAs keyed at once.** The `busMutex` + 20 ms gap is not politeness: two
   +20 dBm PAs off one AMS1117 brown out the rail and corrupt SPI mid-transaction.
6. **Channel keys.** The bridge cannot re-key. Both sides need the **same channel
   PSK and name**, or packets cross and get dropped as undecryptable on the far
   side.
7. **Node list appearance.** 433-side nodes will see 868-side nodes with
   `relay_node = 0xB1`. Positions, telemetry and DMs cross transparently; ACKs
   return on their own because they are just packets with their own ids.

---

## 6. RF safety — read before first power-up

The ELRS dual-band boards have shielding, band filters and designed-in antenna
separation. Two bare Ra-01 modules on a breadboard have none of that. This is the
single most likely way to destroy the hardware.

- **Never power up either module without its antenna connected.** An unterminated
  PA on an SX127x will damage the output stage.
- SX127x RX input tolerates roughly **0 dBm max** before damage, and desense
  starts far below that. A +20 dBm transmitter a few centimetres from the other
  module's antenna can couple enough energy to degrade or destroy the neighbouring
  receiver front end — even across different bands, because PA harmonics and
  broadband noise do not respect the band plan.
- Mitigate: separate the two antennas by **30–50 cm minimum**, mount them
  perpendicular to each other, and put a ground plane or metal shield between the
  two modules. A low-pass filter on the 433 output (and ideally a band-pass on
  each) is the proper fix for sustained full-power operation.
- **Bring up at 2–5 dBm on both radios.** Confirm both still receive at expected
  sensitivity, then raise power in steps.
- Keep antenna feeds short, 50 Ω, and route SMA pigtails away from the SPI bus.

### Regulatory

- 433.050–434.790 MHz is generally capped at **10 mW ERP** across most of ITU
  Region 1. +20 dBm (100 mW) is well over that. Check the local allocation before
  setting TX power.
- 869.525 MHz allows 10 % duty cycle and up to 27 dBm ERP in the EU; other
  868 sub-bands are far more restrictive.

---

## 7. Deltas from the ELRS dual-LR1121 work

| | LR1121 (ELRS) | Ra-01 / Ra-01H (SX1278/76) |
|---|---|---|
| IRQ | single `DIO9` + `IrqFlags` register | `DIO0` = TxDone/RxDone, `DIO1` = RxTimeout |
| Busy pin | yes, must wait on it | **none** — drop all busy-wait logic |
| Clock | TCXO on most ELRS boards | **XTAL** → `tcxoVoltage = 0.0`, else the radio never calibrates |
| RF switch | `SetDioAsRfSwitch`, board-specific | none — fixed internal T/R switch, nothing to drive |
| PA | LP / HP path select | PA_BOOST only, ≤ 20 dBm both modules |
| Band coverage | one chip covers both bands | one chip = one band. Ra-01 410–525, Ra-01H 803–930 |
| SPI clock | up to 16 MHz | 10 MHz max, run at 8 |
| App layer | diversity / band-hop, one logical link | two independent meshes + dedup-based forwarding |

The ELRS radio driver does not port. The *wiring topology* and the two-RadioLib-
instances-on-shared-SPI pattern do.

---

## 8. Open items / next steps

- [ ] Breadboard the wiring, verify both radios respond to `begin()` (check for
      `RADIOLIB_ERR_CHIP_NOT_FOUND`) before any TX.
- [ ] Decide bridge nodenum / `MY_RELAY_ID`.
- [ ] Confirm target modem preset per band, and whether 433 side runs a different
      preset than 868 (affects queue depth and airtime budget).
- [ ] Decide whether the 433 side is legal at the power level we want, or whether
      we cap at 10 mW ERP.
- [ ] Duty-cycle accumulator: needed, or is 869.525 enough?
- [ ] Write the full single-file sketch (fragments above are not compiled).
- [ ] Bench test with two real Meshtastic nodes, one per band, same channel PSK.
