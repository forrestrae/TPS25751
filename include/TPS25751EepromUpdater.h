#pragma once

/**
 * @file TPS25751EepromUpdater.h
 * @brief Update and read-back of the TPS25751 configuration EEPROM over I2Ct
 *
 * The TPS25751 boots its patch bundle from an external EEPROM on its I2Cc bus. A
 * host on I2Ct reaches that EEPROM through the flash 4CC tasks: FLrd reads, FLad
 * and FLwd write, FLvy validates a bundle header (TRM SLVUCR8B Tables 4-19 to
 * 4-22, pages 81 and 82). TI application note SLVAFL1 describes the two-region
 * layout and the update flow, and its EEPROM Boot Flow section says the host
 * writes the EEPROM once MODE reads APP.
 *
 * What the class provides:
 * - the task primitives readFlash(), setWriteAddress(), writeData(), and
 *   verifyBundle();
 * - region pointer reads and writes, the write with a read-back and a check of
 *   the bytes that follow the pointer;
 * - the update flows of SLVAFL1 Figure 2-3: updateRegion(),
 *   updateInactiveRegion(), and updateBothRegions();
 * - programBlank(), which writes the whole two-region layout to an EEPROM
 *   with no valid region pointer, for the recovery of SLVAFL1 Figure 4-1;
 * - read-back with compareRegion() and dump(), and pure image checks;
 * - coldReset(), which issues GAID and waits for APP.
 *
 * Calls that change the EEPROM: setWriteAddress() followed by writeData(),
 * writeRegionPointer(), the three update flows, and programBlank().
 * coldReset() restarts the controller and disconnects the port. Every other
 * call only reads.
 *
 * EEPROM layout (SLVAFL1 Figures 2-1 and 2-6, and the source code on page 10):
 *
 * | Item            | Low region | High region |
 * |-----------------|------------|-------------|
 * | Region pointer  | 0x0000     | 0x0400      |
 * | AppConfigOffset | 0x03FC     | 0x07FC      |
 * | Bundle start    | 0x0800     | 0x4400      |
 *
 * A region pointer is valid when it equals its region's bundle start. SLVAFL1
 * (EEPROM Update Example) states that the controller does not attempt to load
 * a region whose pointer has been erased to 0. That is what makes the update
 * safe: a region is written while its pointer is 0, and the pointer is
 * restored only after the written bundle has been checked.
 *
 * What hardware has shown (stages 1 to 4 of the plan, 2026-11-22 and 23, one
 * board with a 24LC256): FLrd returns its 16 bytes in DATA bytes 0 to 15 and
 * takes its address least significant byte first; FLad and FLwd report 0x00
 * on success; a 4-byte FLwd writes exactly four bytes; the write address
 * advances across 446 chunks; FLvy reports 0x00 for a valid bundle and "not
 * valid" for one with a corrupted body byte; updateRegion(),
 * updateBothRegions(), and coldReset() work on the device, and after a cut
 * at each step of an update the controller boots the region the state table
 * predicts. Not observed: a busy FLwd, any FLad or FLwd failure code, and
 * whether an FLrd moves the write address. programBlank() has not run on
 * hardware; stage 5 covers it.
 *
 * Not a register class: it does not inherit TPS25751Register and is not in the
 * factory, in the manner of TPS25751PatchLoader (ADR-011). It depends on the
 * TPS25751CommandExecutor interface rather than on TPS25751, so it compiles and
 * runs in the native test build.
 *
 * @code
 * TPS25751 pd;
 * TPS25751HostExecutor exec(pd);
 * TPS25751EepromUpdater updater(exec);
 *
 * EepromUpdateStatus s = updater.updateInactiveRegion(image, imageLength);
 * if (s == EepromUpdateStatus::Success) {
 *     s = updater.coldReset();  // the port disconnects while the controller restarts
 * }
 * if (s != EepromUpdateStatus::Success) {
 *     Serial.println(TPS25751EepromUpdater::toString(s));
 *     Serial.println(TPS25751EepromUpdater::toString(updater.lastStep()));
 * }
 * @endcode
 *
 * @see TI SLVAFL1, "TPS25751 and TPS26750 EEPROM Update Over I2C", October 2024
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B, Table 4-1 (p. 61),
 *      Table 4-3 (p. 62), and Tables 4-19 to 4-22 (pp. 81-82)
 */

#include <Arduino.h>
#include <Stream.h>
#include "TPS25751Task.h"
#include "TPS25751CommandExecutor.h"

// ---------------------------------------------------------------------------
// Task codes and timing (TRM SLVUCR8B pages 62, 81, and 82)
// ---------------------------------------------------------------------------

/// 4CC task code: read 16 bytes of the external EEPROM (TRM Table 4-19).
constexpr TPS25751FourCC CMD_FLRD = TPS25751FourCC::of("FLrd");

/// 4CC task code: set the start address for the next EEPROM write (TRM Table 4-20).
constexpr TPS25751FourCC CMD_FLAD = TPS25751FourCC::of("FLad");

/// 4CC task code: write data at the write address, which then advances (TRM Table 4-21).
constexpr TPS25751FourCC CMD_FLWD = TPS25751FourCC::of("FLwd");

/// 4CC task code: validate the bundle header at an address (TRM Table 4-22).
constexpr TPS25751FourCC CMD_FLVY = TPS25751FourCC::of("FLvy");

/// 4CC task code: cold reset request (TRM Table 4-3).
constexpr TPS25751FourCC CMD_GAID = TPS25751FourCC::of("GAID");

/// COMMAND-poll timeout (ms) for FLrd. Hardware stage 1 measured 17.7 ms per
/// call (1984 calls, default I2C clock, 2026-11-22).
constexpr uint32_t FLRD_CMD_TIMEOUT_MS = 200;

/// COMMAND-poll timeout (ms) for FLad. Hardware stages 2 and 3 measured
/// 17.7 ms per call (2026-11-23).
constexpr uint32_t FLAD_CMD_TIMEOUT_MS = 200;

/// COMMAND-poll timeout (ms) for FLwd. The task completes when the data has
/// been written (TRM Table 4-21); the 24LC256 write cycle is at most 5 ms.
/// Hardware stage 3 measured 20.7 ms for a 32-byte chunk, at most 20.8 ms
/// over 449 calls (2026-11-23).
constexpr uint32_t FLWD_CMD_TIMEOUT_MS = 200;

/// COMMAND-poll timeout (ms) for FLvy. The task completes after the header is
/// checked (TRM Table 4-22). Hardware stage 3 measured 0.56 to 0.60 s for a
/// 14272- or 14336-byte bundle (2026-11-23).
constexpr uint32_t FLVY_CMD_TIMEOUT_MS = 1000;

/// COMMAND-poll timeout (ms) for GAID. The task never completes in the normal
/// sense: an APP-mode controller enters Type-C Error Recovery and restarts
/// after a typical 1-second delay (TRM Table 4-3). The poll has to outlast that
/// delay so that it ends on the restart (a NAK, or COMMAND reading 0 after the
/// reboot) and not while MODE still reads APP from before the restart. Three
/// times the typical delay. Hardware stage 3 saw COMMAND answer for 1277 ms
/// after GAID, then the controller stop acknowledging (2026-11-23).
constexpr uint32_t GAID_CMD_TIMEOUT_MS = 3000;

/// Delay (ms) before a bundle chunk whose FLwd reported busy (0xFF) is retried.
constexpr uint32_t FLWD_BUSY_RETRY_DELAY_MS = 10;

/// Retries of one bundle chunk whose FLwd keeps reporting busy (0xFF).
constexpr uint8_t FLWD_BUSY_RETRIES = 3;

/// coldReset(): delay (ms) between GAID and the first MODE read.
constexpr uint32_t GAID_SETTLE_MS = 100;

