# Plan: update the TPS25751 configuration EEPROM over I2C

## Context

The TPS25751 boots its patch bundle (firmware patch plus register configuration) from an
external EEPROM at address 0x50 on its I2Cc bus. A host on I2Ct can rewrite that EEPROM
through four flash 4CC tasks while the controller runs in APP mode. This plan adds
`TPS25751EepromUpdater`, a sequencer for that flow, beside the existing
`TPS25751PatchLoader`.

The work covers three capabilities, as decided on 2026-11-22:

1. Update one region with a new bundle, following the four steps of TI application note
   SLVAFL1, with a helper that brings both regions to the new bundle.
2. Read the EEPROM back through `FLrd`, to compare a region with an image and to dump it.
3. Program a blank or corrupt EEPROM (region pointers and both regions) once the
   controller is in APP mode after a PBM load, which replaces the first-time `minipro`
   program.

No hardware write has been made for this plan. Every protocol claim below is marked
documented, verified, or inferred, and the Hardware validation section orders the
checks that turn inferred into verified.

## Sources

| Short name | Document | Where |
|---|---|---|
| App note | SLVAFL1, October 2024, "TPS25751 and TPS26750 EEPROM Update Over I2C" | `docs/TPS25751 and TPS26750 EEPROM Update Over I2C.pdf` |
| TRM | SLVUCR8B, revised August 2026 | `../device-register-mcp-server/devices/tps25751/docs/TPS25751 Technical Reference Manual - August 2026.pdf`, in the sibling server repo, relative to this checkout or the worktree. Use this copy; the one in `docs/` is byte-identical (checked 2026-11-22) |
| Datasheet | SLVSH93A | `docs/TPS25751 USB Type-C and USB PD Controller.pdf` |
| Images | `4x18650-config-dual_TPS25751_F8091166_{lowRegion,fullFlash}.{bin,c}` and the `F8091164` build | `teensy_mic_sensor/PCB/eeprom/Dual Role/` |

The `tps25751-docs` MCP server holds registers only. It returns nothing for the flash
tasks, so the TRM PDF is the reference for them.

## Protocol facts

### EEPROM layout

Documented in the app note, Figure 2-6 and the source code on page 10, and verified
against `..._fullFlash.bin`:

| Item | Low region (0) | High region (1) |
|---|---|---|
| Region pointer | 0x0000 | 0x0400 |
| AppConfigOffset | 0x03FC | 0x07FC |
| Bundle start | 0x0800 | 0x4400 |

Verified on the image files:

- The full image is 31744 bytes (0x7C00). It holds four meaningful words (pointers
  0x00000800 and 0x00004400, both AppConfigOffset words 0) and two copies of the
  low-region file, at 0x0800 and 0x4400. Every other byte is 0xFF.
- The low-region file is 14336 bytes (0x3800), which is 448 chunks of 32 bytes. It
  starts with Header_ID 0xACE00001, little-endian.
- The word at offset 0x34 of the low-region file is 0xF8091166 in the current build and
  0xF8091164 in the previous build, matching the VERSION register (0x0F) convention.
  Two builds are a small sample, so the updater reports this word and does not gate on
  it.
- Each region has 0x3C00 bytes of room: 0x0800 to 0x4400 for low, and 0x4400 to 0x8000
  for high on the board's 24LC256 (32768 bytes).

The datasheet (I2C Interface section) says the EEPROM "must be at least 36 kilo-bytes".
The board's 32 KB part holds the 31744-byte image and boots, so this does not block the
work. The plan treats EEPROM size as a constructor parameter with a 32768 default.

### Tasks

Documented in the TRM, pages 61, 62, 81, and 82:

| Task | Input (DATA) | Output (DATA) | Notes |
|---|---|---|---|
| `FLrd` | 4-byte address | 16 bytes of memory, little-endian, from byte 0 | No return-code byte |
| `FLad` | 4-byte address, little-endian | Byte 0: standard task return code | 0x0 success, 0x3 rejected, others are errors |
| `FLwd` | Up to 64 data bytes | Byte 0: 0x00 written, 0xFF flash busy | Address auto-increments after each successful write |
| `FLvy` | 4-byte address | Byte 0: 0x00 valid, 0x01 not valid | Completes after the header is checked and validated |
| `GAID` | None | None | Cold restart; COMMAND reads 0 after reboot; the controller can NAK while rebooting |

All four flash tasks make the controller ignore the I2Cc_IRQ pin until they complete.

### Resolved questions

1. Chunk size is 32 bytes. The TRM (page 82) says `FLwd` accepts up to 64 bytes, the
   default EEPROM page size it uses is 32 bytes, and it recommends 32-byte chunks. This
   reconciles the app note's table (64) with its flow chart and summary (32). Both
   bundle start addresses are multiples of 32, so no chunk crosses a 32-byte page, nor
   the 24LC256's 64-byte page. The Microchip datasheet for the 24AA256/24LC256/24FC256
   (DS21203P, 2007; checked in Batch 1) gives a 64-byte page write buffer and a 5 ms
   maximum write cycle time (TWC, byte or page write, Table 1-2 parameter 17), and its
   Write-Protection note says a page write that crosses a physical page boundary wraps
   to the start of the same page and overwrites it. Later revisions are numbered
   DS20001203. A final short chunk is sent at its true length. Teensy 4 `Wire` buffers
   136 bytes (`WireIMXRT.h`), so the DATA write fits in one transaction.
2. Result offsets. `executeCommand()` strips the I2C length byte and copies DATA from
   byte 0. The app note's sample reads `outdata[1]` because its buffer keeps the length
   byte. In library terms: `FLad`, `FLwd`, and `FLvy` report their result in
   `TPS25751TaskResult::returnCode`, and `FLrd` data is `outData[0..15]`, with
   `returnCode` meaningless. Documented by the TRM. Verified in hardware stage 1 on
   2026-11-22 for `FLrd` (data from byte 0) and for `FLvy` reporting 0x00 on a valid
   bundle. The `FLad` and `FLwd` codes and `FLvy` reporting 0x01 are not yet observed.
   Stage 2 on 2026-11-23 observed return code 0x00 for three successful `FLad` calls
   and three successful `FLwd` calls. No failure code of any task has been observed.
3. `FLvy` is not a full verification. The TRM says it completes after the header is
   checked. Whether it also checks the bundle CRC is unknown, so the updater compares
   the written region with the image through `FLrd` before it writes the pointer.
   Hardware stage 3 measures what `FLvy` catches. Result, 2026-11-23: `FLvy` reported
   a bundle with one inverted byte in its body (image offset 0x1BE5) as not valid, so
   it checks more than the header. One byte at one offset is the whole sample, and the
   compare stays on by default.
4. Region selection uses the region pointers first and BOOT_STATUS second. BOOT_STATUS
   is latched at boot, so after an update without a reset it still describes the old
   layout. Following Figure 2-2 alone in that state would erase the only valid pointer.
   The rule is in the Region selection section.
