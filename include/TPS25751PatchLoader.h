#pragma once

/**
 * @file TPS25751PatchLoader.h
 * @brief Patch Burst Mode (PBM) loader for the TPS25751 USB-C PD controller
 *
 * Implements the host-side PBM sequencing documented in TI application note
 * SLVAFV8A ("Using an Embedded Controller (EC) to Load a Patch Bundle Directly
 * to the TPS25751 or TPS26750"), §3-4.
 *
 * This class is built **on top of TPS25751::executeCommand()** (the generic 4CC
 * task interface, TRM Sec 4.4.2) and TPS25751::burstWrite() (the raw PBM burst
 * primitive).  It adds only the PBM-specific 17-step sequencing, mode checks,
 * and status mapping.
 *
 * NOTE: This class is NOT a register decoder and deliberately does NOT inherit
 * TPS25751Register, nor does it participate in the factory pattern.  Reviewers
 * should not expect it to follow the register-class template (STANDARDS.md).
 * It is a standalone sequencer that composes a TPS25751 host reference.
 */

#include <Arduino.h>
#include <Stream.h>
#include "TPS25751Task.h"             // TPS25751FourCC, TPS25751TaskStatus, TPS25751TaskResult
#include "TPS25751RegisterAddress.h"  // TPS25751Registers::Address (INT_MASK1, INT_CLEAR1, …)

class TPS25751;  ///< forward declaration — full type included by TPS25751PatchLoader.cpp

// ---------------------------------------------------------------------------
// PBM constants (SLVAFV8A §3-4 / Table 3-3)
// ---------------------------------------------------------------------------

/// I2C target address of the PBM receive port (Table 3-3).
/// The raw burst sends to this 7-bit I2C address with NO register number and
/// NO length byte — unlike every other TPS25751 write (see burstWrite docs).
constexpr uint8_t PBM_I2C_TARGET = 0x30;

/// Timeout value written to the PBMs session-config byte (SLVAFV8A Table 4-2,
/// ~3.1 s per the app note).
constexpr uint8_t PBM_TIMEOUT = 0x31;

/// 4CC command code to start a Patch Burst Mode session.
constexpr TPS25751FourCC CMD_PBMS = TPS25751FourCC::of("PBMs");

/// 4CC command code to commit (close) a Patch Burst Mode session.
constexpr TPS25751FourCC CMD_PBMC = TPS25751FourCC::of("PBMc");

/// Post-PBMc settle delay in ms before reading MODE (SLVAFV8A §4.1, step 15).
constexpr uint32_t PATCH_APPLY_DELAY_MS = 20;

/// COMMAND-poll timeout (ms) passed to executeCommand() for the PBMs (session
/// start) handshake. PBMs only validates the 6-byte config, so it clears quickly.
constexpr uint32_t PBMS_CMD_TIMEOUT_MS = 500;

/// COMMAND-poll timeout (ms) for the PBMc (commit) handshake — longer than PBMs
/// because the device applies the bundle and begins the PTCH→APP transition
/// before clearing COMMAND.
constexpr uint32_t PBMC_CMD_TIMEOUT_MS = 5000;

// PBM_BURST_CHUNK is declared in TPS25751.h (before the class, so it can be
// used as the default parameter of burstWrite).  It is visible here whenever
// TPS25751.h is included, which TPS25751PatchLoader.cpp does unconditionally.

// ---------------------------------------------------------------------------
// PatchLoadStatus — one distinct value per failure point in the 17-step flow
// ---------------------------------------------------------------------------

/**
 * @brief Outcome of TPS25751PatchLoader::loadPatchBundle()
 *
 * Each enumerator corresponds to a distinct failure point in the 17-step PBM
 * sequence (SLVAFV8A §4.1), giving callers a precise diagnostic.
 */
enum class PatchLoadStatus : uint8_t {
    Success,            ///< All 17 steps completed; device is now in APP mode
    NotInPatchMode,     ///< Pre-flight MODE read did not return PTCH
    PbmsRejected,       ///< TPS25751 rejected the PBMs 4CC ("!CMD")
    BadBundleSize,      ///< PatchStartStatus 0x04 — bundleSize out of valid range
    BadTargetAddress,   ///< PatchStartStatus 0x05 — target address not accepted
    BadTimeout,         ///< PatchStartStatus 0x06 — timeout value not accepted
    BurstFailed,        ///< burstWrite() returned false (one or more I2C chunk errors)
    PbmcRejected,       ///< TPS25751 rejected the PBMc 4CC ("!CMD")
    PatchApplyFailed,   ///< DATA[0] was not zero after PBMc + delay (apply error)
    NotInAppMode,       ///< Final MODE read did not return APP after successful PBMc
    CommandTimeout,     ///< executeCommand() timed out (neither clear nor "!CMD")
    I2cError            ///< An I2C transaction failed inside executeCommand()
};

