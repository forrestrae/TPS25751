# TPS25751 library: testing

The library is tested in two ways. Native unit tests run on the host with
`pio test -e native` (Unity, no hardware, no Arduino framework) and are a hard gate for
every change. Hardware tests run the example sketches against a board, in the stages each
feature plan defines.

The target is more than 90% line coverage of every file in `src/`, measured with
`test/coverage.sh`. The `test/` directory is excluded from the published package by
`export.exclude` in `library.json`.

## Unit Testing

### How the Harness Works

Unit tests run **on the host** via PlatformIO's `native` platform — no Teensy,
no `Wire`, no Arduino framework. The `[env:native]` environment in
`platformio.ini`:

- uses **Unity** as the test framework (`test_framework = unity`);
- compiles every register class, the base class, the debug helpers, the PDO and
  RDO value types, and the factory via `build_src_filter` (`test_build_src =
  yes`), listed in register address order, followed by
  `TPS25751EepromUpdater.cpp`, which reaches the controller only through the
  `TPS25751CommandExecutor` interface, by `TPS25751AppConfigImage.cpp`, which
  works on a buffer only, and by `TPS25751.cpp` and
  `TPS25751PatchLoader.cpp`, which reach the bus through the TwoWire fake (see
  "I2C Tests: the Wire Fake" below), and by the bench console
  (`TPS25751BenchProtocol.cpp` and `TPS25751BenchConsole.cpp`). `TPS25751HostExecutor.cpp` and
  `TPS25751DownstreamDevice.cpp` are not compiled, because no suite tests them; a
  new register class must be added to the filter;
- satisfies the Arduino API symbols those sources reference (`Stream`,
  `Serial`, `F()`, `millis()`, `strncpy_P`) with the **header-only stubs** in
  `test/support/native/` (`Arduino.h`, `Stream.h`). The stubs print to stdout;
  they are just enough surface to compile the decode/encode logic, not an
  Arduino emulation. Every `print()` of the `Stream` stub ends in its virtual
  `write()`, so `FakeStream.h` can capture what a class prints and feed it input.
  Extend the stubs (minimally) if a new source file needs more;
- only runs suites under `test/test_native/` (`test_filter = test_native/*`);
- compiles as `-std=gnu++17`, the standard the Teensy build uses, so language
  features behave the same on the host and on the target. Without the flag the
  host default applies (Apple clang defaults to gnu++14), where attributes on
  enumerators such as the rev B aliases compile only as an extension.

PlatformIO defines **`PIO_UNIT_TESTING`** during test builds;
`TPS25751Register.h` uses it to make the protected `extractBits*` /
`extractEnum*` helpers public so tests can exercise them directly.
`test_TPS25751Register` covers those helpers, including fields past bit 255
(ADR-012).

To compare printed output, include `CaptureStdout.h` from `test/support/native/`.
`captureDebugPrint(reg)` returns what a register's `debugPrint()` printed, and
`captureStdout(fn)` returns what any callable printed.

### Test Structure

Each register class gets its own suite directory under `test/test_native/`,
containing one `.cpp` with its own `main()` (native Unity tests provide their
own runner):

```
test/
  support/native/                  # Arduino API stubs (not a test suite)
  test_native/
    test_TPS25751Status/
      test_TPS25751Status.cpp      # seed suite - use as the reference example
    test_TPS25751<NewClass>/
      test_TPS25751<NewClass>.cpp
```

**`test/test_native/test_TPS25751Status/test_TPS25751Status.cpp` is the
canonical example** — it was written against the real register API and verified
green. Pattern-match it rather than inventing structure.

### Required Test Cases

For every register class:

1. **Construction** — default ctor (allocates zeroed buffer of the register's
   `RegisterInfo` size), data ctor (deep-copies the buffer), null-data and
   wrong-size behavior.
2. **Validation tiers** — `isValid()` / `isValidSize()` (basic),
   `hasValidData()` (data; all-`0xFF` is the bus-error signature), and
   `isSemanticallyValid()` (semantic; one test per logical-consistency rule,
   both passing and failing).