5. Timeouts. `FLad`, `FLrd`, and `FLwd` use 200 ms, and `FLvy` uses 1000 ms until
   measured. `FLwd` returning 0xFF (busy) is retried up to three times after 10 ms, each
   retry preceded by a fresh `FLad` to the chunk's address. Estimate: 448 `FLwd` calls
   at 8 to 20 ms each is 4 to 9 s per region, and the 896 `FLrd` calls of the compare
   add 7 to 18 s. Stage 3 replaces the estimate with a measurement. Stage 1 measured
   `FLrd` at 17.7 ms per call (1984 calls in 35071 ms, default I2C clock), so the
   compare of a 14336-byte region takes about 15.8 s, and the 200 ms timeout has ample
   margin. `FLwd` and `FLvy` times are still estimates. Stage 2 measured `FLad` and
   `FLwd` at 17.7 to 17.8 ms per call, the same as `FLrd`, and no `FLwd` returned
   busy. A 14336-byte update of 1365 tasks should therefore take about 24 s; stage 3
   measures it. Stage 3 measured 25979 ms for a 14272-byte region and 52049 ms for
   both 14336-byte regions, with no busy result in 1798 `FLwd` calls. A 32-byte `FLwd`
   takes 20.7 ms, `FLrd` and `FLad` 17.7 ms, and `FLvy` 0.56 to 0.60 s, so the 200 ms
   and 1000 ms timeouts hold.
6. `GAID` never completes in the normal sense. The updater treats `Success`, `Timeout`,
   and `I2CError` from the executor as "reset issued", then polls MODE until it reads
   APP. The TRM says an APP-mode controller enters Type-C Error Recovery and restarts
   after about 1 s, so the wait defaults to 5 s. The port disconnects during the reset.
   Stage 3 measured the sequence twice with the same result: COMMAND kept answering
   for 1277 ms, then the controller stopped acknowledging and the executor returned
   `I2CError`; MODE read APP again 1938 ms after `GAID` was issued.
7. Sample-code defects not copied: step 3 of the app note ignores the executor result
   for `FLvy`, and the note says I2Cc where the host patch load uses I2Ct.
8. Bus sharing. The updater is synchronous, so it cannot interleave with
   `TPS25751DownstreamDevice` calls from the same thread. The application must not issue
   I2Cr or I2Cw from an interrupt or another task during an update. The TRM states the
   5-second spacing only for I2Cr and I2Cw; nothing on pages 81 and 82 applies it to the
   flash tasks.

Verified in stage 2 on 2026-11-23: `FLwd` honors a 4-byte length exactly.

Verified in stage 3 on 2026-11-23: a zero Low pointer sets Region 0 Invalid in
BOOT_FLAGS at the next boot, and the controller returns to APP 1.94 s after `GAID`.

### Notes from the Batch 1 review (2026-11-22)

The Batch 1 reviewer read TRM pages 61, 62, 81, and 82 and the app note against this
section and found no contradiction. It found three places where the text above is less
precise than the TRM. None changes Batch 1 code. The first needs a decision before
`setWriteAddress()` is written in Batch 2.

1. Table 4-1 (page 61) puts the standard result, TaskResult, in bits 3:0 of the return
   byte. Bits 7:4 "may be used by certain Tasks", and a successful task can use them
   provided TaskResult is 0x0. "Require return code 0" above compares the whole byte.
   That can report a false failure and never a false success. Open: compare the whole
   byte, or bits 3:0 only.
2. Table 4-21 (page 82) gives `FLwd` its own ReturnCode with two values, 0x00 and 0xFF.
   It does not refer to Table 4-1, so no documented code produces `WriteFailed`. The
   status stays as a defensive branch for any other value.
3. The TRM states the address byte order only for `FLad` (Table 4-20). Little-endian
   addresses for `FLrd` and `FLvy`, and `FLrd` returning the byte at the address in DATA
   byte 0, are inferred from Table 4-20, from "Memory contents (little-endian)" in
   Table 4-19, and from the app note's source on page 12. Stage 1 settles both: the
   pointers must read 0x00000800 and 0x00004400, and the dump must equal the image.
   Verified in stage 1 on 2026-11-22 for `FLrd`: both pointers read correctly and all
   1984 reads returned the image's bytes in order. For `FLvy` the evidence is indirect:
   it reported both bundles valid at little-endian 0x0800 and 0x4400, and a byte-swapped
   address would lie far outside the 32 KB part.

Other points recorded during Batch 1:

- Register 0x2D is BOOT_FLAGS in the TRM (page 33) and, since `c092f80` on `main`, in
  the library (`Address::BOOT_FLAGS`). The app note calls it BOOT_STATUS. Every
  "BOOT_STATUS" in this plan means register 0x2D; the code and the example print
  BOOT_FLAGS.
- TRM Table 4-3 (page 62) describes `GAID` as a cold restart in its Description and
  Side Effects rows, and as "a warm restart" in its Additional Information row, which
  repeats the `Gaid` text of Table 4-2. Stage 3 records what the controller does.
- App note Figure 2-3 gives OldRegStart = 0x0400 on the High-region branch. The layout
  on the same page and Figure 2-6 put the low bundle at 0x0800. The library uses the
  layout table above, not the figure's value.
- The status table has no value for an address outside the EEPROM. `readFlash()` and
  `dump()` document that the caller owns the range. Open for Batch 2: whether
  `setWriteAddress()` and the pointer writes need an out-of-range status.
- The fake executor models behaviour the TRM does not describe: `FLrd` past the end of
  the part, `FLad` and `FLwd` rejecting with 0x03, `FLwd` writing exactly the given
  length, and `FLvy` checking only Header_ID. Each is labelled as an assumption in
  `test/support/native/FakeCommandExecutor.h`. Batch 2 tests must not depend on `FLwd`
  returning 0x03.

### Notes from the Batch 2 review (2026-11-23)

The Batch 2 reviewer read TRM pages 60 to 62 and 81 to 82 and the whole app note
against the write path and found no contradiction with this plan. Two statements in
the documents were not carried here before:

- Redundancy is one-directional. The app note (page 2, Boot Process) says that after a
  correct Header_ID in the Low region, a CRC error while reading the Low bundle ends
  the load, and the controller does not try High. A High CRC error does fall back to
  Low. A valid Low pointer over a damaged Low body therefore does not boot, even with
  High intact. The flows restore a pointer only after the `FLrd` compare, so reaching
  that state needs `compareAfterWrite = false` or later corruption.
- DATA after a task is task-defined. TRM page 61 says the controller does not modify
  DATAx after CMDx reads 0, and that the bytes after the return code are used at each
  task's discretion. `executeCommand()` writes exactly the given number of bytes to
  DATA, as the app note's own source does for the 4-byte pointer `FLwd`.

Decisions taken on 2026-11-23:

1. Return codes compare the whole byte, as written above ("require return code 0").
   This can fail falsely and cannot succeed falsely. `FLwd` treats 0x00 as written,
   0xFF as busy, and any other value as `WriteFailed`.
2. No status is added for an address outside the EEPROM. `setWriteAddress()`
   documents that the caller owns the range.
3. Step 2 of `updateRegion()` reads back the first two chunks before it sends the
   rest. The updateRegion section has the sequence and the reason.
4. Stage 2 gains a second pointer test, `p2`, because the neighbor check cannot show
   what it was meant to show. The writeRegionPointer section explains the limit.
5. `coldReset()` stays as written until stage 3 measures what COMMAND and MODE read
   during the restart. The coldReset section states the limit.
6. Reading the AppConfigOffset word in pre-flight is deferred to Batch 3. A valid
   pointer means the pointer equals the bundle start, while the controller looks for
   Header_ID at the region start plus AppConfigOffset. Both words are 0 on this board
   (stage 1 dump), so the gap affects only other boards.

The example's serial commands, which the hardware stages use: `r` read-only report,
`p` and `p2` pointer tests, `u` update the inactive region, `b` update both regions,
`x` cold reset, `c1` to `c4` cut an update after step 1 to 4 of Figure 2-3, and `v`
the `FLvy` corruption experiment. Each command that writes prints its plan and waits
for the line `yes`, sent after the prompt; input queued before the prompt is
discarded. A timing table per 4CC task follows each command, with counts of nonzero
return codes and of `FLwd` busy results.