/// coldReset(): interval (ms) between MODE reads while waiting for APP.
constexpr uint32_t GAID_MODE_POLL_MS = 50;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/**
 * @brief One of the two EEPROM regions (SLVAFL1 Figure 2-1)
 */
enum class EepromRegion : uint8_t {
    Low = 0,   ///< Region 0: pointer at 0x0000, bundle at 0x0800
    High = 1   ///< Region 1: pointer at 0x0400, bundle at 0x4400
};

/**
 * @brief Outcome of a TPS25751EepromUpdater call
 *
 * updateRegion() documents, for every value an update can return, the step
 * that raises it and the state it leaves the EEPROM in. programBlank() does
 * the same for blank programming, the only call that returns EepromNotBlank.
 */
enum class EepromUpdateStatus : uint8_t {
    Success,                  ///< The call completed as requested
    NotInAppMode,             ///< Pre-flight: MODE did not read APP
    NoEepromPresent,          ///< Pre-flight: BOOT_FLAGS (0x2D) did not report I2C EEPROM Present
    BadImageHeader,           ///< checkImage(): the image does not start with 0xACE00001
    BadImageLength,           ///< checkImage(): the image is shorter than 0x38 bytes or longer than the region. Also a null pointer or a length out of range passed to writeData() or compareRegion()
    NoBootableRegion,         ///< Selection: neither region pointer equals its bundle start. Pre-flight: the pointer of the region being kept does not
    EepromNotBlank,           ///< programBlank() pre-flight: a region pointer is valid, so the EEPROM is not blank
    PointerEraseFailed,       ///< Update step 1: the target pointer could not be written to 0
    SetAddressFailed,         ///< FLad reported a failure in its task return code (TRM Table 4-20; Table 4-1 puts the standard TaskResult in bits 3:0)
    WriteFailed,              ///< FLwd returned a code other than written (0x00) or busy (0xFF)
    FlashBusy,                ///< FLwd reported busy (0xFF): for a bundle chunk, still busy after the retries; for a pointer or AppConfigOffset word, at once, since those writes are not retried
    VerifyFailed,             ///< FLvy reported the bundle not valid (0x01) or an unknown code
    CompareMismatch,          ///< compareRegion(): the EEPROM differs from the image. writeRegionPointer(), and the AppConfigOffset write of programBlank(): the word read back differs from the value written. programBlank(), before a pointer write: the region's AppConfigOffset word no longer reads 0. An update: the compare of step 3, or the first chunks did not read back at the bundle start in step 2
    PointerRestoreFailed,     ///< Update step 3: the target pointer could not be written to the bundle start
    OldPointerEraseFailed,    ///< Update step 4: the old region pointer could not be written to 0
    PointerNeighborsChanged,  ///< Bytes 4 to 15 after a pointer, or after an AppConfigOffset word in programBlank(), changed during its write; or the 16 bytes at the target pointer changed while the first chunks of an update were written; or those bytes already held the start of the image before the bundle write
    ResetTimeout,             ///< The controller did not return to APP after GAID
    ResetNotObserved,         ///< coldReset(): MODE read APP, but the call never saw the controller go away (no I2CError on GAID, no failed MODE read), so it cannot tell a restart from no restart. Confirm with VERSION and BOOT_FLAGS; see coldReset()
    CommandRejected,          ///< The executor reported "!CMD" (task code not recognized)
    CommandTimeout,           ///< The executor saw neither clear nor "!CMD" before the timeout
    I2cError                  ///< An I2C transaction or a register read failed
};

/**
 * @brief The step an update or reset was in when it returned
 *
 * lastStep() returns this. It tells a caller which row of the state table in
 * updateRegion() or programBlank() applies when a call returns an executor
 * failure (CommandRejected, CommandTimeout, or I2cError), and it tells a
 * progress callback which phase is reporting. In programBlank() the steps
 * run once per region, and lastTarget() names the region.
 *
 * @see TPS25751EepromUpdater::lastStep()
 * @see SLVAFL1 Figure 2-3
 */
enum class EepromUpdateStep : uint8_t {
    None,                  ///< No update or reset has run on this object
    Preflight,             ///< Region selection or the pre-flight checks; this call has issued no write
    ErasePointer,          ///< Step 1: writing the target region's pointer to 0
    ClearAppConfigOffset,  ///< programBlank() only, before either pass: writing the AppConfigOffset word of the region lastTarget() names to 0
    WriteBundle,           ///< Step 2: FLad to the bundle start, then the FLwd chunks
    VerifyBundle,          ///< Step 3: FLvy at the bundle start
    CompareBundle,         ///< Step 3: FLrd compare of the written region with the image
    CheckAppConfigOffset,  ///< programBlank() only, step 3: FLrd of the target's AppConfigOffset word right before its pointer write; the word must read 0
    RestorePointer,        ///< Step 3: writing the target pointer to the bundle start
    EraseOldPointer,       ///< Step 4: writing the other region's pointer to 0
    Reset,                 ///< coldReset(): GAID issued, or MODE being polled
    Done                   ///< The most recent update or reset completed
};

/**
 * @brief Options for TPS25751EepromUpdater::updateRegion()
 *
 * The defaults give the full flow of SLVAFL1 Figure 2-3 with the added FLrd
 * compare.
 */
struct EepromUpdateOptions {
    /// Compare the written region with the image through FLrd before the
    /// pointer is restored. FLvy completes after the header is checked (TRM
    /// Table 4-22) and is not documented to check the bundle body, so turning
    /// this off leaves the body unverified.
    bool compareAfterWrite = true;

    /// Run step 4 of SLVAFL1 Figure 2-3: write the other region's pointer to 0
    /// after the target's pointer is restored. When false, both pointers are
    /// valid at the end and the controller tries the low region first
    /// (SLVAFL1 Figure 2-9).
    bool invalidateOldRegion = true;
};

/**
 * @brief Progress callback for the update flows
 *
 * Called synchronously from updateRegion(), and so from updateInactiveRegion()
 * and updateBothRegions(), and from programBlank(), in two phases.
 * TPS25751EepromUpdater::lastStep() tells them apart, and @p total is the
 * image length in both.
 *
 * Write phase, lastStep() == EepromUpdateStep::WriteBundle:
 * - once with @p done == 0, after step 1 has completed (the target pointer
 *   has been read back as 0), and before any task of step 2 is issued (in
 *   programBlank() the AppConfigOffset words were written before either
 *   pass);
 * - then after every chunk whose FLwd reported success, with @p done the image
 *   bytes written so far. The last call has @p done == @p total.
 *
 * Compare phase, lastStep() == EepromUpdateStep::CompareBundle, only when
 * EepromUpdateOptions::compareAfterWrite is set:
 * - once with @p done == 0 before the first FLrd;
 * - then after every 16-byte read that matched the image, with @p done the
 *   image bytes compared so far. The last call has @p done == @p total.
 *
 * The read-back after the first two chunks of the write phase makes no call.
 * The check that precedes the first task of step 2 comes after the done == 0
 * call; when it stops the update, that call is the only one.
 * A step that fails makes no further call, so a missing final call means the
 * phase did not complete. updateBothRegions() and programBlank() run both
 * phases once per region; lastTarget() names the region.
 *
 * A callback that never returns stops the update where it stands: at
 * @p done == 0 of the write phase the EEPROM is as step 1 left it (SLVAFL1
 * Figure 2-7), and at @p done == @p total of the write phase as step 2 left it
 * (Figure 2-8). In programBlank() neither state has a valid pointer during
 * the Low pass; see its state table.
 *
 * The callback may call lastStep() and lastTarget(). A callback that returns
 * must not call any other member of the updater and must not issue 4CC tasks
 * through the same executor: the bundle write relies on the write address the
 * controller holds between chunks. A callback that never returns has ended the
 * update and is free to read the EEPROM.
 *
 * @param done  Image bytes written or compared so far in the current phase
 * @param total Image length in bytes
 * @param ctx   The pointer given to setProgressCallback()
 */