3. **Field decode** — at least one hand-packed buffer per meaningful register
   state, asserting **every** accessor. Pack the bytes from the TRM bit
   offsets (cross-checked against the `tps25751-docs` MCP server) and comment
   the packing arithmetic in the test file.
4. **Encode round-trip** (R/W registers only) — set-field → raw-bytes →
   re-decode.
5. **Rule of Five** — copy preserves and deep-copies data; move transfers the
   buffer and leaves the source `!isValid()`.

Base-class behavior (`extractBits*` bounds checking, cross-byte extraction)
lives in the STATUS seed suite and does not need repeating per register.

### Test Skeleton

```cpp
#include <unity.h>
#include "TPS25751Status.h"          // the class under test
#include "TPS25751RegisterAddress.h"

namespace {
constexpr size_t SIZE = TPS25751Registers::Registers::STATUS.size;
// Hand-packed from TRM bit offsets - comment the arithmetic:
//   byte0 = 1 | (6 << 1) | (1 << 4) = 0x1D  (plug, state 6, CC2)
constexpr uint8_t CONNECTED[SIZE] = {0x1D, 0x00, 0xD0, 0x48, 0x00};
}

void setUp() {}
void tearDown() {}

void test_connected_decode()
{
    TPS25751Status status(CONNECTED);
    TEST_ASSERT_TRUE(status.plugPresent());
    TEST_ASSERT_EQUAL(CONNECTED_NO_RA_DETECTED, status.connectionState());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_connected_decode);
    return UNITY_END();
}
```

### I2C Tests: the Wire Fake

`test/support/native/Wire.h` gives the native build a `TwoWire` class and a `Wire`
object with the calls the library makes (`begin()`, `beginTransmission()`, `write()`,
`endTransmission()`, `requestFrom()`, `available()`, `read()`). It hands each transaction
to a target and follows the Teensy 4 driver (`WireIMXRT.cpp`) where the library can see
the difference: a 136-byte transmit buffer and receive buffer, `write()` returning 0
until `beginTransmission()` has been called, `endTransmission()` returning 4 when no
transmission was begun, and `requestFrom()` taking a `uint8_t` length of at least 1.
`test/support/native/FakeTps25751Target.h` is that target, a scripted TPS25751:

- the register protocol of the unique address: a pointer write, then a read of the count
  byte and the register; a write of register, count, and data;
- COMMAND and DATA as a 4CC task: a code written to COMMAND reads back for a scripted
  number of polls, then clears or reads "!CMD", and DATA takes the task's output;
- PBMs opening the burst port at 0x30 on a PatchStartStatus of 0, the port recording the
  raw burst, and PBMc setting MODE to APP after a scripted delay;
- faults by transaction index, time window, address, register, and kind: a write error
  code the driver can return (2 for a NACK, of the address or of a data byte, which the
  driver does not tell apart; 4; or 5) or a read that returns only N bytes, and a hook on
  a task's completion that can open a fault window then;
- a log of every transaction with the stub clock's time.

The fake does not model the driver's bus-idle wait and the forced clocking it falls back
to, clock stretching, the driver's 50 ms transaction timeouts, or `requestFrom()`
returning 4 when the bus did not become idle, which a caller cannot tell from a 4-byte
read.

One fault is not driver behaviour. `Wire.rxMissing` makes `requestFrom()` return the
count the target gave while the last `rxMissing` of those bytes are absent from the
receive buffer, so `available()` falls short of the reported count. The Teensy driver
cannot do this, but the `available()` checks in `TPS25751::readRegister()` guard against
cores that can, and this is how the tests reach them.

The Arduino stub's `millis()`, `micros()`, `delay()`, and `delayMicroseconds()` use a
clock the tests set and read (`fake_arduino::g_nowUs`); `millis()` and `micros()` wrap at
32 bits, as on the Teensy, and `digitalRead()` returns pin
levels the tests set. The poll counts, the 0xFF padding of a long read, and the exact
time MODE switches are choices of the fake, not documented behaviour.

