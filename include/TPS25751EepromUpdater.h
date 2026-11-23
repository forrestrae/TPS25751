#pragma once

/**
 * @file TPS25751EepromUpdater.h
 * @brief Read-back and inspection of the TPS25751 configuration EEPROM over I2Ct
 *
 * The TPS25751 boots its patch bundle from an external EEPROM on its I2Cc bus. A
 * host on I2Ct reaches that EEPROM through the flash 4CC tasks: FLrd reads, FLad
 * and FLwd write, FLvy validates a bundle header (TRM SLVUCR8B Tables 4-19 to
 * 4-22, pages 81 and 82). TI application note SLVAFL1 describes the two-region
 * layout and the update flow, and its EEPROM Boot Flow section says the host
 * writes the EEPROM once MODE reads APP.
 *
 * This version provides the read-only part: the FLrd and FLvy primitives, region
 * pointer reads, target-region selection, comparison with an image, a hex dump,
 * and pure image checks. It issues no FLad, FLwd, or GAID, so it cannot change
 * the EEPROM or reset the controller.
 *
 * EEPROM layout (SLVAFL1 Figures 2-1 and 2-6, and the source code on page 10):
 *
 * | Item            | Low region | High region |
 * |-----------------|------------|-------------|
 * | Region pointer  | 0x0000     | 0x0400      |
 * | AppConfigOffset | 0x03FC     | 0x07FC      |
 * | Bundle start    | 0x0800     | 0x4400      |
 *
 * A region pointer is valid when it equals its region's bundle start.
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
 * uint32_t low = 0;
 * if (updater.readRegionPointer(EepromRegion::Low, low) == EepromUpdateStatus::Success) {
 *     Serial.println(low, HEX);
 * }
 * @endcode
 *
 * @see TI SLVAFL1, "TPS25751 and TPS26750 EEPROM Update Over I2C", October 2024
 * @see TI TPS25751 Technical Reference Manual SLVUCR8B, Tables 4-19 to 4-22, pp. 81-82
 */

#include <Arduino.h>
#include <Stream.h>
#include "TPS25751Task.h"
#include "TPS25751CommandExecutor.h"

// ---------------------------------------------------------------------------
// Flash task codes and timeouts (TRM SLVUCR8B pages 81 and 82)
// ---------------------------------------------------------------------------

/// 4CC task code: read 16 bytes of the external EEPROM (TRM Table 4-19).
constexpr TPS25751FourCC CMD_FLRD = TPS25751FourCC::of("FLrd");

/// 4CC task code: validate the bundle header at an address (TRM Table 4-22).
constexpr TPS25751FourCC CMD_FLVY = TPS25751FourCC::of("FLvy");

/// COMMAND-poll timeout (ms) for FLrd.
constexpr uint32_t FLRD_CMD_TIMEOUT_MS = 200;

/// COMMAND-poll timeout (ms) for FLvy. Not yet measured on hardware; the task
/// completes after the header is checked (TRM Table 4-22).
constexpr uint32_t FLVY_CMD_TIMEOUT_MS = 1000;

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
 * The full set is defined now so that the write flows can add to the
 * implementation without changing the API. The read-only calls raise only
 * Success, BadImageHeader, BadImageLength, NoBootableRegion, VerifyFailed,
 * CompareMismatch, CommandRejected, CommandTimeout, and I2cError.
 */
enum class EepromUpdateStatus : uint8_t {
    Success,                  ///< The call completed as requested
    NotInAppMode,             ///< Pre-flight: MODE did not read APP
    NoEepromPresent,          ///< Pre-flight: BOOT_FLAGS (0x2D) did not report I2C EEPROM Present
    BadImageHeader,           ///< checkImage(): the image does not start with 0xACE00001
    BadImageLength,           ///< checkImage(): the image is shorter than 0x38 bytes or longer than the region
    NoBootableRegion,         ///< Neither region pointer equals its bundle start
    EepromNotBlank,           ///< Blank programming refused: a region pointer is valid
    PointerEraseFailed,       ///< Update step 1: the target pointer could not be written to 0
    SetAddressFailed,         ///< FLad reported a failure in its task return code (TRM Table 4-20; Table 4-1 puts the standard TaskResult in bits 3:0)
    WriteFailed,              ///< FLwd returned an error other than busy
    FlashBusy,                ///< FLwd still reported busy (0xFF) after the retries
    VerifyFailed,             ///< FLvy reported the bundle not valid (0x01) or an unknown code
    CompareMismatch,          ///< compareRegion(): the EEPROM differs from the image
    PointerRestoreFailed,     ///< Update step 3: the target pointer could not be written
    OldPointerEraseFailed,    ///< Update step 4: the old region pointer could not be written to 0
    PointerNeighborsChanged,  ///< Bytes 4 to 15 after a pointer changed during a pointer write
    ResetTimeout,             ///< The controller did not return to APP after GAID
    CommandRejected,          ///< The executor reported "!CMD" (task code not recognized)
    CommandTimeout,           ///< The executor saw neither clear nor "!CMD" before the timeout
    I2cError                  ///< An I2C transaction or a register read failed
};