## Reused as-is

- `TPS25751::executeCommand()` and `TPS25751FourCC`, `TPS25751TaskStatus`,
  `TPS25751TaskResult` (ADR-007).
- `TPS25751::readMode()`, `readBootFlags()`, and `readVersionRegister()`.
- `TPS25751BootFlags`: `region0Invalid()`, `region0EepromError()`, `i2cEepromPresent()`,
  and the Patch Config Source field.
- `TPS25751PatchLoader` for the PTCH to APP step of blank programming (ADR-011).
- `TPS_DEBUG_*` with `DEBUG_CAT_TASK`.

## Design

### Test seam: `TPS25751CommandExecutor`

`src/TPS25751.cpp` is not in the native build, because it needs `Wire`. The updater
therefore depends on a small abstract interface, not on `TPS25751`:

```cpp
// include/TPS25751CommandExecutor.h (no Wire.h)
class TPS25751CommandExecutor {
public:
    virtual ~TPS25751CommandExecutor() = default;
    virtual TPS25751TaskResult executeCommand(TPS25751FourCC cmd,
        const uint8_t* inData, size_t inLen,
        uint8_t* outData, size_t outLen, uint32_t timeoutMs) = 0;
    virtual bool readMode(TPS25751Mode& mode) = 0;
    virtual bool readBootFlags(TPS25751BootFlags& flags) = 0;
    virtual void delayMs(uint32_t ms) = 0;
    virtual uint32_t nowMs() = 0;
};
```

`TPS25751HostExecutor` (`include/TPS25751HostExecutor.h`, `src/TPS25751HostExecutor.cpp`)
adapts a `TPS25751&` to the interface and is excluded from the native build like the
other I2C-facing sources. `TPS25751` itself is unchanged. Virtual functions need no
RTTI, so the `-fno-rtti` constraint holds. `TPS25751EepromUpdater.cpp` includes only the
interface, the task types, and the register classes, so it compiles natively and is
measured by `test/coverage.sh`.

The patch loader and the downstream proxy could move to the same seam later. That is
out of scope here.

### Class: `TPS25751EepromUpdater`

Not a register class, not in the factory, in the manner of ADR-011. Files:
`include/TPS25751EepromUpdater.h`, `src/TPS25751EepromUpdater.cpp`. `TPS25751.h` includes
both new headers at its end, as it does for the patch loader.

```cpp
enum class EepromRegion : uint8_t { Low = 0, High = 1 };

struct EepromUpdateOptions {
    bool compareAfterWrite = true;   // FLrd compare before the pointer is written
    bool invalidateOldRegion = true; // step 4 of the app note
};

class TPS25751EepromUpdater {
public:
    explicit TPS25751EepromUpdater(TPS25751CommandExecutor& exec,
                                   uint32_t eepromSizeBytes = 32768);

    // Task primitives
    EepromUpdateStatus readFlash(uint32_t addr, uint8_t out[16]);
    EepromUpdateStatus setWriteAddress(uint32_t addr);
    EepromUpdateStatus writeData(const uint8_t* data, size_t len);   // len <= 32
    EepromUpdateStatus verifyBundle(uint32_t addr);

    // Region pointers
    EepromUpdateStatus readRegionPointer(EepromRegion r, uint32_t& value);
    EepromUpdateStatus writeRegionPointer(EepromRegion r, uint32_t value);

    // Flows
    EepromUpdateStatus selectTargetRegion(EepromRegion& target);
    EepromUpdateStatus updateRegion(EepromRegion target, const uint8_t* image,
                                    size_t length, EepromUpdateOptions opt = {});
    EepromUpdateStatus updateInactiveRegion(const uint8_t* image, size_t length);
    EepromUpdateStatus updateBothRegions(const uint8_t* image, size_t length);
    EepromUpdateStatus programBlank(const uint8_t* image, size_t length);

    // Read-back
    EepromUpdateStatus compareRegion(EepromRegion r, const uint8_t* image,
                                     size_t length, uint32_t* firstMismatch = nullptr);
    EepromUpdateStatus dump(uint32_t addr, size_t length, Stream& out);

    // Reset
    EepromUpdateStatus coldReset(uint32_t waitForAppMs = 5000);

    static EepromUpdateStatus checkImage(const uint8_t* image, size_t length,
                                         uint32_t regionCapacity);
    static uint32_t imageVersion(const uint8_t* image, size_t length);
    static const char* toString(EepromUpdateStatus s);
};
```

Image input is a pointer and length, as `loadPatchBundle()` takes. A 14 KB bundle fits
in Teensy flash, and the compare step needs random access to the image. A chunk-provider
callback for SD or serial sources can be added later without changing these signatures.

An optional progress callback (`void (*)(uint32_t done, uint32_t total, void* ctx)`) is
set through a setter, so the example can print progress during the 10 to 30 s of work.

### `writeRegionPointer()`

Follows Figure 2-5 of the app note with one added check:

1. `FLrd` the 16 bytes at the pointer address and keep bytes 4 to 15.
2. `FLad` to the pointer address; require return code 0.
3. `FLwd` the 4-byte little-endian value; require return code 0.
4. `FLrd` again. Bytes 0 to 3 must equal the value, and bytes 4 to 15 must equal the
   bytes kept in step 1.

Step 4's neighbor check detects a controller that writes more than the four bytes
given, which the documents do not rule out.

Correction, 2026-11-23: the neighbor check is weaker than the sentence above claims.
Step 1's `FLrd` leaves the neighbors' own values in DATA bytes 4 to 15, so a
controller that wrote up to 16 bytes out of DATA would write them back unchanged and
pass the check. A write past byte 15 is outside the 16 bytes read. The check still
detects a write of different bytes into bytes 4 to 15. Two measures cover the gap in
stage 2: the example's `p` prints 64 bytes at the pointer before and after each write,
and `p2` loads DATA with the 16 bytes at 0x4400 first, sets the write address to
0x0400, prints the DATA register, and writes the four bytes the High pointer already
holds, so that a longer write changes bytes after the pointer and shows in the 64-byte
print. The pointer's value never changes in `p2`. A pointer write does not retry a
busy `FLwd`; busy there fails the step, and the other region still boots.

### `updateRegion()`

Pre-flight, before any write:

1. MODE reads APP.
2. BOOT_STATUS reports I2C EEPROM Present.
3. `checkImage()`: the image starts with 0xACE00001 and its length is at least 0x38
   bytes and at most the region capacity (0x3C00 for both regions at the default size).
4. The other region's pointer equals its bundle start. Step 1 below is safe only when
   the region being kept is bootable.

Then the four steps of Figure 2-3:

1. `writeRegionPointer(target, 0)`.
2. `FLad` to the target bundle start, then `FLwd` in 32-byte chunks with the busy retry.
3. `FLvy` at the target bundle start; then, when `compareAfterWrite` is set,
   `compareRegion()`; then `writeRegionPointer(target, bundleStart)`.
4. When `invalidateOldRegion` is set, `writeRegionPointer(other, 0)`.

Change to step 2, approved on 2026-11-23. Step 2 as first written trusts the
controller's write address for all 448 `FLwd` calls. If `FLad` reported success
without taking effect, the bundle would land just past the pointer word that step 1
wrote, and at the real 14336-byte length it would overwrite the other region's bundle
or pointer. No document suggests the controller does this; the Batch 2 review
reproduced the outcome against the fake. Step 2 is now:

