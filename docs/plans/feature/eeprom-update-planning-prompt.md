# Planning prompt: update the TPS25751 configuration EEPROM over I2C

Produce an implementation plan for a host-side EEPROM updater in this library. The
updater writes a TI patch bundle (firmware patch plus register configuration) to the
external EEPROM on the TPS25751's I2Cc bus, using the flash 4CC tasks that TI application
note SLVAFL1 describes. Plan only: write no library code, and send nothing to the
hardware that writes the EEPROM.

Write the plan to `docs/plans/feature/eeprom-update.md`, in the form of
`docs/plans/feature/load-patch-bundle.md` (context, reused components, decisions,
design, steps, tests, hardware validation). Ask me about the decisions listed under
"Decisions I need to make" before you finalize it.

## Sources to read first

1. `docs/TPS25751 and TPS26750 EEPROM Update Over I2C.pdf` (SLVAFL1, October 2024, 14
   pages). Read all of it, including the flow charts in Figures 2-2 to 2-5 and 4-1 as
   images: text extraction garbles the task names inside the figures.
2. `AGENTS.md` and the engineering documents it lists, in particular ADR-007 (4CC
   executor) and ADR-011 (patch loader) in `docs/engineering/ARCHITECTURE.md`, and the
   4CC and PBM sections of `docs/engineering/CONSTRAINTS.md`.
3. `include/TPS25751PatchLoader.h` and `src/TPS25751PatchLoader.cpp`. This is the
   closest existing feature and the model for class shape, status enum, and example.
4. `TPS25751::executeCommand()` and `burstWrite()` in `include/TPS25751.h` and
   `src/TPS25751.cpp`, and `include/TPS25751Task.h`.
5. `include/TPS25751BootFlags.h` (BOOT_STATUS, 0x2D), which already decodes the region
   flags the update flow branches on.
6. The image set in
   `/Users/frae/Development/hardware/microphone-sensor/teensy_mic_sensor/PCB/eeprom/Dual Role`.

## What the application note specifies

The EEPROM holds two copies of the patch bundle so that an interrupted update leaves a
bootable image. Fixed addresses, from the note's source code:

| Item | Low region (0) | High region (1) |
|---|---|---|
| Region pointer (RegionStart) | 0x0000 | 0x0400 |
| AppConfigOffset | 0x03FC | 0x07FC |
| Patch bundle start | 0x0800 | 0x4400 |

Tasks, from Table 2-1: `FLrd` (4 bytes in, 16 out, read flash at an address), `FLad` (4
in, 1 out, set the write address), `FLwd` (up to 64 in, 1 out, write at the
auto-incrementing address), `FLvy` (4 in, 1 out, verify the bundle at an address), and
`GAID` (cold reset, no data).

`UpdateRegionOfEeprom(i)` updates the region the controller did not boot from, in four
steps:

1. Write 0 to the new region's pointer and read it back with `FLrd`.
2. `FLad` to the new region's bundle start, then `FLwd` the image in chunks.
3. `FLvy` the new bundle; on success write the bundle start into the new region's
   pointer and read it back.
4. Write 0 to the old region's pointer and read it back.

The note's summary then says to update both regions and issue `GAID` so the controller
reboots from the new bundle. The Recovering From EEPROM Failure section covers a
controller that could not boot from EEPROM: read BOOT_STATUS, load a patch with the PBM
tasks to reach APP mode, repair the EEPROM with the flash tasks, then `GAID`. The
library's `TPS25751PatchLoader` already implements the PBM half of that path.

## Facts already verified about the image files

I checked these against the files on 2026-11-22; confirm them rather than rederive them.

- `4x18650-config-dual_TPS25751_F8091166_lowRegion.bin` is 14336 bytes (0x3800, exactly
  224 chunks of 64). It starts with the little-endian Header_ID 0xACE00001 the boot ROM
  expects, and carries 0xF8091166 at offset 0x34, the value the VERSION register (0x0F)
  reads on this board.