Three suites use it. `test_TPS25751Typed` covers the typed wrappers of `TPS25751.cpp`:
each of the 37 `read*(T &)` functions, 15 `write*(const T &)` functions, and 37 factory
`read*Register(validate)` functions is checked for the register address in the pointer
write, the read of size + 1 bytes, the bytes the decoded object holds, the
`[register, length, data...]` frame with a STOP for writes, and its failure paths (a NACKed
pointer write or write, a length byte that is not the register size, an object of the wrong
size); it also covers `readRegisterByAddress()` for all 37 addresses and an unknown one,
`readRegisterAs<T>()`, the address-keyed and `RegisterInfo` forms, `begin()`, and the debug
setters. It also injects a factory that returns null through `TPS25751Factory::setFactory()`
to reach the decoder check of `readRegisterByAddress()` (its `tearDown()` restores the
default factory), and uses `Wire.rxMissing` for a read whose buffer is empty or one byte
short of the count `requestFrom()` reported. `test_TPS25751Core` covers the register read and write with the
length byte and their failures, `readRegisterRaw()` (a length byte equal to, above, and
below the capacity, an address without a register, the refused arguments, and its I2C
failures), `writeCommandData()` (the exact-length DATA write and the
lengths it refuses), `executeCommand()` and `issueCommand()` (the handshake,
DATA read at the instant COMMAND clears or not at all, rejected, the timeout at its exact
bound, a COMMAND poll across a `millis()` wrap, and an I2C failure at each transaction),
`burstWrite()`, and the fake's own match to the driver. `test_TPS25751PatchLoader` covers
`loadPatchBundle()`: the transaction order of SLVAFV8A section 4, no DATA read within
20 ms of `PBMc` completing (the regression test for the defect hardware stage 5 found;
it fails against the earlier loader), a controller modelled off the bus for those 20 ms,
at least 500 µs between the last burst chunk and the next transaction in both modes, the
DATA retry and its cap, the MODE poll through failed and PTCH reads and both outcomes at
its cap, both caps and the IRQ ready wait across a `millis()` wrap, a `PBMs` and a `PBMc`
that each take longer than the 200 ms default timeout, every `PatchLoadStatus`, `debugPrint()`, the PBMs configuration bytes,
and IRQ mode. Since 2026-11-24 it also covers the steps added from TRM Figure 5-1 and
SLVAFV8A step 5: the INT_EVENT1 Ready for Patch poll of polling mode (10 ms interval,
failed reads, its cap, and none in IRQ mode), the MODE poll until PTCH through BOOT and
failed reads in both modes and its cap, both caps across a `millis()` wrap, the PBMs
configuration read back 500 µs after its write (a dropped write rewritten, three
mismatches, and failed writes and read-backs), and `PBMe` after a burst chunk that is not
acknowledged and after "!CMD" on `PBMc`, with its own failures and the failures that must
not issue it. The fake models a dropped write as a fault with `writeError` 0, matches
faults by a minimum write length, holds INT_EVENT1 with bit 81 set, and runs a
`beforeRead` hook so a test can change a register over time.

### Bench console tests

`test_TPS25751BenchProtocol` covers `TPS25751BenchProtocol` and the line prefixer with no
bus at all: the parser, both allowlists, the per-task timeouts, the snapshot list, and the
reply formatting.

`test_TPS25751BenchConsole` runs every console command through the real `TPS25751` class
and the real patch loader. The serial side is `FakeStream.h`. The controller is
`test/support/native/FakeBenchTarget.h`, a second target behind the same Wire fake. The
scripted target above completes a task after a set number of polls and injects faults per
transaction, which suits suites that pin the library's own transactions. The console's
cases are sequences over time, such as a restart that holds COMMAND, takes the controller
off the bus, and brings it back in another mode, so its target is a register table with
hooks:

- registers a test fills directly, with an optional length byte that differs from the
  register's size;
- a task handler that runs at the COMMAND write, sees DATA, and returns complete, reject,
  or pending;
- INT_CLEAR1 clearing the matching bits of INT_EVENT1, and the burst port at 0x30;
- a controller that stops answering, one register whose reads or writes fail, a write that
  is acknowledged and dropped, hooks before a read and after a write, and changes
  scheduled for a time on the stub clock;
- lists of the writes, reads, tasks, and burst bytes the console caused.

