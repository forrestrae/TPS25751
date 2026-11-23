#include <Arduino.h>
#include <Wire.h>
#include <TPS25751.h>  // also brings in TPS25751HostExecutor and TPS25751EepromUpdater

#include "patch_bundle.h"  // tps25751_eeprom_bundle / tps25751_eeprom_bundle_size (placeholder)

// ---------------------------------------------------------------------------
// EEPROM update example
//
// Inspects and rewrites the TPS25751 configuration EEPROM through the flash
// 4CC tasks while the controller runs in APP mode (TI SLVAFL1; TRM SLVUCR8B
// pages 81 and 82). It is driven over serial, one command per line.
//
// COMMANDS THAT ONLY READ
//   r    Read-only report: MODE, VERSION, BOOT_FLAGS (0x2D), both region
//        pointers, both AppConfigOffset words (0x03FC and 0x07FC, as
//        "[offset]" lines), the region an update would target, FLvy for each
//        region, and a hex dump of 0x0000 to 0x7C00. Issues FLrd and FLvy only.
//        The same report without the dump is printed once at start-up.
//
// COMMAND THAT RESTARTS THE CONTROLLER (no EEPROM write)
//   x    Prints both region pointers, then coldReset(): issues GAID, waits for
//        APP, and prints the time to APP, MODE, VERSION, and BOOT_FLAGS. The
//        port disconnects during the restart. Runs at once when a pointer is
//        valid. When neither is, the controller would come up in PTCH, so x
//        warns and waits for the line "yes" first.
//
// COMMAND THAT LOADS THE CONTROLLER'S RAM (no EEPROM write)
//   l    Only while MODE reads PTCH, which is where the controller waits when
//        it finds no bundle to boot: TPS25751PatchLoader::loadPatchBundle()
//        with the embedded image (the low-region bundle serves as the Patch
//        Burst Mode image, SLVAFV8A), then MODE, VERSION, and BOOT_FLAGS. The
//        controller runs the image from RAM; the EEPROM is not written. Waits
//        for the line "yes" first. Refused in any other mode.
//
// COMMANDS THAT WRITE THE EEPROM
//   p    Pointer test: writes the High pointer to 0 and back to 0x4400, and
//        prints the 64 bytes at 0x0400 before, between, and after. Starts
//        with both pointers valid. It also starts with the Low pointer valid
//        and the High pointer 0, but only when a p earlier in the same run
//        left it so (its restore failed) and no p2, u, b, c, or v has been
//        sent since. A High pointer that is 0 for any other reason needs u or b.
//   p2   Pointer rewrite test: shows whether FLwd writes more than the bytes
//        it is given, which p cannot. Loads DATA with the 16 bytes at 0x4400,
//        writes the High pointer with the value it already holds, and reports
//        whether any of the 60 bytes after it changed. It reads DATA before
//        the write and stops, with nothing written, unless at least one DATA
//        byte differs from the byte it would overwrite. Needs both pointers
//        valid. Never changes the pointer's value and repairs nothing.
//   u    updateInactiveRegion() with the embedded image.
//   b    updateBothRegions() with the embedded image.
//   c1   Update the inactive region and halt after step 1 of SLVAFL1
//        Figure 2-3 (target pointer erased).
//   c2   The same, halting after step 2 (bundle written, pointer still 0).
//   c3   The same, stopping after step 3 (target pointer restored, old
//        pointer kept).
//   c4   The complete update, steps 1 to 4.
//   v    FLvy experiment: erases the inactive region's pointer, writes the
//        image, corrupts one byte of it, and reports what FLvy and
//        compareRegion() say. Leaves that pointer at 0; u or b repairs it.
//        The image write makes the same checks as step 2 of an update: the
//        bytes after the pointer before it starts, and a read-back after the
//        first two chunks. A failed check stops it with nothing further
//        written.
//   pb   programBlank() with the embedded image: writes both pointers, both
//        AppConfigOffset words, and the image in both regions, the layout of
//        the full-flash binary. Only when MODE reads APP and no region pointer
//        is valid, which is the state after l on a blank or corrupt EEPROM.
//        Its plan adds both AppConfigOffset words. When it refuses because a
//        pointer is valid, and after a stop in its High pass, it prints both
//        words: u and b do not write them, so do not run u or b while the
//        word of the region they would write is not 0.
//
// RECOVERY OF AN EEPROM THAT DOES NOT BOOT (hardware stage 5)
//   The controller comes up in PTCH. Send l, then pb, then x: the controller
//   restarts from the EEPROM pb wrote. r at start-up and on request says so
//   when MODE reads PTCH.
//
// Every command that writes first prints its plan (target region, image
// version, current VERSION, both pointers) and then waits for the line
//
//   yes
//
// Any other line cancels the command with nothing written. The line counts
// only when it is sent after the "[confirm]" prompt: input that arrived before
// the prompt is discarded, so a "yes" queued behind a command cannot confirm
// a plan nobody has read.
//
// Every command that writes, and l, is also refused while src/patch_bundle.h
// is the placeholder, because TPS25751EepromUpdater::checkImage() rejects it.
// Generate the real header with scripts/bin_to_header.py (see patch_bundle.h).
//
// c1 and c2 halt inside the progress callback and never return to the command
// loop; power-cycle or reset the Teensy afterwards. Keep the board powered
// during every other write: an interrupted write is the failure the two-region
// layout exists for, and c1 to c4 are the commands that test it.
//
// OUTPUT
//   Every line starts with a tag in brackets. A command ends with "[done] <cmd>"
//   followed by "[ready] ...". After p, p2, u, b, v, x, pb, and the c commands a
//   "[timing]" table gives, per 4CC task, the calls, total time, average, and
//   maximum, measured around each executor call, then the calls the executor
//   did not complete (not_success), the completed calls that returned a
//   nonzero DATA byte 0 (rc_nonzero), and those that returned 0xFF (rc_0xFF).
//   In the FLwd row rc_0xFF counts "flash is busy" results. In the FLrd row
//   the byte is memory, not a return code.
//
//   The dump lines of r have the form "xxxxxxxx: xx xx ... xx", which xxd -r
//   turns back into binary. The BEGIN and END marker lines are not neutral to
//   xxd -r. Extract the dump lines first, from a log with exactly one report:
//
//     grep -aE '^[0-9a-f]{8}:( [0-9a-f]{2}){16}\r?$' serial.log > dump.txt
//     wc -l dump.txt                  # must print 1984
//     xxd -r dump.txt > eeprom.bin
//     cmp eeprom.bin <image>_fullFlash.bin
//
//   The pattern takes full 16-byte lines only. p, p2, and v print lines of
//   the same form, so keep them out of the log used for the extraction.
//
//   The library's debug level is errors only. During x the executor reports
//   I2C errors while the controller restarts; those lines are expected.
//
// HARDWARE:
//   - Teensy 4.x MCU
//   - TPS25751 on Wire (I2C pins 18/19, address 0x20), in APP mode, or in
//     PTCH mode for l
//   - External EEPROM on the TPS25751's I2Cc bus (24LC256, 32 KB)
// ---------------------------------------------------------------------------

/// End of the dumped range: the size of the full-flash image (31744 bytes).
constexpr uint32_t DUMP_END = 0x7C00;

/// Bytes shown at the High pointer by the pointer test: the largest FLwd payload.
constexpr size_t POINTER_WINDOW = 64;

/// EEPROM address of the High region pointer.
constexpr uint32_t HIGH_POINTER = TPS25751EepromUpdater::regionPointerAddress(EepromRegion::High);

/// The image the write commands use.
static const uint8_t* const IMAGE = tps25751_eeprom_bundle;
static const size_t IMAGE_SIZE = tps25751_eeprom_bundle_size;

// ---------------------------------------------------------------------------
// Per-task timing
// ---------------------------------------------------------------------------

static const __FlashStringHelper* taskStatusName(TPS25751TaskStatus s)
{
    switch (s) {
        case TPS25751TaskStatus::Success:  return F("Success");
        case TPS25751TaskStatus::Rejected: return F("Rejected");
        case TPS25751TaskStatus::Timeout:  return F("Timeout");
        default:                           return F("I2CError");
    }
}

/**
 * @brief Executor decorator that times every 4CC task it forwards
 *
 * Wraps another TPS25751CommandExecutor and records, per task code, the number
 * of calls, the total and the longest duration, the calls that did not return
 * Success, and the status of the latest call. The duration covers the whole
 * executeCommand() call: the DATA write, the COMMAND write, the COMMAND poll,
 * and the 64-byte DATA read.
 *
 * It also counts what the tasks themselves answered, for the calls the
 * executor completed: how many returned a nonzero DATA byte 0, and how many
 * returned 0xFF. For FLwd, 0xFF is "flash is busy" (TRM Table 4-21), which the
 * updater retries and the library reports only at warning level, so this
 * column is where a busy result shows in a run at error level. For FLad and
 * FLvy the byte is the task's return code. For FLrd it is the first memory
 * byte, not a code, so those two columns say nothing about success there.
 */
class TimingExecutor final : public TPS25751CommandExecutor {
public:
    explicit TimingExecutor(TPS25751CommandExecutor& inner) : _inner(inner) {}

    TPS25751TaskResult executeCommand(TPS25751FourCC cmd,
        const uint8_t* inData, size_t inLen,
        uint8_t* outData, size_t outLen, uint32_t timeoutMs) override
    {
        const uint32_t started = micros();
        const TPS25751TaskResult result =
            _inner.executeCommand(cmd, inData, inLen, outData, outLen, timeoutMs);
        record(cmd, micros() - started, result);
        return result;
    }

