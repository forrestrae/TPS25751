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
   `returnCode` meaningless. Documented by the TRM; confirmed in hardware stage 1.
3. `FLvy` is not a full verification. The TRM says it completes after the header is
   checked. Whether it also checks the bundle CRC is unknown, so the updater compares
   the written region with the image through `FLrd` before it writes the pointer.
   Hardware stage 3 measures what `FLvy` catches.
4. Region selection uses the region pointers first and BOOT_STATUS second. BOOT_STATUS
   is latched at boot, so after an update without a reset it still describes the old
   layout. Following Figure 2-2 alone in that state would erase the only valid pointer.
   The rule is in the Region selection section.
5. Timeouts. `FLad`, `FLrd`, and `FLwd` use 200 ms, and `FLvy` uses 1000 ms until
   measured. `FLwd` returning 0xFF (busy) is retried up to three times after 10 ms, each
   retry preceded by a fresh `FLad` to the chunk's address. Estimate: 448 `FLwd` calls
   at 8 to 20 ms each is 4 to 9 s per region, and the 896 `FLrd` calls of the compare
   add 7 to 18 s. Stage 3 replaces the estimate with a measurement.
6. `GAID` never completes in the normal sense. The updater treats `Success`, `Timeout`,
   and `I2CError` from the executor as "reset issued", then polls MODE until it reads
   APP. The TRM says an APP-mode controller enters Type-C Error Recovery and restarts
   after about 1 s, so the wait defaults to 5 s. The port disconnects during the reset.
7. Sample-code defects not copied: step 3 of the app note ignores the executor result
   for `FLvy`, and the note says I2Cc where the host patch load uses I2Ct.
8. Bus sharing. The updater is synchronous, so it cannot interleave with
   `TPS25751DownstreamDevice` calls from the same thread. The application must not issue
   I2Cr or I2Cw from an interrupt or another task during an update. The TRM states the
   5-second spacing only for I2Cr and I2Cw; nothing on pages 81 and 82 applies it to the
   flash tasks.

Still unknown, each with a hardware check: whether `FLwd` honors a 4-byte length
exactly (stage 2), whether a zero pointer sets BOOT_STATUS Region Invalid (stage 3),
and how long the controller takes to return to APP after `GAID` (stage 3).

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
| `SetAddressFailed` | `FLad` | Target pointer 0; other region boots (Figure 2-7) |
| `WriteFailed`, `FlashBusy` | `FLwd` | Target region partly written, pointer 0; other region boots |
| `VerifyFailed` | `FLvy` | Target written but not valid, pointer 0; other region boots (Figure 2-8) |
| `CompareMismatch` | `compareRegion()` | Same as `VerifyFailed` |
| `PointerRestoreFailed` | Step 3 pointer write | Target pointer uncertain; other region boots |
| `OldPointerEraseFailed` | Step 4 | Both pointers may be valid; low boots first (Figure 2-9) |
| `PointerNeighborsChanged` | Neighbor check | Bytes 4 to 15 after a pointer changed; stop and dump |
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
2. First write: `writeRegionPointer(High, 0)` while the low pointer is valid, then
   restore it to 0x4400. Confirms that `FLwd` writes exactly four bytes (neighbor check)
   and the read-back. This is step 1 of the app note's own flow, so the controller
   still boots from the low region if it fails. Recovery: `write-flash.zsh`.
3. Single-region update to the `F8091164` bundle, with timing recorded per task. Then
   `coldReset()`; VERSION must read 0xF8091164 and BOOT_STATUS must show the high region
   used. Record BOOT_STATUS with the low pointer erased, and the time to APP. Before the
   pointer write of a second run, corrupt one byte of the written region and record
   whether `FLvy` reports it. Then update back to `F8091166` with `updateBothRegions()`
   and compare a full dump with `..._fullFlash.bin`. Recovery: the untouched region, then
   `write-flash.zsh`.
4. Interrupted updates. Cut the update after each of the four steps (a debug hook in
   the example), power-cycle, and confirm the controller boots the region the status
   table predicts. Recovery: as stage 3.
5. Blank programming. Erase the EEPROM with `minipro`, confirm PTCH mode, load the
   bundle with `TPS25751PatchLoader`, run `programBlank()`, `coldReset()`, and compare a
   dump with `..._fullFlash.bin`. Recovery: `write-flash.zsh`.

Two physical points were confirmed on 2026-11-22. First, the EEPROM can be reprogrammed
in place with a SOIC8 clip, so `write-flash.zsh` (`24LC256@SOIC8`) is a recovery path
that needs no rework. Second, the board runs from the 18650 battery pack, so the Teensy
and the controller stay powered while `GAID` puts the port through Type-C Error
Recovery. Keep the pack connected and charged for every write stage: an EEPROM write
interrupted by power loss is the failure the two-region scheme protects against.

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