The task completing at the COMMAND write, the zero padding of a long read, and the times
of scheduled changes are choices of this target. The replies the suite expects carry the
stub clock's time, so a change to a poll interval in the console or the loader shows up as
a changed `t_ms`.

### Sequencer Tests: the EEPROM Updater

`TPS25751EepromUpdater` is the one sequencer with native tests. It reaches the controller
only through `TPS25751CommandExecutor` (ADR-014), so its suite,
`test/test_native/test_TPS25751EepromUpdater/`, drives it with a scripted fake instead of
`Wire`. The suite exists to show that no failure at any task can leave a valid region
pointer over a region that was not verified and compared.

`test/support/native/FakeCommandExecutor.h` models what the flash tasks touch:

- a byte-array EEPROM erased to 0xFF, of a configurable size, with no page model (the
  24LC256 itself wraps a write at a 64-byte page);
- the 64-byte DATA register, written with the input payload and read from byte 0, with
  `returnCode` always DATA byte 0, as `TPS25751::executeCommand()` does;
- `FLrd`, `FLad` with its write address, `FLwd` with the address advancing after each
  write, `FLvy`, and `GAID` with a reboot window in which MODE and BOOT_FLAGS reads fail;
- MODE and BOOT_FLAGS contents and read failures, a fake clock that `delayMs()` advances
  and that wraps like `millis()`, and a log of every call (code, payload, `outLen`,
  timeout, result).

Faults are injected per call, by global call index or by the n-th call of one task code:
an executor status, a forced return code with the task not run, or a hook that edits the
fake after the task runs.

Where the TRM is silent the fake has to choose, and the header labels each choice as an
assumption: `FLrd` past the end of the part reads 0xFF; `FLad` past the end and `FLwd`
without an `FLad` return 0x03; an `FLwd` after an `FLad` that was faulted into having no
effect writes at the stale address; `FLvy` decides validity from the Header_ID alone;
`GAID` clears the write address and reboots at once. A test that depends on one of these
pins the fake, not the device. Two behaviours the fake follows were inferred from the
documents rather than stated by them and have since been verified on the test board: the
`FLrd` byte order and address order (stage 1) and the exact length of a 4-byte `FLwd`
(stage 2; other lengths remain a model). Hardware has contradicted none of the fake's
choices so far. One device behaviour is modelled only on
request: `flrdMovesWriteAddress` makes every `FLrd` leave the write address at the address
it read plus 16. The TRM does not say whether a read moves the write address, hardware has
not shown it, and the default is that it does not.

What the suite covers:

- each task primitive: the exact 4CC, payload, little-endian address, timeout, and result
  decoding;
- region pointer writes, region selection including the stale BOOT_FLAGS case, the compare,
  and the dump format;
- `updateRegion()`, `updateBothRegions()`, and `programBlank()`: the full call sequence,
  built in the test from the plan rather than from the implementation, at 100 bytes and at
  the real 14336 bytes; the final EEPROM byte for byte; every status of the state tables by
  fault injection, with the EEPROM state asserted;
- sweeps that inject an executor failure, or a wrong return code, at every call of an
  update and of `programBlank()`, at both lengths (at the real length the long run of
  compare reads is thinned to the first, every 16th, and the last read of each pass);
- the modelled misplaced bundle write, the trace check, and the stated limits of the step
  2 read-back, each pinned so that a limit is not mistaken for coverage;
- the AppConfigOffset checks of `updateRegion()`: each pre-flight refusal for both targets
  with the EEPROM unchanged, the kept word checked before the target's, the conflict case
  of the plan in `updateBothRegions()` and its opposite, the word check before the step 3
  pointer write with and without the compare, executor failures on each new read, and the
  task counts (1368 for an update, 2732 for `programBlank()`, at 14336 bytes);
- for `programBlank()`, a sweep of an `FLad` that reports success without taking effect at
  each of its 10 `FLad` calls, from three start states, with and without the moving-address
  model, asserting that no valid pointer leads to a region that does not hold the image;
  against the step order used before 2026-11-23 the same sweep fails;