    bool readMode(TPS25751Mode& mode) override { return _inner.readMode(mode); }
    bool readBootFlags(TPS25751BootFlags& flags) override { return _inner.readBootFlags(flags); }
    void delayMs(uint32_t ms) override { _inner.delayMs(ms); }
    uint32_t nowMs() override { return _inner.nowMs(); }

    /// Forget everything recorded so far.
    void reset() { _count = 0; }

    /// Print one line per task code seen since the last reset().
    void print(Stream& out) const
    {
        out.println(F("[timing] task calls total_ms avg_us max_us not_success rc_nonzero rc_0xFF last_status"));
        for (size_t i = 0; i < _count; ++i) {
            const Row& r = _rows[i];
            out.print(F("[timing] "));
            out.write(reinterpret_cast<const uint8_t*>(r.code), sizeof(r.code));
            out.print(' ');
            out.print(r.calls);
            out.print(' ');
            out.print(static_cast<uint32_t>(r.totalUs / 1000));
            out.print(' ');
            out.print(static_cast<uint32_t>(r.totalUs / r.calls));
            out.print(' ');
            out.print(r.maxUs);
            out.print(' ');
            out.print(r.notSuccess);
            out.print(' ');
            out.print(r.rcNonzero);
            out.print(' ');
            out.print(r.rcBusy);
            out.print(' ');
            out.println(taskStatusName(r.lastStatus));
        }
        if (_count == 0) {
            out.println(F("[timing] (no task issued)"));
        }
    }

private:
    struct Row {
        char code[4];
        uint32_t calls;
        uint64_t totalUs;
        uint32_t maxUs;
        uint32_t notSuccess;  ///< calls whose executor status was not Success
        uint32_t rcNonzero;   ///< completed calls whose DATA byte 0 was not 0x00
        uint32_t rcBusy;      ///< completed calls whose DATA byte 0 was 0xFF
        TPS25751TaskStatus lastStatus;
    };

    static constexpr size_t kMaxRows = 8;

    TPS25751CommandExecutor& _inner;
    Row _rows[kMaxRows];
    size_t _count = 0;

    void record(TPS25751FourCC cmd, uint32_t durationUs, const TPS25751TaskResult& result)
    {
        Row* row = nullptr;
        for (size_t i = 0; i < _count; ++i) {
            if (memcmp(_rows[i].code, cmd.code, sizeof(cmd.code)) == 0) {
                row = &_rows[i];
                break;
            }
        }
        if (row == nullptr) {
            if (_count == kMaxRows) {
                return;  // more task codes than rows; this example uses five
            }
            row = &_rows[_count++];
            memcpy(row->code, cmd.code, sizeof(cmd.code));
            row->calls = 0;
            row->totalUs = 0;
            row->maxUs = 0;
            row->notSuccess = 0;
            row->rcNonzero = 0;
            row->rcBusy = 0;
        }
        row->calls++;
        row->totalUs += durationUs;
        if (durationUs > row->maxUs) {
            row->maxUs = durationUs;
        }
        if (result.status != TPS25751TaskStatus::Success) {
            row->notSuccess++;  // returnCode is undefined for these
        } else {
            if (result.returnCode != 0x00) {
                row->rcNonzero++;
            }
            if (result.returnCode == 0xFF) {
                row->rcBusy++;
            }
        }
        row->lastStatus = result.status;
    }
};

// ---------------------------------------------------------------------------

TPS25751 pd(Wire, TPS25751_I2CT_ADDRESS_IDX[0]);
TPS25751HostExecutor hostExec(pd);
TimingExecutor exec(hostExec);
TPS25751EepromUpdater updater(exec);
TPS25751PatchLoader loader(pd);  // l: COMMAND polling only, no I2Ct_IRQ pin

// ---------------------------------------------------------------------------
// Printing helpers
// ---------------------------------------------------------------------------

static void printHex32(uint32_t v)
{
    char buf[11];
    snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(v));
    Serial.print(buf);
}

static void printHex8(uint8_t v)
{
    char buf[5];
    snprintf(buf, sizeof(buf), "0x%02X", v);
    Serial.print(buf);
}

/// One dump line in the format of TPS25751EepromUpdater::dump().
static void printRow(uint32_t addr, const uint8_t* bytes)
{
    char head[10];
    snprintf(head, sizeof(head), "%08lx:", static_cast<unsigned long>(addr));
    Serial.print(head);
    for (size_t i = 0; i < TPS25751EepromUpdater::kFlashReadBytes; ++i) {
        char byte[4];
        snprintf(byte, sizeof(byte), " %02x", bytes[i]);
        Serial.print(byte);
    }
    Serial.println();
}

/// Prints MODE and returns true when it reads APP. @p patch, when given,
/// receives true when MODE read PTCH.
static bool printMode(bool* patch = nullptr)
{
    if (patch != nullptr) {
        *patch = false;
    }
    TPS25751Mode mode;
    if (!pd.readMode(mode)) {
        Serial.println(F("[mode] Failed to read MODE"));
        return false;
    }
    Serial.print(F("[mode] MODE = '"));
    Serial.print(mode.modeString());
    Serial.println(F("'"));
    if (patch != nullptr) {
        *patch = mode.isPatchMode();
    }
    return mode.isApplicationMode();
}

static bool readVersion(uint32_t& value)
{
    auto version = pd.readVersionRegister(/*validate=*/false);
    if (!version) {
        return false;
    }
    value = version->rawVersion();
    return true;
}

static void printVersion()
{
    uint32_t value = 0;
    if (!readVersion(value)) {
        Serial.println(F("[version] Failed to read VERSION"));
        return;
    }
    Serial.print(F("[version] VERSION = "));
    printHex32(value);
    Serial.println();
}

static void printFlag(const __FlashStringHelper* name, bool value)
{
    Serial.print(F("[boot]   "));
    Serial.print(name);
    Serial.println(value ? F(": 1") : F(": 0"));
}

static void printBootFlags()
{
    TPS25751BootFlags flags;
    if (!pd.readBootFlags(flags)) {
        Serial.println(F("[boot] Failed to read BOOT_FLAGS"));
        return;
    }
    Serial.print(F("[boot] BOOT_FLAGS raw ="));
    for (size_t i = 0; i < flags.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), " %02X", flags.raw()[i]);
        Serial.print(buf);
    }
    Serial.println();
    Serial.print(F("[boot]   Patch Config Source: "));
    Serial.println(toString(flags.patchConfigSource()));
    printFlag(F("I2C EEPROM Present"), flags.i2cEepromPresent());
    printFlag(F("Region 0 Attempted"), flags.region0Attempted());
    printFlag(F("Region 1 Attempted"), flags.region1Attempted());
    printFlag(F("Region 0 Invalid"), flags.region0Invalid());
    printFlag(F("Region 1 Invalid"), flags.region1Invalid());
    printFlag(F("Region 0 EEPROM Error"), flags.region0EepromError());
    printFlag(F("Region 1 EEPROM Error"), flags.region1EepromError());
    printFlag(F("Region 0 CRC Fail"), flags.region0CrcFail());
    printFlag(F("Region 1 CRC Fail"), flags.region1CrcFail());
    printFlag(F("Patch Header Error"), flags.patchHeaderError());
}

/// Prints one pointer line and returns true when the pointer read as valid.
static bool printPointer(EepromRegion r)
{
    Serial.print(F("[pointer] "));
    Serial.print(TPS25751EepromUpdater::toString(r));
    Serial.print(F(" pointer at "));
    printHex32(TPS25751EepromUpdater::regionPointerAddress(r));
    Serial.print(F(" = "));

    uint32_t value = 0;
    const EepromUpdateStatus s = updater.readRegionPointer(r, value);
    if (s != EepromUpdateStatus::Success) {
        Serial.print(F("read failed: "));
        Serial.println(TPS25751EepromUpdater::toString(s));
        return false;
    }
    const bool valid = (value == TPS25751EepromUpdater::bundleStart(r));
    printHex32(value);
    Serial.println(valid ? F(" (valid)") : F(" (not valid)"));
    return valid;
}

static void printPointers()
{
    printPointer(EepromRegion::Low);
    printPointer(EepromRegion::High);
}

/// One AppConfigOffset line. The controller looks for a region's Header_ID at
/// the region start plus this word (SLVAFL1 Boot Process), so it must be 0.
static void printAppConfigOffset(EepromRegion r)
{
    const uint32_t addr = TPS25751EepromUpdater::appConfigOffsetAddress(r);
    Serial.print(F("[offset] "));
    Serial.print(TPS25751EepromUpdater::toString(r));
    Serial.print(F(" AppConfigOffset at "));
    printHex32(addr);
    Serial.print(F(" = "));

    uint8_t row[TPS25751EepromUpdater::kFlashReadBytes];
    const EepromUpdateStatus s = updater.readFlash(addr, row);
    if (s != EepromUpdateStatus::Success) {
        Serial.print(F("read failed: "));
        Serial.println(TPS25751EepromUpdater::toString(s));
        return;
    }
    const uint32_t value = static_cast<uint32_t>(row[0]) | (static_cast<uint32_t>(row[1]) << 8) |
                           (static_cast<uint32_t>(row[2]) << 16) | (static_cast<uint32_t>(row[3]) << 24);
    printHex32(value);
    Serial.println(value == 0 ? F(" (0)") : F(" (not 0)"));
}