using EepromProgressCallback = void (*)(uint32_t done, uint32_t total, void* ctx);

// ---------------------------------------------------------------------------
// TPS25751EepromUpdater
// ---------------------------------------------------------------------------

/**
 * @brief Sequencer for the TPS25751 flash 4CC tasks
 *
 * Holds a reference to a TPS25751CommandExecutor and the EEPROM size. Every call
 * is synchronous and blocks until it finishes; a region update takes tens of
 * seconds. Use it with the controller in APP mode: SLVAFL1's EEPROM Boot Flow
 * section describes the EEPROM update in APP mode. The update flows check MODE
 * before they write; the primitives do not.
 *
 * The application must not issue other 4CC tasks (for example I2Cr or I2Cw
 * through TPS25751DownstreamDevice) from an interrupt or another task while an
 * update runs: every task shares COMMAND and DATA.
 *
 * @note Not a register class. Does not inherit TPS25751Register. Does not
 *       register with TPS25751RegisterFactory.
 */
class TPS25751EepromUpdater {
public:
    /// Bytes FLrd returns per call (TRM Table 4-19, bits 127:0).
    static constexpr size_t kFlashReadBytes = 16;

    /// Largest FLwd payload this class sends. The TRM accepts up to 64 bytes
    /// and recommends 32, the default EEPROM page size the task uses (Table 4-21).
    static constexpr size_t kFlashWriteChunk = 32;

    /// Image bytes an update writes before it reads them back: two chunks.
    static constexpr size_t kEarlyReadBackBytes = 2 * kFlashWriteChunk;

    /// Header_ID at the start of every patch bundle, little-endian (SLVAFL1 section 2.1).
    static constexpr uint32_t kBundleHeaderId = 0xACE00001UL;

    /// Image offset of the version word, which matches the VERSION register (0x0F).
    static constexpr size_t kImageVersionOffset = 0x34;

    /// Smallest image checkImage() accepts: the header through the version word.
    static constexpr size_t kMinImageLength = 0x38;

    /// EEPROM size assumed when none is given: the board's 24LC256.
    static constexpr uint32_t kDefaultEepromSize = 32768;

    /// Task return code this class requires from FLad: the whole byte is 0
    /// (TRM Table 4-1, "Task completed successfully").
    static constexpr uint8_t kTaskSuccess = 0x00;

    /// FLwd return code for a successful write (TRM Table 4-21).
    static constexpr uint8_t kWriteDone = 0x00;

    /// FLwd return code for "Error, flash is busy" (TRM Table 4-21).
    static constexpr uint8_t kWriteBusy = 0xFF;

    /// FLvy return code for a valid patch/configuration (TRM Table 4-22).
    static constexpr uint8_t kVerifyValid = 0x00;

    /// FLvy return code for a patch/configuration that is not valid (TRM Table 4-22).
    static constexpr uint8_t kVerifyNotValid = 0x01;

    /**
     * @brief Construct an updater bound to an executor
     * @param exec            Host executor (must outlive this object)
     * @param eepromSizeBytes Size of the external EEPROM in bytes; sets the high
     *                        region's capacity (default 32768, the 24LC256)
     */
    explicit TPS25751EepromUpdater(TPS25751CommandExecutor& exec,
                                   uint32_t eepromSizeBytes = kDefaultEepromSize);

    // --- Layout ------------------------------------------------------------

    /**
     * @brief EEPROM address of a region's pointer word
     * @param r Region
     * @return 0x0000 for Low, 0x0400 for High
     * @see SLVAFL1 Figure 2-1
     */
    static constexpr uint32_t regionPointerAddress(EepromRegion r)
    {
        return r == EepromRegion::Low ? 0x0000UL : 0x0400UL;
    }

    /**
     * @brief EEPROM address of a region's AppConfigOffset word
     * @param r Region
     * @return 0x03FC for Low, 0x07FC for High
     * @see SLVAFL1 Figure 2-1
     */
    static constexpr uint32_t appConfigOffsetAddress(EepromRegion r)
    {
        return r == EepromRegion::Low ? 0x03FCUL : 0x07FCUL;
    }

    /**
     * @brief EEPROM address where a region's bundle starts
     *
     * This is also the value a valid region pointer holds.
     * @param r Region
     * @return 0x0800 for Low, 0x4400 for High
     * @see SLVAFL1 Figure 2-6 and the source code on page 10
     */
    static constexpr uint32_t bundleStart(EepromRegion r)
    {
        return r == EepromRegion::Low ? 0x0800UL : 0x4400UL;
    }

    /**
     * @brief The region that is not @p r
     * @param r Region
     * @return High for Low, Low for High
     */
    static constexpr EepromRegion otherRegion(EepromRegion r)
    {
        return r == EepromRegion::Low ? EepromRegion::High : EepromRegion::Low;
    }

    /**
     * @brief Bytes available for a bundle in a region
     *
     * Low runs from 0x0800 to the high bundle start; High runs from 0x4400 to the
     * end of the EEPROM. Both are 0x3C00 at the default 32768-byte size.
     * @param r Region
     * @return Capacity in bytes, 0 if the EEPROM ends before the region starts
     */
    uint32_t regionCapacity(EepromRegion r) const;

    /**
     * @brief EEPROM size given at construction
     * @return Size in bytes
     */
    uint32_t eepromSizeBytes() const { return _eepromSize; }

    // --- Task primitives -----------------------------------------------------

    /**
     * @brief Read 16 bytes of the EEPROM with FLrd
     *
     * Sends the 4-byte address least significant byte first and copies the 16
     * bytes from DATA byte 0. FLrd has no return-code byte (TRM Table 4-19), so
     * the executor's returnCode is ignored. @p out is written only on Success.
     *
     * The TRM states the address byte order for FLad only (Table 4-20) and does
     * not say which DATA byte holds the byte at @p addr. Hardware stage 1
     * verified both on 2026-11-22: the two region pointers read as 0x00000800
     * and 0x00004400, and a dump of 0x0000 to 0x7C00 equalled the full-flash
     * image byte for byte.
     *
     * The caller owns the range. The updater does not bound @p addr against
     * the EEPROM size, and what the controller returns past the end of the
     * part is undocumented.
     *
     * @param addr EEPROM address
     * @param out  Buffer of at least kFlashReadBytes bytes; must not be nullptr
     * @return Success, or CommandRejected, CommandTimeout, or I2cError from the executor
     * @see TRM SLVUCR8B Table 4-19, p. 81
     */
    EepromUpdateStatus readFlash(uint32_t addr, uint8_t out[kFlashReadBytes]);

    /**
     * @brief Set the address of the next EEPROM write with FLad
     *
     * Sends the 4-byte address least significant byte first, as Table 4-20
     * states ("treated as 32-bit little-endian value"), and requires the task
     * return code to be 0.
     *
     * The whole return byte is compared with 0. Table 4-1 puts the standard
     * TaskResult in bits 3:0 and lets a task use bits 7:4 for its own codes
     * while TaskResult is 0x0, so a success that set bits 7:4 would be
     * reported as SetAddressFailed. The comparison can therefore report a
     * failure that is not one; it cannot report a success that is not one.
     * Hardware stage 2 observed 0x00 for successful FLad calls; no failure
     * code has been observed.
     *
     * FLad alone changes nothing in the EEPROM. The caller owns the range:
     * @p addr is not bounded against the EEPROM size, and what FLad returns
     * for an address past the end of the part is undocumented.
     *
     * @param addr EEPROM address the next writeData() starts at
     * @return Success for return code 0x00; SetAddressFailed for any other
     *         code; CommandRejected, CommandTimeout, or I2cError from the executor
     * @see TRM SLVUCR8B Table 4-20, p. 81, and Table 4-1, p. 61
     */
    EepromUpdateStatus setWriteAddress(uint32_t addr);