// ---------------------------------------------------------------------------
// TPS25751EepromUpdater
// ---------------------------------------------------------------------------

/**
 * @brief Sequencer for the TPS25751 flash 4CC tasks (read-back subset)
 *
 * Holds a reference to a TPS25751CommandExecutor and the EEPROM size. Every call
 * is synchronous. Use it with the controller in APP mode: SLVAFL1's EEPROM Boot
 * Flow section describes the EEPROM update in APP mode. The TRM task tables
 * (pages 81 and 82) state no mode requirement, and this class does not check
 * MODE.
 *
 * @note Not a register class. Does not inherit TPS25751Register. Does not
 *       register with TPS25751RegisterFactory.
 */
class TPS25751EepromUpdater {
public:
    /// Bytes FLrd returns per call (TRM Table 4-19, bits 127:0).
    static constexpr size_t kFlashReadBytes = 16;

    /// Header_ID at the start of every patch bundle, little-endian (SLVAFL1 section 2.1).
    static constexpr uint32_t kBundleHeaderId = 0xACE00001UL;

    /// Image offset of the version word, which matches the VERSION register (0x0F).
    static constexpr size_t kImageVersionOffset = 0x34;

    /// Smallest image checkImage() accepts: the header through the version word.
    static constexpr size_t kMinImageLength = 0x38;

    /// EEPROM size assumed when none is given: the board's 24LC256.
    static constexpr uint32_t kDefaultEepromSize = 32768;

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
     * Two details are inferred, not stated by the TRM, and hardware stage 1
     * confirms them: the address byte order (Table 4-20 states little-endian
     * for FLad only; the SLVAFL1 source code casts a uint32_t the same way for
     * FLrd), and that DATA byte 0 holds the EEPROM byte at @p addr.
     *
     * @param addr EEPROM address
     * @param out  Buffer of at least kFlashReadBytes bytes; must not be nullptr
     * @return Success, or CommandRejected, CommandTimeout, or I2cError from the executor
     * @see TRM SLVUCR8B Table 4-19, p. 81
     */
    EepromUpdateStatus readFlash(uint32_t addr, uint8_t out[kFlashReadBytes]);

    /**
     * @brief Run FLvy on the bundle at an address
     *
     * FLvy completes after the bundle header is checked; whether it also checks
     * the bundle CRC is not documented, so a Success here does not prove the
     * bundle body is intact. Use compareRegion() for that. The address goes
     * out least significant byte first, inferred as for readFlash().
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

    // --- Read-back -----------------------------------------------------------

    /**
     * @brief Compare a region's bundle bytes with an image through FLrd
     *
     * Reads @p length bytes from bundleStart(r) in 16-byte FLrd calls. A final
     * read shorter than 16 bytes compares only the bytes the image covers.
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
     * @param addr   First EEPROM address
     * @param length Bytes to dump (0 prints nothing)
     * @param out    Stream to print to
     * @return Success, or the first readFlash() failure
     */
    EepromUpdateStatus dump(uint32_t addr, size_t length, Stream& out);

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

private:
    TPS25751CommandExecutor& _exec;
    uint32_t _eepromSize;

    /// Map a non-Success executor status to CommandRejected, CommandTimeout, or I2cError.
    static EepromUpdateStatus mapTaskStatus(TPS25751TaskStatus ts);

    /// Write @p value to @p out[0..3], least significant byte first.
    static void putLe32(uint8_t* out, uint32_t value);

    /// Read a little-endian 32-bit word from @p in[0..3].
    static uint32_t getLe32(const uint8_t* in);
};