1. Before any task: if bytes 4 to 15 of step 1's read-back equal the first 12 bytes of
   the image, return `PointerNeighborsChanged`. Those bytes are the trace of an earlier
   misplaced write, which would otherwise become the reference for check 5 and hide a
   repeat of the same failure. The EEPROM must be dumped and repaired before another
   update.
2. `FLad` to the target bundle start, then the first two chunks.
3. `FLrd` the 16 bytes at the target's pointer address.
4. `FLrd` the first two chunks back and compare them with the image. A difference
   returns `CompareMismatch`.
5. The pointer row read in 3 must still hold a 0 word, and bytes 4 to 15 must equal
   what step 1's read-back saw. A difference returns `PointerNeighborsChanged`. This
   check exists because check 4 is blind when the target already holds the same image.
6. A fresh `FLad` to the next unwritten byte, then the remaining chunks. The documents
   do not say whether `FLrd` disturbs the controller's write address, and the busy
   retry already uses the same pattern.

The pointer row is read before the bundle start, so the last `FLrd` before the second
`FLad` is inside the target region. If an `FLrd` moved the write address and the second
`FLad` also failed silently, the rest of the bundle would then start inside the target
region, not at a pointer word.

For a write address that is wrong from the start, this bounds the damage to the first
two chunks. In the modelled case, where step 1 left the write address four bytes past
the pointer word, those are the 64 unused bytes after it. On a 24LC256 a 32-byte write
that starts 36 bytes into a 64-byte page can wrap onto the pointer word unless the
controller splits the write; check 5 tests the word, so the update still stops.
Residual limits: the second `FLad` is not verified; a write address that goes wrong
after the read-back is not seen before step 3, and step 3 checks only the target
region; when the target already held the same image, two chunks misplaced anywhere
other than the pointer row go unseen. Whether an `FLrd` moves the write address is
unknown, and no hardware stage shows it. A 14336-byte update with no retries issues 5
`FLad`, 451 `FLwd`, 908 `FLrd`, and 1 `FLvy`.

A stop in step 2 with `CompareMismatch` or `PointerNeighborsChanged` is final for the
hardware stages: dump the EEPROM and find the misplaced bytes before any further
write. The example prints a line saying so, and its `v` command runs the same checks.

The progress callback is called with `done == 0` after step 1 and before any step 2
task, then after each successful chunk up to `done == total`, and in the same way
during the compare of step 3. `lastStep()` tells the phases apart.

### Region selection

`selectTargetRegion()` reads both pointers and BOOT_STATUS. A pointer is valid when it
equals that region's bundle start.

| Low pointer | High pointer | Target |
|---|---|---|
| Valid | Valid | Figure 2-2: Low if `region0Invalid()` or `region0EepromError()`, otherwise High |
| Valid | Not valid | High |
| Not valid | Valid | Low |
| Not valid | Not valid | None: returns `NoBootableRegion`; use `programBlank()` |

`updateInactiveRegion()` is `selectTargetRegion()` followed by `updateRegion()`.

### `updateBothRegions()`

Runs `updateInactiveRegion()`, then `updateRegion()` on the other region with
`invalidateOldRegion = false`. The result is the full-image layout of Figure 2-6: the
new bundle in both regions and both pointers valid. Following the app note's flow
literally for the second pass would always leave one pointer erased; keeping both
valid is an inference from Figure 2-6, which the app note presents as the normal
initial state.

### `programBlank()`

For an EEPROM with no valid pointer, with the controller already in APP mode (for
example after `TPS25751PatchLoader::loadPatchBundle()`):

1. Pre-flight as above, except that check 4 is replaced by "no pointer is valid", so
   the call cannot be used to overwrite a working EEPROM by mistake.
2. For Low, then High: write the pointer to 0, write the AppConfigOffset word to 0,
   write the bundle, `FLvy`, compare, write the pointer to the bundle start.

Writing the AppConfigOffset words uses the same four-byte write and neighbor check as
the pointers. Whether BOOT_STATUS reports I2C EEPROM Present for a blank device after a
PBM boot is inferred; stage 5 confirms it. If it does not, pre-flight check 2 is dropped
for this call only.

### `coldReset()`

Kept separate from the update calls, so the application chooses when the port
disconnects. It issues `GAID`, waits 100 ms, then polls MODE every 50 ms until APP or
the timeout, tolerating read failures while the controller reboots. The caller then
reads VERSION and BOOT_STATUS to confirm the new bundle; the example does this and
prints the Patch Config Source and region flags.

Limit, recorded 2026-11-23: `coldReset()` can return `Success` without a restart. TRM
Table 4-3 says an APP-mode controller restarts after a typical 1-second Error Recovery
delay, and the sequence above accepts a MODE read of APP 100 ms after `GAID`. The same
holds when the executor times out with COMMAND never clearing, and when `GAID` never
reached the controller. The executor timeout for `GAID` is 3000 ms, so the COMMAND
poll outlasts the delay if COMMAND keeps reading `GAID` until the restart. Stage 3
judges the reset by VERSION and BOOT_FLAGS changing and by the `GAID` row of the
timing table, not by the returned status, and the rule is then set from that
measurement.

Measurement, stage 3, 2026-11-23, two resets with the same result. COMMAND kept
reading nonzero for 1277 ms after `GAID`, then the controller stopped acknowledging
I2C: the executor's COMMAND poll failed and returned `I2CError`, and the MODE read
100 ms later was not acknowledged either. MODE read APP 1938 ms after `GAID`. On this
board the restart is therefore visible as a failed I2C transaction, and the cases
feared above, a `Timeout` with COMMAND never clearing and a `Success` before the
restart, did not occur. Open: whether `coldReset()` should require an observed I2C
failure, from the executor or from a MODE read, before it accepts APP, and return
`ResetTimeout` otherwise.

### Status values and the state each leaves

| `EepromUpdateStatus` | Raised by | EEPROM state afterwards |
|---|---|---|
| `Success` | All | As requested |
| `NotInAppMode` | Pre-flight | Unchanged |
| `NoEepromPresent` | Pre-flight | Unchanged |
| `BadImageHeader`, `BadImageLength` | `checkImage()` | Unchanged |
| `NoBootableRegion` | Selection, pre-flight | Unchanged |
| `EepromNotBlank` | `programBlank()` pre-flight | Unchanged |
| `PointerEraseFailed` | Step 1 | Target pointer uncertain; other region still boots (Figure 2-6 or 2-7) |
| `SetAddressFailed` | `FLad`, including the second `FLad` of step 2 and the `FLad` of a busy retry | Target pointer 0; other region boots (Figure 2-7) |
| `WriteFailed`, `FlashBusy` | `FLwd` | Target region partly written, pointer 0; other region boots |
| `VerifyFailed` | `FLvy` | Target written but not valid, pointer 0; other region boots (Figure 2-8) |
| `CompareMismatch` | `compareRegion()` in step 3; the read-back of the first two chunks in step 2 | Step 3: same as `VerifyFailed`. Step 2: at most two chunks written, pointer 0; other region boots |
| `PointerRestoreFailed` | Step 3 pointer write | Target pointer uncertain; other region boots |
| `OldPointerEraseFailed` | Step 4 | Both pointers may be valid; low boots first (Figure 2-9) |
| `PointerNeighborsChanged` | Neighbor check of a pointer write; the two pointer-row checks in step 2 | Bytes 4 to 15 after a pointer changed; stop and dump. From step 2: the first chunks landed in the target's pointer row, or the trace of an earlier misplaced write was found there before any bundle write; other region boots |
| `ResetTimeout` | `coldReset()` | Unchanged; controller did not return to APP |
| `CommandRejected`, `CommandTimeout`, `I2cError` | Executor | Depends on the step; `lastStep()` reports it |