    /**
     * @brief Write up to 32 bytes at the write address with FLwd
     *
     * One FLwd task, with no retry. The controller writes at the address set
     * by setWriteAddress() and advances that address after each successful
     * write (TRM Table 4-21), so consecutive calls write consecutive bytes.
     *
     * The TRM accepts up to 64 bytes and recommends 32-byte chunks, the default
     * EEPROM page size the task uses. This call accepts 1 to kFlashWriteChunk
     * bytes. Keep each write inside one 32-byte page: the documents do not
     * describe a write that crosses a page, and the 24LC256 wraps such a write
     * within its own page. The update flows start page-aligned and never cross.
     *
     * Table 4-21 gives FLwd two return codes of its own, 0x00 and 0xFF, and
     * does not refer to Table 4-1. Any other value is treated as a failure;
     * no documented code produces it. Whether the controller writes exactly
     * @p len bytes for a payload shorter than a page is not documented.
     * Hardware stage 2 observed 0x00 for successful writes and showed that a
     * 4-byte FLwd writes exactly four bytes, although DATA bytes 4 to 63 still
     * held other data; no busy or failure code has been observed.
     *
     * A null @p data, a @p len of 0, and a @p len above kFlashWriteChunk
     * return BadImageLength without issuing a task. An FLwd with no payload
     * would write whatever DATA still holds.
     *
     * @param data Bytes to write; must not be nullptr
     * @param len  Number of bytes, 1 to kFlashWriteChunk
     * @return Success for return code 0x00; FlashBusy for 0xFF; WriteFailed
     *         for any other code; BadImageLength for a rejected argument;
     *         CommandRejected, CommandTimeout, or I2cError from the executor
     * @see TRM SLVUCR8B Table 4-21, p. 82
     */
    EepromUpdateStatus writeData(const uint8_t* data, size_t len);

    /**
     * @brief Run FLvy on the bundle at an address
     *
     * FLvy completes after the bundle header is checked; whether it also checks
     * the bundle CRC is not documented, so a Success here does not prove the
     * bundle body is intact. Use compareRegion() for that. Hardware stage 3
     * (2026-11-23) found FLvy reporting a bundle with one inverted body byte,
     * at image offset 0x1BE5, as not valid, so it checks more than the header.
     * That one byte at one offset is the whole sample.
     *
     * The address goes out least significant byte first, as for readFlash().
     * The TRM does not state the order for FLvy. Hardware stage 1 supports it
     * indirectly (2026-11-22): FLvy reported 0x00 for the bundles at 0x0800 and
     * 0x4400, and either address byte-swapped lies far outside the 32 KB part.
     * Which nonzero code stage 3 saw was not recorded.
     *
     * @param addr EEPROM address of the bundle (normally bundleStart())
     * @return Success for return code 0x00; VerifyFailed for 0x01 or any other
     *         code; CommandRejected, CommandTimeout, or I2cError from the executor
     * @see TRM SLVUCR8B Table 4-22, p. 82
     */
    EepromUpdateStatus verifyBundle(uint32_t addr);

    // --- Region pointers -----------------------------------------------------

    /**
     * @brief Read a region pointer word
     * @param r     Region
     * @param value Receives the 32-bit word, assembled least significant byte
     *              first (SLVAFL1 page 12 source code); written only on Success
     * @return Success, or the readFlash() failure
     * @see SLVAFL1 Figure 2-1
     */
    EepromUpdateStatus readRegionPointer(EepromRegion r, uint32_t& value);

    /**
     * @brief Write a region pointer word and confirm it
     *
     * Follows SLVAFL1 Figure 2-5 with one added check. Four tasks, in order:
     *
     * 1. FLrd the 16 bytes at the pointer address and keep bytes 4 to 15.
     * 2. FLad to the pointer address; the return code must be 0.
     * 3. FLwd the value as 4 bytes, least significant byte first; the return
     *    code must be 0. A busy result (0xFF) is not retried here.
     * 4. FLrd again. Bytes 4 to 15 must equal the bytes kept in step 1, and
     *    bytes 0 to 3 must equal the value.
     *
     * The neighbor check in step 4 looks for a controller that writes more
     * than the four bytes given, which the documents do not rule out. It is
     * tested before the value, so a write that changed both reports
     * PointerNeighborsChanged. The check has two limits. It reads 16 bytes, so
     * a write that reached past byte 15 goes unseen. And it compares bytes 4
     * to 15 with what step 1 read: if the controller took extra bytes from
     * DATA, and DATA still held the step 1 read in those positions, it would
     * write the same values back. dump() over the surrounding range is the
     * complete check. Hardware stage 2 showed both sides of that limit: FLad
     * leaves DATA bytes 4 to 63 as the preceding FLrd left them, so the blind
     * spot is real, and a 4-byte FLwd writes only its four bytes, so it does
     * not matter on that controller.
     *
     * This call does no pre-flight. Writing 0 to the only valid pointer leaves
     * the EEPROM with no bootable region; updateRegion() guards against that,
     * this call does not.
     *
     * State after a failure: a failure in step 1 or 2 has written nothing. A
     * failure in step 3 or 4 leaves the pointer word uncertain until it is
     * read again.
     *
     * @param r     Region whose pointer is written
     * @param value Word to write: bundleStart(r) makes the pointer valid, 0 erases it
     * @return Success; PointerNeighborsChanged when bytes 4 to 15 changed;
     *         CompareMismatch when the word read back differs from @p value;
     *         SetAddressFailed from step 2; WriteFailed or FlashBusy from step 3;
     *         CommandRejected, CommandTimeout, or I2cError from the executor
     * @see SLVAFL1 Figure 2-5 and the source code on page 12
     */
    EepromUpdateStatus writeRegionPointer(EepromRegion r, uint32_t value);

    // --- Flows ---------------------------------------------------------------

    /**
     * @brief Choose the region an update should write
     *
     * The region pointers decide first, BOOT_FLAGS (0x2D) second; SLVAFL1
     * Figure 2-2 decides from BOOT_FLAGS alone and calls the register
     * BOOT_STATUS. BOOT_FLAGS is latched at boot, so after an update without a
     * reset it describes the old layout; following Figure 2-2 alone in that
     * state could select the region whose pointer is the only valid one.
     *
     * | Low pointer | High pointer | Target |
     * |---|---|---|
     * | Valid | Valid | Low if region0Invalid() or region0EepromError(), else High (Figure 2-2) |
     * | Valid | Not valid | High |
     * | Not valid | Valid | Low |
     * | Not valid | Not valid | None: NoBootableRegion |
     *
     * BOOT_FLAGS is read only in the first row.
     *
     * @param target Receives the region; written only on Success
     * @return Success, NoBootableRegion, I2cError if BOOT_FLAGS cannot be read,
     *         or a readFlash() failure
     */
    EepromUpdateStatus selectTargetRegion(EepromRegion& target);