static void printAppConfigOffsets()
{
    printAppConfigOffset(EepromRegion::Low);
    printAppConfigOffset(EepromRegion::High);
}

static void printTarget()
{
    EepromRegion target = EepromRegion::Low;
    const EepromUpdateStatus s = updater.selectTargetRegion(target);
    Serial.print(F("[target] Update would write region: "));
    if (s == EepromUpdateStatus::Success) {
        Serial.println(TPS25751EepromUpdater::toString(target));
    } else {
        Serial.print(F("none ("));
        Serial.print(TPS25751EepromUpdater::toString(s));
        Serial.println(F(")"));
    }
}

static void printVerify(EepromRegion r)
{
    const uint32_t start = TPS25751EepromUpdater::bundleStart(r);
    const EepromUpdateStatus s = updater.verifyBundle(start);
    Serial.print(F("[verify] FLvy "));
    Serial.print(TPS25751EepromUpdater::toString(r));
    Serial.print(F(" bundle at "));
    printHex32(start);
    Serial.print(F(": "));
    Serial.println(TPS25751EepromUpdater::toString(s));
}

/// checkImage() for the embedded image against the smaller region.
static EepromUpdateStatus imageStatus()
{
    const uint32_t low = updater.regionCapacity(EepromRegion::Low);
    const uint32_t high = updater.regionCapacity(EepromRegion::High);
    return TPS25751EepromUpdater::checkImage(IMAGE, IMAGE_SIZE, (low < high) ? low : high);
}

static void printImage()
{
    Serial.print(F("[image] Embedded image: "));
    Serial.print(IMAGE_SIZE);
    Serial.print(F(" bytes, version "));
    printHex32(TPS25751EepromUpdater::imageVersion(IMAGE, IMAGE_SIZE));
    Serial.print(F(", checkImage() = "));
    Serial.println(TPS25751EepromUpdater::toString(imageStatus()));
    if (imageStatus() != EepromUpdateStatus::Success) {
        Serial.println(F("[image] Placeholder or unusable image: every command that writes the EEPROM is refused."));
    }
}

static void printTiming()
{
    exec.print(Serial);
}

// ---------------------------------------------------------------------------
// Read-only report
// ---------------------------------------------------------------------------

static void dumpEeprom()
{
    Serial.println(F("-----BEGIN EEPROM DUMP 0x00000000-0x00007C00-----"));
    const uint32_t started = millis();
    const EepromUpdateStatus s = updater.dump(0, DUMP_END, Serial);
    const uint32_t elapsed = millis() - started;
    Serial.println(F("-----END EEPROM DUMP-----"));

    Serial.print(F("[dump] Result: "));
    Serial.println(TPS25751EepromUpdater::toString(s));
    if (s != EepromUpdateStatus::Success) {
        Serial.println(F("[dump] Dump incomplete; the lines above stop at the failing read."));
        return;
    }
    Serial.print(F("[dump] "));
    Serial.print(DUMP_END / TPS25751EepromUpdater::kFlashReadBytes);
    Serial.print(F(" FLrd calls in "));
    Serial.print(elapsed);
    Serial.println(F(" ms"));
}

/// Everything the report prints except the dump. Issues only FLrd and FLvy.
/// Returns true when MODE reads APP.
static bool printStatus()
{
    bool patch = false;
    const bool app = printMode(&patch);
    printVersion();
    printBootFlags();

    if (!app) {
        Serial.println(F("[report] Not in APP mode; SLVAFL1 describes EEPROM access in APP mode. Skipping the flash tasks."));
        if (patch) {
            Serial.println(F("[report] MODE reads PTCH: the controller found no bundle it could boot and waits for a patch over I2Ct. To recover, send l to load the embedded image into its RAM, then pb to program the EEPROM, then x to restart from it."));
        } else {
            Serial.println(F("[report] This example recovers only from PTCH. In any other mode, wait for APP or restart the controller, then send r."));
        }
        return false;
    }
    printPointers();
    printAppConfigOffsets();
    printTarget();
    printVerify(EepromRegion::Low);
    printVerify(EepromRegion::High);
    return true;
}

static void runReport()
{
    Serial.println(F("[report] TPS25751 EEPROM inspection (read-only)"));
    if (printStatus()) {
        dumpEeprom();
        Serial.println(F("[report] Done. No EEPROM write was made."));
    }
}

// ---------------------------------------------------------------------------
// Serial input
// ---------------------------------------------------------------------------

/// True after a CR ended a line: a LF that follows is the rest of that CRLF.
static bool lastWasCr = false;

/// Reads one line, blocking, and returns the number of characters in it. CR,
/// LF, and CRLF each end a line. The count includes any NUL character the line
/// held, so a caller that compares the text must compare the length as well. A
/// line longer than the buffer comes back empty, with tooLong set.
static size_t readLine(char* buf, size_t size, bool& tooLong)
{
    size_t n = 0;
    tooLong = false;
    for (;;) {
        if (Serial.available() <= 0) {
            delay(1);
            continue;
        }
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' && lastWasCr) {
            lastWasCr = false;  // second half of a CRLF
            continue;
        }
        lastWasCr = (c == '\r');
        if (c == '\r' || c == '\n') {
            if (tooLong) {
                n = 0;
            }
            buf[n] = '\0';
            return n;
        }
        if (n + 1 < size) {
            buf[n++] = c;
        } else {
            tooLong = true;
        }
    }
}

/// Throws away the serial input that has already arrived and returns the
/// number of bytes dropped. The LF of the CRLF that ended the command line is
/// dropped without being counted.
static size_t discardPendingInput()
{
    size_t dropped = 0;
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n' && lastWasCr && dropped == 0) {
            lastWasCr = false;
            continue;
        }
        ++dropped;
    }
    return dropped;
}

/// Asks for confirmation and waits for one line. Only the exact line "yes",
/// sent after the prompt, confirms.
///
/// The prompt is printed and flushed first, and only then is the input that has
/// already arrived discarded. So nothing received before the prompt went out
/// can confirm: not a "yes" queued behind the command, not one typed during a
/// long report, and not one that arrived while the plan was being printed.
static bool confirm()
{
    Serial.println(F("[confirm] Type yes to proceed; anything else cancels."));
    Serial.flush();
    const size_t dropped = discardPendingInput();
    if (dropped > 0) {
        Serial.print(F("[confirm] Discarded "));
        Serial.print(dropped);
        Serial.println(F(" byte(s) of input that arrived before this prompt."));
    }

    char line[16];
    bool tooLong = false;
    const size_t length = readLine(line, sizeof(line), tooLong);
    // The length is compared too: "yes" followed by a NUL and more characters
    // is not the line "yes".
    if (!tooLong && length == 3 && memcmp(line, "yes", 3) == 0) {
        Serial.println(F("[confirm] Confirmed."));
        return true;
    }
    Serial.println(F("[confirm] Cancelled; nothing was written."));
    return false;
}

// ---------------------------------------------------------------------------
// Checks and plan shared by the commands that write
// ---------------------------------------------------------------------------

/// The embedded image is a bundle, MODE reads APP, and BOOT_FLAGS reports the
/// EEPROM. Prints the reason and returns false otherwise.
static bool writeAllowed()
{
    const EepromUpdateStatus image = imageStatus();
    if (image != EepromUpdateStatus::Success) {
        Serial.print(F("[refused] The embedded image is not a usable patch bundle: checkImage() = "));
        Serial.println(TPS25751EepromUpdater::toString(image));
        Serial.println(F("[refused] Replace src/patch_bundle.h (scripts/bin_to_header.py). Nothing was written."));
        return false;
    }
    TPS25751Mode mode;
    if (!pd.readMode(mode) || !mode.isApplicationMode()) {
        Serial.println(F("[refused] MODE does not read APP. Nothing was written."));
        return false;
    }
    TPS25751BootFlags flags;
    if (!pd.readBootFlags(flags) || !flags.i2cEepromPresent()) {
        Serial.println(F("[refused] BOOT_FLAGS does not report an I2C EEPROM. Nothing was written."));
        return false;
    }
    return true;
}

static bool selectTarget(EepromRegion& target)
{
    const EepromUpdateStatus s = updater.selectTargetRegion(target);
    if (s == EepromUpdateStatus::Success) {
        return true;
    }
    Serial.print(F("[refused] No target region: selectTargetRegion() = "));
    Serial.print(TPS25751EepromUpdater::toString(s));
    Serial.println(F(". Nothing was written."));
    return false;
}

/// The plan every write command prints before it asks for confirmation.
static void printPlan(const __FlashStringHelper* action, EepromRegion target, bool thenOther)
{
    Serial.print(F("[plan] Action: "));
    Serial.println(action);
    Serial.print(F("[plan] Target region: "));
    Serial.print(TPS25751EepromUpdater::toString(target));
    if (thenOther) {
        Serial.print(F(", then "));
        Serial.print(TPS25751EepromUpdater::toString(TPS25751EepromUpdater::otherRegion(target)));
    }
    Serial.println();
    Serial.print(F("[plan] Image: "));
    Serial.print(IMAGE_SIZE);
    Serial.print(F(" bytes, version "));
    printHex32(TPS25751EepromUpdater::imageVersion(IMAGE, IMAGE_SIZE));
    Serial.println();
    Serial.print(F("[plan] Current VERSION: "));
    uint32_t version = 0;
    if (readVersion(version)) {
        printHex32(version);
        Serial.println();
    } else {
        Serial.println(F("read failed"));
    }
    printPointers();
    Serial.println(F("[plan] This command writes the EEPROM."));
}