- the progress callback, `lastStep()`, `lastTarget()`, and `coldReset()`, including both of
  its APP outcomes. A `rebootDurationMs` of 0 scripts a controller that reads APP with no
  failed MODE read, which `coldReset()` must report as `ResetNotObserved`.

`TPS25751HostExecutor` is a pass-through to `TPS25751` and stays outside the native build.
The example `examples/eeprom-update` is built by `pio run -e example-eeprom-update` and
exercised on hardware only. The hardware stages of `docs/plans/feature/eeprom-update.md` run
its commands: `r` (stage 1 and every later check), `p` and `p2` (stage 2), `u`, `v`, `b`, and
`x` (stage 3), `c1` to `c4` with `x` (stage 4), and for stage 5 `zero`, which writes both
region pointers to 0 in place of an external erase, with `l`, `pb`, and `x`. The plan sets
the order of each stage.

### Factory suite

`test_TPS25751RegisterFactory` runs natively. For every address in the TRM it
checks that each of the three `createRegister()` overloads returns a non-null
object of the size in `getRegisterInfo()`. With no RTTI on the target, it
checks the class by behavior: the factory object's `debugPrint()` output must
equal that of the expected class constructed from the same bytes. A sweep over
all 256 address byte values checks that the factory and `getRegisterInfo()`
agree on which addresses exist. A new register needs one line in the suite's
`kAll` table, mapping its `Address` to its class; the suite then fails until
all three factory switches have the case.

### Not covered natively

`TPS25751DownstreamDevice.cpp` and `TPS25751HostExecutor.cpp` are not in the native
build, so the `I2Cr` and `I2Cw` payload encoding, the 7-bit address mask, the 11-byte
write cap, and the 5 s spacing warning have no host test. They were verified on hardware
against a BQ25798 (CONSTRAINTS.md, 4CC Command Interface).

## Hardware testing

There is no automated test against a device. A feature plan under `docs/plans/` defines
numbered hardware stages, each run with an example sketch on a Teensy 4.0 and the TPS25751
board, and records the result of each stage. For a new register, read it on hardware with
`examples/read-registers`, compare the decode with the TRM, and record any difference in
the class header and in CONSTRAINTS.md. A difference between hardware and the TRM is a
finding, not a bus error: the firmware sets reserved bits and reports length bytes the
TRM does not document.

## Test Organization

### Directory Structure

```
test/
  ├── support/
  │   └── native/                       # Arduino API stubs for [env:native]
  │       ├── Arduino.h
  │       └── Stream.h
  ├── test_native/                      # host-run suites: pio test -e native
  │   ├── test_TPS25751Status/
  │   │   └── test_TPS25751Status.cpp   # seed suite (reference example)
  │   └── test_TPS25751<Class>/
  │       └── test_TPS25751<Class>.cpp
  └── README
```

Directories that do not start with `test_` (like `support/`) are ignored by the
test runner. Embedded/on-target suites, if ever added, would live in a sibling
`test_embedded/` tree and a matching test env — none exist today.

### Naming Conventions

- Test directory: `test_native/test_[ClassName]/`
- Test file: `test_[ClassName].cpp`
- Test function: `test_[what_it_tests]()`
- Use snake_case for test function names

## Running Tests

### Local Testing

```bash
# Run all native unit tests (the standard command)
pio test -e native

# Run one suite
pio test -e native -f "test_native/test_TPS25751Status"

# Verbose output (shows the stubs' stdout, e.g. debugPrint output)
pio test -e native -v
```

`pio test` without `-e native` targets the hardware example environments
(`default_envs`) and will try to upload to a Teensy — always pass `-e native`
for unit tests.

### Script Tests

The Python tools under `scripts/` have their own tests in `scripts/tests/`. Run them from
the library root:

```bash
python3 -m unittest discover -s scripts/tests -t scripts -q
```