- `..._fullFlash.bin` is 31744 bytes (0x7C00). Its pointer at 0x0000 is 0x00000800, its
  pointer at 0x0400 is 0x00004400, and both AppConfigOffset words are 0. The bytes at
  0x0800 and at 0x4400 are each identical to the low-region file. The full image is
  therefore the layout in the note's Figure 2-6 with the same bundle in both regions,
  and the low-region file is the unit the update flow writes to either region.
- `..._lowRegion.c` and `..._fullFlash.c` hold the same bytes as `const char` arrays
  named `tps25750x_lowRegion_i2c_array` and `tps25750x_fullFlash_i2c_array`, with
  `gSizeLowRegionArray` and `gSizeFullFlashArray`.
- `write-flash.zsh` programs the full image with `minipro -p "24LC256@SOIC8"`. The
  EEPROM is a 24LC256: 32768 bytes, so the 0x7C00 image fits. Confirm its page size and
  write-cycle time from the Microchip datasheet before fixing chunk size and pacing.
- The `F8091164` subdirectory holds the previous build of the same configuration. Two
  builds give a way to prove an update on hardware, because VERSION changes between
  them.

## Open questions the plan must resolve

The note is inconsistent in places, and its sample code targets a different I2C layer.
For each item, state the answer, its evidence, and how hardware confirms it.

1. `FLwd` chunk size. Table 2-1 gives 64 bytes of input; Figure 2-3 says "page size up
   to 32 bytes"; the summary says 32 bytes at a time; the sample code uses an undefined
   `PATCH_BUNDLE_SIZE`. Decide the chunk size, check it against the EEPROM page size and
   the Teensy 4 `Wire` transmit buffer (a 64-byte DATA write is 66 bytes on the wire),
   and say what happens to a final short chunk.
2. Return-code and output offsets. The sample checks `outdata[1]` and assembles the
   `FLrd` value from `outdata[1..4]`, which suggests its buffer includes the I2C length
   byte. `executeCommand()` strips that byte and reports DATA byte 0 as `returnCode`.
   Establish where each task's result sits in the library's terms, in particular
   whether `FLrd` returns data from DATA byte 0 with no return-code byte. `FLrd` is
   read-only, so hardware can settle this safely before any write.
3. Task documentation. A text search of
   `docs/TPS25751 USB Type-C and USB PD Controller.pdf` found none of the flash task
   names. Check whether the `tps25751-docs` MCP server or TRM SLVUCR8B documents `FLrd`,
   `FLad`, `FLwd`, `FLvy`, and `GAID`, including return-code values, and cite what you
   find. If only the application note covers them, say so.
4. Region selection. Figure 2-2 chooses the region from BOOT_STATUS
   (`region0EepromError()` or `region0Invalid()`), the sample code uses an unexplained
   active and inactive region, and the summary says to query with `FLrd`. Define one
   rule from `TPS25751BootFlags` and the two region pointers, and define what the
   updater does when both pointers are valid, as they are after a full-image program.
5. Timing. The flow chart polls COMMAND every 10 ms; the sample delays 75 microseconds
   between `FLwd` calls; `executeCommand()` defaults to a 200 ms timeout. Set a timeout
   per task, with `FLvy` over a 14 KB bundle the likely outlier, and estimate total
   update time for 224 chunks.
6. `GAID`. The controller resets, so COMMAND may never read back as cleared and the
   executor may report `Timeout` or `I2CError`. Specify how the updater issues the
   reset, waits for APP mode, and confirms the result through MODE, VERSION, and
   BOOT_STATUS.
7. Sample-code defects not to copy: step 3 ignores the `ExecCmd` result for `FLvy`, and
   the note says "I2Cc" where the host patch load uses I2Ct.
8. Bus sharing. The EEPROM (0x50) shares I2Cc with the BQ25798 that
   `TPS25751DownstreamDevice` proxies. State whether an update needs downstream traffic
   paused and whether the I2Cr and I2Cw spacing rule interacts with the flash tasks.

## Design the plan must cover

- A class beside `TPS25751PatchLoader`, not a register class and not in the factory,
  built on `executeCommand()`. Propose the name, the public API, and a new ADR.
- Primitives for the four flash tasks and for the note's `WriteRegionPointer()`, with
  the read-back check, plus the four-step region update, a both-regions update, and the
  reset.