    /**
     * @brief Write a new bundle to one region and make it the region that boots
     *
     * Pre-flight, in this order, before any write. A failure here leaves the
     * EEPROM unchanged:
     *
     * 1. MODE reads APP (NotInAppMode; I2cError if MODE cannot be read).
     * 2. BOOT_FLAGS (0x2D) reports I2C EEPROM Present (NoEepromPresent;
     *    I2cError if it cannot be read).
     * 3. checkImage() accepts the image for regionCapacity(@p target)
     *    (BadImageHeader, BadImageLength).
     * 4. The other region's pointer equals its bundle start (NoBootableRegion).
     *    Step 1 below is safe only when the region being kept is bootable.
     *    Only the pointer is checked, not the bundle behind it.
     *
     * Then the four steps of SLVAFL1 Figure 2-3, with a read-back added to
     * step 2:
     *
     * 1. writeRegionPointer(@p target, 0).
     * 2. Before any task, one check of what step 1 read: if bytes 4 to 15
     *    after the target pointer equal the first 12 bytes of the image, the
     *    call stops with PointerNeighborsChanged (see below). Otherwise
     *    FLad to bundleStart(@p target), then FLwd in 32-byte chunks, the last
     *    chunk at its true length. A chunk whose FLwd reports busy (0xFF) is
     *    retried up to FLWD_BUSY_RETRIES times, each retry after
     *    FLWD_BUSY_RETRY_DELAY_MS and preceded by a fresh FLad to that chunk's
     *    address. After the first two chunks (kEarlyReadBackBytes, or the whole
     *    image when it is shorter) the write pauses for a read-back, in this
     *    order: FLrd of the 16 bytes at the target pointer, then FLrd of the
     *    chunks at the bundle start. The chunks are compared with the image
     *    first (CompareMismatch), then the 16 bytes with what the read-back of
     *    step 1 saw (PointerNeighborsChanged). A fresh FLad to the next
     *    unwritten byte follows, then the remaining chunks.
     * 3. FLvy at bundleStart(@p target); then, when @p opt.compareAfterWrite is
     *    set, an FLrd compare of the region with the image; then
     *    writeRegionPointer(@p target, bundleStart(@p target)).
     * 4. When @p opt.invalidateOldRegion is set, writeRegionPointer(other, 0).
     *
     * The first failure ends the call. Nothing is written after it.
     *
     * What each status means, with "target" the region being written and
     * "other" the region being kept:
     *
     * | Status | Raised by | EEPROM state afterwards |
     * |---|---|---|
     * | Success | The whole flow | Target holds the image and its pointer is valid. Other pointer is 0 when invalidateOldRegion is set, else unchanged |
     * | NotInAppMode, NoEepromPresent, BadImageHeader, BadImageLength, NoBootableRegion | Pre-flight | Unchanged |
     * | PointerEraseFailed | Step 1, when its pointer write fails with SetAddressFailed, WriteFailed, FlashBusy, or CompareMismatch | Target pointer uncertain, target region untouched. Other region still boots (SLVAFL1 Figure 2-6 or 2-7) |
     * | SetAddressFailed | Step 2, the FLad to the bundle start, the FLad after the read-back, or the FLad of a busy retry | Target pointer 0, region untouched or partly written. Other region boots (Figure 2-7) |
     * | WriteFailed, FlashBusy | Step 2, an FLwd chunk | Target region partly written, pointer 0. Other region boots |
     * | VerifyFailed | Step 3, FLvy | Target written but not valid, pointer 0. Other region boots (Figure 2-8) |
     * | CompareMismatch | Step 2, the read-back of the first two chunks (lastStep() is WriteBundle) | The first chunks are not at the bundle start; where they went is unknown. The target pointer read 0 after step 1 but is not judged on this path, and chunks that landed next to it can have overwritten it (see below). Other region boots unless the chunks landed on it |
     * | CompareMismatch | Step 3, the compare (lastStep() is CompareBundle) | Same as VerifyFailed |
     * | PointerRestoreFailed | Step 3, when its pointer write fails as for step 1 | Target holds the image, its pointer uncertain. Other region boots |
     * | OldPointerEraseFailed | Step 4, when its pointer write fails as for step 1 | Target holds the image and its pointer is valid. Other pointer uncertain, so both may be valid; the low region boots first (Figure 2-9) |
     * | PointerNeighborsChanged | The neighbor check of the pointer write in step 1, 3, or 4 | Bytes 4 to 15 after that pointer changed. Stop and dump the EEPROM |
     * | PointerNeighborsChanged | Step 2, the pointer read of the read-back (lastStep() is WriteBundle) | Target pointer word or the 12 bytes after it changed while the first two chunks were written. Stop and dump the EEPROM |
     * | PointerNeighborsChanged | Step 2, before any task (lastStep() is WriteBundle, and no FLad or FLwd was issued after step 1) | Target pointer 0 from step 1, nothing else written by this call. The 12 bytes after the target pointer hold the start of the image: the trace of an earlier misplaced write. Dump and repair the EEPROM before another update |
     * | CommandRejected, CommandTimeout, I2cError | The executor, in any step | As the row of the step lastStep() names |
     *
     * For an executor failure, lastStep() gives the row: Preflight leaves the
     * EEPROM unchanged; ErasePointer reads as PointerEraseFailed; WriteBundle
     * as WriteFailed; VerifyBundle and CompareBundle as VerifyFailed, except
     * that the region may in fact be valid; RestorePointer as
     * PointerRestoreFailed; EraseOldPointer as OldPointerEraseFailed. A task
     * that timed out may still have run, which is why those pointer states
     * are "uncertain".
     *
     * What the read-back in step 2 is for. The chunks go wherever the
     * controller's write address points, and SLVAFL1 Figure 2-3 sends all of
     * them on the strength of one FLad. An FLad that returned 0 without
     * taking effect would leave the address where it was, and the bundle
     * would go there. Where that is, no document says. The case the tests
     * model is that it sits just past the target's pointer word. That holds
     * only if the address advanced by the four bytes step 1 wrote and step
     * 1's own read-back left it alone. In that case the bundle would run on
     * from the pointer row across the other region's pointer or bundle, and
     * the read-back stops it after two chunks: either the bytes are missing
     * at the bundle start (CompareMismatch), or, when the region already held
     * the same image, the pointer word or the 12 bytes after it have changed
     * (PointerNeighborsChanged). In that modelled case the misplaced bytes
     * are the 64 after the target's pointer word, in otherwise unused EEPROM,
     * and the other region and its pointer are untouched. One qualification
     * even there: the second chunk then starts 36 bytes into a 64-byte page
     * of the 24LC256 and crosses the page end. Unless the controller splits
     * the write, the bytes past the page end wrap onto the start of the page,
     * which is the pointer word. The check tests the word, so the update
     * still stops, but the target pointer may then no longer read 0.
     *
     * The reads are ordered for what follows them: the pointer row first, the
     * bundle start last. The last FLrd before the second FLad is then inside
     * the target region. If an FLrd moves the write address, which is not
     * documented either way, and the second FLad does not take effect, the
     * remaining chunks start near that last read and not at the pointer row.
     *
     * A stop of that kind leaves the first bytes of the image after the
     * target's pointer word. A second attempt would read them in step 1 and
     * take them as its reference, so the same failure would pass the check
     * unseen and the rest of the bundle would follow. Step 2 therefore begins
     * with a check that costs no task: if bytes 4 to 15 of step 1's read-back
     * equal the first 12 bytes of the image, the call returns
     * PointerNeighborsChanged before any FLad or FLwd of step 2. The progress
     * callback has had its done == 0 call by then and gets no other. The
     * EEPROM has to be dumped and repaired before another update.
     *
     * What it does not prove. It checks the first two chunks only. The FLad
     * that follows the read-back is not verified: nothing reads back a chunk
     * sent after it until step 3. A write address that is wrong after the
     * read-back, or goes wrong later, is therefore not seen before FLvy or
     * the compare of step 3, when the bytes have landed, and not at all when
     * the target already held the same image. When the target already held
     * the same image, two chunks misplaced anywhere other than just past its
     * pointer word also go unseen; the FLad after the read-back then sends
     * the remaining chunks to the right place, so 64 bytes are misplaced. A
     * misplaced write that only repeats bytes an earlier one left after the
     * pointer word changes nothing, so the read-back cannot see it. The check
     * before step 2 catches such a repeat only where it reads: when bytes 4 to
     * 15 after the pointer hold the first 12 bytes of the image, the call stops
     * before any task of step 2. Stray bytes at other offsets, past byte 15 of
     * the pointer row or starting elsewhere within it, are not seen. In these
     * cases the table above does not hold. No TI document describes a write address
     * that differs from the one FLad was given.
     *
     * The call blocks for the whole update. Stage 1 measured 17.7 ms per FLrd,
     * so the compare alone takes about 16 s for a 14336-byte image. An
     * application with a watchdog services it from the progress callback.
     *
     * @param target Region to write
     * @param image  Bundle image (a low-region binary); must stay valid for the call
     * @param length Image length in bytes
     * @param opt    Options; the defaults compare after writing and erase the old pointer
     * @return A status from the table above
     * @see SLVAFL1 Figures 2-3 and 2-6 to 2-10
     * @see setProgressCallback()
     */
    EepromUpdateStatus updateRegion(EepromRegion target, const uint8_t* image,
                                    size_t length, EepromUpdateOptions opt = {});