`test_bin_to_header.py` covers `scripts/bin_to_header.py`, which turns a low-region
binary into the header `examples/eeprom-update` embeds. It checks that the bytes round
trip, the declarations and the provenance comment (size, SHA-256, version word), that the
output is reproducible and compiles as C++17, and that the script applies the checks of
`TPS25751EepromUpdater::checkImage()` (Header_ID, minimum length, one region at most) and
writes no header for a rejected file. `test_appconfig_image.py` covers
`scripts/appconfig_image.py`. It reads the bytes of the native fixture in
`test/test_native/test_TPS25751AppConfigImage/AppConfigFixture.h` and expects the same CRCs
as that suite, so the host editor and `TPS25751AppConfigImage` cannot drift apart. The
other files test the git workflow tooling. The
verify gate runs this suite.

### Bench server tests

The MCP server in `tools/pd-bench` has its own pytest suite. It talks to a simulated
console behind a fake serial port. `tests/conftest.py` makes a real port unreachable for
every test: it names a port that does not exist, which also switches discovery off, and
makes opening any pyserial port raise. Run the suite from its directory:

```bash
cd tools/pd-bench && uv run --locked pytest
```

The verify gate runs this suite and fails if `uv` is missing or the run leaves untracked
files in `tools/pd-bench`.

### USB PD Capture Bundles

`test/fixtures/pd-captures/` holds USB PD captures from a ChargerLAB KM003C, each
paired with the TPS25751 registers that describe the same negotiation. Its
`README.md` indexes the bundles and defines `registers.json`, the format the bench
console's `SNAP` command emits. `test/support/pd_capture_fixture.py` turns each
bundle into `test/support/native/fixtures/PdCapture_<bundle>.h`, which the suites
include as `"fixtures/PdCapture_<bundle>.h"` through the native env's
`-I test/support/native`. The headers are committed, so `pio test -e native` needs
no Python.

A register suite that uses a bundle (0x30, 0x34, 0x35) builds the register class
from the bundle's register bytes and checks that it decodes to the wire words. The
`TPS25751Pdo` and `TPS25751Rdo` suites decode the wire words directly, and the
`TPS25751ReceivedSOPIdentity` suite feeds wire Discover Identity VDOs through the 0x48
decoders, since no bundle yet holds a partner identity. Write the expected field
values by hand from the `usb-pd-docs` tables and cite the table, so the generator
cannot hide a decode error. After changing a bundle, rerun the generator; `--check`
fails while a header is stale. The generator has its own unit tests:

```bash
python3 test/support/pd_capture_fixture.py --check
python3 -m unittest discover -s test/support -p "test_*.py"
```

## Coverage

`test/coverage.sh` measures line coverage of the native suites and prints one
line per file in `src/`, sorted with the least-covered file first. Run it from
the library root:

```bash
test/coverage.sh
```

Pass `--html` to also write a per-line report to `.pio/coverage/index.html`.
The script needs gcovr (`brew install gcovr` or `pipx install gcovr`). It
builds `[env:native-coverage]`, which extends `[env:native]` with `--coverage`
and `-O0`; `test/support/coverage_link.py` adds `--coverage` to the link step,
which `build_flags` does not reach. The script rebuilds that environment from
scratch with an empty build cache on every run, because counters accumulate
across runs and the shared cache in `.pio/buildcache` restores object files
without the `.gcno` notes gcov needs. On macOS it reads Apple clang's counters
with `xcrun llvm-cov gcov`.

Only sources compiled by `[env:native]` appear in the report, so
`TPS25751HostExecutor.cpp` and `TPS25751DownstreamDevice.cpp` are not measured.
`TPS25751EepromUpdater.cpp` is, through the executor interface, and `TPS25751.cpp`,
`TPS25751PatchLoader.cpp`, and the bench console are, through the Wire fake. Neither the verify gate nor CI runs the coverage step; the >90%
target is checked by running the script at review.

The latest measurement, on 2026-11-24 with 1600 tests, is 99% of lines overall (6715 of
6777): `TPS25751.cpp`, `TPS25751EepromUpdater.cpp`, and `TPS25751BenchProtocol.cpp` at
100%, `TPS25751PatchLoader.cpp` and `TPS25751BenchConsole.cpp` at 99%, and every other
file at 93% or more. Most lines still missed cannot be reached by
any input: `default:` returns in `toString()` switches over enums that `extractEnum()`
clamps, and `> max` checks on fields too narrow to exceed the maximum. Do not add a test
that calls a function only to count its lines.