// ---------------------------------------------------------------------------
// TPS25751PatchLoader
// ---------------------------------------------------------------------------

/**
 * @brief Loads a TI patch bundle into the TPS25751 via Patch Burst Mode (PBM)
 *
 * When the TPS25751 boots without a configuration EEPROM it enters PTCH (patch)
 * mode and waits for the host to push the firmware+configuration image directly
 * over the I2Ct bus.  This class sequences the 17-step PBM handshake from TI
 * application note SLVAFV8A §4.1:
 *
 *   1.  (Optional) Wait for I2Ct_IRQ low — device signals "Ready-for-Patch".
 *   2.  Read MODE — confirm PTCH via readModeRegister().
 *   3.  (IRQ mode) Configure INT_MASK1 / clear INT_CLEAR1.
 *   4-9. executeCommand(PBMs, config[6]) — start the burst session.
 *        config = bundleSize[4, LE] + targetAddr(0x30) + timeout(0x31).
 *  10.  burstWrite(0x30, image, length) — stream the image in Wire-sized chunks.
 *  11.  (IRQ mode) Clear INT_CLEAR1 again.
 *  12-14. executeCommand(PBMc) — commit the bundle.
 *  15.  delay(PATCH_APPLY_DELAY_MS).
 *  16.  Read DATA[0] — confirm PatchStartStatus is 0.
 *  17.  Read MODE — confirm APP.
 *
 * The 4CC handshake (DATA staging, COMMAND write, completion polling, "!CMD"
 * detection, DATA read-back) is handled entirely by TPS25751::executeCommand();
 * this class just supplies the payload and maps TPS25751TaskResult to
 * PatchLoadStatus.
 *
 * @note Not a register class.  Does not inherit TPS25751Register.  Does not
 *       register with TPS25751RegisterFactory.  See ARCHITECTURE.md ADR-011.
 */
class TPS25751PatchLoader {
public:
    /**
     * @brief Construct a patch loader bound to a TPS25751 host
     * @param pd     The TPS25751 instance to drive (must outlive this object)
     * @param irqPin Arduino pin wired to I2Ct_IRQ, or -1 for pure polling mode.
     *               In polling mode the loader relies solely on executeCommand()'s
     *               built-in COMMAND polling; the IRQ pin is never read.
     */
    explicit TPS25751PatchLoader(TPS25751& pd, int irqPin = -1);

    /**
     * @brief Execute the full 17-step PBM sequence (SLVAFV8A §4.1)
     *
     * @param image  Pointer to the patch-bundle byte array (low-region binary
     *               produced by TI's USBCPD Application Customization Tool —
     *               see examples/load-patch-bundle/src/patch_bundle.h)
     * @param length Total image length in bytes (written as bundleSize LE in
     *               the PBMs config; must match the actual byte count exactly)
     * @return Detailed per-step outcome; inspect PatchLoadStatus for the
     *         precise failure point.
     *
     * @see SLVAFV8A §4.1 (17-step PBM sequence, SLVAFV8A Tables 4-1 / 4-2)
     */
    PatchLoadStatus loadPatchBundle(const uint8_t* image, size_t length);

    /**
     * @brief Print the last loadPatchBundle() result to a stream
     * @param s Output stream (defaults to Serial)
     */
    void debugPrint(Stream& s = Serial) const;

    /**
     * @brief Convert a PatchLoadStatus to a human-readable C-string
     * @param status The status value to describe
     * @return Pointer to a string literal; never nullptr
     */
    static const char* toString(PatchLoadStatus status);

private:
    TPS25751& _pd;
    int        _irqPin;
    PatchLoadStatus _lastStatus{PatchLoadStatus::I2cError};

    // --- Step helpers ---

    /// Step 1: if irqPin >= 0, wait for the pin to go LOW (ready-for-patch).
    bool waitReadyForPatch() const;

    /// Step 2: read MODE and confirm isPatchMode().
    bool confirmPatchMode() const;

    /// Step 17: read MODE and confirm isApplicationMode().
    bool confirmAppMode() const;

    /// Step 3: configure INT_MASK1 to enable CMD1-complete interrupt (IRQ mode only).
    void configInterruptMask() const;

    /// Steps 3 / 11: write all-ones to INT_CLEAR1 to clear pending interrupts (IRQ mode only).
    void clearInterrupts() const;

    /**
     * @brief Map a TPS25751TaskStatus to a PatchLoadStatus
     *
     * Shared by the PBMs and PBMc call sites so both use the same mapping.
     * TPS25751TaskStatus::Success is never returned here — the caller checks
     * returnCode to distinguish sub-error codes from a successful issue.
     *
     * @param ts       Raw executor status (never Success at the call sites below)
     * @param rejected PatchLoadStatus to return for TPS25751TaskStatus::Rejected
     */
    static PatchLoadStatus mapTaskStatus(TPS25751TaskStatus ts, PatchLoadStatus rejected);
};