// ---------------------------------------------------------------------------
// Progress and the cut points of c1 and c2
// ---------------------------------------------------------------------------

static uint8_t cutAfterStep = 0;  // 1 or 2: halt in the progress callback after that step
static EepromUpdateStep progressStep = EepromUpdateStep::None;
static EepromRegion progressTarget = EepromRegion::Low;
static uint8_t progressDecile = 0xFF;

static void resetProgress(uint8_t cut)
{
    cutAfterStep = cut;
    progressStep = EepromUpdateStep::None;
    progressDecile = 0xFF;
}

static void printProgressLine(EepromRegion target, const __FlashStringHelper* phase,
                              uint32_t done, uint32_t total)
{
    Serial.print(F("[progress] "));
    Serial.print(TPS25751EepromUpdater::toString(target));
    Serial.print(' ');
    Serial.print(phase);
    Serial.print(' ');
    Serial.print((total == 0) ? 100u : static_cast<unsigned>((static_cast<uint64_t>(done) * 100) / total));
    Serial.print(F("% ("));
    Serial.print(done);
    Serial.print('/');
    Serial.print(total);
    Serial.println(')');
}

static void printCutLine(uint8_t step)
{
    Serial.print(F("[cut] Step "));
    Serial.print(static_cast<unsigned>(step));
    Serial.println(F(" of SLVAFL1 Figure 2-3 is complete. Power-cycle the board now."));
}

/// Stops the update where it stands. Reading the pointers here issues FLrd from
/// inside the progress callback, which the updater forbids for a callback that
/// returns; this one never does, so the abandoned update cannot be disturbed.
[[noreturn]] static void haltAfterStep(uint8_t step)
{
    Serial.println(F("[cut] Update stopped inside the progress callback; updateRegion() does not return."));
    printPointers();
    printTiming();
    printCutLine(step);
    Serial.println(F("[halt] Halted. This command never returns; power-cycle or reset the Teensy to continue."));
    Serial.flush();
    for (;;) {
        delay(1000);
    }
}

/// Progress callback: one line per 10% of each phase, and the cut points.
static void onProgress(uint32_t done, uint32_t total, void*)
{
    const EepromUpdateStep step = updater.lastStep();
    const EepromRegion target = updater.lastTarget();
    if (step != progressStep || target != progressTarget) {
        progressStep = step;
        progressTarget = target;
        progressDecile = 0xFF;
    }

    const uint8_t decile =
        (total == 0) ? 10 : static_cast<uint8_t>((static_cast<uint64_t>(done) * 10) / total);
    if (decile != progressDecile) {
        progressDecile = decile;
        printProgressLine(target, step == EepromUpdateStep::WriteBundle ? F("write") : F("compare"),
                          done, total);
    }

    // The write phase reports done == 0 once step 1 is complete and no task of
    // step 2 has been issued, and done == total once step 2 is complete.
    if (step == EepromUpdateStep::WriteBundle) {
        if (cutAfterStep == 1 && done == 0) {
            haltAfterStep(1);
        }
        if (cutAfterStep == 2 && done == total) {
            haltAfterStep(2);
        }
    }
}

static void printUpdateResult(EepromUpdateStatus s, uint32_t elapsedMs)
{
    Serial.print(F("[update] Result: "));
    Serial.println(TPS25751EepromUpdater::toString(s));
    Serial.print(F("[update] Last step: "));
    Serial.println(TPS25751EepromUpdater::toString(updater.lastStep()));
    Serial.print(F("[update] Last target: "));
    Serial.println(TPS25751EepromUpdater::toString(updater.lastTarget()));
    Serial.print(F("[update] Elapsed: "));
    Serial.print(elapsedMs);
    Serial.println(F(" ms"));
    // A stop by one of the checks of step 2 means bytes may lie where they do
    // not belong, or that an earlier attempt left some there.
    if (updater.lastStep() == EepromUpdateStep::WriteBundle &&
        (s == EepromUpdateStatus::CompareMismatch || s == EepromUpdateStatus::PointerNeighborsChanged)) {
        Serial.println(F("[update] A check of step 2 stopped the write. Dump the EEPROM with r and find the misplaced bytes before any further write."));
    }
}

// ---------------------------------------------------------------------------
// p: pointer test (hardware stage 2)
// ---------------------------------------------------------------------------

static bool readWindow(uint8_t* buf)
{
    for (size_t off = 0; off < POINTER_WINDOW; off += TPS25751EepromUpdater::kFlashReadBytes) {
        if (updater.readFlash(HIGH_POINTER + off, buf + off) != EepromUpdateStatus::Success) {
            return false;
        }
    }
    return true;
}

static void printWindow(const __FlashStringHelper* tag, const __FlashStringHelper* label,
                        const uint8_t* buf, bool ok)
{
    Serial.print(tag);
    Serial.print(label);
    if (!ok) {
        Serial.println(F(": read failed"));
        return;
    }
    Serial.println(':');
    for (size_t off = 0; off < POINTER_WINDOW; off += TPS25751EepromUpdater::kFlashReadBytes) {
        printRow(HIGH_POINTER + off, buf + off);
    }
}

/// Compares bytes 4 to 63 of two windows: everything after the pointer word.
static void printNeighbors(const __FlashStringHelper* what, const uint8_t* before, bool beforeOk,
                           const uint8_t* after, bool afterOk)
{
    Serial.print(F("[p] Bytes 0x0404 to 0x043F unchanged by the "));
    Serial.print(what);
    if (!beforeOk || !afterOk) {
        Serial.println(F(": unknown (read failed)"));
        return;
    }
    Serial.println(memcmp(before + 4, after + 4, POINTER_WINDOW - 4) == 0 ? F(": yes") : F(": NO"));
}

static void printPointerWrite(uint32_t value, EepromUpdateStatus s)
{
    Serial.print(F("[p] writeRegionPointer(High, "));
    printHex32(value);
    Serial.print(F("): "));
    Serial.println(TPS25751EepromUpdater::toString(s));
}

/// True while the High pointer is 0 because a pointer test in this run left it
/// so: its restore did not report Success, and no other command that writes
/// (p2, u, b, c1 to c4, v) has started since. False after a Teensy reset.
///
/// Only then may the pointer test start from a High pointer of 0. The test
/// ends by making that pointer valid, and this example has no other way to
/// validate a pointer without FLvy and the compare. A High pointer that is 0
/// for another reason (v, c1, c2, a failed update) can stand in front of a
/// region that is not a good bundle, and FLvy may check the header only, so
/// from there the High region has to be rewritten with u or b.
static bool pointerTestLeftHighErased = false;

static void cmdPointerTest()
{
    if (!writeAllowed()) {
        return;
    }

    // The Low pointer must be valid whatever the High pointer reads: the
    // controller boots Low while the test holds the High pointer at 0.
    const uint32_t highStart = TPS25751EepromUpdater::bundleStart(EepromRegion::High);
    uint32_t low = 0;
    uint32_t high = 0;
    if (updater.readRegionPointer(EepromRegion::Low, low) != EepromUpdateStatus::Success ||
        updater.readRegionPointer(EepromRegion::High, high) != EepromUpdateStatus::Success ||
        low != TPS25751EepromUpdater::bundleStart(EepromRegion::Low)) {
        Serial.println(F("[refused] The pointer test needs the Low pointer valid. Nothing was written."));
        printPointers();
        return;
    }

    // Start states: both pointers valid, or the High pointer 0 because this
    // run's own test left it so.
    const bool startedErased = (high == 0);
    if (startedErased && !pointerTestLeftHighErased) {
        Serial.println(F("[refused] The High pointer is 0, and no pointer test in this run left it so. Run u or b to rewrite the High region. Nothing was written."));
        printPointers();
        return;
    }
    if (!startedErased && high != highStart) {
        Serial.println(F("[refused] The High pointer is neither valid nor 0. Run u or b to rewrite the High region. Nothing was written."));
        printPointers();
        return;
    }

    printPlan(F("p: pointer test. Writes the High pointer to 0, then back to 0x00004400"),
              EepromRegion::High, false);
    if (startedErased) {
        Serial.println(F("[plan] Start state: Low pointer valid, High pointer 0, left by a pointer test in this run whose restore failed. The test ends with the High pointer valid."));
    } else {
        Serial.println(F("[plan] Start state: both pointers valid."));
    }
    if (!confirm()) {
        return;
    }

    exec.reset();
    uint8_t before[POINTER_WINDOW];
    uint8_t afterErase[POINTER_WINDOW];
    uint8_t afterRestore[POINTER_WINDOW];

    const bool beforeOk = readWindow(before);
    printWindow(F("[p] "), F("Before"), before, beforeOk);
    if (!beforeOk) {
        Serial.println(F("[p] Stopped before any write."));
        printTiming();
        return;
    }

    const EepromUpdateStatus erase = updater.writeRegionPointer(EepromRegion::High, 0);
    printPointerWrite(0, erase);
    const bool eraseOk = readWindow(afterErase);
    printWindow(F("[p] "), F("After the erase"), afterErase, eraseOk);
    printNeighbors(F("erase"), before, beforeOk, afterErase, eraseOk);
    if (erase != EepromUpdateStatus::Success) {
        Serial.println(F("[p] The erase did not report Success; attempting the restore anyway."));
    }

    const EepromUpdateStatus restore = updater.writeRegionPointer(EepromRegion::High, highStart);
    printPointerWrite(highStart, restore);
    // Only a restore that did not report Success lets the next test start from
    // a High pointer of 0.
    pointerTestLeftHighErased = (restore != EepromUpdateStatus::Success);
    if (pointerTestLeftHighErased) {
        Serial.println(F("[p] The restore did not report Success. Send p again to repeat it; p2, u, b, c1 to c4, and v end that option."));
    }
    const bool restoreOk = readWindow(afterRestore);
    printWindow(F("[p] "), F("After the restore"), afterRestore, restoreOk);
    printNeighbors(F("restore"), before, beforeOk, afterRestore, restoreOk);
    Serial.print(F("[p] All 64 bytes equal the bytes before the test"));
    if (startedErased) {
        Serial.println(F(": not applicable (the High pointer started at 0)"));
    } else if (!restoreOk) {
        Serial.println(F(": unknown (read failed)"));
    } else {
        Serial.println(memcmp(before, afterRestore, POINTER_WINDOW) == 0 ? F(": yes") : F(": NO"));
    }

    printPointers();
    printTiming();
}

