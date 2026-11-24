#include "TPS25751PatchLoader.h"
#include "TPS25751.h"
#include "TPS25751Mode.h"
#include "TPS25751Data.h"
#include "TPS25751Debug.h"
#include "TPS25751RegisterAddress.h"
#include <Arduino.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TPS25751PatchLoader::TPS25751PatchLoader(TPS25751& pd, int irqPin)
    : _pd(pd), _irqPin(irqPin)
{
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

PatchLoadStatus TPS25751PatchLoader::mapTaskStatus(TPS25751TaskStatus ts,
                                                    PatchLoadStatus rejected)
{
    switch (ts) {
        case TPS25751TaskStatus::Rejected:  return rejected;
        case TPS25751TaskStatus::Timeout:   return PatchLoadStatus::CommandTimeout;
        case TPS25751TaskStatus::I2CError:  return PatchLoadStatus::I2cError;
        default:
            // Should not reach here (Success is not passed to this helper)
            return PatchLoadStatus::I2cError;
    }
}

bool TPS25751PatchLoader::waitReadyForPatch() const
{
    if (_irqPin < 0) {
        TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "PBM: step 1 skipped (pure-polling mode)");
        return true;
    }

    constexpr uint32_t READY_TIMEOUT_MS = 5000;
    uint32_t start = millis();
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK,
        "PBM: step 1 — waiting for IRQ low on pin %d (timeout %u ms)",
        _irqPin, READY_TIMEOUT_MS);

    while (digitalRead(_irqPin) != LOW) {
        if (millis() - start > READY_TIMEOUT_MS) {
            TPS_DEBUG_WARN(DEBUG_CAT_TASK,
                "PBM: step 1 timeout — IRQ did not go low within %u ms", READY_TIMEOUT_MS);
            return false;
        }
        delay(1);
    }

    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 1 — IRQ low, device ready for patch");
    return true;
}

bool TPS25751PatchLoader::confirmPatchMode() const
{
    auto mode = _pd.readModeRegister(/*validate=*/false);
    if (!mode) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: step 2 — failed to read MODE register");
        return false;
    }
    if (!mode->isPatchMode()) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: step 2 — device not in PTCH mode (got '%s')", mode->modeString());
        return false;
    }
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 2 — confirmed PTCH mode");
    return true;
}

PatchLoadStatus TPS25751PatchLoader::stagePbmsConfig(const uint8_t* config, size_t length) const
{
    // Steps 4 and 5. SLVAFV8A writes DATA, waits 500 us, reads it back, and
    // rewrites it until it matches (step 5, page 8; code, page 17); its trace
    // shows a first read-back of zeros after an acknowledged write. TRM Figure
    // 5-1 (page 88) rewrites until "DATA1 write completed successfully". Both
    // loop without a bound; PBMS_DATA_ATTEMPTS bounds it here.
    bool anyReadBack = false;
    for (uint8_t attempt = 1; attempt <= PBMS_DATA_ATTEMPTS; ++attempt) {
        if (!_pd.writeCommandData(config, length)) {
            TPS_DEBUG_WARN(DEBUG_CAT_TASK, "PBM: step 4: DATA write failed (attempt %u)", (unsigned)attempt);
            continue;
        }
        delayMicroseconds(PBMS_DATA_VERIFY_DELAY_US);
        auto data = _pd.readDataRegister(/*validate=*/false);
        if (!data || !data->isValid()) {
            TPS_DEBUG_WARN(DEBUG_CAT_TASK, "PBM: step 5: DATA read-back failed (attempt %u)", (unsigned)attempt);
            continue;
        }
        anyReadBack = true;
        if (memcmp(data->raw(), config, length) == 0) {
            TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 5: DATA holds the PBMs configuration (attempt %u)",
                           (unsigned)attempt);
            return PatchLoadStatus::Success;
        }
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "PBM: step 5: DATA read-back differs from the write (attempt %u)",
                       (unsigned)attempt);
    }
    TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: steps 4 and 5: no matching DATA read-back in %u attempts",
                    (unsigned)PBMS_DATA_ATTEMPTS);
    return anyReadBack ? PatchLoadStatus::PbmsConfigMismatch : PatchLoadStatus::I2cError;
}

