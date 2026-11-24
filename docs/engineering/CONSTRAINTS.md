# TPS25751 Library - Technical Constraints

**Last Updated:** 2026-11-23
**Status:** Active

---

## Overview

This document details the technical constraints and platform-specific considerations that affect the TPS25751 library implementation. Understanding these constraints is critical for writing code that compiles and runs correctly on the target platform.

---

## Table of Contents

1. [Platform Constraints](#platform-constraints)
2. [I2C Protocol Details](#i2c-protocol-details)
3. [Register Access Patterns](#register-access-patterns)
4. [USB Power Delivery Complexity](#usb-power-delivery-complexity)
5. [Memory Considerations](#memory-considerations)
6. [Common Pitfalls](#common-pitfalls)

---

## Platform Constraints

### Teensy 4.x Embedded Platform

The library targets Teensy 4.0 and 4.1 microcontrollers with the following characteristics:

**Processor:**
- ARM Cortex-M7 @ 600 MHz
- 1 MB Flash
- 512 KB RAM (Teensy 4.0) / 1024 KB RAM (Teensy 4.1)

**Implications:**
- Limited memory compared to desktop systems
- No virtual memory or paging
- Real-time constraints
- Limited standard library support

### No RTTI (Run-Time Type Information)

**CRITICAL:** The platform compiles with `-fno-rtti` flag.

**What this means:**
- `dynamic_cast<T>()` will **NOT** compile
- `typeid()` will **NOT** compile
- Virtual function overhead reduced
- Smaller binary size

**❌ NEVER do this:**
```cpp
// COMPILATION ERROR - dynamic_cast requires RTTI
auto* status = dynamic_cast<TPS25751Status*>(reg.get());

// COMPILATION ERROR - typeid requires RTTI
if (typeid(*reg) == typeid(TPS25751Status)) { }
```

**✅ ALWAYS do this:**
```cpp
// Use static_cast, dispatching on the register's address
if (reg->getAddress() == static_cast<uint8_t>(TPS25751Registers::Address::STATUS)) {
    auto* status = static_cast<TPS25751Status*>(reg.get());
    // Now safe to use status
}

// Or use the factory directly (keyed on Address)
auto status = TPS25751Factory::getInstance()
    .createRegister(TPS25751Registers::Address::STATUS);
auto* statusPtr = static_cast<TPS25751Status*>(status.get());
```

**Why this matters:**
- Code must rely on factory pattern for type safety
- Use address-based type identification
- Design must work without runtime type queries

### Template Constraints

**Problem:** `static_assert(false)` in template causes immediate compilation failure

**❌ DON'T:**
```cpp
template<typename T>
void processRegister(T* reg) {
    static_assert(false, "Not implemented");  // FAILS IMMEDIATELY!
}
```

**Why:** The template is instantiated during parsing, and `static_assert(false)` always fails, even if the function is never called.

**✅ DO - Use SFINAE instead:**
```cpp
// Option 1: Type traits
template<typename T>
struct is_register : std::false_type {};

template<>
struct is_register<TPS25751Status> : std::true_type {};

template<typename T>
typename std::enable_if<is_register<T>::value, void>::type
processRegister(T* reg) {
    // Implementation for valid register types
}

// Option 2: Dependent type
template<typename T>
struct dependent_false : std::false_type {};

template<typename T>
void processRegister(T* reg) {
    static_assert(dependent_false<T>::value, "Not implemented");
}
```

### Limited C++ Standard Library

**Available:**
- Basic STL containers (`std::vector`, `std::array`, `std::unique_ptr`)
- Standard algorithms
- Memory management (`std::move`, `std::forward`)
- Type traits

**Limited/Unavailable:**
- Some C++17/20 features
- Full iostream library (use Serial instead)
- Thread support
- Filesystem support

**Best Practices:**
- Test on actual hardware, not just desktop compiler
- Avoid bleeding-edge C++ features
- Use Arduino-compatible patterns where possible

### Arduino Environment Integration

**Serial Output:**
```cpp
// Use Serial, not std::cout
Serial.println("Status");

// Use F() macro to save RAM
Serial.println(F("This string is stored in flash"));
```

**Timing:**
```cpp
// Use Arduino functions
delay(1000);           // milliseconds
delayMicroseconds(5);  // microseconds
millis();              // milliseconds since boot
micros();              // microseconds since boot
```

**I2C:**
```cpp
// Use Wire library
Wire.begin();
Wire.beginTransmission(address);
Wire.write(data);
Wire.endTransmission();
```

---

## I2C Protocol Details

### TPS25751 Custom I2C Protocol

The TPS25751 uses a **non-standard** I2C protocol:

**Standard I2C read:**
```
START | ADDR+W | REG | START | ADDR+R | DATA... | STOP
```

**TPS25751 I2C read:**
```
START | ADDR+W | REG | START | ADDR+R | LENGTH | DATA... | STOP
                                        ^^^^^^^
                                        First byte is length!
```

**Key Difference:** The first byte read is the **byte count**, not data.

### Reading Registers

**Correct implementation:**
```cpp
bool TPS25751::readRegister(uint8_t regAddr, uint8_t* buffer, size_t bufferSize) {
    // Write register address
    Wire.beginTransmission(i2cAddress_);
    Wire.write(regAddr);
    if (Wire.endTransmission(false) != 0) {  // Repeated start
        return false;
    }

    // Read length byte first
    Wire.requestFrom(i2cAddress_, 1);
    if (Wire.available() < 1) {
        return false;
    }
    uint8_t length = Wire.read();

    // Validate length
    if (length > bufferSize) {
        return false;
    }

    // Read actual data
    Wire.requestFrom(i2cAddress_, length);
    for (size_t i = 0; i < length && Wire.available(); i++) {
        buffer[i] = Wire.read();
    }

    return true;
}
```

**Common mistake:**
```cpp
// WRONG - ignores length byte
Wire.requestFrom(addr, 5);  // Reads length + 4 bytes of data
buffer[0] = Wire.read();     // This is the LENGTH, not data!
```

### Writing Registers

Writes mirror the read framing: the length byte is **sent**, not just received.

**TPS25751 I2C write:**
```
START | ADDR+W | REG | LENGTH | DATA... | STOP
                       ^^^^^^^
                       Byte count, sent right after the register address
```

**Correct implementation** (`TPS25751::writeRegister`, `src/TPS25751.cpp`):
```cpp
bool TPS25751::writeRegister(uint8_t regAddr, const uint8_t* data, size_t length) const {
    _wire.beginTransmission(_address);
    _wire.write(regAddr);
    _wire.write(static_cast<uint8_t>(length));   // length byte, same as the read path
    size_t written = _wire.write(data, length);
    if (written != length) return false;          // didn't queue all bytes
    return _wire.endTransmission(true) == 0;
}
```

**Common mistake:**
```cpp
// WRONG - omits the length byte; the controller will misinterpret the payload
Wire.beginTransmission(addr);
Wire.write(regAddr);
Wire.write(data, length);
Wire.endTransmission();
```

### 4CC Command Interface & I2Cc Downstream-Device Proxy (TRM Sec 4.4.2/4.4.3)

Beyond the 15 directly-mapped status/config registers, the TPS25751 exposes a generic
**4CC command-task** interface through two registers:

- **COMMAND** (`0x08`, 4 bytes): write a 4-character ASCII task code (e.g. `"I2Cr"`) to
  start a task. The controller **clears COMMAND to all-zero** on success, or **writes
  back `"!CMD"`** if the code is unrecognized. Reading COMMAND while a task is in
  flight returns the in-progress code.
- **DATA** (`0x09`, 64 bytes): generic scratch register carrying the task's
  input/output payload. It has no fixed layout of its own — interpretation depends
  entirely on which task is running.

The handshake (`TPS25751::executeCommand()`, `src/TPS25751.cpp`): write input payload to
DATA (if any) → write the 4CC to COMMAND → poll COMMAND until it clears or rejects (or
`timeoutMs` elapses) → on success, read the full 64-byte DATA back (`DATA[0]` is the
task's return code).

**I2Cr** and **I2Cw** are 4CC tasks that proxy register access to a device on the
TPS25751's *secondary* I2C bus (I2Cc_SDA/SCL), e.g. a BQ25798 charger
(`TPS25751DownstreamDevice`, `include/TPS25751DownstreamDevice.h`):

| Task | Input DATA layout | Output DATA layout |
|------|--------------------|---------------------|
| **I2Cr** (read) | `DATA[0]`=target addr (7-bit, bit 7 reserved=0), `DATA[1]`=register offset, `DATA[2]`=NumBytes (max **63** — `DATA[0]` holds the return code) | `DATA[0]`=task return code, `DATA[1..NumBytes]`=bytes read — **read data starts at offset 1**, not 0 |
| **I2Cw** (write) | `DATA[0]`=target addr (7-bit, bit 7 reserved=0), `DATA[1]`=**Length**, `DATA[2]`=register offset, `DATA[3..]`=payload (max 11 bytes). **`Length` counts the register-offset byte + payload (= payload_len + 1)**: the controller transmits exactly `Length` bytes starting at the offset byte, so `Length == payload_len` does a pointer-only write that changes nothing. | `DATA[0]`=task return code (success only means the write was *queued*, not necessarily completed on the I2Cc wire — confirm with an I2Cr read-back) |

> **I2Cr vs I2Cw are not symmetric.** I2Cr is `{addr, regOffset, numBytes}` with the count = data bytes. I2Cw is `{addr, Length, regOffset, payload}` — length and offset are swapped **and** `Length` includes the offset byte (`payload_len + 1`). Both confirmed on hardware against a BQ25798.

**5-second minimum spacing:** per the TRM, consecutive **I2Cr** commands — and,
separately, consecutive **I2Cw** commands — must be issued **at least 5 seconds
apart**. This is host pacing guidance distinct from `executeCommand()`'s much
shorter (~200 ms) completion poll. `TPS25751DownstreamDevice` tracks the last
issue time of each task type via `millis()` and emits a `DEBUG_CAT_TASK` warning
if called sooner — it does **not** hard-block, since callers may already be
managing the timing themselves.

> **Direct (non-proxied) transport bypasses all of the above.** Constructing
> `TPS25751DownstreamDevice` (or a driver built on it, e.g. `BQ25798::Device` in
> the sibling `lib/BQ25798` repo) with the `(TwoWire&, addr)`
> constructor instead of `(const TPS25751&, addr)` selects a **direct** transport:
> reads/writes are plain Arduino-`Wire` register transactions to the part on the
> MCU's own bus, with no 4CC relay. Because there is no I2Cr/I2Cw, **none** of the
> proxy constraints apply — no TRM 5 s spacing, no 63-byte read / 11-byte write
> payload cap (the only limit is the `TwoWire` buffer), and the I2Cr-vs-I2Cw
> asymmetry is irrelevant. The trade-off: direct mode requires the part to actually
> be wired to the bus you pass (it is *not* on the TPS25751's I2Cc bus), and the
> caller must `wire.begin()` it. Use it for bench/bring-up; use the proxied
> constructor for the production I2Cc topology. See ADR-010 in ARCHITECTURE.md.

### Patch Burst Mode (PBM) — Raw Burst to 0x30

When the TPS25751 boots in PTCH mode (no EEPROM) it exposes a special I2C receive
port at address **`0x30`** that accepts the raw firmware+configuration image during a
Patch Burst Mode (PBM) session (TI app note SLVAFV8A, §3-4).

**The raw burst write is a fundamentally different access shape from every other
TPS25751 write:**

| Write type | I2C transaction |
|---|---|
| Normal SMBUS register write | `START, addr(W), regNum, byteCount, data..., STOP` |
| PBM raw burst (Table 3-3) | `START, 0x30(W), imageBytes..., STOP` |

There is **no register number** and **no byte-count (length) byte** in the PBM burst.
`TPS25751::burstWrite()` implements this shape and MUST NOT emit either prefix.

**The PBM pointer auto-increments and is NOT reset by I2C START/STOP** (SLVAFV8A
footnote 3).  This means the full image can be split across many small transactions:

```cpp
// Each chunk is its own transaction, pointer advances through them:
while (offset < length) {
    wire.beginTransmission(0x30);          // NO regNum, NO byteCount
    wire.write(data + offset, thisChunk);
    wire.endTransmission(true);            // STOP, but pointer keeps going
    offset += thisChunk;
}
```

**Teensy `Wire` TX buffer constraint:** Stock Arduino/Teensy `Wire` has a 32-byte TX
buffer (`BUFFER_LENGTH`).  `PBM_BURST_CHUNK` (default 32) caps each chunk so it fits.
Raise it only if your project configures a larger Wire buffer.

**`0x30` is an I2C device address, not a register offset.**  It is the PBM receive
port's 7-bit I2C address, entirely separate from the TPS25751's own I2C device
address (0x20-0x23) and from `TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES`
(which happens to share the value 0x30 as a register *offset* within the TPS25751).

**PBMs config layout (6 bytes, little-endian `bundleSize`):**

```
config[0] = (length >>  0) & 0xFF  // bundleSize LSB
config[1] = (length >>  8) & 0xFF
config[2] = (length >> 16) & 0xFF
config[3] = (length >> 24) & 0xFF  // bundleSize MSB
config[4] = 0x30                   // PBM_I2C_TARGET
config[5] = 0x32                   // PBM_TIMEOUT
```

The timeout byte is in 100 ms units (TRM Table 4-15), so 0x31 is 4.9 s, and the TRM
recommends 0x32 (5 s). SLVAFV8A is inconsistent: Table 4-1 (page 8) lists 0x31 as "3.1
seconds", the byte listings of steps 4 and 5 (page 8) send 0x32, and the DATA read-back of
step 9 (page 10) shows 0x31. The loader sends 0x32 since 2026-11-24. Five loads on hardware with 0x32 (stage 7 of the
plan, 2026-11-24) all returned Success; every load before it sent 0x31.

**PatchStartStatus** (DATA byte 0 after PBMs / PBMc, i.e. `result.returnCode`):
`0x00` = success; `0x04` = bad bundle size; `0x05` = bad target address; `0x06` =
bad timeout.  Map each to a distinct `PatchLoadStatus` enumerator (not a generic
error).

#### After PBMc: 20 ms before DATA

Do not read DATA as soon as `PBMc` clears COMMAND. SLVAFV8A (step 15, page 11, and its
code on page 18) and TRM Figure 5-1 (page 88) wait 20 ms between `PBMc` completing and the
DATA read, while the controller loads and applies the image; the TRM says `PBMc` changes
the I2C target address back to its normal value before it completes (Table 4-16, page
79). The loader therefore issues `PBMc` with `TPS25751::issueCommand()`, which reads no
DATA, waits 20 ms, reads DATA (retrying every 10 ms on a failed read, for up to 5000 ms),
and then polls MODE every 10 ms until APP (up to 5000 ms; the caps are a choice). `PBMs`
is different: SLVAFV8A reads its DATA right after COMMAND clears (steps 8 and 9), so it
stays on `executeCommand()`.

Evidence, one board, hardware stage 5 of the EEPROM-update plan, with the earlier code
that read DATA at once: the read returned 28 of 65 bytes, the load reported `I2cError`
although the patch had loaded, and every later read from the host failed with error 2
until the host restarted. The Teensy 4 driver returns 2 only after `wait_idle()` found the
bus free and the peripheral then saw a NACK, of the address or of a data byte (the driver
cannot tell which); a busy bus, lost arbitration, a pin-low timeout, and its 50 ms timeout
return 4. A controller that did not acknowledge fits the codes. They argue against a held
line on the host side but do not rule out a host fault, and the cause is not established.
The current sequence has not run on hardware.

#### 500 µs before PBMc

TRM Figure 5-1 (page 88) has one delay box, "Delay at least 500us", between "Entire Patch
Bundle has been written? Yes" and the `PBMc` write; it has none between burst
transactions. SLVAFV8A step 10 (page 10) and its code (pages 17 and 18) send the image as
three separate I2C transactions of up to 4095 bytes, with 500 µs after each one, the last
included. The loader waits `PBM_PRE_COMMIT_DELAY_US` (500 µs) once, directly after
`burstWrite()` returns, so that the next transaction is at least 500 µs after the last
chunk: the INT_CLEAR1 write of step 11 in IRQ mode, the `PBMc` COMMAND write otherwise.
Each 32-byte chunk of `burstWrite()` is a separate transaction to 0x30, the same kind of
transaction as the app note's bursts, and the loader sends them back to back
(`interChunkDelayUs` stays 0), as it did in hardware stage 5, which loaded an image with no
delay at all. Whether to add the spacing is an open decision.

#### Where the loader departs from the documented flow

These are facts about the code, each with the document it differs from. None has been
observed to cause a failure.

- ReadyForPatch. TRM Figure 5-1 starts by polling INT_EVENT1.ReadyForPatch every 10 ms.
  The loader never reads INT_EVENT1. In IRQ mode it waits for the I2Ct_IRQ pin to go low
  (up to 5 s); in polling mode it does not wait and does not retry.
- MODE before the load. Figure 5-1 polls MODE until it reads PTCH. The loader reads MODE
  once and returns `NotInPatchMode` if that read fails or does not read PTCH.
- DATA before `PBMs`. SLVAFV8A step 5 (page 8) reads DATA back 500 µs after writing it and
  rewrites it until it matches; Figure 5-1 asks "DATA1 write completed successfully?". The
  loader writes DATA once and does not read it back.
- Transaction spacing. SLVAFV8A sends its 11392-byte example image as three burst
  transactions with 500 µs after each (step 10, page 10; code, pages 17 and 18). The
  loader sends the test board's 14336-byte image as 448 transactions of 32 bytes, back to
  back. TRM Figure 5-1 shows no spacing between burst transactions. Whether to add it is
  open (see above).
- IRQ mode. With an IRQ pin, the loader enables CMD1 Complete in INT_MASK1 and clears
  INT_CLEAR1 (step 3), and clears INT_CLEAR1 again after the burst (step 11), but it reads
  the I2Ct_IRQ pin only in step 1. After `PBMs` and `PBMc` it polls COMMAND every 5 ms, as
  in polling mode. SLVAFV8A waits for I2Ct_IRQ low before it reads CMD1 (steps 7 and 13,
  pages 9 and 11; code, pages 17 and 18).
- COMMAND poll interval. Figure 5-1 polls CMD1 with a 10 ms delay. `waitForCommandClear()`,
  used for `PBMs` and `PBMc`, polls every 5 ms.
- PBMe. Figure 5-1 issues `PBMe` after a burst that is not acknowledged and after `PBMc`
  reads "!CMD". The loader returns `BurstFailed` or `PbmcRejected` and never issues `PBMe`.
- The wait after `PBMc`. TRM Table 4-16 (page 79) says to check DATA when CMD1 goes to 0;
  the 20 ms delay appears only in Figure 5-1 and SLVAFV8A step 15 (page 11). The loader
  follows the figure and the app note.
- DATA read length after `PBMc`. SLVAFV8A step 16 (page 12) reads 40 bytes, and TRM
  Table 4-16 gives `PBMc` 320 bits of output. The loader reads the whole 64-byte DATA
  register and checks byte 0 only.
- PatchLoaded. Figure 5-1 polls INT_EVENT1.PatchLoaded after DATA reads 0. The loader does
  not. SLVAFV8A footnote 5 (page 11) says the interrupt can replace the 20 ms delay and
  that doing so saved no time; the loader keeps the delay.
- The delay before the final MODE read. Figure 5-1 waits 10 ms before reading MODE; its
  footnote 2 calls the delay optional but recommended. The loader reads MODE right after
  the DATA check, then polls every 10 ms until APP (SLVAFV8A code, page 18).
- The `PBMs` timeout byte is 0x32, as TRM Table 4-15 recommends (see the configuration
  layout above). Five loads on hardware with it returned Success (plan, stage 7); earlier loads sent 0x31.

See `TPS25751PatchLoader` (`include/TPS25751PatchLoader.h`, `src/TPS25751PatchLoader.cpp`)
for the complete 17-step implementation, and ADR-011 in ARCHITECTURE.md for the
design rationale.

### Flash tasks and EEPROM update

The four flash tasks reach the external EEPROM (0x50 on I2Cc), and they do not share one
result format. SLVAFL1 (EEPROM Boot Flow, page 2) says the host can write the EEPROM once
MODE reads APP; neither it nor the TRM says whether the tasks are refused in other modes,
and they have not been tried outside APP. The updater checks MODE before it writes. `TPS25751EepromUpdater` (ADR-014) applies the
rules below; code that issues the tasks directly must apply them itself. Sources: TRM
SLVUCR8B Tables 4-1, 4-3, and 4-19 to 4-22 (pages 61, 62, 81, and 82), TI application
note SLVAFL1, and the hardware stages recorded in
`docs/plans/feature/eeprom-update.md` (one board with a 24LC256, 2026-11-22 and 23).
The datasheet (SLVSH93A, I2C Interface) asks for an EEPROM of at least 36 KB; the board's
32 KB part holds the 31744-byte full-flash image and boots, and the updater takes the EEPROM
size as a constructor parameter.

#### Result offsets in library terms

`TPS25751::executeCommand()` strips the I2C length byte and copies DATA from byte 0. The
SLVAFL1 sample code reads `outdata[1]` because its buffer keeps the length byte; do not
copy that offset.

| Task | Input (DATA) | Result | Status |
|---|---|---|---|
| `FLrd` | 4-byte address, least significant byte first | `outData[0..15]`, the byte at the address first. There is no return code; `returnCode` is the first memory byte | Verified, stage 1 |
| `FLad` | 4-byte address, least significant byte first (Table 4-20) | `returnCode`, standard return code, 0x00 success | 0x00 observed, stage 2 |
| `FLwd` | 1 to 64 data bytes; the library sends at most 32 | `returnCode`: 0x00 written, 0xFF flash busy (Table 4-21) | 0x00 observed; busy never seen |
| `FLvy` | 4-byte address, least significant byte first | `returnCode`: 0x00 valid, 0x01 not valid (Table 4-22) | Valid observed, stage 1; not valid observed, stage 3, exact byte not recorded |
| `GAID` | none | none; the controller restarts (Table 4-3) | See "Cold reset" below |

The TRM states the address byte order for `FLad` only. Stage 1 verified it for `FLrd`: both
pointers read correctly and a dump of 0x0000 to 0x7C00 equalled the full-flash image. For
`FLvy` the evidence is indirect: both bundles verified at 0x0800 and 0x4400, and either
address byte-swapped lies outside the 32 KB part.

Compare the whole return byte with 0. Table 4-1 puts the standard TaskResult in bits 3:0
and lets a task use bits 7:4 while TaskResult is 0, so the whole-byte compare can report a
false failure and never a false success. `FLwd` has its own two codes and does not refer to
Table 4-1; the library treats any other value as `WriteFailed`.

#### Chunk size and the EEPROM page

Send `FLwd` data in 32-byte chunks. The TRM (Table 4-21) accepts up to 64 bytes and
recommends 32, the default page size the task uses. The 24LC256 has a 64-byte page and a
5 ms maximum write cycle, and a page write that crosses a page boundary wraps to the start
of the same page (Microchip DS21203P). Both bundle starts are multiples of 64, so no
32-byte chunk crosses a page. The pointer and AppConfigOffset words are 4-byte writes
inside one page. The write address advances after each successful `FLwd`, so a bundle
needs one `FLad`; stage 3 verified the advance across 446 chunks. The DATA write of one
chunk fits the Teensy 4 `Wire` buffer of 136 bytes.

#### What `FLvy` does and does not prove

The TRM says `FLvy` completes after the header is checked and validated. Stage 3 inverted
one body byte (image offset 0x1BE5) of a freshly written region, and `FLvy` reported it not
valid, so it checks more than the header. One byte at one offset is the whole sample;
whether it checks the CRC of every byte is not documented. Keep the `FLrd` compare before
any pointer write.

That matters because redundancy is one-directional. SLVAFL1 (Boot Process, page 2) says
that after a correct Header_ID in the Low region, a CRC error while reading the Low bundle
ends the load, and the controller does not try High. A High CRC error does fall back to
Low. A valid Low pointer over a damaged Low body therefore does not boot, even with an
intact High region.

#### The stale BOOT_FLAGS trap

BOOT_FLAGS (0x2D, BOOT_STATUS in SLVAFL1) and VERSION are latched at boot. After an update
without a restart they describe the old layout. SLVAFL1 Figure 2-2 chooses the region to
update from BOOT_FLAGS alone; in that state it would choose the region whose pointer is
the only valid one, and step 1 would erase it. Stage 3 reached exactly that state: the Low
pointer was 0 and BOOT_FLAGS still read as at boot. Choose the target from the region
pointers first and read BOOT_FLAGS only when both pointers are valid, as
`selectTargetRegion()` does.

#### AppConfigOffset words

The controller looks for a region's Header_ID at the region start plus that region's
AppConfigOffset word (0x03FC and 0x07FC; SLVAFL1 page 2). Both words are 0 in the board's
image. A region therefore counts as bootable only if its pointer equals its bundle start
and its word reads 0 (decided 2026-11-23).

- `programBlank()` writes both words to 0 before it writes any pointer or bundle, and reads
  each once more right before that region's pointer write.
- `updateRegion()` reads both words but does not write them. Its pre-flight refuses with
  `NoBootableRegion` when the kept region's word is not 0 and with `AppConfigOffsetNotZero`
  when the target's word is not 0, writing nothing; step 3 reads the target's word again
  before the pointer write (`CompareMismatch`, `lastStep()` `CheckAppConfigOffset`).
- `selectTargetRegion()` reads the pointers only, so a target it chooses can still be
  refused by the pre-flight.
- The repair for a word that is not 0 is to write both pointers to 0 and run
  `programBlank()` (the example's `zero`, then `pb`).

These reads are not hardware-tested: stages 3 and 4 ran the update before they were added,
and stage 5 ran `programBlank()`, whose word reads predate them.

#### Recovery from PTCH

When no region loads, the controller waits in PTCH for a patch over I2Ct. SLVAFL1
(Recovering From EEPROM Failure, Figure 4-1, page 13) recovers by loading a patch with the
PBM tasks (`TPS25751PatchLoader`, ADR-011) and then correcting the EEPROM with the flash
tasks (`programBlank()` when no pointer is valid). The same page notes that this needs a
host able to run and correct the EEPROM while the controller sits in PTCH, and that a
system which needs the sink path before it can boot must select a suitable dead-battery
configuration with the ADCINx pins; SafeMode does not apply there.

#### DATA register behaviour

TRM page 61 says the controller does not modify DATA after COMMAND reads 0, and that the
bytes after a return code are task-defined. Stage 2 showed what that means on this
controller: `FLad` leaves DATA bytes 4 to 63 as the preceding `FLrd` left them, and a 4-byte
host write to DATA does not clear the rest. A 4-byte `FLwd` nevertheless wrote exactly four
bytes. A check that compares neighbor bytes after a pointer write cannot by itself prove
the write length, because DATA may hold those neighbors' own values; the example's `p2`
command loads DATA with other bytes first.

#### Timing

Measured in stages 1 to 3 at the default I2C clock, with the executor reading all 64 DATA
bytes after every task:

| Item | Time |
|---|---|
| `FLrd`, `FLad` | 17.7 ms per call (stages 1 to 3) |
| `FLwd`, 4 bytes | 17.7 to 17.8 ms (stage 2, three calls) |
| `FLwd`, 32 bytes | 20.7 ms average, 20.8 ms maximum over the 449 calls of the stage 3 single-region update |
| `FLvy` | 0.56 to 0.60 s |
| One region, 14272 bytes, with compare | 25979 ms |
| Both regions, 14336 bytes each | 52049 ms |

No `FLwd` returned busy in the 1798 calls of stage 3. The library's timeouts (200 ms for
`FLrd`, `FLad`, and `FLwd`, 1000 ms for `FLvy`) have ample margin. Most of each task's time is the 64-byte DATA read; an `outLen`-bounded read
in `executeCommand()` is the first optimization if the update must be faster.

#### Cold reset

`GAID` never completes in the normal sense. An APP-mode controller enters Type-C Error
Recovery and restarts after a typical 1-second delay (Table 4-3), and the port disconnects.
Stages 3 and 4 measured the same sequence on every reset: COMMAND kept answering for 1276
to 1277 ms, then a COMMAND poll failed on I2C and `executeCommand()` returned `I2CError`;
the MODE read 100 ms later failed with error 4, not with a NACK; MODE read APP 1938 to
1939 ms after `GAID`. The Teensy 4 driver returns 4 from `endTransmission()` when no
transmission was begun (not the case here), when the bus did not become idle within 16 ms
and `force_clock()` could not free it, on lost arbitration, on a pin-low timeout (PLTF; the
driver leaves MCFGR1.TIMECFG clear, and which pins that covers was not checked against the
i.MX RT1060 reference manual), and on its own 50 ms transaction timeout. Which one occurred
is not recorded. That is one board, with the F8091164
and the F8091166 bundle running; whether the two bundles differ in firmware is not recorded.
TRM Table 4-3 marks `GAID` complete by COMMAND returning to 0 on reboot; `coldReset()` does
not take that as evidence, so that documented completion without a caught NAK is a second
false-negative path. A successful MODE read other than APP is not evidence either, because
`coldReset()` does not read MODE before `GAID`.

MODE reads APP before and after a restart, so it cannot by itself tell a restart from a
`GAID` the controller accepted without restarting. `coldReset()` therefore returns `Success`
only when it saw the controller go away first, through an `I2CError` on `GAID` or a failed
MODE read, and returns `ResetNotObserved` when MODE reads APP without that evidence. The
cost is a false negative: a restart that fits between two MODE reads, or a firmware that
finishes the `GAID` handshake before resetting and is back before the first read, is
reported as not observed. If that is ever seen, revisit `GAID_SETTLE_MS` and
`GAID_MODE_POLL_MS` or add a timing-independent witness. An I2C failure that kept `GAID`
from the controller still looks like a restart, so confirm any result by VERSION and
BOOT_FLAGS.
Table 4-3 calls `GAID` a cold restart in its Description and a warm restart in its
Additional Information row.

#### Bus sharing

The updater is synchronous. Every 4CC task shares COMMAND and DATA, and the bundle write
relies on the write address the controller holds between chunks. The application must not
issue other tasks, such as I2Cr or I2Cw through `TPS25751DownstreamDevice`, from an
interrupt or another task during an update, and a progress callback that returns must not
issue any. The TRM states the 5-second spacing for I2Cr and I2Cw only; nothing on pages 81
and 82 applies it to the flash tasks. Each flash task makes the controller ignore the
I2Cc_IRQ pin until it completes, so an interrupt from a device on I2Cc can be deferred;
that has not been measured. The EEPROM also answers on the MCU's own bus where it is wired
to one (`Wire2` on this board), and a direct read there matched the image. The controller
masters the same bus at boot, for the BQ25798, and during every flash task, so a direct
write needs its own review; the library does not offer one.

### Maximum Read Sizes

Different registers have different maximum sizes:

| Register | Max Size | Notes |
|----------|----------|-------|
| STATUS | 5 bytes | Fixed size |
| MODE | 4 bytes | Fixed size |
| DATA | 64 bytes | **Largest register** |
| RECEIVED_SOURCE_CAPABILITIES | 53 bytes | Variable (up to 7 PDOs); see `TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES` |
| IO_CONFIG | 49 bytes | Large, boot-fixed GPIO configuration; read-only |

**Implication:** Buffers must accommodate largest expected register.

### I2C Timing

**Clock Speed:** Typically 100 kHz or 400 kHz
**Considerations:**
- Slower speeds more reliable with longer cables
- Faster speeds reduce read latency
- Some operations require delay between transactions

```cpp
Wire.setClock(400000);  // 400 kHz
```

---

## Register Access Patterns

### Read-Only Registers (R)

**Examples:** STATUS, MODE, BOOT_FLAGS, RECEIVED_SOURCE_CAPABILITIES, IO_CONFIG

**Characteristics:**
- Can only be read
- Writing has no effect or may cause errors
- Reflect current hardware state

**Implementation:**
```cpp
bool isReadOnly() const override {
    return true;
}
```

> **⚠️ Update-frequency caveat (ADC_RESULTS, 0x6A).** Not every read-only
> register is refreshed on every read. Per the TRM, the PD controller updates
> the internal-ADC measurement block **only in certain modes** — in particular
> it does **not** update ADC_RESULTS while in **Unconnected Sleep**. A stale or
> all-zero ADC read in that state is therefore *expected behavior, not a bus
> error or a decode bug*. `TPS25751ADCResults::isSemanticallyValid()`
> deliberately accepts an all-zero buffer for this reason (it still rejects the
> all-`0xFF` bus-error signature, matching the house convention). Callers that
> need fresh telemetry must ensure the port is out of Unconnected Sleep before
> trusting the values.

> **⚠️ IO_CONFIG (0x5C) is read-only, despite earlier planning notes calling it
> R/W.** The TRM/register-docs mark every IO_CONFIG field `[R]` and state:
> "This register cannot be modified at run-time, the GPIO configurations are
> set according to the configuration during the boot process." `TPS25751IOConfig`
> is therefore decode-only — no setters, no write path, no `kAddress`. It is
> also the library's first register whose bit offsets run past 255 (up to 391
> for the GPIO12 Mapped Event field).
>
> TX_BATTERY_CAPABILITIES (0x7D, Batch K) is the first R/W register past bit
> 255. Battery 3 occupies bits 287:216 (bytes 27 to 35), so its Design Capacity
> starts at bit 248 and ends at 263, and its Last Full Charge Capacity and Battery
> Type start at bits 264 and 280.
>
> Fields past bit 255 need no special handling. `TPS25751Register::extractBits*()`,
> `setBits()`, and the `extractEnum*()` templates take a `uint16_t bitPos`
> (ADR-012 in ARCHITECTURE.md) and compute every byte offset and bound in
> `size_t`, so a field anywhere in the buffer is reachable and a field that runs
> past the end reads 0 or is not written. Both classes call them directly. Until
> ADR-012 (2026-11-19) the position was `uint8_t`: IO_CONFIG assembled its fields
> from `_raw[]` by hand and 0x7D carried class-private byte helpers, and a large
> offset cast to `uint8_t` would have wrapped to a low bit. When a caller computes
> a bit position, keep the arithmetic in `uint16_t` or wider; narrowing it to
> `uint8_t` reintroduces that wrap.

### Registers written to trigger an action

**Examples:** COMMAND, INT_CLEAR1

The TRM (SLVUCR8B) marks no TPS25751 register write-only. COMMAND (p.16) and
INT_CLEAR1 (p.23) are R/W registers that the host writes to trigger an action.
`TPS25751::executeCommand()` reads COMMAND to poll for completion. Reading
INT_CLEAR1 back carries no event state, so the library has no
`readInterruptClear()` and only writes it.

### Read/Write Registers (R/W)

**Examples:** PORT_CONTROL, INT_MASK1, AUTONEGOTIATE_SINK

**Characteristics:**
- Can be read and written
- Reading returns last written value or current state
- Support read-modify-write patterns

**Implementation:**
```cpp
bool isReadOnly() const override {
    return false;
}

// Support both reading and writing
bool validateData() const override {
    // Full validation
    return checkReservedBits() && checkFieldRanges();
}
```

**Usage pattern:**
```cpp
// Read-modify-write
auto portControl = tps.readPortControl();
portControl->setDataRoleSwapEnabled(true);
tps.writePortControl(*portControl);
```

**Trap — a register's documented reset value can change between TRM revisions.**
SLEEP_CONTROL (0x70) resets to `0x03` in SLVUCR8B Rev. B (page 52): Sleep Mode
Allowed = 1, Sleep Time = `1h` (100 ms), which `isSemanticallyValid()` accepts.
Rev. A listed `0x01` (Sleep Time = `0h`, a *Reserved* code), a combination
`isSemanticallyValid()` rejects. Rev. B also redefined Sleep Time `2h` from
1200 ms to 1000 ms. A read of 0x70 after a power cycle on 2026-11-18 returned
`0x03`, matching Rev. B (Teensy 4.0 host, TPS25751 with an EEPROM-loaded
configuration, so the read does not separate the silicon reset from a value set
by that configuration). If a device does report `0x01`, `isSemanticallyValid() == false`
is expected and is not an I2C error. (General rule: semantic validation encodes
*logical* consistency, which a reset state is not obligated to satisfy.)

**Trap: firmware can set bits the TRM marks reserved.**
PD3_CONFIGURATION (0x42) lists bits 3:0 as reserved with reset 0h (SLVUCR8B
Rev. B, Table 3-26, p.46), and its documented reset is `0x00080010`. On
2026-11-18 a TPS25751 in APP mode with an EEPROM-loaded configuration (Boot
Flags Revision ID 193, Teensy 4.0 host) read `0x0008001A`: the documented reset
plus bits 3 and 1. The read does not show whether the silicon reset or the
EEPROM configuration sets them. With all reserved bits checked, the validated
read failed on that board and `readPD3ConfigRegister(true)` returned nullptr. `TPS25751PD3Config` therefore checks only bits 31:21, 16:12, 9,
and 6:5 (`kCheckedReservedMask`, `0xFFE1F260`), leaves bits 3:0 unchecked
(`kFirmwareReservedMask`), and reports them in `debugPrint()`. Its setters
never touch any reserved bit, so a read-modify-write sends bits 3:0 back as
read. A write round trip on the same board confirmed this: clearing Support
Get Revision read back `0x0000001A`, and restoring it read back `0x0008001A`.
This follows the rule in `docs/plans/trm-revB-audit-remaining-registers.md`
("Validation tiers"): when hardware shows firmware setting a reserved bit, drop
that check and record why here.

**Trap: GLOBAL_SYSTEM_CONFIGURATION (0x27) is 14 bytes on the wire, not the
TRM's 116 bits, and its reserved bits carry firmware data.** SLVUCR8B Rev. B
(Table 3-13, p.28) gives a 116-bit register, which rounds up to 15 bytes. On
2026-11-18 the same board as above (APP mode, EEPROM configuration, Teensy 4.0
host) answered a raw read of 0x27 with a length byte of 14 on four consecutive
reads. `TPS25751::readRegister()` rejects any length byte that differs from
`RegisterInfo`, so a 15-byte `RegisterInfo` would fail every read.
`Registers::GLOBAL_SYSTEM_CONFIGURATION` is therefore 14 bytes, and bits
115:112 are not transmitted. The data was `05 09 14 00 1C 03 00 00 00 00 00 00
00 00`. The documented fields decode to PP Cable1 enabled, PP1 Source, ILIM Over
Shoot none, PP3 bi-directional, and RCP 6 mV, but the firmware also sets bit 2
(reserved 7:1), bit 11 (13:11), bit 20 (21:19), and bits 34, 35, 36, 40, and 41
(115:24). Every reserved range holds data, so `TPS25751GlobalSystemConfig`
checks no reserved bit; its semantic rules are PP3 Config ≤ 5 and PP1 Config ≤ 1.
Two consequences for writers:

- A write must start from a fresh read of the same board. The setters touch
  only their own fields, so a read-modify-write sends the undocumented bits back
  unchanged. Writing a default-constructed object clears them and also sets PP1
  and PP3 to Disabled.
- The TRM says any modification "will cause a port disconnect and reconnect
  with the new settings". On 2026-11-19, with the board on a bench supply and
  nothing attached, `setRcpThreshold()` plus `writeGlobalSystemConfig()` from a
  fresh read changed byte 2 from `14` to `54`. The read-back matched exactly,
  so only bits 23:22 changed and every undocumented bit survived. Writing the
  original bytes back also read back exactly. With no partner there was nothing
  to disconnect: STATUS kept toggling Port Role about every 85 ms, as it does
  when unattached, and INT_EVENT1 latched nothing. The reconnect with a partner
  attached is not observed.

`debugPrint()` prints all 14 raw bytes so a before-and-after comparison shows
whether the undocumented bits survived.

**Trap: LIQUID_DETECTION_CONFIG (0x98) is 12 bytes on the wire, not the TRM's
83 bits.** SLVUCR8B Rev. B (p.60) gives 83 bits, which rounds up to 11 bytes, and
the library shipped an 11-byte `RegisterInfo` from Batch F. On 2026-11-18 the
same board answered a raw read of 0x98 with a length byte of 12 (`0C`) followed
by 12 zero bytes. Because `TPS25751::readRegister()` rejects a length byte that
differs from `RegisterInfo`, every 0x98 read had failed, and
`example-read-registers` printed "Failed to read register data for address 0x98"
on every loop. `Registers::LIQUID_DETECTION_CONFIG` is now 12 bytes. Only bits
82:0 are defined; bits 95:83 are not decoded and the setters do not touch them.
With 0x27 (14 bytes against 116 bits) this is the second register whose TRM width
disagrees with the length byte, so confirm the length byte on hardware for any
register whose TRM width is not a whole number of bytes.

**RECEIVED_SOP_IDENTITY (0x48) and INT_EVENT1 bit 51.** 0x48 holds the latest
Discover Identity response. INT_EVENT1 bit 51 means "The Discover Modes process
has completed" (p.19), a later discovery step, and INT_EVENT1 has no Discover
Identity event. Treat bit 51 as a hint and Response Type (bits 7:6) as the
authoritative status. On 2026-11-18 the board was attached to a PD source as a
UFP sink, with PORT_CONTROL Automatic ID Request = 1. 0x48 read Response Type 0
(not sent or pending) with zero VDOs, and bit 51 was clear. A SOP Discover
Identity is normally sent by the DFP, so this result is expected for a UFP; the
TRM does not state the firmware's policy.

On 2026-11-19 the board was attached to an iPhone as the source and DFP, and
0x48 read Response Type 1 (ACK) with six VDOs; the ID Header VDO carries VID
0x05AC. Polling every 14 ms showed the order of events on attach: 0x48 cleared
to all zero in the same sample as the attach, filled about 240 ms later, and
INT_EVENT1 bit 51 set 106 ms after that. Bit 51 therefore lags the 0x48 update,
as its TRM text implies. With no partner, 0x48 read Response Type 0 and zero
VDOs, but after a detach it kept the previous partner's identity until the next
attach cleared it, so a nonzero 0x48 does not mean a partner is present now.

INT_EVENT1 latches only sources that INT_MASK1 enables. This board's EEPROM
configuration enables only Ready for Patch and Patch Loaded (INT_MASK1
`00 … 00 03`), so with the shipped configuration INT_EVENT1 stays all zero
through attach, discovery, and detach. To observe bit 51 or the plug event,
set the matching INT_MASK1 bits first.

Typed decode on hardware (Batch L, 2026-11-19): with the same iPhone attached and the
board DFP, 0x48 read Response Type ACK with three valid VDOs (the earlier walkthrough
saw six): ID Header `0xD50005AC`, Cert Stat `0x00000000`, Product `0x710C2166`. Per
USB PD R3.3 Table 6.34 the ID Header decodes to USB Host and USB Device capable, UFP
PDUSB Peripheral, Alternate Modes supported, DFP PDUSB Host, Connector Type 00b
(deprecated "unknown", the value a PD 3.0 product sends), and VID 0x05AC (Apple).
The Product VDO gives PID 0x710C and bcdDevice 0x2166, and XID is 0. The number of
valid VDOs varies between attaches of the same partner, so read it on every attach.

**TX_SOURCE_INFO (0x78) is R/W, but the firmware owns it.** SLVUCR8B Rev. B
(p.55) lists every field as R/W and also says the register "is automatically
updated by the PD firmware and does not require any EC implementation". A host
write can therefore be overwritten, and the TRM does not say when. It also
defines two Port Type bits, 63 and 31, with identical text, and does not say how
the upper word's 0.5 W PDP fields relate to the lower word's 1 W fields. On
2026-11-18 the same board, sinking from a PD charger, reported a length byte of 8
and read `00 00 00 80 00 00 00 80`, the documented reset 8000000080000000h. A
write that changed only the upper word (Port Maximum PDP 30, Port Guaranteed PDP
20, in 0.5 W units) read back exactly, and restoring the original read back
exactly; the firmware did not overwrite the value within the 50 ms before the
read-back. With no partner attached on 2026-11-19 the register also read the
reset. When an iPhone attached and the board became the source, the firmware
rewrote the lower word within about 110 ms of the attach, to `64 64 00 80`: Port
Reported PDP and Port Present PDP 100 W, Port Maximum PDP 0 W, Port Type
Guaranteed. It left the upper word at its reset, and the value stayed after
the iPhone was unplugged. A Present PDP above the Maximum PDP is not consistent
with the field descriptions, but neither the TRM nor USB PD R3.3 states a rule
between them, so the library checks none. The firmware value also meets the two
Source_Info rules that the class checks (see USB PD Specification Structures): DPS
Port is clear, and both Port Maximum PDP fields read 0. `TPS25751TransmitSourceInfo` checks a
Guaranteed PDP at most Maximum PDP rule on the upper word that the TRM does not
state. The firmware leaves both of those fields at 0, so hardware has not
exercised the rule; if firmware-written data breaks it, drop the rule under
"Validation tiers" in the plan.

TRANSMITTED_PPS_STATUS (0x7A) reported a length byte of 4 on the same board, read
`00 00 00 00`, and round-tripped a written value (`FA 00 3C 02`: 5000 mV,
3000 mA, temperature normal, constant voltage) and the restore exactly. The
board's PD3_CONFIGURATION (`0x0008001A`) has bits 18 and 20 clear, so the
controller was answering Get_Source_Info and Get_PPS_Status with Not_Supported,
whatever the two registers held.

**TX_SOURCE_CAPABILITIES_EXTENDED (0x77) does not read its documented reset.**
SLVUCR8B Rev. B (p.54) gives a reset of 0 for every field. On 2026-11-18 the
same board, sinking from a PD charger, reported a length byte of 15 and read
`00 00 00 00 00 00 00 00 00 00 00 00 00 7F 00`: Source PDP (bits 110:104) is 0x7F,
filled from the firmware configuration, and every other field is 0. No reserved
bit (119:111) was set, so `TPS25751TransmitSourceCapsExtended` keeps that check;
validation accepts both the documented reset and the board's value. A write of
distinct test bytes read back exactly over four reads 250 ms apart, and the
restore read back exactly. Source Inputs bit 0, which the TRM says the
Barrel_Jack_Event GPIO event can change, did not change on its own; this board
has no barrel-jack event.

TRANSMIT_SINK_CAPABILITIES_EXTENDED (0x7E) reported a length byte of 14, read all
zero (the documented reset), and round-tripped a written value and the restore
exactly. Both registers match their TRM widths, unlike 0x27 and 0x98. The board's
PD3_CONFIGURATION has bits 8 and 17 clear, so the controller answers
Get_Source_Cap_Extended and Get_Sink_Cap_Extended with Not_Supported, and neither
register's contents go out on the wire. The field encodings come from the USB PD
specification; see "USB PD Specification Structures (Batch L)" below for the typed
decode.

**TX_BATTERY_CAPABILITIES (0x7D): the TRM labels battery 3 as battery 0.**
SLVUCR8B Rev. B (p.58, Table 3-37) describes the five fields at bits 287:216 as
"battery index 0", but names them VID 3 and PID 3, and they sit at the fourth
72-bit stride. Three field names (Battery Type, Battery Design Capacity, Battery
Last Full Charge Capacity) also repeat four times. The library reads bits 287:216
as battery 3, the only reading consistent with the field names and the stride, and
identifies every field by bit range. On 7Bh, the TRM (p.57, Table 3-36) names
battery 3's capacity "Battery 3 Present Info" where batteries 0 to 2 use "Present
Capacity"; the library uses one name, `rawPresentCapacity(3)`. Both defects are
recorded in the register-docs server's `known_document_issues.json`.

**TRANSMITTED_BATTERY_STATUS (0x7B) and TX_BATTERY_CAPABILITIES (0x7D) on hardware.**
On 2026-11-19 the same board, sinking from a PD charger, reported length bytes of
16 and 36, as the TRM widths imply. 0x7B read `00 02 FF FF` followed by twelve zero
bytes, the documented reset FFFF0200h, with no reserved byte set, so
`TPS25751TransmitBatteryStatus` keeps its reserved-byte checks. The reset contains
0xFF bytes but is not all 0xFF, so it passes the data tier. 0x7D read all zero.
Raw writes of distinct bytes read back exactly on both registers over four reads
250 ms apart, the firmware did not overwrite them, and the restores read back
exactly. Through the classes, setting every battery 3 field of 0x7D and calling
`writeTransmitBatteryCapabilities()` left bytes 0 to 26, including a nonzero byte
26 (battery 2's Battery Type), unchanged on the controller. 0x77 reads zero fixed
and zero hot-swappable batteries while 0x7B battery 0 holds the reset value. Decoded
per USB PD R3.3 Table 6.24 (Batch L), that reset entry's Battery Info 0x02 sets
Battery Present, so the controller's own defaults disagree: 0x7B reports battery 0
present (capacity unknown) while 0x77 and 0x7E report no batteries. A host that
enables the battery replies should write all three registers consistently. The board's PD3_CONFIGURATION has bits 11 and
10 clear, so the controller answers Get_Battery_Status and Get_Battery_Capabilities
with Not_Supported.

**CUSTOMER_USE (0x06) is read-only at runtime in rev B.** SLVUCR8B Rev. B (p.15)
marks the field `[R]`: it "can only be changed during application customization,
not at runtime." The library exposed `writeCustomerUse()` from Batch A. Until
2026-11-19 that call returned false without writing, because `getRegisterInfo()`
had no CUSTOMER_USE case. With the case added, the write reaches the bus and the
controller ACKs it, but a read-back 100 ms later still shows the old value (bench,
2026-11-19). The library now treats CUSTOMER_USE as read-only: `writeCustomerUse()`,
`TPS25751CustomerUse::setValue()`, `setBytes()`, and `kAddress` are removed, so
`writeRegister<TPS25751CustomerUse>()` does not compile. This is a breaking change
for any caller of those methods. Set the bytes in the application configuration
(the EEPROM image or the patch bundle) instead.

---

## USB Power Delivery Complexity

### PDO (Power Data Object) Types

USB PD defines 4 PDO types, each with different bit layouts:

#### 1. Fixed Supply PDO (Type 00)

```
Bits 31-30: 00 (Fixed Supply)
Bits 29-28: Dual-Role Power
Bits 27-26: USB Suspend Supported
Bit  25:    Unconstrained Power
Bit  24:    USB Communications Capable
Bits 23-22: Dual-Role Data
Bits 21-20: Unchunked Extended Messages Supported
Bit  19-10: Voltage in 50mV units
Bits 9-0:   Maximum current in 10mA units
```

**Voltage calculation:**
```cpp
uint16_t voltageBits = (pdo >> 10) & 0x3FF;
uint16_t voltageMillivolts = voltageBits * 50;
```

**Current calculation:**
```cpp
uint16_t currentBits = pdo & 0x3FF;
uint16_t currentMilliamps = currentBits * 10;
```

#### 2. Battery PDO (Type 01)

```
Bits 31-30: 01 (Battery)
Bits 29-20: Maximum Voltage in 50mV units
Bits 19-10: Minimum Voltage in 50mV units
Bits 9-0:   Maximum Allowable Power in 250mW units
```

**Power calculation:**
```cpp
uint16_t powerBits = pdo & 0x3FF;
uint32_t powerMilliwatts = powerBits * 250;
```

#### 3. Variable Supply PDO (Type 10)

```
Bits 31-30: 10 (Variable)
Bits 29-20: Maximum Voltage in 50mV units
Bits 19-10: Minimum Voltage in 50mV units
Bits 9-0:   Maximum current in 10mA units
```

#### 4. Augmented PDO / PPS (Type 11)

```
Bits 31-30: 11 (Augmented)
Bits 29-28: 00 (SPR PPS APDO subtype)
Bit  27:    PPS Power Limited (Source only; the Sink PPS PDO has no such bit)
Bits 24-17: Maximum Voltage in 100mV units  ← 8-bit field, different unit!
Bits 15-8:  Minimum Voltage in 100mV units  ← 8-bit field, different unit!
Bits 6-0:   Maximum Current in 50mA units   ← 7-bit field, different unit!
```

**Note:** PPS uses **different units** than other PDO types! The max/min
voltage fields are **8 bits** at offsets 24:17 / 15:8 and the current is **7
bits** at 6:0 — verified against the hardware-tested decode in
`src/TPS25751ReceivedSourceCaps.cpp`. (Earlier revisions of this section wrongly
listed "Bits 26-17" / "Bits 16-8"; the decode `.cpp` is the source of truth —
corrected in Batch D when the encode inverse was added.)

**Trap: not every Augmented PDO is a PPS PDO.** Bits 31:30 = 11b mark an
Augmented PDO (APDO); bits 29:28 then select the APDO subtype, and only 00b is
the SPR PPS layout above. On 2026-11-18 the test board's TRANSMIT_SOURCE_CAPS
(0x32, EEPROM configuration) carried PDO 5 = `0xE004B1F4`, subtype 10b. Read with
the PPS layout it gave a 17700 mV minimum and a 200 mV maximum, and the whole
register failed `isSemanticallyValid()`. USB PD R3.3 Table 6.7 defines the subtypes:
00b SPR PPS, 01b EPR AVS, 10b SPR AVS, and 11b invalid. That PDO 5 is an SPR AVS
Source APDO (Table 6.15): Peak Current 00b, Maximum Current 15V 300 (3.0 A), and
Maximum Current 20V 500 (5.0 A), all in range. The `usb-pd-docs` MCP server serves
the PDO, APDO, and RDO tables (6.6 to 6.22) since `device-register-mcp-server`
c4cca8f and c000cdf (2026-11-20). Since 2026-11-22 the library decodes every PDO
through `TPS25751Pdo` (ADR-013): SPR PPS for a Source and a Sink, and the SPR AVS
Source APDO; EPR AVS and the deprecated SPR AVS Sink APDO are named and kept raw.
The paragraph below describes the PPS-only decode that came before. Every PDO class (`TPS25751ActivePDOContract`,
`TPS25751Received{Source,Sink}Caps`, `TPS25751Transmit{Source,Sink}Caps`)
therefore exposes the subtype (`apdoSubtype()` / `getAPDOSubtype()`) and
`isSprPps()` / `isSprPpsPDO()`. The PPS accessors return 0 (or false) for any
other subtype, semantic validation skips the PPS rules for it, and
`debugPrint()` names the subtype instead of printing a PPS decode. The shared
helpers `apdoSubtype(pdo)` and `isSprPpsApdo(pdo)` live in
`TPS25751ActivePDOContract.h` next to `PDOType`.

### PDO Parsing Strategy

Decode a PDO word through `TPS25751Pdo` (ADR-013), never with ad hoc shifts in a
register class. Build it with the word, its 1-based position, and the role of its
sender; `kind()` combines the PDO type (bits 31:30, Table 6.6) and the APDO type (bits
29:28, Table 6.7):

```cpp
TPS25751Pdo p = sourceCaps.pdo(i);        // position i + 1, PdoRole::Source
switch (p.kind()) {
    case PdoKind::Fixed:    p.fixedVoltage_mV(); p.peakCurrent(); break;  // flags only when i == 0
    case PdoKind::Battery:  p.batteryPower_mW(); break;
    case PdoKind::Variable: p.variableCurrent_mA(); break;
    case PdoKind::SprPps:   p.ppsMaxVoltage_mV(); break;
    case PdoKind::SprAvs:   p.avsCurrent15V_mA(); break;
    default: break;  // EprAvs and InvalidApdo: raw() only
}
```

### RDO (Request Data Object) Structure

Bits 31:22 are common to every RDO (USB PD R3.3 Tables 6.19 to 6.22); the rest
depends on the PDO at the Object Position, so decode through
`TPS25751Rdo(word, rdoKindFor(pdo))`:

```
Bits 31-28: Object Position (which PDO, 1-7)
Bit  27:    Giveback (deprecated, ignore)
Bit  26:    Capability Mismatch
Bit  25:    USB Communications Capable
Bit  24:    No USB Suspend
Bit  23:    Unchunked Extended Messages Supported
Bit  22:    EPR Capable (the TRM marks it reserved in 0x35)
Fixed/Variable: bits 19-10 Operating Current (10 mA), bits 9-0 deprecated copy
Battery:        bits 19-10 Operating Power (250 mW), bits 9-0 deprecated copy
PPS:            bits 20-9 Output Voltage (20 mV), bits 6-0 Operating Current (50 mA)
AVS:            bits 20-9 Output Voltage (25 mV, bits 10-9 = 00b), bits 6-0 Operating Current (50 mA)
```

### PDO/RDO Array Handling

**Source Capabilities format:**
```
Byte 0:      PDO Count (0-7)
Bytes 1-4:   PDO 1 (32 bits, little-endian)
Bytes 5-8:   PDO 2
...
Bytes 25-28: PDO 7
```

**Parsing:**
```cpp
uint8_t pdoCount = data[0];
if (pdoCount > 7) {
    return false;  // Invalid
}

for (uint8_t i = 0; i < pdoCount; i++) {
    uint32_t pdo =
        data[1 + i*4 + 0] |
        (data[1 + i*4 + 1] << 8) |
        (data[1 + i*4 + 2] << 16) |
        (data[1 + i*4 + 3] << 24);

    parsePDO(pdo, i);
}
```

> **The byte-1 PDO offset above is NOT universal.** It holds for
> RECEIVED_SOURCE/SINK_CAPS (0x30/0x31) and TRANSMIT_SINK_CAPS (0x33), but
> **TRANSMIT_SOURCE_CAPS (0x32)** inserts two bytes after the count byte:
> bits 9:8 = Power Path for PDO 1 and bits 23:10 reserved (SLVUCR8B Rev. B,
> page 36; Rev. A used bits 21:10 for Power Path for PDO 2-7), so **PDO 1
> starts at byte 3** and PDO *n* at `3 + (n-1)*4`.
> Always check the specific register's field table before assuming the offset.
>
> The firmware on the test board still fills bits 23:10 with the rev A layout.
> On 2026-11-18 bytes 1 and 2 of 0x32 read `AA 2A`: Power Path for PDO 1 (bits
> 9:8) = 2h, PP_EXT, and bits 21:10 = `0xAAA`, which in rev A means PP_EXT for
> PDOs 2 to 7. The library follows rev B and treats bits 23:10 as reserved, so it
> neither decodes nor writes them, and a read-modify-write keeps the firmware's
> values.

### Transmit Capabilities encode quirks (Batch D)

TRANSMIT_SOURCE_CAPS (0x32, 63 B) and TRANSMIT_SINK_CAPS (0x33, 53 B) are the
first registers with a PDO *encode* path (`TPS25751TransmitSourceCaps` /
`TPS25751TransmitSinkCaps`). Traps specific to encoding:

- **Mask every field to its bit width when building a PDO word**, so an
  over-range input (e.g. a voltage that overflows the 10-bit field) cannot bleed
  into the neighbouring or type-select bits. The `build*PDO()` helpers do this;
  hand-assembled words must too.
- **Set the PPS subtype bits.** An Augmented (PPS) PDO must have bits 31:30 = 11
  **and** bits 29:28 = 00; leaving 29:28 unset is correct, but do not assume the
  buffer was zeroed if you are editing in place.
- **Round-trip is only exact for exact-multiple inputs** because of the LSB
  quantization (÷50, ÷100, ÷250, ÷10). Unit tests must use values that are exact
  multiples of the field's LSB, or assert against the quantized value.
- **The Sink Fixed PDO differs from the Source Fixed PDO** (bit 28 = Higher
  Capability not USB Suspend; bits 24:23 = Fast-Role-Swap current, a 2-bit
  field; no Unchunked bit). Model that 2-bit field as `uint8_t`, not `bool`.
  The read-only `TPS25751ReceivedSinkCaps::getFixedFastRoleSwapCurrent()` returned
  `bool` and truncated codes 2 and 3 to 1 until 6627be1 (2026-11-19). Since ADR-013
  every capability class reads it as `pdo(0).fastRoleSwapCurrent()`, a 2-bit enum.

### USB PD Specification Structures (Batch L)

The TRM defers the field encodings of 0x48, 0x77, 0x7A, 0x7B, 0x7D, and 0x7E to the
USB PD specification. Batch L decodes them from USB PD R3.3 V1.0 (Rev. 3.3), served by
the `usb-pd-docs` MCP server (`specifications/USB_PD/specification.json` in the
sibling `device-register-mcp-server` repo). Take every encoding from that server, not
from memory. Points that are easy to get wrong:

- Offsets. TRM byte n of 0x77 is Source_Capabilities_Extended Data Block (Table 6.59)
  byte n + 10, and TRM byte n of 0x7E is Sink_Capabilities_Extended Data Block
  (Table 6.70) byte n + 10, because the controller prepends VID, PID, XID, and the
  firmware and hardware versions. The EPR PDP bytes fall in TRM reserved bits:
  0x77 byte 14 (bits 119:112) is the EPR Source PDP Rating, which an SPR Source shall
  set to 0 (Table 6.59), and 0x7E bits 111:88 are the three EPR Sink PDP bytes. Both
  classes require those bits to read 0. 0x7B entries are whole BSDOs (Table 6.24), so
  BSDO bit 8 is bit 0 of the Battery Info byte. 0x7D entries are whole Battery
  Capability Data Blocks (Table 6.64).
- Source PDP width. The TRM gives 0x77 Source PDP as 7 bits (110:104); the
  specification field is a byte with valid values 0 to 100 W. Every code with bit 7
  set is invalid anyway, so the 7-bit TRM field and the bit 111 reserved check stand.
  The test board reads 0x7F (127), which `sourcePdpValid()` reports as invalid.
- Duty Cycle. Peak Current (Table 6.59) and Sink Load Characteristics (Table 6.70)
  give the duty-cycle sub-field as 4 bits with values "0..31"; only 0 to 15 fit, so
  the library decodes 0 to 75 percent.
- Touch Temp. The two extended blocks encode it differently: in 0x77, 0 is IEC
  60950-1; in 0x7E, 0 is "No applicable standard" and 1 is IEC 60950-1.
- Conditional fields. BSDO Charging Status is reserved when the battery is absent or
  the capacity is FFFFh, and Source Inputs bit 1 is ignored when bit 0 is 0. The
  accessors decode the bits regardless; `debugPrint()` prints them only when they
  mean something.
- 0x7A Present Temperature Flag 00b is "Not Supported" in Table 6.67; the TRM leaves it
  undefined. `PresentTemperatureFlag::NotSupported` names it, and `NotDefined` stays
  as a deprecated alias of the same value.
- 0x78 TX Source Info is the two Source_Info Data Objects: bits 31:0 are SIDO1
  (Table 6.29, 1 W steps) and bits 63:32 are SIDO2 (Table 6.30, 0.5 W steps). The two
  Port Type bits the TRM leaves unexplained are bit 31 of each object. Table 6.30 says
  DPS Port requires Port Type 0 in both, and Table 6.29 says SIDO1 Port Maximum PDP
  equals SIDO2 Port Maximum PDP rounded down to 1 W. Semantic validation checks both
  rules. The TRM states only the first, and only for bit 63. A host that sets one
  Port Maximum PDP field must set the other to match, or the register fails
  validation. The values the firmware wrote on hardware, at reset and with a sink
  attached, meet both rules.
- Finding the mapping. Since `device-register-mcp-server` 8673c30, `tps25751-docs`
  prints a Specification block for 48h, 77h, 78h, 7Ah, 7Bh, 7Dh, and 7Eh that names
  the object and bits each register carries, and `explain_bitfield` names the object
  field under each register field. Since 696c854 (2026-11-22) it does the same for
  30h to 35h, the PDOs and RDOs. Start there, then read the object in `usb-pd-docs`.
- 0x48 VDO slots. For a dual-role partner the response is the UFP VDO, a 32-bit Pad
  Object of all 0s, then the DFP VDO (USB PD R3.3 page 158, Figure 6.5), so VDO index 4
  can read zero with a valid VDO count. After a version 1.0 UFP VDO 1 (Table 6.41),
  index 4 is UFP VDO 2 (Table 6.42). The library keeps indices 3 to 5 raw.
- PDOs and RDOs (cross-check against `usb-pd-docs` on 2026-11-22). The PDO field
  layouts the classes decode match Tables 6.8 to 6.13. Three points the tables add:
  - Bits 29:22 of a Fixed PDO above 5 V are Device Flags, reserved and populated only
    in PDO 1 (Table 6.10). `TPS25751Pdo` therefore decodes the flags only at position
    1 and returns false elsewhere, and 0x34 takes them from First PDO Control Bits
    through `firstPdoControl()`. Until 2026-11-22 the flag getters read those bits
    from any PDO.
  - 0x34 bits 41:32 (First PDO Control Bits) are bits 29:20 of PDO 1, Dual-Role Power
    through Peak Current (Table 6.8). The TRM does not say so. Hardware confirms it
    with two chargers: on 2026-11-19 0x34 read 0x098 there while PDO 1 of 0x30 was
    0x0981912C, whose bits 29:20 are 0x098, and on 2026-11-22 it read 0x080 (bit 7,
    Unconstrained Power) while PDO 1 decoded as Unconstrained Power set. With that
    second charger on a 20 V contract, the 0x34 debug output printed Unconstrained
    Power: No from the active PDO's reserved bits.
  - 0x35 bits 31:0 follow the Fixed and Variable RDO (Table 6.19), which is TI's
    layout. In a Battery RDO (Table 6.20) bits 19:10 are Operating Power in 250 mW
    units, and in a PPS or AVS RDO (Tables 6.21, 6.22) bits 20:9 are Output Voltage and
    bits 6:0 Operating Current in 50 mA units. TRM reserved bit 22 is EPR Capable in
    every RDO. 0x35 therefore hands out `TPS25751Rdo` objects decoded by the kind of
    the requested PDO; its old accessors read every RDO as Fixed/Variable, so a 9 V
    3 A PPS request read as 2250 mA.
    On hardware, with the board sinking, 0x35 RequestedRDO (bits 127:96) read the
    same word as bits 31:0 with both chargers (0x4184B12C, then 0x4104B12C), although
    the TRM describes it as the RDO a partner sends while this device is source.
  Validation of 0x30 to 0x34 follows the tables through `validatePdoList()` and
  `TPS25751Pdo::isValidForReceiver()`. Received lists (0x30, 0x31, and 0x34's
  active PDO) reject only what the tables tell a receiver to treat as invalid: a
  Fixed or Variable current above 500, a Battery power above 400, a PPS current above
  100, a Source PPS voltage outside 110/160/210 and 33/50 (Table 6.13), a Sink PPS
  voltage outside 50 to 210 (Table 6.14), an SPR AVS current above 500, APDO type
  11b, and a zero voltage; zero currents and powers are valid. Every list also
  requires PDO 1 to be a Fixed PDO at exactly 5 V. Transmit lists (0x32, 0x33) add
  the sender rules: the order Fixed, Battery, Variable, SPR AVS, PPS (page 130); no
  EPR AVS or invalid APDO; a minimum voltage at most the maximum in every Battery,
  Variable, and SPR PPS PDO; in 0x32, a Variable PDO's minimum voltage at least 80% of
  its maximum (Table 6.12) and SPR AVS currents equal to the 15 V and 20 V Fixed PDOs'
  (Table 6.15); in 0x33, no SPR AVS APDO (page 137). The minimum-at-most-maximum rule
  is the library's own choice (2026-11-22): the tables state no such rule, so a
  received list does not check it. The old per-type rules (nonzero current and a 4750
  to 5250 mV PDO 1) are gone. The board's 0x30, 0x32, and 0x33 values pass every
  rule.

### USB PD Specification Reference

For complete details, refer to:
- **USB Power Delivery Specification R3.3 V1.0**, via the `usb-pd-docs` MCP server:
  Tables 6.6 and 6.7 (PDO and APDO type), 6.8 to 6.12 (Fixed, Battery, and Variable
  PDOs), 6.13 to 6.17 (PPS and AVS APDOs), 6.18 (PDO Peak Current), 6.19 to 6.22
  (RDOs), 6.24 (BSDO), 6.29 and 6.30 (Source_Info SIDO1 and SIDO2), 6.34 (ID Header
  VDO), 6.38 (Cert Stat VDO), 6.39 (Product VDO), 6.40 to 6.44 and 6.53 (Product Type
  VDOs, not decoded), 6.59 (Source_Capabilities_Extended), 6.64 (Battery
  Capabilities), 6.67 (PPS Status), and 6.70 (Sink_Capabilities_Extended)

---

## Memory Considerations

### RAM Constraints

**Teensy 4.0:** 512 KB RAM
**Teensy 4.1:** 1024 KB RAM

**Guidelines:**
- Minimize global variables
- Use `const` and `constexpr` for constants (stored in flash)
- Use `F()` macro for string literals
- Avoid large stack allocations

### Flash Memory for Strings

**❌ Waste RAM:**
```cpp
Serial.println("This string uses RAM");
```

**✅ Save RAM:**
```cpp
Serial.println(F("This string is in flash"));
```

**For const data:**
```cpp
// Stored in flash
const char* CONNECTION_STATES[] PROGMEM = {
    "Not Connected",
    "Connected.DFP",
    "Connected.UFP",
    // ...
};
```

### Stack vs Heap

**Prefer stack allocation for small objects:**
```cpp
TPS25751Status status(data, 5);  // Stack - fast, automatic cleanup
```

**Use heap for large or dynamic objects:**
```cpp
auto reg = std::make_unique<TPS25751Status>();  // Heap - managed lifetime
```

**Avoid:**
```cpp
uint8_t largeBuffer[1024];  // Too large for stack on embedded!
```

---

## Common Pitfalls

### 1. Forgetting I2C Length Byte

**Wrong:**
```cpp
Wire.requestFrom(addr, 5);
data[0] = Wire.read();  // This is LENGTH, not data!
```

**Right:**
```cpp
Wire.requestFrom(addr, 1);
uint8_t length = Wire.read();
Wire.requestFrom(addr, length);
// Now read actual data
```

### 2. Using dynamic_cast

**Wrong:**
```cpp
auto* status = dynamic_cast<TPS25751Status*>(reg.get());  // Won't compile!
```

**Right:**
```cpp
auto* status = static_cast<TPS25751Status*>(reg.get());
```

### 3. String Literals in RAM

**Wrong:**
```cpp
for (int i = 0; i < 100; i++) {
    Serial.println("Iteration");  // Wastes RAM
}
```

**Right:**
```cpp
for (int i = 0; i < 100; i++) {
    Serial.println(F("Iteration"));  // Stored in flash
}
```

### 4. Incorrect PDO Unit Conversion

**Wrong:**
```cpp
// All PDO types use same units - WRONG!
uint16_t voltage = (pdo >> 10) & 0x3FF;
uint16_t voltageMv = voltage * 50;  // Wrong for PPS!
```

**Right:**
```cpp
uint8_t type = (pdo >> 30) & 0x03;
if (type == 0b11) {  // PPS uses 100mV units
    uint16_t voltage = (pdo >> 17) & 0x1FF;
    uint16_t voltageMv = voltage * 100;
} else {  // Others use 50mV units
    uint16_t voltage = (pdo >> 10) & 0x3FF;
    uint16_t voltageMv = voltage * 50;
}
```

### 5. Not Checking Reserved Bits

**Wrong:**
```cpp
bool validateData() const {
    return true;  // Too permissive!
}
```

**Right:**
```cpp
bool validateData() const {
    // Check reserved bits are 0
    if ((data_[0] & RESERVED_MASK) != 0) {
        return false;
    }
    return true;
}
```

Check reserved bits against the TRM, then against hardware. If a real device
sets a reserved bit, the check rejects every valid read from it. See the
PD3_CONFIGURATION and GLOBAL_SYSTEM_CONFIGURATION traps under Read/Write
Registers.

### 6. Large Stack Allocations

**Wrong:**
```cpp
void processData() {
    uint8_t buffer[2048];  // Too large for embedded stack!
}
```

**Right:**
```cpp
void processData() {
    static uint8_t buffer[2048];  // Static/global, or
    auto buffer = std::make_unique<uint8_t[]>(2048);  // Heap
}
```

### 7. Forgetting the I2Cr/I2Cw Target Address Is 7-Bit

**Wrong:**
```cpp
// WRONG - passes a raw 8-bit address (possibly including a R/W bit) straight
// into the I2Cr/I2Cw input payload
uint8_t in[3] = { deviceAddress, devReg, len };
```

**Right:**
```cpp
// RIGHT - bit 7 of the I2Cr/I2Cw target-address byte is reserved (must be 0);
// mask it off once, e.g. in the TPS25751DownstreamDevice constructor
_deviceAddress = static_cast<uint8_t>(deviceAddress & 0x7F);
```

Also remember the I2Cr **read data starts at DATA offset 1** (`DATA[0]` is the task
return code), the I2Cw payload is capped at **11 bytes**, and the I2Cw **`Length`
field counts the register-offset byte + payload (`payload_len + 1`)** — see
[4CC Command Interface & I2Cc Downstream-Device Proxy](#4cc-command-interface--i2cc-downstream-device-proxy-trm-sec-442443).

---

## Downstream-Device Decode & Encode Conventions (moved to lib/BQ25798)

The decode/encode conventions established for the BQ25798 downstream charger
driver — big-endian 16-bit assembly (never `extractBits16`), signed ADC channels
via `int16_t`, datasheet-sourced ADC LSB step sizes, read-modify-write field
setters that preserve reserved bits — moved with the driver to the sibling
**`lib/BQ25798`** repository; see `docs/engineering/CONVENTIONS.md` there. They
apply, by analogy, to any downstream device whose registers are big-endian.

Two things remain true on the host side regardless of the device driver:

- The generic I2Cr/I2Cw framing rules in the [4CC Command Interface](#4cc-command-interface--i2cc-downstream-device-proxy-trm-sec-442443)
  section above apply unchanged to *typed* downstream writes — payload `len ≤ 11`,
  `Length = payload + 1` (counts the register-offset byte) — because they ride the
  same I2Cw task as raw writes.
- `TPS25751Register::extractBits16` is **little-endian** by design (matches the
  TPS25751's own registers); big-endian downstream devices must assemble
  multi-byte fields manually.

---

## Platform-Specific Workarounds

### Dealing with RTTI Absence

**Problem:** Need type identification without RTTI

**Solution:** Use factory pattern with address-based lookup

```cpp
class TPS25751Register {
public:
    virtual uint8_t getAddress() const = 0;

    // Type-safe casting helper — compare against the expected register's address
    template<typename T>
    T* as() {
        if (getAddress() == static_cast<uint8_t>(T::staticAddress())) {
            return static_cast<T*>(this);
        }
        return nullptr;
    }
};
```

### Template Metaprogramming Without static_assert(false)

**Problem:** Need compile-time errors for invalid template instantiations

**Solution:** Use dependent types

```cpp
template<typename>
struct always_false : std::false_type {};

template<typename T>
void foo() {
    static_assert(always_false<T>::value, "Invalid type");
}
```

---

## Reference Documents

1. **TI TPS25751 Technical Reference Manual**
   - I2C protocol details
   - Register definitions
   - Electrical characteristics

2. **USB Power Delivery Specification R3.3 V1.0** (via the `usb-pd-docs` MCP server)
   - PDO/RDO formats
   - Negotiation protocols
   - Voltage/current encoding

3. **Teensy 4.x Documentation**
   - Memory layout
   - I2C implementation
   - Arduino compatibility

4. **ARM Cortex-M7 Technical Reference**
   - Processor capabilities
   - Optimization guidelines

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2025-10-20 | Claude Code | Initial constraints document extracted from implementation plan |
| 1.1 | 2026-06-24 | Claude Code | Added BQ25798 downstream device decode conventions (big-endian, signed ADC, LSB sources) |
| 1.2 | 2026-07-02 | Claude Code | BQ25798 decode/encode conventions moved to the sibling lib/BQ25798 repo with the driver; generic I2Cr/I2Cw framing rules retained here |
| 1.3 | 2026-07-07 | Claude Code | Batch C: documented the ADC_RESULTS (0x6A) update-frequency caveat — not refreshed in Unconnected Sleep, so a stale/all-zero read is expected, not an error (Read-Only Registers section) |
| 1.4 | 2026-11-18 | Claude Code | Batch G of the rev B plan: firmware sets PD3_CONFIGURATION (0x42) reserved bits 3:0 on hardware, so validation no longer checks them (Read/Write Registers section) |
| 1.5 | 2026-11-18 | Claude Code | Batch H of the rev B plan: GLOBAL_SYSTEM_CONFIGURATION (0x27) reports 14 bytes on hardware and the firmware sets bits in all its reserved ranges; RECEIVED_SOP_IDENTITY (0x48) status comes from Response Type, not INT_EVENT1 bit 51 (Read/Write Registers section) |
| 1.6 | 2026-11-18 | Claude Code | LIQUID_DETECTION_CONFIG (0x98) reports 12 bytes on hardware, not 11 (Read/Write Registers section); PDO classes decode only the SPR PPS APDO subtype after a subtype 10b APDO in 0x32 failed validation (USB Power Delivery Complexity section); the firmware still uses the rev A Power Path layout in 0x32 bits 21:10 |
| 1.7 | 2026-11-18 | Claude Code | Batch J of the rev B plan: TX_SOURCE_CAPABILITIES_EXTENDED (0x77) reads Source PDP 0x7F from firmware configuration, not the documented reset; 0x77 and TRANSMIT_SINK_CAPABILITIES_EXTENDED (0x7E) report 15 and 14 bytes as the TRM implies, and the firmware sets none of their reserved bits (Read/Write Registers section) |
| 1.8 | 2026-11-19 | Claude Code | Batch K of the rev B plan: TX_BATTERY_CAPABILITIES (0x7D) is the first R/W register past bit 255 and uses class-local byte helpers (Read-Only Registers section, IO_CONFIG note); the TRM labels 0x7D battery 3 as battery 0; 0x7B and 0x7D report 16 and 36 bytes and round-trip on hardware; CUSTOMER_USE (0x06) ignores runtime writes (Read/Write Registers section) |
| 1.9 | 2026-11-19 | Claude Code | CUSTOMER_USE (0x06) is read-only in the library: `writeCustomerUse()`, the class setters, and `kAddress` are removed (Read/Write Registers section) |
| 1.10 | 2026-11-19 | Claude Code | Batches G to I bench checks: a GLOBAL_SYSTEM_CONFIGURATION (0x27) RCP write preserves the undocumented bits; RECEIVED_SOP_IDENTITY (0x48) fills about 106 ms before INT_EVENT1 bit 51 and keeps the last identity after detach; INT_EVENT1 latches only INT_MASK1-enabled sources; the firmware fills the TX_SOURCE_INFO (0x78) lower word when the board sources (Read/Write Registers section) |
| 1.11 | 2026-11-19 | Claude Code | Batch L of the rev B plan: typed USB PD R3.3 decode for 0x48, 0x77, 0x7E, 0x7B, and 0x7D and the 0x7A PTF 00b meaning (new USB PD Specification Structures section); 0x48 decode of an iPhone on hardware; the 0x7B reset value reports a battery that 0x77 and 0x7E do not (Read/Write Registers section) |
| 1.12 | 2026-11-19 | Claude Code | ADR-012: the base-class bit helpers take a 16-bit bit position, so IO_CONFIG (0x5C) and TX_BATTERY_CAPABILITIES (0x7D) read and write through them (Read-Only Registers section, IO_CONFIG note) |
| 1.13 | 2026-11-19 | Claude Code | USB PD cross-check against the linked `tps25751-docs` output: 0x77 byte 14 is the EPR Source PDP Rating, 0x78 is SIDO1 and SIDO2, and the Specification block (USB PD Specification Structures section) |
| 1.14 | 2026-11-19 | Claude Code | TX_SOURCE_INFO (0x78) semantic validation checks the USB PD R3.3 Table 6.29 and 6.30 rules: DPS Port requires both Port Type bits Managed, and the 1 W Port Maximum PDP equals the 0.5 W value rounded down (USB PD Specification Structures and Read/Write Registers sections) |
| 1.15 | 2026-11-22 | Claude Code | PDO, APDO, and RDO cross-check against `usb-pd-docs` (device-register-mcp-server 696c854): the 0x32 subtype 10b APDO is an SPR AVS Source APDO, 0x34 First PDO Control Bits are PDO 1 bits 29:20 (confirmed on hardware), fixed-PDO flags are reserved outside PDO 1, 0x35 decodes as a Fixed or Variable RDO only, and the 0x48 Pad Object (USB PD Specification Structures section) |
| 1.16 | 2026-11-22 | Claude Code | PDOs and RDOs decode through `TPS25751Pdo` and `TPS25751Rdo` (ADR-013): fixed-PDO flags only for PDO 1, SPR AVS Source decode, RDOs by kind, and validation of 0x30 to 0x34 per USB PD R3.3 (USB PD Specification Structures and PDO Parsing Strategy sections) |
| 1.17 | 2026-11-23 | Claude Code | Flash tasks and EEPROM update section: result offsets, return-code compare, 32-byte chunks and the 24LC256 page, the `FLvy` limit, one-directional redundancy, the stale BOOT_FLAGS trap, DATA behaviour, timing, cold reset, and bus sharing, with the hardware results of stages 1 to 4 (ADR-014) |
| 1.18 | 2026-11-23 | Claude Code | Flash tasks and EEPROM update section: what SLVAFL1 says about APP mode, AppConfigOffset words and recovery from PTCH (SLVAFL1 page 13) under their own headings, `FLwd` timing per run |
| 1.19 | 2026-11-23 | Claude Code | `programBlank()` writes both AppConfigOffset words first; `coldReset()` requires an observed restart and returns `ResetNotObserved` otherwise (Flash tasks and EEPROM update section) |
| 1.20 | 2026-11-23 | Claude Code | `updateRegion()` checks both AppConfigOffset words in its pre-flight and the target's again before the pointer write (AppConfigOffset words section); PTCH after a restart with no valid pointer observed in stage 5 |
| 1.21 | 2026-11-23 | Claude Code | PBM: 20 ms between `PBMc` completing and the DATA read, `issueCommand()`, the stage 5 evidence and what it does not establish, the 500 µs burst spacing the figures show (Patch Burst Mode section) |
| 1.22 | 2026-11-23 | Claude Code | The MODE read after a failed `GAID` poll failed with error 4, not a NACK (Cold reset section) |
| 1.23 | 2026-11-23 | Claude Code | PBM: TRM Figure 5-1 has one 500 µs delay, before `PBMc`, which the loader now gives; the points where the loader departs from the documented flow; the `PBMs` timeout byte. Error 4 lists every source in the Teensy 4 driver (Cold reset section) |
| 1.24 | 2026-11-23 | Claude Code | PBM: the app note spaces its separate burst transactions by 500 µs and the loader does not, an open decision; the loader's IRQ mode polls COMMAND instead of waiting for I2Ct_IRQ |
| 1.25 | 2026-11-23 | Claude Code | PBM: SLVAFV8A shows both 0x31 and 0x32 for the `PBMs` timeout byte |

---

*Understanding these constraints is essential for successful implementation on the target platform.*