    /**
     * @brief Update the region the controller should not boot from next
     *
     * selectTargetRegion() followed by updateRegion() with the default
     * options. Selection runs first, so it issues FLrd before updateRegion()
     * checks MODE. A selection failure writes nothing and leaves lastStep() at
     * Preflight. After a completed call the updated region is the only one
     * with a valid pointer, so the controller boots it at its next restart.
     *
     * @param image  Bundle image; must stay valid for the call
     * @param length Image length in bytes
     * @return NoBootableRegion or a read failure from selection, or the
     *         updateRegion() status
     * @see updateRegion() for every status and the state it leaves
     * @see SLVAFL1 Figure 2-2
     */
    EepromUpdateStatus updateInactiveRegion(const uint8_t* image, size_t length);

    /**
     * @brief Bring both regions to the same bundle, with both pointers valid
     *
     * Runs updateInactiveRegion(), then updateRegion() on the other region
     * with invalidateOldRegion false. The result is the full-image layout of
     * SLVAFL1 Figure 2-6: the bundle in both regions and both pointers valid.
     * The application note's flow, followed literally for the second pass,
     * would always leave one pointer erased; keeping both valid is inferred
     * from Figure 2-6, which the note presents as the normal initial state.
     *
     * Before any write the image is checked against the smaller of the two
     * region capacities (BadImageHeader, BadImageLength), so the first pass
     * cannot succeed with an image the second region cannot hold.
     *
     * The first failure ends the call. The states in updateRegion()'s table
     * then apply to the pass that failed, and lastTarget() names that pass's
     * target. A failure in the second pass leaves the first pass complete:
     * its region holds the new bundle with a valid pointer, and boots.
     *
     * @param image  Bundle image; must stay valid for the call
     * @param length Image length in bytes
     * @return Success, or the first failure from either pass
     * @see updateRegion() for every status and the state it leaves
     */
    EepromUpdateStatus updateBothRegions(const uint8_t* image, size_t length);

    /**
     * @brief Write the full two-region layout to an EEPROM with no valid region pointer
     *
     * For a blank or corrupt EEPROM, with the controller already in APP mode
     * from a patch it received over I2Ct, for example through
     * TPS25751PatchLoader::loadPatchBundle(). SLVAFL1 (Recovering From EEPROM
     * Failure, Figure 4-1, page 13) describes that recovery: push a patch with
     * the PBM tasks, then correct the EEPROM with the flash tasks. The result
     * is the layout of SLVAFL1 Figure 2-6 with @p image in both regions, the
     * layout the full-flash binary holds (SLVAFL1 page 3 Note).
     *
     * Pre-flight, in this order, before any write. A failure here leaves the
     * EEPROM unchanged:
     *
     * 1. MODE reads APP (NotInAppMode; I2cError if MODE cannot be read).
     * 2. BOOT_FLAGS (0x2D) reports I2C EEPROM Present (NoEepromPresent;
     *    I2cError if it cannot be read). The TRM (page 33) sets the bit when
     *    an EEPROM device was discovered during boot. That it is set for a
     *    blank EEPROM after a PBM boot is inferred; hardware stage 5 of the
     *    plan checks it.
     * 3. checkImage() accepts the image for the smaller of the two region
     *    capacities (BadImageHeader, BadImageLength).
     * 4. Neither region pointer equals its bundle start (EepromNotBlank), so
     *    the call cannot overwrite an EEPROM with a valid pointer. That is
     *    as far as the check tells whether the EEPROM boots: it reads the two
     *    pointer words only, not the bundles or the AppConfigOffset words.
     *    Both pointers are read, Low first; a read failure returns its status.
     *
     * Then, in this order (decided 2026-11-23):
     *
     * 1. Both AppConfigOffset words (appConfigOffsetAddress()) to 0, Low
     *    (0x03FC) then High (0x07FC), before any pointer or bundle write. Each
     *    uses the four-task write and read-back of a pointer: FLrd of the 16
     *    bytes at the word, FLad, a 4-byte FLwd, FLrd again; bytes 4 to 15
     *    must not change and the word must read 0. The controller looks for a
     *    region's Header_ID at the region start plus this word (SLVAFL1 Boot
     *    Process, page 2).
     * 2. For the Low region and then the High region, steps 1 to 3 of
     *    updateRegion() with the compare always on: writeRegionPointer(region,
     *    0); the bundle write of step 2 with its trace check and read-back;
     *    FLvy; the FLrd compare; then one FLrd of the region's AppConfigOffset
     *    word, which must read 0 (CompareMismatch otherwise, with the pointer
     *    still 0); then writeRegionPointer(region, bundleStart(region)).
     *
     * There is no step 4: both pointers stay valid. The first failure ends the
     * call, and nothing is written after it. With no retry, a 14336-byte image
     * issues 2732 tasks: 2 FLrd of pre-flight, 8 for the two words, and 1361
     * per region. That is about the 52 s hardware stage 3 measured for
     * updateBothRegions().
     *
     * Why both words first. The last write before each bundle FLad is then
     * the pointer erase, as in updateRegion(), so the pointer-row checks of
     * step 2 look where a write address left behind by an FLad that did not
     * take effect would put the chunks. And a stop in either pass leaves both
     * words already 0, so updateRegion() can complete the layout from any
     * stop. A failure writing the High word ends the call before Low is
     * written, which costs nothing on a part that had no valid pointer.
     *
     * What the read-backs of the word writes cover. The 16 bytes at 0x03FC
     * run into the High pointer word at 0x0400 and the 8 bytes after it. The
     * 16 bytes at 0x07FC run into the first 12 bytes at 0x0800, which this
     * call has not written yet. The check compares those bytes before and
     * after the write, and no trace check applies to either row. Hardware
     * stage 2 showed that a 4-byte FLwd writes exactly four bytes.
     *
     * State afterwards, for a part on which pre-flight found no valid
     * pointer. "Region" is the one lastTarget() names:
     *
     * | Status | Raised by (lastStep()) | EEPROM state afterwards |
     * |---|---|---|
     * | Success | The whole flow (Done) | Both pointers valid, both AppConfigOffset words 0, the image in both regions. Every other byte as before the call: a blank part then equals the full-flash layout byte for byte |
     * | NotInAppMode, NoEepromPresent, BadImageHeader, BadImageLength, EepromNotBlank | Pre-flight (Preflight) | Unchanged |
     * | SetAddressFailed, WriteFailed, FlashBusy, CompareMismatch | The word write of the region (ClearAppConfigOffset) | No pointer written. That region's word uncertain; for High, the Low word reads 0 |
     * | PointerNeighborsChanged | The neighbor check of the word write (ClearAppConfigOffset) | No pointer written. The 12 bytes after the word changed: for Low, the High pointer word and the 8 bytes after it; for High, the first 12 bytes at 0x0800, before the Low bundle is written. Stop and dump the EEPROM |
     * | PointerEraseFailed, PointerNeighborsChanged | Step 1 of the region's pass (ErasePointer) | Both words 0. Region pointer uncertain, the region otherwise untouched |
     * | SetAddressFailed, WriteFailed, FlashBusy, CompareMismatch, PointerNeighborsChanged | Step 2 (WriteBundle) | As the step 2 rows of updateRegion(): region pointer 0, region untouched, partly written, or the first chunks misplaced. Both words 0 |
     * | VerifyFailed, CompareMismatch | Step 3, FLvy (VerifyBundle) or the compare (CompareBundle) | Region written but not valid, pointer 0. Both words 0 |
     * | CompareMismatch | The word check before the pointer write (CheckAppConfigOffset) | Region holds the image, compared, pointer 0; its word no longer reads 0 |
     * | PointerRestoreFailed, PointerNeighborsChanged | The pointer write (RestorePointer) | Region holds the image, compared, its word read 0; its pointer uncertain |
     * | CommandRejected, CommandTimeout, I2cError | The executor, in any step | As the row of the step lastStep() names |
     *
     * What else holds. Until the Low pass has written its pointer, no pointer
     * is valid unless a failed pointer write took effect, so the controller
     * keeps running the patch it holds in RAM and has no region to load at its
     * next restart. A failure in the High pass (lastTarget() High and
     * lastStep() other than ClearAppConfigOffset) leaves the Low pass
     * complete: the Low region holds the image, compared, and its word read 0
     * right before its pointer was written. After any failure a valid pointer
     * leads only to a region that passed FLvy and the compare, with a word
     * that read 0 before the pointer write.
     *
     * Recovery. While no pointer reads valid, call programBlank() again; after
     * PointerNeighborsChanged, or CompareMismatch from step 2, dump the EEPROM
     * first. When only the Low pointer reads valid after a High-pass failure,
     * updateRegion(High) with invalidateOldRegion false completes the layout;
     * the High word was written 0 before the passes began, and reading it
     * first confirms it. updateRegion() does not write the word.
     *
     * Limits. Each pass inherits the limits of steps 1 to 3 of updateRegion(),
     * stated there, because the last write before its bundle FLad is the
     * pointer erase and the last read is the erase's read-back. Under the
     * modelled faults (an FLad that reports success without taking effect,
     * alone or with an FLrd that moves the write address to the address read
     * plus 16), the other stray writes this flow can make land on a pointer
     * word about to be erased, on bytes after a pointer, on the Low bundle
     * start before the Low pass writes it, or past the end of a written
     * image. The test suite sweeps every FLad of the call under both models
     * from three start states and finds no valid pointer over a region that
     * does not hold the image. No TI document describes either behaviour.
     *
     * The progress callback runs as for updateRegion(), once per pass;
     * lastTarget() names the region in progress. The word writes make no
     * call.
     *
     * @param image  Bundle image (a low-region binary); must stay valid for the call
     * @param length Image length in bytes
     * @return A status from the table above, or I2cError or a readFlash()
     *         failure from pre-flight
     * @see updateRegion() for steps 1 to 3 and their limits
     * @see SLVAFL1 Figures 2-1, 2-3, 2-6, and 4-1
     */
    EepromUpdateStatus programBlank(const uint8_t* image, size_t length);