void TPS25751PatchLoader::endSession() const
{
    // TRM Table 4-17 (page 80): PBMe ends the session, restores the second
    // target address, and leaves MODE at PTCH. Table 4-15 (page 76): a second
    // PBMs in an open session ignores its DATA input, so without PBMe a retry
    // with an image of another size would keep this session's bundle size.
    const TPS25751TaskResult r = _pd.executeCommand(CMD_PBME, nullptr, 0, nullptr, 0, PBME_CMD_TIMEOUT_MS);
    if (r.status == TPS25751TaskStatus::Success) {
        TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: PBMe complete, return code 0x%02X", r.returnCode);
    } else {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "PBM: PBMe failed with executor status %u", (unsigned)r.status);
    }
}

PatchLoadStatus TPS25751PatchLoader::readPatchResult() const
{
    // Step 16. A failed read is retried every PBM_POLL_INTERVAL_MS up to
    // PBM_DATA_READ_CAP_MS (a choice; the documents give no bound).
    const uint32_t start = millis();
    for (;;) {
        auto data = _pd.readDataRegister(/*validate=*/false);
        if (data && data->isValid()) {
            if (data->raw()[0] != 0x00) {
                TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                    "PBM: step 16: PatchStartStatus not cleared (DATA[0]=0x%02X)", data->raw()[0]);
                return PatchLoadStatus::PatchApplyFailed;
            }
            return PatchLoadStatus::Success;
        }
        if (millis() - start >= PBM_DATA_READ_CAP_MS) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: step 16: no DATA read succeeded within %u ms",
                            (unsigned)PBM_DATA_READ_CAP_MS);
            return PatchLoadStatus::I2cError;
        }
        delay(PBM_POLL_INTERVAL_MS);
    }
}

PatchLoadStatus TPS25751PatchLoader::waitForAppMode() const
{
    // Step 17. SLVAFV8A polls MODE every 10 ms until APP (code, page 18). Failed
    // reads are tolerated; the poll ends at PBM_MODE_POLL_CAP_MS (a choice).
    const uint32_t start = millis();
    bool anyRead = false;
    for (;;) {
        auto mode = _pd.readModeRegister(/*validate=*/false);
        if (mode) {
            anyRead = true;
            if (mode->isApplicationMode()) {
                TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 17: MODE reads APP, patch load complete");
                return PatchLoadStatus::Success;
            }
        }
        if (millis() - start >= PBM_MODE_POLL_CAP_MS) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: step 17: MODE did not read APP within %u ms",
                            (unsigned)PBM_MODE_POLL_CAP_MS);
            // The last read that succeeded was not APP, or none succeeded.
            return anyRead ? PatchLoadStatus::NotInAppMode : PatchLoadStatus::I2cError;
        }
        delay(PBM_POLL_INTERVAL_MS);
    }
}

void TPS25751PatchLoader::configInterruptMask() const
{
    if (_irqPin < 0) return;

    // Enable CMD1-complete interrupt in INT_MASK1.
    // CMD1_COMPLETE is INT_EVENT1 bit 30 (TRM §5.1.3.3):
    //   byte index 3 (bits 31-24), bit 6 within that byte => mask byte[3] = 0x40.
    uint8_t mask[TPS25751Registers::Registers::INT_MASK1.size];
    memset(mask, 0x00, sizeof(mask));
    mask[3] = 0x40;  // bit 30 = CMD1_COMPLETE

    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "PBM: step 3 — configuring INT_MASK1 (CMD1_COMPLETE)");
    _pd.writeRegister(TPS25751Registers::Address::INT_MASK1, mask, sizeof(mask));
}

void TPS25751PatchLoader::clearInterrupts() const
{
    if (_irqPin < 0) return;

    // Write all-ones to INT_CLEAR1 to clear every pending interrupt.
    uint8_t clear[TPS25751Registers::Registers::INT_CLEAR1.size];
    memset(clear, 0xFF, sizeof(clear));

    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "PBM: clearing INT_CLEAR1");
    _pd.writeRegister(TPS25751Registers::Address::INT_CLEAR1, clear, sizeof(clear));
}

// ---------------------------------------------------------------------------
// Main sequence
// ---------------------------------------------------------------------------