// ---------------------------------------------------------------------------
// p2: does FLwd write more than the bytes it is given?
//
// p cannot show this. The FLrd inside writeRegionPointer() leaves the bytes
// after the pointer in DATA, so a controller that wrote more than the four
// bytes given would write those bytes back with their own values. p2 loads
// DATA with other bytes first, the start of the High bundle, and then writes
// the High pointer with the value it already holds. The pointer never changes,
// and any byte after it that changes came out of DATA.
// ---------------------------------------------------------------------------

/// Reads the 64-byte DATA register as the host sees it into @p out and prints
/// it. Read only. Returns false, after saying so, when the read fails.
static bool readAndPrintDataRegister(uint8_t* out)
{
    auto data = pd.readDataRegister(/*validate=*/false);
    if (!data || data->size() < POINTER_WINDOW) {
        Serial.println(F("[p2] DATA register before FLwd: read failed"));
        return false;
    }
    memcpy(out, data->raw(), POINTER_WINDOW);
    Serial.println(F("[p2] DATA register before FLwd:"));
    for (size_t off = 0; off < POINTER_WINDOW; off += 16) {
        char offset[5];
        snprintf(offset, sizeof(offset), "%02X:", static_cast<unsigned>(off));
        Serial.print(F("[p2] DATA+"));
        Serial.print(offset);
        for (size_t i = 0; i < 16; ++i) {
            char byte[4];
            snprintf(byte, sizeof(byte), " %02x", out[off + i]);
            Serial.print(byte);
        }
        Serial.println();
    }
    return true;
}

static void printStatusLine(const __FlashStringHelper* what, EepromUpdateStatus s)
{
    Serial.print(F("[p2] "));
    Serial.print(what);
    Serial.print(F(": "));
    Serial.println(TPS25751EepromUpdater::toString(s));
}

static void cmdPointerRewriteTest()
{
    pointerTestLeftHighErased = false;  // another writing command has started
    if (!writeAllowed()) {
        return;
    }
    const uint32_t highStart = TPS25751EepromUpdater::bundleStart(EepromRegion::High);
    uint32_t low = 0;
    uint32_t high = 0;
    if (updater.readRegionPointer(EepromRegion::Low, low) != EepromUpdateStatus::Success ||
        updater.readRegionPointer(EepromRegion::High, high) != EepromUpdateStatus::Success ||
        low != TPS25751EepromUpdater::bundleStart(EepromRegion::Low) || high != highStart) {
        Serial.println(F("[refused] p2 needs both region pointers valid. Nothing was written."));
        printPointers();
        return;
    }

    printPlan(F("p2: pointer rewrite test. Writes the High pointer with the value it already holds, 0x00004400"),
              EepromRegion::High, false);
    Serial.println(F("[plan] The pointer's value does not change. DATA is loaded with the 16 bytes at 0x00004400 first, so a write of more than four bytes would change the bytes after the pointer."));
    if (!confirm()) {
        return;
    }

    exec.reset();
    uint8_t before[POINTER_WINDOW];
    uint8_t after[POINTER_WINDOW];

    // 1. The 64 bytes at the High pointer as they are.
    const bool beforeOk = readWindow(before);
    printWindow(F("[p2] "), F("Before"), before, beforeOk);
    if (!beforeOk) {
        Serial.println(F("[p2] Stopped before any write."));
        printTiming();
        return;
    }

    // 2. Load DATA. FLrd returns its 16 bytes in DATA bytes 0 to 15. If bytes
    //    4 to 15 are erased bytes, they are what follows the pointer already
    //    and the test would prove nothing. Whether the bytes really are in
    //    DATA when FLwd runs is settled in step 4, from DATA itself.
    uint8_t loaded[TPS25751EepromUpdater::kFlashReadBytes];
    const EepromUpdateStatus load = updater.readFlash(highStart, loaded);
    printStatusLine(F("readFlash(0x00004400)"), load);
    if (load != EepromUpdateStatus::Success) {
        Serial.println(F("[p2] Stopped before any write."));
        printPointers();
        printTiming();
        return;
    }
    printRow(highStart, loaded);
    bool allErased = true;
    for (size_t i = 4; i < sizeof(loaded); ++i) {
        if (loaded[i] != 0xFF) {
            allErased = false;
        }
    }
    if (allErased) {
        Serial.println(F("[p2] Bytes 4 to 15 at 0x00004400 are all 0xFF, so writing them would show nothing. The test cannot be conclusive. Stopped before any write."));
        printPointers();
        printTiming();
        return;
    }

    // 3. Point the write address at the High pointer.
    const EepromUpdateStatus addressed = updater.setWriteAddress(HIGH_POINTER);
    printStatusLine(F("setWriteAddress(0x00000400)"), addressed);
    if (addressed != EepromUpdateStatus::Success) {
        Serial.println(F("[p2] FLad did not succeed, so FLwd was not attempted. Nothing was written."));
        printPointers();
        printTiming();
        return;
    }

    // 4. What DATA holds now, which is what FLwd finds beyond its four bytes.
    //    The test can show an over-write only where a DATA byte differs from
    //    the byte it would replace, so that is counted here, from DATA as read
    //    and not from what FLrd returned. With no such byte, or with no DATA
    //    read, a clean result would prove nothing, and FLwd is not issued.
    uint8_t dataNow[POINTER_WINDOW];
    if (!readAndPrintDataRegister(dataNow)) {
        Serial.println(F("[p2] Without the DATA bytes the test cannot be conclusive. Stopped before FLwd; nothing was written."));
        printPointers();
        printTiming();
        return;
    }
    size_t showing = 0;
    size_t firstShowing = 0;  // the first DATA position that differs, 0 for none
    for (size_t i = 4; i < POINTER_WINDOW; ++i) {
        if (dataNow[i] != before[i]) {
            if (showing == 0) {
                firstShowing = i;
            }
            ++showing;
        }
    }
    Serial.print(F("[p2] DATA bytes 4 to 63 that differ from the bytes at 0x0404 to 0x043F: "));
    Serial.print(showing);
    Serial.println(F(" of 60"));
    if (showing == 0) {
        Serial.println(F("[p2] No DATA byte differs from the byte an over-write would replace. The test cannot be conclusive. Stopped before FLwd; nothing was written."));
        printPointers();
        printTiming();
        return;
    }

    // 5. The pointer's own value, least significant byte first: 00 44 00 00.
    const uint8_t word[4] = {
        static_cast<uint8_t>(highStart & 0xFF),
        static_cast<uint8_t>((highStart >> 8) & 0xFF),
        static_cast<uint8_t>((highStart >> 16) & 0xFF),
        static_cast<uint8_t>((highStart >> 24) & 0xFF),
    };
    const EepromUpdateStatus written = updater.writeData(word, sizeof(word));
    printStatusLine(F("writeData(00 44 00 00)"), written);

    // 6. The 64 bytes afterwards.
    const bool afterOk = readWindow(after);
    printWindow(F("[p2] "), F("After"), after, afterOk);

    // 7. The verdict.
    Serial.print(F("[p2] Bytes 0x0400 to 0x0403 read 00 44 00 00"));
    if (!afterOk) {
        Serial.println(F(": unknown (read failed)"));
    } else {
        Serial.println(memcmp(after, word, sizeof(word)) == 0 ? F(": yes") : F(": NO"));
    }
    const bool flwdOk = (written == EepromUpdateStatus::Success);
    const bool changed = afterOk && memcmp(before + 4, after + 4, POINTER_WINDOW - 4) != 0;
    Serial.print(F("[p2] Bytes 0x0404 to 0x043F equal the bytes before"));
    if (!afterOk) {
        Serial.println(F(": unknown (read failed)"));
    } else if (changed) {
        Serial.println(F(": NO"));
        for (size_t i = 4; i < POINTER_WINDOW; ++i) {
            if (before[i] == after[i]) {
                continue;
            }
            Serial.print(F("[p2] Changed "));
            printHex32(HIGH_POINTER + static_cast<uint32_t>(i));
            Serial.print(F(": "));
            printHex8(before[i]);
            Serial.print(F(" -> "));
            printHex8(after[i]);
            Serial.println();
        }
    } else if (!flwdOk) {
        // Unchanged bytes after a write that did not report success say
        // nothing about what a successful FLwd writes.
        Serial.println(F(": not conclusive (writeData() did not return Success)"));
    } else {
        Serial.println(F(": yes"));
    }

    if (changed) {
        Serial.println(F("[p2] Verdict: FLwd wrote more than the four bytes given."));
        Serial.println(F("[p2] No command of this example restores those bytes. Every later full dump will differ from the full-flash image at those offsets."));
    } else if (!afterOk) {
        Serial.println(F("[p2] Verdict: none. The bytes could not be read after the write."));
    } else if (!flwdOk) {
        Serial.println(F("[p2] Verdict: not conclusive. writeData() did not return Success, so this run does not show what FLwd writes."));
    } else {
        // A write of N bytes covers DATA positions 0 to N-1, so the first
        // differing position P shows any write of P+1 bytes or more. A write
        // of 5 to P bytes would have put back the bytes already there.
        Serial.print(F("[p2] Verdict: FLwd wrote only the four bytes given. A write of "));
        Serial.print(firstShowing + 1);
        Serial.print(F(" bytes or more would have shown (first differing DATA position "));
        Serial.print(firstShowing);
        Serial.print(F("; "));
        Serial.print(showing);
        Serial.println(F(" of 60 positions differ)."));
    }
    if (afterOk && memcmp(after, word, sizeof(word)) != 0) {
        Serial.println(F("[p2] The High pointer no longer reads 0x00004400. Run u or b to rewrite the High region."));
    }
    Serial.println(F("[p2] No repair was attempted. Nothing further was written."));

    // 8.
    printPointers();
    printTiming();
}