`lastStep()` returns an enum naming the step in progress, so a caller can map an
executor failure to the table above.

## Batches

Each batch ends with a green `pio test -e native` and `pio run -e example-read-registers`.

### Before Batch 1: worktree setup

The implementation runs in a git worktree on branch `feat/eeprom-update`, not in the
main checkout, so `main`'s working tree stays clean for other work and for
`scripts/integrate.py`. Do these steps before any code is written:

1. Create the worktree from the main checkout, as a sibling directory:

   ```bash
   git worktree add -b feat/eeprom-update ../TPS25751-eeprom-update main
   ```

   The `.mcp.json` servers resolve `../device-register-mcp-server` relative to the
   checkout, so a sibling directory under `lib/` keeps them working.
2. Move this plan and the planning prompt, which are untracked in the main checkout,
   into the worktree at the same paths
   (`docs/plans/feature/eeprom-update.md` and
   `docs/plans/feature/eeprom-update-planning-prompt.md`). Move them; do not leave
   copies behind in the main checkout.
3. In the worktree, commit both files as the branch's first commit, with a
   `docs(plan)` subject.
4. Copy the reference PDFs from the main checkout's `docs/` into the worktree's
   `docs/`. `.gitignore` excludes `docs/*.pdf`, so a new worktree has none of them, and
   the app note and the TRM are both needed there:

   ```bash
   cp docs/*.pdf ../TPS25751-eeprom-update/docs/
   ```

   Run it from the main checkout. The copies stay untracked and ignored.

All later work, including `scripts/verify.py`, runs in the worktree.
`scripts/integrate.py feat/eeprom-update` runs from the main checkout, and only after I
confirm the branch is finished.

### Sessions, models, and effort

One orchestrator session owns the plan, the worktree, the reviews, and the hardware
gates. It delegates each batch to one implementation subagent working in the worktree,
and each review to a separate subagent that has not seen the implementer's reasoning.

| Role | Model | Effort | Reason |
|---|---|---|---|
| Orchestrator | Fable 5.2 | high | Holds the plan across batches, judges hardware results against the status table, and decides when a gate is met |
| Batch 1 implementer | Opus 5.5 | high | Well-specified interface, primitives, and fake; nothing writes the EEPROM |
| Batch 2 implementer | Fable 5.2 | xhigh | Pointer writes and the update flows; an ordering error here can leave no bootable region |
| Batch 3 implementer | Opus 5.5 | high | `programBlank()` reuses Batch 2's verified primitives; the rest is documentation |
| Reviewer, each batch | Fable 5.2 | high (xhigh for Batch 2) | Independent read of the diff against this plan and `CODE_REVIEW_GUIDELINES.md` |

The orchestrator does not write batch code itself. It runs `scripts/verify.py` after
each review passes, then stops for the batch's hardware stage. Hardware stages that
write the EEPROM (2 to 5) start only on my explicit go-ahead for that stage.

### Batch 1: seam, primitives, and read-back (no EEPROM writes)

- `TPS25751CommandExecutor`, `TPS25751HostExecutor`.
- `TPS25751EepromUpdater` with `readFlash()`, `readRegionPointer()`,
  `selectTargetRegion()`, `compareRegion()`, `dump()`, `verifyBundle()`, `checkImage()`,
  `imageVersion()`, `toString()`.
- Native test support: `FakeCommandExecutor`, a scripted fake that models a byte-array
  EEPROM, the `FLad` address register with auto-increment, MODE, BOOT_STATUS, and
  injectable faults per call.
- Example `examples/eeprom-update` in read-only form: prints MODE, VERSION, BOOT_STATUS,
  both pointers, the selected target, the `FLvy` result for each region, and a hex dump.
- Confirm the 24LC256 page size and write time from the Microchip datasheet.
- Gate: hardware stage 1.

Status, 2026-11-22: implemented, reviewed, and passed hardware stage 1 (results under
Hardware validation). The native suite has
49 tests and `src/TPS25751EepromUpdater.cpp` has 100% line coverage (162 of 162 lines).
Two `platformio.ini` entries that Batch 3 lists landed here, because the Batch 1 tests
and the stage 1 build need them: `+<TPS25751EepromUpdater.cpp>` in `[env:native]` and
`[env:example-eeprom-update]`. The example reruns its read-only report when it
receives `r` over serial, so a monitor attached late still captures a full dump.

### Batch 2: region update and reset

- `setWriteAddress()`, `writeData()`, `writeRegionPointer()`, `updateRegion()`,
  `updateInactiveRegion()`, `updateBothRegions()`, `coldReset()`, progress callback.
- Example gains the update path: it prints the plan of action (target region, image
  version, current VERSION) and waits for the user to type `yes` over serial before the
  first write. It ships a placeholder `patch_bundle.h`, like `load-patch-bundle`, and
  refuses to run while the placeholder is in place, because `checkImage()` rejects it.
- `scripts/bin_to_header.py`: converts a `lowRegion.bin` to the example's header as a
  `const uint8_t` array, with a test under `scripts/tests/`. The TI `.c` files use
  `const char` and a `tps25750x_` name, so a generated header is cleaner than editing
  them. `scripts/` is not in the published package.
- Gate: hardware stages 2, 3, and 4.

Status, 2026-11-23: implemented and reviewed clean, including the step 2 read-back and
`p2`; stages 2, 3, and 4 passed (results under Hardware validation), stage 4 with
`GAID` resets in place of power cycles. After stage 4 the EEPROM is identical to the
F8091166 full-flash image and the controller runs that bundle from the Low region.
`src/TPS25751EepromUpdater.cpp` has 100% line coverage (432 of 432 lines) and the
updater suite has 131 tests. Two review nits are carried into Batch 3:

- The `p2` pass line counts the DATA positions that could show a longer write but not
  where they start. If `FLad` left DATA bytes 4 to 15 equal to the EEPROM bytes, a
  16-byte write would pass. Until the line names the first such position, read the
  DATA dump in the `p2` log and confirm that position 4 differs.
- One sentence of the `updateRegion()` Doxygen limits paragraph predates the trace
  check and should be limited to offsets that check does not read.

### Batch 3: blank programming and documentation

- `programBlank()` and the example's recovery path, which runs
  `TPS25751PatchLoader::loadPatchBundle()` when MODE reads PTCH and then offers
  `programBlank()`.
- Documentation: a new ADR-014 in `ARCHITECTURE.md` (non-register sequencer, executor
  seam, pointer-first region selection, compare before pointer write); a "Flash tasks
  and EEPROM update" subsection in `CONSTRAINTS.md` with the result offsets, the 32-byte
  chunk, the `FLvy` limit, and the stale BOOT_STATUS trap; `TESTING.md` for the fake and
  the newly measured file; `examples/README.md`; `AGENTS.md`; Doxygen.
- `platformio.ini`: `[env:example-eeprom-update]`, and the two new native sources in
  `[env:native]`.
- Gate: hardware stage 5.