PatchLoadStatus TPS25751PatchLoader::loadPatchBundle(const uint8_t* image, size_t length)
{
    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "PBM: loadPatchBundle start — image=%p length=%u irqPin=%d",
        image, (unsigned)length, _irqPin);

    // Step 1: optional IRQ wait (device signals ready-for-patch)
    if (!waitReadyForPatch()) {
        _lastStatus = PatchLoadStatus::CommandTimeout;
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: failed at step 1 (IRQ wait)");
        return _lastStatus;
    }

    // Step 2: confirm PTCH mode
    if (!confirmPatchMode()) {
        _lastStatus = PatchLoadStatus::NotInPatchMode;
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: failed at step 2 (mode check)");
        return _lastStatus;
    }

    // Step 3: configure interrupt mask and clear pending interrupts (IRQ mode only)
    configInterruptMask();
    clearInterrupts();

    // Steps 4 to 9: stage the PBMs configuration in DATA and check it, then
    // start the burst session with PBMs.
    // Config payload (6 bytes): bundleSize[4, little-endian] + targetAddr + timeout
    uint8_t config[6];
    config[0] = static_cast<uint8_t>((length >>  0) & 0xFF);  // bundleSize LSB
    config[1] = static_cast<uint8_t>((length >>  8) & 0xFF);
    config[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    config[3] = static_cast<uint8_t>((length >> 24) & 0xFF);  // bundleSize MSB
    config[4] = PBM_I2C_TARGET;  // 0x30
    config[5] = PBM_TIMEOUT;     // 0x32 (five loads on hardware, stage 7)

    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "PBM: step 4-9 — issuing PBMs (bundleSize=%u, target=0x%02X, timeout=0x%02X)",
        (unsigned)length, PBM_I2C_TARGET, PBM_TIMEOUT);

    _lastStatus = stagePbmsConfig(config, sizeof(config));
    if (_lastStatus != PatchLoadStatus::Success) {
        return _lastStatus;
    }

    // DATA is already staged and checked, so PBMs goes out with no input.
    TPS25751TaskResult pbmsResult = _pd.executeCommand(
        CMD_PBMS,
        nullptr, 0,
        nullptr, 0,
        PBMS_CMD_TIMEOUT_MS);

    if (pbmsResult.status != TPS25751TaskStatus::Success) {
        _lastStatus = mapTaskStatus(pbmsResult.status, PatchLoadStatus::PbmsRejected);
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: PBMs failed with executor status %u", (unsigned)pbmsResult.status);
        return _lastStatus;
    }

    // PatchStartStatus is DATA byte 0 = result.returnCode (TRM SLVUCR8B Table
    // 4-15; SLVAFV8A step 9, page 10)
    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "PBM: PBMs complete, PatchStartStatus=0x%02X", pbmsResult.returnCode);

    switch (pbmsResult.returnCode) {
        case 0x00:
            break;  // success — proceed to burst
        case 0x04:
            _lastStatus = PatchLoadStatus::BadBundleSize;
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: PatchStartStatus=0x04 (BadBundleSize)");
            return _lastStatus;
        case 0x05:
            _lastStatus = PatchLoadStatus::BadTargetAddress;
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: PatchStartStatus=0x05 (BadTargetAddress)");
            return _lastStatus;
        case 0x06:
            _lastStatus = PatchLoadStatus::BadTimeout;
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: PatchStartStatus=0x06 (BadTimeout)");
            return _lastStatus;
        default:
            TPS_DEBUG_WARN(DEBUG_CAT_TASK,
                "PBM: unknown PatchStartStatus 0x%02X, treating as PbmsRejected",
                pbmsResult.returnCode);
            _lastStatus = PatchLoadStatus::PbmsRejected;
            return _lastStatus;
    }

    // Step 10: burst the full image to the PBM receive port (0x30)
    // Raw burst: no register number, no length byte.  Auto-incrementing pointer
    // survives I2C START/STOP, so chunking into Wire-buffer-sized transactions is safe.
    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "PBM: step 10 — bursting %u bytes to 0x%02X in %u-byte chunks",
        (unsigned)length, PBM_I2C_TARGET, (unsigned)PBM_BURST_CHUNK);

    if (!_pd.burstWrite(PBM_I2C_TARGET, image, length)) {
        _lastStatus = PatchLoadStatus::BurstFailed;
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: step 10 — burstWrite failed");
        endSession();  // TRM Figure 5-1: not acknowledged, issue PBMe
        return _lastStatus;
    }

    // At least 500 us between the end of the burst and the next transaction,
    // directly after the burst so that both modes get it: TRM Figure 5-1 (page
    // 88) has "Delay at least 500us" before writing PBMc and no delay between
    // burst transactions. SLVAFV8A step 10 (page 10) and its code (pages 17
    // and 18) wait 500 us after each of its separate burst transactions. The
    // 32-byte chunks are separate transactions too, sent back to back, as in
    // hardware stage 5, which loaded an image with no delay at all. Whether to
    // space them is an open decision (CONSTRAINTS.md, Patch Burst Mode).
    delayMicroseconds(PBM_PRE_COMMIT_DELAY_US);
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 10: burst complete");

    // Step 11: clear interrupts again (IRQ mode only)
    clearInterrupts();

    // Steps 12 to 14: PBMc commits the bundle. issueCommand() waits for COMMAND
    // to clear and does not read DATA. TI's documents wait 20 ms between PBMc
    // completing and the DATA read (SLVAFV8A step 15, page 11, and the code on
    // page 18; TRM Figure 5-1, page 88). Hardware stage 5 of the EEPROM-update
    // plan, on one board, ran the earlier sequence, which read DATA at once:
    // the read returned 28 of 65 bytes, this call returned I2cError although
    // the patch had loaded, and every later read failed with error 2 until the
    // host restarted. The cause of those failures is not established. The
    // sequence below has not run on hardware.
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: steps 12 to 14: issuing PBMc");

    const TPS25751TaskStatus pbmcStatus = _pd.issueCommand(CMD_PBMC, nullptr, 0, PBMC_CMD_TIMEOUT_MS);
    if (pbmcStatus != TPS25751TaskStatus::Success) {
        _lastStatus = mapTaskStatus(pbmcStatus, PatchLoadStatus::PbmcRejected);
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: PBMc failed with executor status %u", (unsigned)pbmcStatus);
        // TRM Figure 5-1 issues PBMe after "!CMD" only. After a timeout PBMc
        // may still be running, and after an I2C failure its state is unknown.
        if (pbmcStatus == TPS25751TaskStatus::Rejected) {
            endSession();
        }
        return _lastStatus;
    }

    // Step 15: let the controller load and apply the image.
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "PBM: step 15: delay %u ms for patch apply", (unsigned)PATCH_APPLY_DELAY_MS);
    delay(PATCH_APPLY_DELAY_MS);

    // Step 16: DATA[0] must read 0.
    _lastStatus = readPatchResult();
    if (_lastStatus != PatchLoadStatus::Success) {
        return _lastStatus;
    }

    // Step 17: MODE must read APP.
    _lastStatus = waitForAppMode();
    if (_lastStatus != PatchLoadStatus::Success) {
        return _lastStatus;
    }

    _lastStatus = PatchLoadStatus::Success;
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: loadPatchBundle SUCCESS");
    return _lastStatus;
}