// ---------------------------------------------------------------------------
// u, b: updates
// ---------------------------------------------------------------------------

static void cmdUpdate(bool both)
{
    pointerTestLeftHighErased = false;  // another writing command has started
    if (!writeAllowed()) {
        return;
    }
    EepromRegion target = EepromRegion::Low;
    if (!selectTarget(target)) {
        return;
    }
    printPlan(both ? F("b: updateBothRegions() with the embedded image")
                   : F("u: updateInactiveRegion() with the embedded image"),
              target, both);
    if (!confirm()) {
        return;
    }

    exec.reset();
    resetProgress(0);
    const uint32_t started = millis();
    const EepromUpdateStatus s = both ? updater.updateBothRegions(IMAGE, IMAGE_SIZE)
                                      : updater.updateInactiveRegion(IMAGE, IMAGE_SIZE);
    printUpdateResult(s, millis() - started);
    printPointers();
    printTiming();
    if (s == EepromUpdateStatus::Success) {
        Serial.println(F("[update] The controller still runs the bundle it booted. Send x to restart it."));
    }
}

// ---------------------------------------------------------------------------
// c1 to c4: interrupted updates (hardware stage 4)
// ---------------------------------------------------------------------------

static void cmdCut(uint8_t step)
{
    pointerTestLeftHighErased = false;  // another writing command has started
    if (!writeAllowed()) {
        return;
    }
    EepromRegion target = EepromRegion::Low;
    if (!selectTarget(target)) {
        return;
    }

    const __FlashStringHelper* action =
        (step == 1) ? F("c1: update the inactive region and halt after step 1 (target pointer erased)")
      : (step == 2) ? F("c2: update the inactive region and halt after step 2 (bundle written, pointer still 0)")
      : (step == 3) ? F("c3: update the inactive region and stop after step 3 (target pointer restored, old pointer kept)")
                    : F("c4: update the inactive region completely, steps 1 to 4");
    printPlan(action, target, false);
    if (!confirm()) {
        return;
    }

    exec.reset();
    resetProgress(step <= 2 ? step : 0);
    EepromUpdateOptions opt;
    opt.invalidateOldRegion = (step != 3);  // c3 leaves out step 4

    const uint32_t started = millis();
    const EepromUpdateStatus s = updater.updateRegion(target, IMAGE, IMAGE_SIZE, opt);
    resetProgress(0);

    // c1 and c2 reach this line only when the update failed before its cut point.
    printUpdateResult(s, millis() - started);
    printPointers();
    printTiming();
    if (s == EepromUpdateStatus::Success && step >= 3) {
        printCutLine(step);
    } else {
        Serial.print(F("[cut] The update ended before the cut point; step "));
        Serial.print(static_cast<unsigned>(step));
        Serial.println(F(" was not completed. See the result above."));
    }
}

// ---------------------------------------------------------------------------
// v: FLvy experiment (hardware stage 3), built from the public primitives
// ---------------------------------------------------------------------------

static void printStep(const __FlashStringHelper* what, EepromUpdateStatus s)
{
    Serial.print(F("[v] "));
    Serial.print(what);
    Serial.print(F(": "));
    Serial.println(TPS25751EepromUpdater::toString(s));
}

/// One chunk at the current write address, with the updater's busy rule.
static EepromUpdateStatus writeChunkWithRetry(uint32_t addr, const uint8_t* data, size_t len)
{
    EepromUpdateStatus s = updater.writeData(data, len);
    for (uint8_t retry = 0; s == EepromUpdateStatus::FlashBusy && retry < FLWD_BUSY_RETRIES; ++retry) {
        delay(FLWD_BUSY_RETRY_DELAY_MS);
        s = updater.setWriteAddress(addr);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
        s = updater.writeData(data, len);
    }
    return s;
}

/// Image bytes @p from to @p to as chunks at the current write address, with a
/// progress line at each new 10%. @p written follows the bytes written so far.
static EepromUpdateStatus writeImageChunks(EepromRegion target, size_t from, size_t to, size_t& written)
{
    const uint32_t start = TPS25751EepromUpdater::bundleStart(target);
    size_t offset = from;
    while (offset < to) {
        const size_t remaining = to - offset;
        const size_t n = (remaining < TPS25751EepromUpdater::kFlashWriteChunk)
                             ? remaining : TPS25751EepromUpdater::kFlashWriteChunk;
        const uint8_t decileBefore = static_cast<uint8_t>((static_cast<uint64_t>(offset) * 10) / IMAGE_SIZE);
        const EepromUpdateStatus s = writeChunkWithRetry(start + offset, IMAGE + offset, n);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
        offset += n;
        written = offset;
        const uint8_t decileAfter = static_cast<uint8_t>((static_cast<uint64_t>(offset) * 10) / IMAGE_SIZE);
        if (decileAfter != decileBefore) {
            printProgressLine(target, F("write"), offset, IMAGE_SIZE);
        }
    }
    return EepromUpdateStatus::Success;
}

/// Writes the image to the target region from the public primitives, with the
/// sequence and the checks of step 2 of updateRegion(): the check on the bytes
/// after the pointer before any task, FLad, two chunks, the read of the
/// pointer row, the read-back at the bundle start, a fresh FLad, and the
/// remaining chunks. Each check prints its result. After a failed check
/// nothing further is written.
///
/// @p pointerRow is the 16 bytes at the target pointer as read after the
/// erase. @p written receives the image bytes sent when the call returns.
static EepromUpdateStatus writeImageWithPrimitives(EepromRegion target, const uint8_t* pointerRow,
                                                   size_t& written)
{
    const uint32_t start = TPS25751EepromUpdater::bundleStart(target);
    const uint32_t pointerAddr = TPS25751EepromUpdater::regionPointerAddress(target);
    const size_t early = (IMAGE_SIZE < TPS25751EepromUpdater::kEarlyReadBackBytes)
                             ? IMAGE_SIZE : TPS25751EepromUpdater::kEarlyReadBackBytes;
    written = 0;

    // Before any task: the trace of an earlier misplaced write.
    if (memcmp(pointerRow + 4, IMAGE, 12) == 0) {
        Serial.println(F("[v] Check before the write: FAILED. The 12 bytes after the pointer hold the start of the image, the trace of an earlier misplaced write. Dump and repair the EEPROM first."));
        return EepromUpdateStatus::PointerNeighborsChanged;
    }
    Serial.println(F("[v] Check before the write: passed. The 12 bytes after the pointer are not the start of the image."));

    printProgressLine(target, F("write"), 0, IMAGE_SIZE);
    EepromUpdateStatus s = updater.setWriteAddress(start);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    s = writeImageChunks(target, 0, early, written);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // The read-back: the pointer row first, the bundle start last, so that the
    // last read before the next FLad is inside the region being written.
    uint8_t now[TPS25751EepromUpdater::kFlashReadBytes];
    s = updater.readFlash(pointerAddr, now);
    if (s != EepromUpdateStatus::Success) {
        printStep(F("Read of the pointer row after the first chunks"), s);
        return s;
    }
    uint32_t mismatch = 0;
    s = updater.compareRegion(target, IMAGE, early, &mismatch);
    if (s == EepromUpdateStatus::CompareMismatch) {
        Serial.print(F("[v] Check after the first chunks: FAILED. They do not read back at the bundle start; first difference at image offset "));
        printHex32(mismatch);
        Serial.println();
        return s;
    }
    if (s != EepromUpdateStatus::Success) {
        printStep(F("Read-back of the first chunks"), s);
        return s;
    }
    const bool wordIsZero = (now[0] == 0 && now[1] == 0 && now[2] == 0 && now[3] == 0);
    if (!wordIsZero || memcmp(now + 4, pointerRow + 4, sizeof(now) - 4) != 0) {
        Serial.println(F("[v] Check after the first chunks: FAILED. The 16 bytes at the pointer changed while they were written."));
        printRow(pointerAddr, now);
        return EepromUpdateStatus::PointerNeighborsChanged;
    }
    Serial.println(F("[v] Check after the first chunks: passed. They read back at the bundle start, and the 16 bytes at the pointer are unchanged."));

    if (early == IMAGE_SIZE) {
        return EepromUpdateStatus::Success;
    }
    s = updater.setWriteAddress(start + static_cast<uint32_t>(early));
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    return writeImageChunks(target, early, IMAGE_SIZE, written);
}