- A status enum with one enumerator per failure point. For each, document the state
  the EEPROM is left in and which region the controller boots from next, following the
  note's Figures 2-7 to 2-10.
- Pre-flight checks before the first write: MODE is APP, BOOT_STATUS reports an EEPROM,
  the image starts with Header_ID 0xACE00001, and its length fits the region (0x3C00
  bytes between 0x0800 and 0x4400, and 0x3C00 from 0x4400 to the end of a 24LC256).
  Consider comparing the image's version word with VERSION to skip a no-op update.
- Image input. The current image is a 14 KB array; decide between a pointer and length,
  as `loadPatchBundle()` takes, and a chunk-provider callback that also serves an SD
  card or serial source.
- A read-back path built on `FLrd` (16 bytes per call) for verification and for dumping
  the EEPROM, independent of `FLvy`.
- Recovery: how the updater composes with `TPS25751PatchLoader` when the controller is
  in PTCH mode with a blank or corrupt EEPROM, and whether writing the region pointers
  and both regions through the flash tasks can replace the first-time `minipro` program.
- An example under `examples/` with its own `[env:example-...]` entry, which prints the
  plan of action and requires explicit confirmation over serial before the first write.
- Documentation updates: `AGENTS.md`, `ARCHITECTURE.md`, `CONSTRAINTS.md`,
  `examples/README.md`, and Doxygen per `docs/engineering/DOCUMENTATION.md`.

## Testing

`[env:native]` compiles only the platform-independent sources. `src/TPS25751.cpp` and
`src/TPS25751PatchLoader.cpp` are not in its `build_src_filter`, there is no `Wire` stub
in `test/support/native/`, and the patch loader has no native suite. The plan must say
how the update sequencing reaches the >90% coverage target: for example a narrow
command-executor seam the updater depends on, with a scripted fake in the tests, or a
`TwoWire` fake that brings `TPS25751.cpp` into the native build. Recommend one and
give its cost. The suite must cover every failure enumerator and assert the exact task
sequence and DATA payloads for both regions, including little-endian addresses.

## Hardware validation and risk

A failed update can leave the board without a bootable EEPROM image. The plan must
order hardware work from safe to destructive and name the recovery for each stage.

1. Read-only: BOOT_STATUS, VERSION, the two region pointers through `FLrd`, and a full
   `FLrd` dump compared byte for byte with `..._fullFlash.bin`.
2. `FLvy` on both existing regions.
3. A single-region update with the `F8091164` bundle, then `GAID`, confirmed by
   VERSION, then the return to `F8091166`.
4. An interrupted update at each of the four steps, confirming the controller still
   boots from the untouched region.

Recovery paths to document: the PBM load through `TPS25751PatchLoader`, and
reprogramming with `write-flash.zsh`. State what each needs physically, and what power
source keeps the board alive during an update when the controller itself is the sink.

## Decisions I need to make

Present each with a recommendation and the reason.

- Scope of the first version: region update only, or also full-image programming of a
  blank EEPROM and the EEPROM dump.
- Whether the updater always updates both regions, as the note's summary says, or one
  region per call with the second optional.
- Whether the example ships a placeholder image, like `load-patch-bundle`, or this
  board's `F8091166` bundle. The library is published to the PlatformIO Registry, so a
  board-specific TI binary in the package has size and licensing consequences.
- Whether `GAID` is part of the update call or a separate call the application makes.
- The test seam, from the Testing section.

## Constraints on the work

- Follow the Git Workflow section of `AGENTS.md`: a topic branch such as
  `feat/eeprom-update`, Conventional Commit subjects, no push, and no integration into
  `main` without my confirmation that the branch is finished.
- Platform rules from `CONSTRAINTS.md` apply: no RTTI, `F()` for strings, no
  `static_assert(false)` in templates.
- Split the plan into batches that each end with a green `pio test -e native` and a
  building example, with the hardware stages above as explicit gates between them.
- Mark every claim as documented (with document and page), verified on hardware, or
  inferred. The earlier plans in `docs/plans/` do this; keep the practice.