// ---------------------------------------------------------------------------
// toString / debugPrint
// ---------------------------------------------------------------------------

const char* TPS25751PatchLoader::toString(PatchLoadStatus status)
{
    switch (status) {
        case PatchLoadStatus::Success:           return "Success";
        case PatchLoadStatus::NotInPatchMode:    return "NotInPatchMode";
        case PatchLoadStatus::PbmsRejected:      return "PbmsRejected";
        case PatchLoadStatus::BadBundleSize:     return "BadBundleSize";
        case PatchLoadStatus::BadTargetAddress:  return "BadTargetAddress";
        case PatchLoadStatus::BadTimeout:        return "BadTimeout";
        case PatchLoadStatus::BurstFailed:       return "BurstFailed";
        case PatchLoadStatus::PbmcRejected:      return "PbmcRejected";
        case PatchLoadStatus::PatchApplyFailed:  return "PatchApplyFailed";
        case PatchLoadStatus::NotInAppMode:      return "NotInAppMode";
        case PatchLoadStatus::CommandTimeout:    return "CommandTimeout";
        case PatchLoadStatus::I2cError:          return "I2cError";
        case PatchLoadStatus::PbmsConfigMismatch: return "PbmsConfigMismatch";
        default:                                 return "Unknown";
    }
}

void TPS25751PatchLoader::debugPrint(Stream& s) const
{
    s.println(F("========================================"));
    s.println(F("TPS25751PatchLoader"));
    s.print(F("  lastStatus : "));
    s.println(toString(_lastStatus));
    s.println(F("========================================"));
}