    // --- Read-back -----------------------------------------------------------

    /**
     * @brief Compare a region's bundle bytes with an image through FLrd
     *
     * Reads @p length bytes from bundleStart(r) in 16-byte FLrd calls. A final
     * read shorter than 16 bytes compares only the bytes the image covers.
     * This call does not invoke the progress callback; the compare inside
     * updateRegion() does.
     *
     * @param r             Region
     * @param image         Image to compare against
     * @param length        Bytes to compare, 1 to regionCapacity(r)
     * @param firstMismatch Optional; on CompareMismatch receives the image offset
     *                      (not the EEPROM address) of the first differing byte.
     *                      Untouched for any other result.
     * @return Success, CompareMismatch, BadImageLength for a null image or a
     *         length of 0 or above the capacity, or a readFlash() failure
     */
    EepromUpdateStatus compareRegion(EepromRegion r, const uint8_t* image,
                                     size_t length, uint32_t* firstMismatch = nullptr);

    /**
     * @brief Print EEPROM contents as a hex dump that xxd -r turns back into binary
     *
     * One line per FLrd call: the absolute EEPROM address as eight lowercase hex
     * digits, a colon and a space, then each byte as two lowercase hex digits
     * separated by single spaces, and nothing else. A final line holds fewer than
     * 16 bytes when @p length is not a multiple of 16. Lines printed before a
     * failure stay printed.
     *
     * The caller owns the range. The updater does not bound @p addr or
     * @p addr + @p length against the EEPROM size, and the 32-bit address can
     * wrap. What the controller returns past the end of the part is
     * undocumented, so such lines are printed as if they were EEPROM data.
     *
     * @param addr   First EEPROM address
     * @param length Bytes to dump (0 prints nothing)
     * @param out    Stream to print to
     * @return Success, or the first readFlash() failure
     */
    EepromUpdateStatus dump(uint32_t addr, size_t length, Stream& out);

    // --- Reset ---------------------------------------------------------------

    /**
     * @brief Restart the controller with GAID and wait for APP mode
     *
     * Kept apart from the update calls so that the application chooses when
     * the port disconnects: an APP-mode controller enters Type-C Error
     * Recovery and restarts after a typical 1-second delay (TRM Table 4-3).
     *
     * GAID never completes in the normal sense, because the processor
     * restarts. The executor result is read as follows:
     * - Success (COMMAND read 0, as it does after the reboot), Timeout, and
     *   I2CError (the controller can NAK while it reboots) all count as "reset
     *   issued". I2CError also counts as having seen the controller go away.
     * - Rejected ("!CMD") returns CommandRejected at once; no reset happened.
     *
     * After a reset was issued the call waits GAID_SETTLE_MS, then reads MODE
     * every GAID_MODE_POLL_MS until it reads APP or @p waitForAppMs has
     * passed since the GAID task returned. A failed MODE read counts as having
     * seen the controller go away. At least one MODE read is always made. The
     * time is taken from the executor's clock and is safe across its wrap.
     *
     * The call returns Success only when MODE reads APP after it has seen the
     * controller go away. APP without that evidence returns ResetNotObserved,
     * and the call returns at that first APP read.
     *
     * Why (decided 2026-11-23; docs/plans/feature/eeprom-update.md, coldReset
     * section). The controller reads APP before and after a restart, so MODE
     * alone cannot tell a completed restart from a GAID the controller
     * accepted without restarting, or rejected in a way the executor reports
     * as success. Without the rule the caller would trust an EEPROM boot that
     * never happened.
     *
     * Evidence. Hardware stages 3 and 4 of the plan, on one board with
     * firmware F8091166, saw the same sequence on every reset: the executor's
     * COMMAND poll returned I2CError 1276 to 1277 ms after GAID, and MODE read
     * APP 1938 to 1939 ms after it. That is one board and one firmware.
     *
     * Cost. A restart that completes between two MODE reads without failing
     * the GAID poll, or a firmware that completes the GAID handshake before it
     * resets and is back before the first MODE read, is reported as
     * ResetNotObserved although it happened. The caller then confirms with
     * VERSION (0x0F) and BOOT_FLAGS (0x2D). If that false negative is ever
     * seen, revisit GAID_SETTLE_MS and GAID_MODE_POLL_MS, or add a restart
     * witness that does not depend on timing.
     *
     * Remaining limit. An I2C failure that kept GAID from reaching the
     * controller looks the same as a NAK during the reboot, so Success does
     * not prove a restart either. Read VERSION and BOOT_FLAGS after any
     * result, which show the bundle and the region that booted.
     *
     * The call does not change the EEPROM. It sets lastStep() to Reset, and to
     * Done on Success.
     *
     * @param waitForAppMs Longest wait for MODE to read APP, from the return of
     *                     the GAID task (default 5000 ms). The call can block
     *                     for up to GAID_CMD_TIMEOUT_MS more while GAID is
     *                     polled.
     * @return Success when MODE read APP after the controller was seen to go
     *         away; ResetNotObserved when MODE read APP without that;
     *         ResetTimeout when MODE did not read APP in time;
     *         CommandRejected when the controller rejected GAID
     * @see TRM SLVUCR8B Table 4-3, p. 62
     * @see SLVAFL1 Figure 2-2
     */
    EepromUpdateStatus coldReset(uint32_t waitForAppMs = 5000);