Status, 2026-11-23: implemented; the review found no blocker and its findings are
fixed; a re-review and stage 5 are pending, and `programBlank()` has not run on
hardware. `src/TPS25751EepromUpdater.cpp` has 100% line coverage (487 of 487 lines) and
the updater suite has 146 tests. The example gained `l` (a PBM load while MODE reads
PTCH; it loads the controller's RAM only) and `pb` (`programBlank()`); `r` now prints
both AppConfigOffset words.

Notes from the Batch 3 review, 2026-11-23. The reviewer read the app note in full and
TRM pages 33, 60 to 62, and 79 to 82, and found no contradiction with this plan. Open
points, none decided:

1. Step order of `programBlank()`. The programBlank section orders each pass as
   pointer, AppConfigOffset word, bundle. The last write before the bundle `FLad` is
   then at the AppConfigOffset word, not at the pointer, so the pointer-row check of
   step 2 looks in the wrong place for chunks misplaced by an `FLad` that reports
   success without taking effect. The reviewer swept a no-effect `FLad` at each of the
   10 `FLad` calls against the fake, from three start states, with and without an
   `FLrd` that moves the write address. A bad outcome is a valid pointer over a damaged
   region.

   | Order | `FLad` without effect | Also an `FLrd` that moves the write address |
   |---|---|---|
   | As planned: pointer, word, bundle, per pass | No bad outcome | 6 bad runs, 3 returning Success |
   | Word, pointer, bundle, per pass | 3 bad runs, 2 returning Success: the High pointer erase follows the write at 0x07FC, so four zero bytes land on the Low Header_ID at 0x0800 | 3 further bad runs |
   | Both words first (Low, then High), then each pass as steps 1 to 3 of `updateRegion()` | No bad outcome | No bad outcome |

   Costs of writing both words first: a failure writing the High word ends the call
   before Low is bootable, and each word's read-back is one pass older when its pointer
   is written, which one more `FLrd` before the pointer write closes. On a blank
   EEPROM, where stage 5 starts, every single-fault case is detected in either order.
   Neither hypothetical fault has been seen in stages 1 to 4. Stage 5 verifies the
   exact task sequence, so the order is best settled before it.
2. A dead end under the planned order. A stop in the first tasks of the High pass
   leaves the Low pointer valid and the High AppConfigOffset word still 0xFFFFFFFF on
   an erased part. `pb` then refuses because a pointer is valid, and `u` and `b` do not
   write that word, so no example command reaches the full-flash layout. That the
   controller does not boot High in that state is inferred from the app note (page 2),
   not observed. Writing both words first removes the state. The example now prints
   both words and warns against `u` and `b` while one is not 0.
3. The AppConfigOffset pre-flight of `updateRegion()` (decision 6 of 2026-11-23) was
   deferred because both words are 0 on this board. Point 2 is a state on this board
   where they are not.
4. `coldReset()` requiring an observed I2C failure before it accepts APP (see the
   coldReset section).
5. How stage 5 reaches an EEPROM with no valid pointer: `minipro` after a teardown, as
   written, or an example command that writes both pointers to 0. The command tests a
   corrupt EEPROM, not a blank one, and would ship in the example.
6. The app note (page 13) says the PBM-then-flash recovery needs a host that can run
   while the controller sits in PTCH; a system that needs the sink path before it can
   boot must select a suitable dead-battery configuration through ADCINx. The board
   under test runs from its pack, so this does not affect the stages.

Stage 5 should also read 0x03C0 to 0x03FF and 0x07C0 to 0x07FF in its dump. The two
AppConfigOffset writes are the first `FLwd` calls that start in the middle of a page,
and an over-long write there would wrap to the start of the 24LC256 page, outside the
16 bytes the read-back covers.

## Tests

`test/test_native/test_TPS25751EepromUpdater`, built on `FakeCommandExecutor`. Target:
above 90% line coverage of `TPS25751EepromUpdater.cpp` in `test/coverage.sh`.

- Each primitive: exact 4CC, exact DATA payload, little-endian address bytes, timeout
  passed, result decoding (`FLrd` data from byte 0; return code for the others).
- `writeRegionPointer()`: the four-call sequence, a read-back mismatch, and a changed
  neighbor byte.
- `selectTargetRegion()`: all four rows of the table, both BOOT_STATUS branches of the
  first row, and the stale BOOT_STATUS case (flags say low is healthy, low pointer is 0).
- `updateRegion()` for Low and for High: the full call sequence, 448 chunks for the real
  length, a short final chunk for a length that is not a multiple of 32, the fake
  EEPROM's final contents, and both option flags.
- Every status in the table, by fault injection at each step, with the fake EEPROM's
  state asserted to match the "state afterwards" column.
- `FLwd` busy: success on a retry (with the fresh `FLad`) and failure after three.
- Pre-flight: PTCH mode, no EEPROM, bad header, short image, oversized image, other
  region not bootable.
- `updateBothRegions()`: both pointers valid at the end, both regions equal the image.
- `programBlank()`: refused on a non-blank EEPROM; on a blank one the result equals
  `..._fullFlash.bin`'s layout (pointers, zero AppConfigOffset words, two bundles).
- `coldReset()`: executor returns `Timeout` or `I2CError` and MODE later reads APP;
  MODE never returns.
- `checkImage()` and `imageVersion()` as pure functions.

`TPS25751HostExecutor` is a pass-through and stays outside the native build, like the
other I2C-facing sources.

## Hardware validation

Ordered from safe to destructive. Each stage states its recovery.

1. Read-only. Read MODE, VERSION, and BOOT_STATUS; `FLrd` both pointers; dump 0x0000 to
   0x7C00 through `FLrd` and compare byte for byte with `..._fullFlash.bin`; run `FLvy`
   on both bundle starts. Confirms result offsets and the layout. No recovery needed.

   Result, 2026-11-22, branch commit `143a663`, `examples/eeprom-update` on the Teensy
   4.0, board on the 18650 pack: passed.

   | Item | Reading |
   |---|---|
   | MODE | `APP ` |
   | VERSION (0x0F) | 0xF8091166 |
   | BOOT_FLAGS (0x2D) raw | `18 00 F0 A2 C1` |
   | Patch Config Source | EEPROM Loaded |
   | I2C EEPROM Present | 1 |
   | Region 0 Attempted, Region 1 Attempted | 1, 0 |
   | Region Invalid, EEPROM Error, CRC Fail (both regions), Patch Header Error | all 0 |
   | Low pointer at 0x0000 | 0x00000800, valid |
   | High pointer at 0x0400 | 0x00004400, valid |
   | `selectTargetRegion()` | High |
   | `FLvy` at 0x0800 and at 0x4400 | return code 0x00 (valid) for both |
   | Dump 0x0000 to 0x7C00 | 1984 lines, 31744 bytes, identical to `..._F8091166_fullFlash.bin` (`cmp`, and SHA-256 `a3e549ca...3096fdaec3` for both) |
   | Dump time | 35071 ms for 1984 `FLrd` calls, 17.7 ms per call |

   Verified by this stage: the EEPROM layout on the board, `FLrd` data in DATA bytes 0
   to 15, the little-endian `FLrd` address, `FLvy` reporting 0x00 in the return code for
   a valid bundle, and the 200 ms `FLrd` timeout. Not shown by this stage: any `FLvy`
   failure code, `FLrd` behaviour when the EEPROM does not answer (Table 4-19 has no
   return code), and whether a BQ25798 interrupt was deferred while the flash tasks made
   the controller ignore I2Cc_IRQ.

   Independent read, 2026-11-23, read-only, run in a fork of the orchestrator session
   at my request and not rerun since. A scratch sketch read the 24LC256 at 0x50
   directly on the Teensy's `Wire2`, which is the controller's I2Cc bus, at 100 kHz
   while the controller ran in APP. The probe was acknowledged. The dump of 0x0000 to
   0x7C00 (1984 lines, 31744 bytes) is byte-identical to
   `..._F8091166_fullFlash.bin`; every 16 bytes were read twice with 0 retries and 0
   mismatches, in 7246 ms. The sketch sends only the two address bytes of a random
   read. Together with the `FLrd` dump above, this verifies that `FLrd` returns the
   chip's contents and not a controller-side copy, which the read-back and the compare
   rely on. One clean read says nothing about direct writes on a bus the controller
   also masters.
2. First write: `writeRegionPointer(High, 0)` while the low pointer is valid, then
   restore it to 0x4400. Confirms that `FLwd` writes exactly four bytes (neighbor check)
   and the read-back. This is step 1 of the app note's own flow, so the controller
   still boots from the low region if it fails. Recovery: `write-flash.zsh`.

   Procedure, set on 2026-11-23: run `p`, then `p2`, then `r`, and compare the dump
   with `..._F8091166_fullFlash.bin`. `p` shows the read-back and the erase and restore
   of the pointer. `p2` shows whether `FLwd` writes more than the four bytes given,
   which `p` cannot show; its only passing result is the line `[p2] Verdict: FLwd
   wrote only the four bytes given.`, and it stops before the write when the DATA
   register holds nothing that could show a longer write. The full dump shows that
   nothing else changed.

   Result, 2026-11-23, branch commit `8f599d5`, `examples/eeprom-update` built with the
   F8091166 low-region image (14336 bytes, SHA-256 `60c6cdd8...add77772c`), Teensy 4.0:
   passed. Which supply powered the board was not recorded.

   | Step | Result |
   |---|---|
   | `p`, erase | `writeRegionPointer(High, 0)` returned Success; 0x0400 read `00 00 00 00`; bytes 0x0404 to 0x043F unchanged |
   | `p`, restore | `writeRegionPointer(High, 0x4400)` returned Success; 0x0400 read `00 44 00 00`; all 64 bytes equal the bytes before the test |
   | `p2`, DATA after `FLad`, before `FLwd` | `00 04 00 00 fe ff ff ff 80 06 00 00 80 31 00 00`, then 48 bytes of 0x00 |
   | `p2`, positions that could show a longer write | 57 of 60, starting at position 4 (DATA 0xFE against 0xFF in the EEPROM) |
   | `p2`, write | `writeData(00 44 00 00)` returned Success; 0x0400 read `00 44 00 00`; bytes 0x0404 to 0x043F unchanged |
   | `p2`, verdict | `FLwd` wrote only the four bytes given |
   | `r` after both tests | Both pointers valid, `FLvy` valid for both bundles, BOOT_FLAGS raw `18 00 F0 A2 C1` as in stage 1 |
   | Dump after both tests | 1984 lines, 31744 bytes, identical to `..._F8091166_fullFlash.bin` and to the stage 1 dump |
   | Task results | 3 `FLad` and 3 `FLwd`, all with return code 0x00; no busy result; 17.7 to 17.8 ms per call |

   Verified by this stage: `FLad` and `FLwd` report 0x00 in the return code on success;
   a 4-byte `FLwd` writes exactly four bytes; the pointer write, its read-back, and the
   neighbor check work on the device; the pointer can be erased and restored.

   The DATA printout also settles two points the documents leave open. `FLad` leaves
   DATA bytes 4 to 63 as they were: bytes 4 to 15 still held the bytes of the preceding
   `FLrd`. A 4-byte host write does not clear the rest of DATA. This confirms that the
   blind spot of the neighbor check was real, because in `writeRegionPointer()` those
   DATA bytes hold the neighbors' own values, and that it does not matter on this
   controller, because `FLwd` takes only the bytes of the host's write.

   Not shown by this stage: any failure code, a busy `FLwd`, the auto-increment of the
   write address across chunks, and whether an `FLrd` moves the write address.
3. Single-region update to the `F8091164` bundle, with timing recorded per task. Then
   `coldReset()`; VERSION must read 0xF8091164 and BOOT_STATUS must show the high region
   used. Record BOOT_STATUS with the low pointer erased, and the time to APP. Before the
   pointer write of a second run, corrupt one byte of the written region and record
   whether `FLvy` reports it. Then update back to `F8091166` with `updateBothRegions()`
   and compare a full dump with `..._fullFlash.bin`. Recovery: the untouched region, then
   `write-flash.zsh`.

   Procedure notes, set on 2026-11-23. After `u` and before `x`, run `r` and diff the
   whole dump against the stage 1 dump: only the target region and its pointer word,
   and the other pointer word, may differ. Until a reset, the running controller can
   still repair the EEPROM. The first reset after `u` has no fallback inside the
   EEPROM: the old pointer is 0 by design, so a new region that did not boot would
   leave the controller in PTCH. Recovery from that state is a PBM load with the
   `load-patch-bundle` example followed by an update, or `write-flash.zsh` with the
   clip; "the untouched region" above is reachable only by one of those. Before stage
   3, check the SHA-256 that `bin_to_header.py` prints against the TI export.

   Result, 2026-11-23, branch commit `e91707c`, Teensy 4.0: passed, and the board is
   back in its starting state. Which supply powered the board was not recorded. Both
   low-region binaries were first checked against TI's C exports of the same builds
   and are equal (F8091164: 14272 bytes, SHA-256 `15019eac...6f81ec59`; F8091166:
   14336 bytes, SHA-256 `60c6cdd8...add77772c`).

   | Step | Result |
   |---|---|
   | `u` with F8091164 | Target High; Success in 25979 ms; Low pointer 0, High pointer valid |
   | `u`, tasks | `FLrd` 908 at 17.7 ms; `FLad` 5 at 17.7 ms; `FLwd` 449 at 20.7 ms (maximum 20.8 ms); `FLvy` 1 at 599 ms; every return code 0x00; no busy result |
   | `r` before any reset | Dump equals the stage 1 dump with exactly three changes: the Low pointer word 0, the High pointer word 0x4400, and the F8091164 bundle at 0x4400. No byte outside those ranges differs. The last 64 bytes of the old High bundle (0x7BC0 to 0x7BFF) remain, because the new bundle is 64 bytes shorter |
   | State before the reset | VERSION 0xF8091166 and BOOT_FLAGS `18 00 F0 A2 C1`, both stale; `selectTargetRegion()` returned Low, following the pointers |
   | `x` | `coldReset()` Success; `GAID` executor call ended with `I2CError` after 1277 ms; APP 1938 ms after `GAID` |
   | After the reset | VERSION 0xF8091164; BOOT_FLAGS `78 00 F0 A2 C1`: Patch Config Source EEPROM Loaded, Region 0 Attempted 1, Region 1 Attempted 1, Region 0 Invalid 1, all other region flags 0 |
   | `v` on Low, clean write | Image write Success in 9346 ms; `FLvy` valid; `compareRegion()` Success |
   | `v`, one byte inverted | Image offset 0x1BE5 (EEPROM 0x23E5), 0x28 to 0xD7; `FLvy` returned not valid; `compareRegion()` returned `CompareMismatch` at offset 0x1BE5; Low pointer left at 0 |
   | `b` with F8091166 | Low, then High; Success in 52049 ms; both pointers valid; `FLrd` 1818, `FLad` 9, `FLwd` 901, `FLvy` 2; no busy result |
   | `r` after `b` | Dump identical to `..._F8091166_fullFlash.bin` (31744 bytes, SHA-256 `a3e549ca...3096fdaec3`) |
   | `x` | `coldReset()` Success; `GAID` `I2CError` after 1277 ms; APP after 1938 ms; VERSION 0xF8091166; BOOT_FLAGS `18 00 F0 A2 C1`, as in stage 1 |

   Verified by this stage: a full region update on the device, including the
   auto-increment of the write address across 446 chunks and the read-back of step 2;
   the controller boots the High region when the Low pointer is 0, and reports Region
   0 Invalid; BOOT_FLAGS and VERSION stay stale until a reset, and pointer-first
   selection gives the right target in that state; `FLvy` reports a corrupted body
   byte; `updateBothRegions()` restores the full-flash layout; `GAID` restarts the
   controller, which reloads the bundle from the EEPROM.

   Not shown by this stage: a busy `FLwd`, any `FLad` or `FLwd` failure code, the exact
   `FLvy` failure code (the example prints the status, not the byte), the effect of
   the reset on the port and the PD contract, and whether an `FLrd` moves the write
   address.
4. Interrupted updates. Cut the update after each of the four steps (a debug hook in
   the example), power-cycle, and confirm the controller boots the region the status
   table predicts. Recovery: as stage 3.

   Result, 2026-11-23, branch commit `55ed165`, Teensy 4.0: passed with one deviation
   that I asked for. Each cut was followed by a `GAID` cold reset (`x`), not by a power
   cycle, because a power cycle needs the stack apart while the pack is fitted. Which
   supply powered the board was not recorded. The cuts used the F8091164 image on a
   board that ran F8091166, so VERSION shows which bundle booted. Before each reset a
   full `FLrd` dump was compared with the contents the cut should leave; all four were
   equal, with no byte changed outside the expected ranges.

   | Cut | EEPROM after the cut | After the reset |
   |---|---|---|
   | `c1`, after step 1 | High pointer 0; nothing else changed | VERSION 0xF8091166; BOOT_FLAGS `18 00 F0 A2 C1`; Low booted, High not attempted |
   | `c2`, after step 2 | High pointer 0; F8091164 bundle written at 0x4400, not verified | VERSION 0xF8091166; BOOT_FLAGS `18 00 F0 A2 C1`; Low booted, High not attempted |
   | `c3`, after step 3 | Both pointers valid; High holds F8091164, Low holds F8091166 | VERSION 0xF8091166; BOOT_FLAGS `18 00 F0 A2 C1`; Low booted first, High not attempted (Figure 2-9) |
   | `c4`, after step 4 | Low pointer 0; High pointer valid over F8091164 | VERSION 0xF8091164; BOOT_FLAGS `78 00 F0 A2 C1`; Region 0 Invalid, High booted (Figure 2-10) |

   `c1` and `c2` halt the sketch inside the progress callback; the Teensy was reset by
   flashing the same build again, which does not reset the controller. `c3` took 25894
   ms and `c4` 25966 ms, with no busy result. `c2` continued from the state `c1` left,
   and `c3` from the state `c2` left, so each started with the target pointer already
   0. `c4` wrote the image the region already held, which is the case the pointer-row
   check of step 2 exists for; it passed.

   Afterwards `b` with F8091166 succeeded in 52101 ms, a full dump was identical to
   `..._F8091166_fullFlash.bin`, and after `x` VERSION read 0xF8091166 with BOOT_FLAGS
   `18 00 F0 A2 C1`. Every reset of this stage showed the same sequence as stage 3: the
   `GAID` executor call ended with `I2CError` after 1276 to 1277 ms, and MODE read APP
   after 1938 to 1939 ms.

   Verified by this stage: in each of the four states the status table names, the
   controller boots the region the table predicts after a `GAID` restart, and the
   EEPROM holds exactly what the table says.

   Not shown by this stage: a boot after a loss of power from each state, and an
   EEPROM write interrupted in the middle of a task. The cuts fall between tasks, and
   a `GAID` restart keeps the supply up. The power-on boot path is the same code as far
   as the documents say, and BOOT_FLAGS after a `GAID` restart of the full-flash layout
   equals the value stage 1 read, but neither is a test of a power cycle.
5. Blank programming. Erase the EEPROM with `minipro`, confirm PTCH mode, load the
   bundle with `TPS25751PatchLoader`, run `programBlank()`, `coldReset()`, and compare a
   dump with `..._fullFlash.bin`. Recovery: `write-flash.zsh`.

Two physical points were confirmed on 2026-11-22. First, the EEPROM can be reprogrammed
in place with a SOIC8 clip, so `write-flash.zsh` (`24LC256@SOIC8`) is a recovery path
that needs no rework. Second, the board runs from the 18650 battery pack, so the Teensy
and the controller stay powered while `GAID` puts the port through Type-C Error
Recovery. Keep the pack connected and charged for every write stage: an EEPROM write
interrupted by power loss is the failure the two-region scheme protects against.

Correction, 2026-11-23. The first point above is wrong as written, and the second
needs a qualification:

- The EEPROM is on the bottom of the board, above a battery-protection PCB. The SOIC8
  clip cannot be attached while the board runs from the 18650 pack. `write-flash.zsh`
  needs the stack taken apart and the board unpowered. The teardown is simple, but it
  is rework, so every "Recovery: `write-flash.zsh`" line above carries that cost.
- With the pack fitted, a power cycle also needs the stack apart, which affects stage
  4.
- The board can run from a bench supply instead of the pack. That leaves the EEPROM
  exposed for the clip and makes a power cycle a switch. Cautions for that setup: a
  voltage inside the pack's range with a current limit; the USB-C port unconnected
  during writes unless a stage needs it, because the BQ25798 then sees a supply where
  it expects a pack; leads secured before each write command.
- `load-patch-bundle` has run successfully on this board with the EEPROM fitted, so a
  PBM load is a recovery from PTCH that needs no teardown. After it, with the Low
  pointer 0 and the High pointer valid, `u` selects Low and passes pre-flight check 4.
  Pre-flight check 2 (I2C EEPROM Present in BOOT_FLAGS after a PBM boot) is still
  inferred; stage 5 confirms it.
- The EEPROM is reachable from the Teensy directly on `Wire2`.

Open, not decided: which supply the write stages use (the paragraph above says the
pack); a direct `Wire2` dump as an independent read-only check after `p`, `p2`, and
`u`; and a direct `Wire2` restore sketch as a recovery that needs no clip. The restore
sketch is outside this plan and writes the EEPROM on a bus the controller also
masters, at boot, for the BQ25798, and during every flash task, so it needs its own
decision, review, and go-ahead.

## Risks

- A wrong pointer write can leave no bootable region. Mitigations: pre-flight check 4,
  pointer-first selection, the neighbor check, and stage 2 before any bundle write.
  Residual risk is covered by PBM load plus `programBlank()`, or `minipro`.
- `FLvy` may validate only the header. Mitigation: the `FLrd` compare, on by default.
- The update blocks for 10 to 30 s per region. The progress callback keeps the example
  responsive; an application with a watchdog must service it from the callback.
- The executor always reads all 64 DATA bytes after each task, which is most of the
  per-chunk time. If stage 3 shows the update is too slow, an `outLen`-bounded read in
  `executeCommand()` is the first optimization; it is not part of this plan.

## Out of scope

- A streaming image source (SD card, serial).
- Moving `TPS25751PatchLoader` and `TPS25751DownstreamDevice` onto the executor seam.
- Interrupt-driven task completion (deferred under ADR-007).
- TPS26750 support, although the app note covers both parts.
