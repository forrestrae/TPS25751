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

bool TPS25751PatchLoader::confirmAppMode() const
{
    auto mode = _pd.readModeRegister(/*validate=*/false);
    if (!mode) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: step 17 — failed to read MODE register");
        return false;
    }
    if (!mode->isApplicationMode()) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: step 17 — device not in APP mode after patch (got '%s')",
            mode->modeString());
        return false;
    }
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 17 — confirmed APP mode, patch load complete");
    return true;
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

    // Steps 4-9: PBMs — start the burst session
    // Config payload (6 bytes): bundleSize[4, little-endian] + targetAddr + timeout
    uint8_t config[6];
    config[0] = static_cast<uint8_t>((length >>  0) & 0xFF);  // bundleSize LSB
    config[1] = static_cast<uint8_t>((length >>  8) & 0xFF);
    config[2] = static_cast<uint8_t>((length >> 16) & 0xFF);
    config[3] = static_cast<uint8_t>((length >> 24) & 0xFF);  // bundleSize MSB
    config[4] = PBM_I2C_TARGET;  // 0x30
    config[5] = PBM_TIMEOUT;     // 0x31

    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "PBM: step 4-9 — issuing PBMs (bundleSize=%u, target=0x%02X, timeout=0x%02X)",
        (unsigned)length, PBM_I2C_TARGET, PBM_TIMEOUT);

    TPS25751TaskResult pbmsResult = _pd.executeCommand(
        CMD_PBMS,
        config, sizeof(config),
        nullptr, 0,
        PBMS_CMD_TIMEOUT_MS);

    if (pbmsResult.status != TPS25751TaskStatus::Success) {
        _lastStatus = mapTaskStatus(pbmsResult.status, PatchLoadStatus::PbmsRejected);
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: PBMs failed with executor status %u", (unsigned)pbmsResult.status);
        return _lastStatus;
    }

    // PatchStartStatus is DATA byte 0 = result.returnCode (SLVAFV8A Table 4-2)
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
        return _lastStatus;
    }

    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 10 — burst complete");

    // Step 11: clear interrupts again (IRQ mode only)
    clearInterrupts();

    // Steps 12-14: PBMc — commit (close) the bundle session
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "PBM: step 12-14 — issuing PBMc");

    TPS25751TaskResult pbmcResult = _pd.executeCommand(
        CMD_PBMC,
        nullptr, 0,
        nullptr, 0,
        PBMC_CMD_TIMEOUT_MS);

    if (pbmcResult.status != TPS25751TaskStatus::Success) {
        _lastStatus = mapTaskStatus(pbmcResult.status, PatchLoadStatus::PbmcRejected);
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: PBMc failed with executor status %u", (unsigned)pbmcResult.status);
        return _lastStatus;
    }

    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "PBM: PBMc complete, returnCode=0x%02X", pbmcResult.returnCode);

    // Step 15: delay for the device to apply the patch
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK,
        "PBM: step 15 — delay %u ms for patch apply", PATCH_APPLY_DELAY_MS);
    delay(PATCH_APPLY_DELAY_MS);

    // Step 16: read DATA[0] and confirm PatchStartStatus is cleared to 0
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "PBM: step 16 — reading DATA to confirm PatchStartStatus=0");
    auto data = _pd.readDataRegister(/*validate=*/false);
    if (!data || !data->isValid()) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "PBM: step 16 — failed to read DATA register");
        _lastStatus = PatchLoadStatus::I2cError;
        return _lastStatus;
    }
    if (data->raw()[0] != 0x00) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
            "PBM: step 16 — PatchStartStatus not cleared (DATA[0]=0x%02X)", data->raw()[0]);
        _lastStatus = PatchLoadStatus::PatchApplyFailed;
        return _lastStatus;
    }

    // Step 17: confirm APP mode
    if (!confirmAppMode()) {
        _lastStatus = PatchLoadStatus::NotInAppMode;
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