    // --- Progress and state ----------------------------------------------------

    /**
     * @brief Set or clear the progress callback of the update flows
     *
     * @param callback Function to call, or nullptr for none (the default)
     * @param ctx      Passed back to @p callback unchanged; may be nullptr
     * @see EepromProgressCallback for when it is called and with what
     */
    void setProgressCallback(EepromProgressCallback callback, void* ctx = nullptr);

    /**
     * @brief The step the most recent update or reset was in when it returned
     *
     * Set by updateRegion(), updateInactiveRegion(), updateBothRegions(),
     * programBlank(), and coldReset(); the primitives, writeRegionPointer(),
     * compareRegion(), and dump() leave it unchanged. After a failure it names the step that
     * failed. Inside a progress callback it names the phase that is reporting.
     *
     * @return The step, None before the first flow, Done after a completed one
     * @see updateRegion() for the EEPROM state each step leaves
     */
    EepromUpdateStep lastStep() const { return _lastStep; }

    /**
     * @brief The region the most recent update pass wrote or meant to write
     *
     * Set when updateRegion() starts, whether called directly or through
     * updateInactiveRegion() or updateBothRegions(). It is the "target" of the
     * state table in updateRegion(). A selection failure in
     * updateInactiveRegion() leaves the value of the pass before it.
     * programBlank() sets it to Low when it starts, pre-flight included, and
     * to High when its High pass starts; it is the "region" of the state
     * table in programBlank().
     *
     * @return The region; Low before the first update
     */
    EepromRegion lastTarget() const { return _lastTarget; }

    // --- Pure helpers --------------------------------------------------------

    /**
     * @brief Check an image before it is written to a region
     *
     * @param image          Image bytes
     * @param length         Image length in bytes
     * @param regionCapacity Bytes available in the target region (regionCapacity())
     * @return Success; BadImageLength for a null image, a length below
     *         kMinImageLength, or a length above @p regionCapacity;
     *         BadImageHeader when the first word is not kBundleHeaderId
     */
    static EepromUpdateStatus checkImage(const uint8_t* image, size_t length,
                                         uint32_t regionCapacity);

    /**
     * @brief Version word of an image
     *
     * The little-endian word at image offset 0x34 equals the VERSION register
     * (0x0F) of a controller running that bundle in the two builds checked. It is
     * reported, not used as a gate.
     *
     * @param image  Image bytes
     * @param length Image length in bytes
     * @return The version word, or 0 for a null image or one shorter than kMinImageLength
     */
    static uint32_t imageVersion(const uint8_t* image, size_t length);

    /**
     * @brief Convert an EepromUpdateStatus to a string
     * @param s Status
     * @return Pointer to a string literal; never nullptr
     */
    static const char* toString(EepromUpdateStatus s);

    /**
     * @brief Convert an EepromRegion to a string
     * @param r Region
     * @return "Low" or "High"
     */
    static const char* toString(EepromRegion r);

    /**
     * @brief Convert an EepromUpdateStep to a string
     * @param step Step
     * @return Pointer to a string literal; never nullptr
     */
    static const char* toString(EepromUpdateStep step);

private:
    TPS25751CommandExecutor& _exec;
    uint32_t _eepromSize;
    EepromProgressCallback _progress = nullptr;
    void* _progressCtx = nullptr;
    EepromUpdateStep _lastStep = EepromUpdateStep::None;
    EepromRegion _lastTarget = EepromRegion::Low;

    /// Map a non-Success executor status to CommandRejected, CommandTimeout, or I2cError.
    static EepromUpdateStatus mapTaskStatus(TPS25751TaskStatus ts);

    /// Write @p value to @p out[0..3], least significant byte first.
    static void putLe32(uint8_t* out, uint32_t value);

    /// Read a little-endian 32-bit word from @p in[0..3].
    static uint32_t getLe32(const uint8_t* in);

    /// Pass Success, the executor failures, and PointerNeighborsChanged
    /// through; turn every other pointer-write failure into @p stepFailure.
    static EepromUpdateStatus mapPointerWriteFailure(EepromUpdateStatus s,
                                                     EepromUpdateStatus stepFailure);

    /// The four tasks of writeRegionPointer() for the 4-byte word at @p addr.
    /// On Success, @p readBack (when not nullptr) receives the 16 bytes the
    /// final read-back saw.
    EepromUpdateStatus writeWordChecked(uint32_t addr, uint32_t value, uint8_t* readBack = nullptr);

    /// Pre-flight checks 1 to 3, shared by updateRegion() and programBlank():
    /// MODE reads APP, BOOT_FLAGS reports the EEPROM, and checkImage()
    /// accepts the image for @p capacity. Issues no task.
    EepromUpdateStatus checkHostAndImage(const uint8_t* image, size_t length, uint32_t capacity);

    /// The four pre-flight checks of updateRegion(); issues no write.
    EepromUpdateStatus preflight(EepromRegion target, const uint8_t* image, size_t length);

    /// The four pre-flight checks of programBlank(); issues no write.
    EepromUpdateStatus preflightBlank(const uint8_t* image, size_t length);

    /// Step 1 of an update: the target pointer to 0, with lastStep() set to
    /// ErasePointer and the failures of the write mapped to PointerEraseFailed.
    /// @p pointerRow receives the 16 bytes the read-back saw.
    EepromUpdateStatus eraseTargetPointer(EepromRegion target, uint8_t* pointerRow);

    /// Steps 2 and 3 of an update, shared by updateRegion() and
    /// programBlank(): the bundle write with its read-back, FLvy, the compare
    /// when @p compare is set, and the target pointer to the bundle start.
    /// Sets lastStep() for each phase. @p pointerRow is the 16 bytes step 1's
    /// read-back saw at the target pointer. With @p checkAppConfigOffset set
    /// (programBlank()), the target's AppConfigOffset word is read right before
    /// the pointer write and must be 0 (CompareMismatch otherwise).
    EepromUpdateStatus installBundle(EepromRegion target, const uint8_t* image, size_t length,
                                     const uint8_t* pointerRow, bool compare,
                                     bool checkAppConfigOffset);

    /// Step 2 of updateRegion(): FLad, the first two chunks, the early
    /// read-back, a fresh FLad, and the remaining chunks, with progress calls.
    /// @p pointerRow is the 16 bytes step 1's read-back saw at the target pointer.
    EepromUpdateStatus writeBundle(EepromRegion target, const uint8_t* image, size_t length,
                                   const uint8_t* pointerRow);

    /// Image bytes @p from to @p to as chunks at the current write address,
    /// with the busy retry and a progress call after each chunk.
    EepromUpdateStatus writeChunks(uint32_t base, const uint8_t* image, size_t from, size_t to,
                                   uint32_t total);

    /// The early read-back of step 2. Reads the 16 bytes at the target pointer,
    /// then the first @p written image bytes at the bundle start. Judges the
    /// bundle bytes first (CompareMismatch), then the 16 bytes against
    /// @p pointerRow (PointerNeighborsChanged).
    EepromUpdateStatus checkEarlyChunks(EepromRegion target, const uint8_t* image, size_t written,
                                        const uint8_t* pointerRow);

    /// One bundle chunk at the current write address, with the busy retry;
    /// @p addr is the chunk's EEPROM address, used by the fresh FLad of a retry.
    EepromUpdateStatus writeChunk(uint32_t addr, const uint8_t* data, size_t len);

    /// compareRegion(), with progress calls when @p report is set.
    EepromUpdateStatus compareImage(EepromRegion r, const uint8_t* image, size_t length,
                                    uint32_t* firstMismatch, bool report);

    /// Invoke the progress callback, if one is set.
    void reportProgress(uint32_t done, uint32_t total);
};