/// Closes the experiment. @p checkFailed is set when the image write stopped on
/// one of its checks: bytes may then lie where they do not belong, and an
/// update is not the next step.
static void finishExperiment(EepromRegion target, const __FlashStringHelper* note, bool checkFailed = false)
{
    Serial.print(F("[v] "));
    Serial.println(note);
    Serial.print(F("[v] The "));
    Serial.print(TPS25751EepromUpdater::toString(target));
    if (checkFailed) {
        Serial.println(F(" pointer is left at 0, so that region cannot boot. A check failed: dump the EEPROM with r and find the misplaced bytes before any further write."));
    } else {
        Serial.println(F(" pointer is left at 0, so that region cannot boot. Run u or b to repair it."));
    }
    printPointers();
    printTiming();
}

static void cmdVerifyExperiment()
{
    pointerTestLeftHighErased = false;  // another writing command has started
    if (!writeAllowed()) {
        return;
    }
    // The byte to corrupt sits in the middle of the image, well past the header.
    if (IMAGE_SIZE < 0x400) {
        Serial.println(F("[refused] The image is too small for this experiment. Nothing was written."));
        return;
    }
    EepromRegion target = EepromRegion::Low;
    if (!selectTarget(target)) {
        return;
    }
    const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
    uint32_t otherPointer = 0;
    if (updater.readRegionPointer(other, otherPointer) != EepromUpdateStatus::Success ||
        otherPointer != TPS25751EepromUpdater::bundleStart(other)) {
        Serial.println(F("[refused] The other region's pointer is not valid, and this experiment erases the target pointer. Nothing was written."));
        printPointers();
        return;
    }

    printPlan(F("v: FLvy experiment. Erases the target pointer, writes the image, corrupts one byte, leaves the pointer at 0"),
              target, false);
    if (!confirm()) {
        return;
    }

    exec.reset();
    const uint32_t start = TPS25751EepromUpdater::bundleStart(target);

    // 1. Erase the target pointer, as step 1 of an update does.
    EepromUpdateStatus s = updater.writeRegionPointer(target, 0);
    Serial.print(F("[v] writeRegionPointer("));
    Serial.print(TPS25751EepromUpdater::toString(target));
    Serial.print(F(", 0x00000000): "));
    Serial.println(TPS25751EepromUpdater::toString(s));
    if (s != EepromUpdateStatus::Success) {
        Serial.println(F("[v] The pointer erase failed; the experiment stops before the region is written."));
        printPointers();
        printTiming();
        return;
    }

    // The 16 bytes at the erased pointer: the reference for the checks the
    // image write makes, as the read-back of step 1 is for updateRegion().
    uint8_t pointerRow[TPS25751EepromUpdater::kFlashReadBytes];
    s = updater.readFlash(TPS25751EepromUpdater::regionPointerAddress(target), pointerRow);
    printStep(F("Read of the pointer row after the erase"), s);
    if (s != EepromUpdateStatus::Success) {
        finishExperiment(target, F("The pointer row could not be read; the experiment stops before the region is written."));
        return;
    }

    // 2. Write the image, with the read-back after the first two chunks.
    const uint32_t started = millis();
    size_t written = 0;
    s = writeImageWithPrimitives(target, pointerRow, written);
    printStep(F("Image write"), s);
    Serial.print(F("[v] Image write took "));
    Serial.print(millis() - started);
    Serial.println(F(" ms"));
    if (s != EepromUpdateStatus::Success) {
        Serial.print(F("[v] The image write stopped after "));
        Serial.print(written);
        Serial.println(F(" bytes. Nothing further was written."));
        finishExperiment(target, F("The image write failed; the experiment stops here."),
                         s == EepromUpdateStatus::CompareMismatch ||
                             s == EepromUpdateStatus::PointerNeighborsChanged);
        return;
    }

    // 3. The clean region must verify and compare before it is corrupted.
    uint32_t mismatch = 0;
    const EepromUpdateStatus cleanVerify = updater.verifyBundle(start);
    printStep(F("FLvy after the clean write"), cleanVerify);
    const EepromUpdateStatus cleanCompare = updater.compareRegion(target, IMAGE, IMAGE_SIZE, &mismatch);
    printStep(F("compareRegion() after the clean write"), cleanCompare);
    if (cleanVerify != EepromUpdateStatus::Success || cleanCompare != EepromUpdateStatus::Success) {
        finishExperiment(target, F("The clean write did not verify; the experiment stops here."));
        return;
    }

    // 4. Overwrite one byte: a full chunk that differs from the image in that byte.
    const size_t offset = (IMAGE_SIZE / 2 / TPS25751EepromUpdater::kFlashWriteChunk) *
                              TPS25751EepromUpdater::kFlashWriteChunk + 5;
    const size_t chunkStart = offset - 5;
    const size_t remaining = IMAGE_SIZE - chunkStart;
    const size_t n = (remaining < TPS25751EepromUpdater::kFlashWriteChunk)
                         ? remaining : TPS25751EepromUpdater::kFlashWriteChunk;
    uint8_t chunk[TPS25751EepromUpdater::kFlashWriteChunk];
    memcpy(chunk, IMAGE + chunkStart, n);
    chunk[5] = static_cast<uint8_t>(chunk[5] ^ 0xFF);

    Serial.print(F("[v] Corrupting image offset "));
    printHex32(static_cast<uint32_t>(offset));
    Serial.print(F(" (EEPROM address "));
    printHex32(start + static_cast<uint32_t>(offset));
    Serial.print(F("): "));
    printHex8(IMAGE[offset]);
    Serial.print(F(" -> "));
    printHex8(chunk[5]);
    Serial.println();

    s = updater.setWriteAddress(start + static_cast<uint32_t>(chunkStart));
    if (s == EepromUpdateStatus::Success) {
        s = writeChunkWithRetry(start + static_cast<uint32_t>(chunkStart), chunk, n);
    }
    printStep(F("Corrupting write"), s);
    if (s != EepromUpdateStatus::Success) {
        finishExperiment(target, F("The corrupting write failed; the region may or may not be changed."));
        return;
    }
    uint8_t row[TPS25751EepromUpdater::kFlashReadBytes];
    if (updater.readFlash(start + static_cast<uint32_t>(chunkStart), row) == EepromUpdateStatus::Success) {
        Serial.println(F("[v] The 16 bytes that hold the corrupted byte now read:"));
        printRow(start + static_cast<uint32_t>(chunkStart), row);
    }

    // 5. What FLvy and the compare say about the corrupted region.
    const EepromUpdateStatus corruptVerify = updater.verifyBundle(start);
    printStep(F("FLvy after corrupting one byte"), corruptVerify);
    if (corruptVerify == EepromUpdateStatus::Success) {
        Serial.println(F("[v] FLvy did NOT report the corrupted byte."));
    } else if (corruptVerify == EepromUpdateStatus::VerifyFailed) {
        Serial.println(F("[v] FLvy reported the corrupted byte."));
    } else {
        Serial.println(F("[v] FLvy did not complete."));
    }

    const EepromUpdateStatus corruptCompare = updater.compareRegion(target, IMAGE, IMAGE_SIZE, &mismatch);
    printStep(F("compareRegion() after corrupting one byte"), corruptCompare);
    if (corruptCompare == EepromUpdateStatus::CompareMismatch) {
        Serial.print(F("[v] compareRegion() first mismatch at image offset "));
        printHex32(mismatch);
        Serial.println();
    }

    finishExperiment(target, F("Experiment complete."));
}

// ---------------------------------------------------------------------------
// x: cold reset
// ---------------------------------------------------------------------------

static void cmdColdReset()
{
    // What the controller will find when it restarts. With no valid pointer it
    // has no bundle to load and comes up in PTCH, where only a patch load over
    // I2C or an external programmer brings it back, so that case asks first.
    const bool lowValid = printPointer(EepromRegion::Low);
    const bool highValid = printPointer(EepromRegion::High);
    if (!lowValid && !highValid) {
        Serial.println(F("[x] Warning: no region pointer read as valid. After this reset the controller finds no bundle to load and comes up in PTCH mode."));
        if (!confirm()) {
            Serial.println(F("[x] No reset was issued."));
            return;
        }
    }

    Serial.println(F("[x] coldReset(): issuing GAID. The port disconnects while the controller restarts."));
    exec.reset();
    const uint32_t started = millis();
    const EepromUpdateStatus s = updater.coldReset();
    const uint32_t elapsed = millis() - started;

    Serial.print(F("[x] Result: "));
    Serial.println(TPS25751EepromUpdater::toString(s));
    if (s == EepromUpdateStatus::Success) {
        Serial.print(F("[x] Time to APP: "));
        Serial.print(elapsed);
        Serial.println(F(" ms from issuing GAID"));
        if (elapsed < 1000) {
            Serial.println(F("[x] Note: under 1000 ms. TRM Table 4-3 gives a typical 1-second delay before the restart, so MODE may have been read before it. Send r in a few seconds and compare."));
        }
    } else {
        Serial.print(F("[x] APP was not confirmed; coldReset() returned after "));
        Serial.print(elapsed);
        Serial.println(F(" ms"));
    }
    printMode();
    printVersion();
    printBootFlags();
    printTiming();
}

// ---------------------------------------------------------------------------
// l: load the image into the controller's RAM (PTCH only), and pb: program a
// blank or corrupt EEPROM (hardware stage 5). SLVAFL1 Figure 4-1: push a patch
// with the PBM tasks, then correct the EEPROM with the flash tasks.
// ---------------------------------------------------------------------------

static void cmdLoadPatch()
{
    const EepromUpdateStatus image = imageStatus();
    if (image != EepromUpdateStatus::Success) {
        Serial.print(F("[refused] The embedded image is not a usable patch bundle: checkImage() = "));
        Serial.println(TPS25751EepromUpdater::toString(image));
        Serial.println(F("[refused] Replace src/patch_bundle.h (scripts/bin_to_header.py). Nothing was loaded."));
        return;
    }
    TPS25751Mode mode;
    if (!pd.readMode(mode)) {
        Serial.println(F("[refused] MODE could not be read. Nothing was loaded."));
        return;
    }
    if (!mode.isPatchMode()) {
        Serial.print(F("[refused] l runs only while MODE reads PTCH; MODE reads '"));
        Serial.print(mode.modeString());
        Serial.println(F("'. Nothing was loaded."));
        return;
    }

    Serial.println(F("[plan] Action: l: TPS25751PatchLoader::loadPatchBundle() with the embedded image (Patch Burst Mode over I2Ct)"));
    Serial.print(F("[plan] Image: "));
    Serial.print(IMAGE_SIZE);
    Serial.print(F(" bytes, version "));
    printHex32(TPS25751EepromUpdater::imageVersion(IMAGE, IMAGE_SIZE));
    Serial.println();
    Serial.println(F("[plan] This command loads the controller's RAM. It does not write the EEPROM; after a restart the controller loads from the EEPROM again."));
    if (!confirm()) {
        return;
    }

    const uint32_t started = millis();
    const PatchLoadStatus s = loader.loadPatchBundle(IMAGE, IMAGE_SIZE);
    const uint32_t elapsed = millis() - started;

    Serial.print(F("[l] Result: "));
    Serial.println(TPS25751PatchLoader::toString(s));
    Serial.print(F("[l] Elapsed: "));
    Serial.print(elapsed);
    Serial.println(F(" ms"));
    printMode();
    printVersion();
    printBootFlags();
    if (s == PatchLoadStatus::Success) {
        Serial.println(F("[l] The controller runs the image from RAM. The EEPROM is unchanged: send pb to program it when no region pointer is valid, or r to inspect it first."));
    } else {
        Serial.println(F("[l] The load did not complete. MODE above shows where the controller stands; nothing was written to the EEPROM."));
    }
}

/// After a pb refusal or a failed pb: u and b never write an AppConfigOffset
/// word, so a region they write behind a word that is not 0 would not load.
static void printOffsetAdvice()
{
    Serial.println(F("[pb] Read [offset] High above. Do not run u or b while it, or the [offset] line of any region they would write, is not 0x00000000: u and b do not write that word, and no command of this example does."));
}

static void cmdProgramBlank()
{
    pointerTestLeftHighErased = false;  // another writing command has started
    // In PTCH, say what comes first; with the placeholder, writeAllowed() says why not.
    TPS25751Mode mode;
    if (imageStatus() == EepromUpdateStatus::Success && pd.readMode(mode) && mode.isPatchMode()) {
        Serial.println(F("[refused] MODE reads PTCH. Send l first to load the image into the controller's RAM. Nothing was written."));
        return;
    }
    if (!writeAllowed()) {
        return;
    }
    uint32_t low = 0;
    uint32_t high = 0;
    if (updater.readRegionPointer(EepromRegion::Low, low) != EepromUpdateStatus::Success ||
        updater.readRegionPointer(EepromRegion::High, high) != EepromUpdateStatus::Success) {
        Serial.println(F("[refused] The region pointers could not be read. Nothing was written."));
        return;
    }
    if (low == TPS25751EepromUpdater::bundleStart(EepromRegion::Low) ||
        high == TPS25751EepromUpdater::bundleStart(EepromRegion::High)) {
        Serial.println(F("[refused] A region pointer is valid. pb is only for an EEPROM with no valid pointer. Nothing was written."));
        printPointers();
        printAppConfigOffsets();
        printOffsetAdvice();
        return;
    }

    printPlan(F("pb: programBlank() with the embedded image. Writes both pointers, both AppConfigOffset words, and the image in both regions"),
              EepromRegion::Low, true);
    printAppConfigOffsets();
    Serial.println(F("[plan] On success the EEPROM holds the full-flash layout. After a failure in the Low pass no pointer is valid unless the pointers printed afterwards say so. After a failure in the High pass the Low pass is complete: its pointer was written after the region passed FLvy and the compare."));
    if (!confirm()) {
        return;
    }

    exec.reset();
    resetProgress(0);
    const uint32_t started = millis();
    const EepromUpdateStatus s = updater.programBlank(IMAGE, IMAGE_SIZE);
    printUpdateResult(s, millis() - started);
    printPointers();
    printAppConfigOffsets();
    printTiming();

    if (s == EepromUpdateStatus::Success) {
        Serial.println(F("[pb] The EEPROM holds the full-flash layout. The controller still runs the image in its RAM. Send x to restart it from the EEPROM, then r to check."));
        return;
    }
    if (updater.lastStep() == EepromUpdateStep::Preflight) {
        return;  // refused before any write; the result line says why
    }
    if (updater.lastStep() == EepromUpdateStep::ClearAppConfigOffset &&
        s == EepromUpdateStatus::PointerNeighborsChanged) {
        Serial.println(F("[pb] Bytes after an AppConfigOffset word changed during its write. Dump the EEPROM with r before any further write."));
    }
    if (updater.lastTarget() == EepromRegion::High) {
        // Established: the Low pointer was written after FLvy and the compare.
        // Not established: that nothing of the High pass reached the Low region.
        Serial.println(F("[pb] The Low pass completed: the Low pointer was written after the Low region passed FLvy and the compare. The High pass did not complete."));
        if (s == EepromUpdateStatus::PointerNeighborsChanged || updater.lastStep() == EepromUpdateStep::WriteBundle) {
            Serial.println(F("[pb] This stop can mean bytes landed outside the High region, and the Low bundle starts at 0x0800, next to the High AppConfigOffset word. Check the Low region with r (compare the dump with the image) before relying on it."));
        }
        printOffsetAdvice();
    } else {
        Serial.println(F("[pb] The Low pass did not complete. Unless a pointer above reads valid, the controller has no region to load and comes up in PTCH at its next restart; l then pb recovers from there."));
    }
}

// ---------------------------------------------------------------------------
// Command loop
// ---------------------------------------------------------------------------

static void printReady()
{
    Serial.println(F("[ready] Commands: r p p2 u b x c1 c2 c3 c4 v l pb"));
}

static void runCommand(const char* line)
{
    if (strcmp(line, "r") == 0) {
        runReport();
    } else if (strcmp(line, "p") == 0) {
        cmdPointerTest();
    } else if (strcmp(line, "p2") == 0) {
        cmdPointerRewriteTest();
    } else if (strcmp(line, "u") == 0) {
        cmdUpdate(/*both=*/false);
    } else if (strcmp(line, "b") == 0) {
        cmdUpdate(/*both=*/true);
    } else if (strcmp(line, "x") == 0) {
        cmdColdReset();
    } else if (strcmp(line, "c1") == 0) {
        cmdCut(1);
    } else if (strcmp(line, "c2") == 0) {
        cmdCut(2);
    } else if (strcmp(line, "c3") == 0) {
        cmdCut(3);
    } else if (strcmp(line, "c4") == 0) {
        cmdCut(4);
    } else if (strcmp(line, "v") == 0) {
        cmdVerifyExperiment();
    } else if (strcmp(line, "l") == 0) {
        cmdLoadPatch();
    } else if (strcmp(line, "pb") == 0) {
        cmdProgramBlank();
    } else {
        Serial.print(F("[error] Unknown command: "));
        Serial.println(line);
        printReady();
        return;
    }
    Serial.print(F("[done] "));
    Serial.println(line);
    printReady();
}

void setup()
{
    Serial.begin(115200);
    while (!Serial && millis() < 3000) {
        delay(10);
    }

    // Errors only, so warnings cannot land inside the dump.
    TPS25751::setDebugLevel(DEBUG_LEVEL_ERROR);
    TPS25751::setDebugCategories(DEBUG_CAT_TASK | DEBUG_CAT_I2C);

    pd.begin();
    updater.setProgressCallback(onProgress, nullptr);

    Serial.println(F("[setup] TPS25751 EEPROM update example"));
    Serial.println(F("[setup] r reads. x restarts the controller. l loads the image into the controller's RAM in PTCH mode. p p2 u b c1 c2 c3 c4 v pb write the EEPROM. l and each command that writes wait for a typed yes."));
    printImage();
    printStatus();
    printReady();
}

void loop()
{
    char line[16];
    bool tooLong = false;
    const size_t length = readLine(line, sizeof(line), tooLong);
    if (tooLong) {
        Serial.println(F("[error] Line too long"));
        printReady();
        return;
    }
    if (length == 0) {
        return;  // an empty line at the prompt is ignored
    }
    if (strlen(line) != length) {
        // The line held a NUL character; no command does.
        Serial.println(F("[error] Unknown command"));
        printReady();
        return;
    }
    runCommand(line);
}
