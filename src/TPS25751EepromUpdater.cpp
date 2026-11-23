#include "TPS25751EepromUpdater.h"
#include "TPS25751BootFlags.h"
#include "TPS25751Mode.h"
#include "TPS25751Debug.h"
#include <stdio.h>
#include <string.h>

// Includes only the executor interface, the task types, and register classes, so
// this file compiles in the native test build (no Wire, no TPS25751).

// ---------------------------------------------------------------------------
// Construction and layout
// ---------------------------------------------------------------------------

TPS25751EepromUpdater::TPS25751EepromUpdater(TPS25751CommandExecutor& exec,
                                             uint32_t eepromSizeBytes)
    : _exec(exec), _eepromSize(eepromSizeBytes)
{
}

uint32_t TPS25751EepromUpdater::regionCapacity(EepromRegion r) const
{
    const uint32_t start = bundleStart(r);
    uint32_t end = (r == EepromRegion::Low) ? bundleStart(EepromRegion::High) : _eepromSize;
    if (end > _eepromSize) {
        end = _eepromSize;
    }
    return (end > start) ? (end - start) : 0;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::mapTaskStatus(TPS25751TaskStatus ts)
{
    switch (ts) {
        case TPS25751TaskStatus::Rejected: return EepromUpdateStatus::CommandRejected;
        case TPS25751TaskStatus::Timeout:  return EepromUpdateStatus::CommandTimeout;
        default:                           return EepromUpdateStatus::I2cError;
    }
}

void TPS25751EepromUpdater::putLe32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFF);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

uint32_t TPS25751EepromUpdater::getLe32(const uint8_t* in)
{
    return static_cast<uint32_t>(in[0])
         | (static_cast<uint32_t>(in[1]) << 8)
         | (static_cast<uint32_t>(in[2]) << 16)
         | (static_cast<uint32_t>(in[3]) << 24);
}

EepromUpdateStatus TPS25751EepromUpdater::mapPointerWriteFailure(EepromUpdateStatus s,
                                                                 EepromUpdateStatus stepFailure)
{
    switch (s) {
        case EepromUpdateStatus::Success:
        case EepromUpdateStatus::CommandRejected:
        case EepromUpdateStatus::CommandTimeout:
        case EepromUpdateStatus::I2cError:
        case EepromUpdateStatus::PointerNeighborsChanged:
            return s;
        default:
            return stepFailure;
    }
}

void TPS25751EepromUpdater::reportProgress(uint32_t done, uint32_t total)
{
    if (_progress != nullptr) {
        _progress(done, total, _progressCtx);
    }
}

void TPS25751EepromUpdater::setProgressCallback(EepromProgressCallback callback, void* ctx)
{
    _progress = callback;
    _progressCtx = ctx;
}

// ---------------------------------------------------------------------------
// Task primitives
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::readFlash(uint32_t addr, uint8_t out[kFlashReadBytes])
{
    uint8_t payload[4];
    putLe32(payload, addr);

    // FLrd returns the 16 memory bytes from DATA byte 0; there is no return-code
    // byte (TRM Table 4-19), so result.returnCode is the first memory byte and is
    // not interpreted. The TRM does not state the address byte order or the data
    // order for FLrd; hardware stage 1 verified both on 2026-11-22.
    uint8_t data[kFlashReadBytes];
    const TPS25751TaskResult result = _exec.executeCommand(
        CMD_FLRD, payload, sizeof(payload), data, sizeof(data), FLRD_CMD_TIMEOUT_MS);

    if (result.status != TPS25751TaskStatus::Success) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLrd at 0x%08lX failed, executor status %u",
                        static_cast<unsigned long>(addr), static_cast<unsigned>(result.status));
        return mapTaskStatus(result.status);
    }

    memcpy(out, data, sizeof(data));
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "EEPROM: FLrd at 0x%08lX", static_cast<unsigned long>(addr));
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::verifyBundle(uint32_t addr)
{
    uint8_t payload[4];
    putLe32(payload, addr);

    const TPS25751TaskResult result = _exec.executeCommand(
        CMD_FLVY, payload, sizeof(payload), nullptr, 0, FLVY_CMD_TIMEOUT_MS);

    if (result.status != TPS25751TaskStatus::Success) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLvy at 0x%08lX failed, executor status %u",
                        static_cast<unsigned long>(addr), static_cast<unsigned>(result.status));
        return mapTaskStatus(result.status);
    }

    // FLvy reports its result in DATA byte 0: 0x00 valid, 0x01 not valid
    // (TRM Table 4-22). Any other code is undocumented and treated as not valid.
    if (result.returnCode != kVerifyValid) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "EEPROM: FLvy at 0x%08lX returned 0x%02X (not valid)",
                       static_cast<unsigned long>(addr), result.returnCode);
        return EepromUpdateStatus::VerifyFailed;
    }

    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "EEPROM: FLvy at 0x%08lX valid", static_cast<unsigned long>(addr));
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::setWriteAddress(uint32_t addr)
{
    uint8_t payload[4];
    putLe32(payload, addr);  // Table 4-20: "treated as 32-bit little-endian value"

    const TPS25751TaskResult result = _exec.executeCommand(
        CMD_FLAD, payload, sizeof(payload), nullptr, 0, FLAD_CMD_TIMEOUT_MS);

    if (result.status != TPS25751TaskStatus::Success) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLad to 0x%08lX failed, executor status %u",
                        static_cast<unsigned long>(addr), static_cast<unsigned>(result.status));
        return mapTaskStatus(result.status);
    }

    // The whole return byte must be 0. Table 4-1 puts TaskResult in bits 3:0 and
    // lets a task use bits 7:4 while TaskResult is 0x0, so this can report a
    // false failure and never a false success.
    if (result.returnCode != kTaskSuccess) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLad to 0x%08lX returned 0x%02X",
                        static_cast<unsigned long>(addr), result.returnCode);
        return EepromUpdateStatus::SetAddressFailed;
    }

    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "EEPROM: FLad to 0x%08lX", static_cast<unsigned long>(addr));
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::writeData(const uint8_t* data, size_t len)
{
    // No task for a rejected argument: an FLwd with no payload would write
    // whatever DATA still holds.
    if (data == nullptr || len == 0 || len > kFlashWriteChunk) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLwd refused, bad argument (len=%u)",
                        static_cast<unsigned>(len));
        return EepromUpdateStatus::BadImageLength;
    }

    const TPS25751TaskResult result = _exec.executeCommand(
        CMD_FLWD, data, len, nullptr, 0, FLWD_CMD_TIMEOUT_MS);

    if (result.status != TPS25751TaskStatus::Success) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLwd of %u bytes failed, executor status %u",
                        static_cast<unsigned>(len), static_cast<unsigned>(result.status));
        return mapTaskStatus(result.status);
    }

    // Table 4-21 gives FLwd two codes of its own: 0x00 written, 0xFF busy. Any
    // other value has no documented meaning and is treated as a failure.
    if (result.returnCode == kWriteBusy) {
        TPS_DEBUG_WARN(DEBUG_CAT_TASK, "EEPROM: FLwd reported flash busy (0xFF)");
        return EepromUpdateStatus::FlashBusy;
    }
    if (result.returnCode != kWriteDone) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: FLwd returned 0x%02X", result.returnCode);
        return EepromUpdateStatus::WriteFailed;
    }
    return EepromUpdateStatus::Success;
}

// ---------------------------------------------------------------------------
// Region pointers and selection
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::readRegionPointer(EepromRegion r, uint32_t& value)
{
    uint8_t bytes[kFlashReadBytes];
    const EepromUpdateStatus s = readFlash(regionPointerAddress(r), bytes);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    value = getLe32(bytes);
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::writeWordChecked(uint32_t addr, uint32_t value)
{
    // 1. Keep the 16 bytes at the address; bytes 4 to 15 must survive the write.
    uint8_t before[kFlashReadBytes];
    EepromUpdateStatus s = readFlash(addr, before);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // 2. Point the write address at the word.
    s = setWriteAddress(addr);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // 3. Write the four bytes, least significant first. Busy is not retried here.
    uint8_t word[4];
    putLe32(word, value);
    s = writeData(word, sizeof(word));
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // 4. Read back. The neighbors are checked first: a write that reached past
    //    the word matters more than a word that did not take.
    uint8_t after[kFlashReadBytes];
    s = readFlash(addr, after);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    if (memcmp(before + sizeof(word), after + sizeof(word), kFlashReadBytes - sizeof(word)) != 0) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                        "EEPROM: bytes 4 to 15 after 0x%08lX changed during a 4-byte write",
                        static_cast<unsigned long>(addr));
        return EepromUpdateStatus::PointerNeighborsChanged;
    }
    if (getLe32(after) != value) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: word at 0x%08lX reads 0x%08lX, wrote 0x%08lX",
                        static_cast<unsigned long>(addr),
                        static_cast<unsigned long>(getLe32(after)),
                        static_cast<unsigned long>(value));
        return EepromUpdateStatus::CompareMismatch;
    }
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::writeRegionPointer(EepromRegion r, uint32_t value)
{
    return writeWordChecked(regionPointerAddress(r), value);
}

EepromUpdateStatus TPS25751EepromUpdater::selectTargetRegion(EepromRegion& target)
{
    uint32_t lowPtr = 0;
    uint32_t highPtr = 0;

    EepromUpdateStatus s = readRegionPointer(EepromRegion::Low, lowPtr);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    s = readRegionPointer(EepromRegion::High, highPtr);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    const bool lowValid = (lowPtr == bundleStart(EepromRegion::Low));
    const bool highValid = (highPtr == bundleStart(EepromRegion::High));

    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "EEPROM: pointers low=0x%08lX (%s) high=0x%08lX (%s)",
                   static_cast<unsigned long>(lowPtr), lowValid ? "valid" : "not valid",
                   static_cast<unsigned long>(highPtr), highValid ? "valid" : "not valid");

    // Pointers first: write the region whose pointer is not the only valid one.
    if (lowValid && !highValid) {
        target = EepromRegion::High;
        return EepromUpdateStatus::Success;
    }
    if (!lowValid && highValid) {
        target = EepromRegion::Low;
        return EepromUpdateStatus::Success;
    }
    if (!lowValid && !highValid) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: no valid region pointer");
        return EepromUpdateStatus::NoBootableRegion;
    }

    // Both valid: SLVAFL1 Figure 2-2 decides from BOOT_FLAGS (0x2D).
    TPS25751BootFlags flags;
    if (!_exec.readBootFlags(flags)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: failed to read BOOT_FLAGS");
        return EepromUpdateStatus::I2cError;
    }
    target = (flags.region0Invalid() || flags.region0EepromError())
                 ? EepromRegion::Low
                 : EepromRegion::High;
    return EepromUpdateStatus::Success;
}

// ---------------------------------------------------------------------------
// Update flows (SLVAFL1 Figure 2-3)
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::preflight(EepromRegion target, const uint8_t* image,
                                                    size_t length)
{
    // 1. SLVAFL1 describes the EEPROM update with the controller in APP mode.
    TPS25751Mode mode;
    if (!_exec.readMode(mode)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: failed to read MODE");
        return EepromUpdateStatus::I2cError;
    }
    if (!mode.isApplicationMode()) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: MODE is not APP; update refused");
        return EepromUpdateStatus::NotInAppMode;
    }

    // 2. The controller found an EEPROM on I2Cc at boot.
    TPS25751BootFlags flags;
    if (!_exec.readBootFlags(flags)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: failed to read BOOT_FLAGS");
        return EepromUpdateStatus::I2cError;
    }
    if (!flags.i2cEepromPresent()) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: BOOT_FLAGS reports no I2C EEPROM; update refused");
        return EepromUpdateStatus::NoEepromPresent;
    }

    // 3. The image is a bundle and fits the target region.
    EepromUpdateStatus s = checkImage(image, length, regionCapacity(target));
    if (s != EepromUpdateStatus::Success) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: image rejected (%s)", toString(s));
        return s;
    }

    // 4. The region being kept must be bootable before the target pointer is
    //    erased, or the erase would leave no valid pointer at all.
    const EepromRegion other = otherRegion(target);
    uint32_t otherPointer = 0;
    s = readRegionPointer(other, otherPointer);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    if (otherPointer != bundleStart(other)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                        "EEPROM: %s pointer reads 0x%08lX, not its bundle start; update of %s refused",
                        toString(other), static_cast<unsigned long>(otherPointer), toString(target));
        return EepromUpdateStatus::NoBootableRegion;
    }
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::writeChunk(uint32_t addr, const uint8_t* data, size_t len)
{
    EepromUpdateStatus s = writeData(data, len);

    // Busy: wait, set the address again, and send the same chunk. The fresh FLad
    // makes the retry independent of whether the busy attempt moved the address.
    for (uint8_t retry = 0; s == EepromUpdateStatus::FlashBusy && retry < FLWD_BUSY_RETRIES; ++retry) {
        _exec.delayMs(FLWD_BUSY_RETRY_DELAY_MS);
        const EepromUpdateStatus a = setWriteAddress(addr);
        if (a != EepromUpdateStatus::Success) {
            return a;
        }
        s = writeData(data, len);
    }
    return s;
}

EepromUpdateStatus TPS25751EepromUpdater::writeBundle(EepromRegion target, const uint8_t* image,
                                                      size_t length)
{
    const uint32_t base = bundleStart(target);
    const uint32_t total = static_cast<uint32_t>(length);

    // Step 1 is complete and no task of step 2 has been issued yet.
    reportProgress(0, total);

    EepromUpdateStatus s = setWriteAddress(base);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // The write address advances after each successful FLwd (Table 4-21), so the
    // chunks need no FLad between them. Both bundle starts are multiples of 32,
    // so no chunk crosses a 32-byte page. The last chunk goes out at its true
    // length.
    size_t offset = 0;
    while (offset < length) {
        const size_t remaining = length - offset;
        const size_t n = (remaining < kFlashWriteChunk) ? remaining : kFlashWriteChunk;
        s = writeChunk(base + static_cast<uint32_t>(offset), image + offset, n);
        if (s != EepromUpdateStatus::Success) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: bundle write stopped at image offset 0x%04lX (%s)",
                            static_cast<unsigned long>(offset), toString(s));
            return s;
        }
        offset += n;
        reportProgress(static_cast<uint32_t>(offset), total);
    }
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::updateRegion(EepromRegion target, const uint8_t* image,
                                                       size_t length, EepromUpdateOptions opt)
{
    const EepromRegion other = otherRegion(target);
    const uint32_t start = bundleStart(target);
    _lastTarget = target;

    // Every failure below returns at once: nothing is written after it.

    _lastStep = EepromUpdateStep::Preflight;
    EepromUpdateStatus s = preflight(target, image, length);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Step 1: erase the target pointer, so that the controller cannot load the
    // region while it is being written.
    _lastStep = EepromUpdateStep::ErasePointer;
    s = mapPointerWriteFailure(writeRegionPointer(target, 0), EepromUpdateStatus::PointerEraseFailed);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Step 2: write the bundle.
    _lastStep = EepromUpdateStep::WriteBundle;
    s = writeBundle(target, image, length);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Step 3: FLvy, the compare, and only then the pointer. SLVAFL1's sample
    // code ignores the executor result for FLvy; this does not.
    _lastStep = EepromUpdateStep::VerifyBundle;
    s = verifyBundle(start);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    if (opt.compareAfterWrite) {
        _lastStep = EepromUpdateStep::CompareBundle;
        s = compareImage(target, image, length, nullptr, /*report=*/true);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
    }

    _lastStep = EepromUpdateStep::RestorePointer;
    s = mapPointerWriteFailure(writeRegionPointer(target, start), EepromUpdateStatus::PointerRestoreFailed);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Step 4: erase the old pointer, so that the controller boots the new region.
    if (opt.invalidateOldRegion) {
        _lastStep = EepromUpdateStep::EraseOldPointer;
        s = mapPointerWriteFailure(writeRegionPointer(other, 0), EepromUpdateStatus::OldPointerEraseFailed);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
    }

    _lastStep = EepromUpdateStep::Done;
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "EEPROM: %s region updated, %lu bytes",
                   toString(target), static_cast<unsigned long>(length));
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::updateInactiveRegion(const uint8_t* image, size_t length)
{
    _lastStep = EepromUpdateStep::Preflight;
    EepromRegion target = EepromRegion::Low;
    const EepromUpdateStatus s = selectTargetRegion(target);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    return updateRegion(target, image, length);
}

EepromUpdateStatus TPS25751EepromUpdater::updateBothRegions(const uint8_t* image, size_t length)
{
    // The image must fit both regions before either is written, or the first
    // pass could complete with an image the second region cannot hold.
    _lastStep = EepromUpdateStep::Preflight;
    const uint32_t lowCapacity = regionCapacity(EepromRegion::Low);
    const uint32_t highCapacity = regionCapacity(EepromRegion::High);
    EepromUpdateStatus s = checkImage(image, length,
                                      (lowCapacity < highCapacity) ? lowCapacity : highCapacity);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    s = updateInactiveRegion(image, length);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Second pass: the region the first pass kept. Its pointer was erased by
    // step 4 of the first pass, and the first target now boots, so pre-flight
    // check 4 holds. Step 4 is skipped, which leaves both pointers valid.
    EepromUpdateOptions second;
    second.invalidateOldRegion = false;
    return updateRegion(otherRegion(_lastTarget), image, length, second);
}

// ---------------------------------------------------------------------------
// Read-back
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::compareRegion(EepromRegion r, const uint8_t* image,
                                                        size_t length, uint32_t* firstMismatch)
{
    return compareImage(r, image, length, firstMismatch, /*report=*/false);
}

EepromUpdateStatus TPS25751EepromUpdater::compareImage(EepromRegion r, const uint8_t* image,
                                                       size_t length, uint32_t* firstMismatch,
                                                       bool report)
{
    if (image == nullptr || length == 0 || length > regionCapacity(r)) {
        return EepromUpdateStatus::BadImageLength;
    }

    const uint32_t base = bundleStart(r);
    const uint32_t total = static_cast<uint32_t>(length);
    uint8_t bytes[kFlashReadBytes];

    if (report) {
        reportProgress(0, total);
    }

    for (size_t offset = 0; offset < length; offset += kFlashReadBytes) {
        const EepromUpdateStatus s = readFlash(base + static_cast<uint32_t>(offset), bytes);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
        const size_t remaining = length - offset;
        const size_t n = (remaining < kFlashReadBytes) ? remaining : kFlashReadBytes;
        for (size_t i = 0; i < n; ++i) {
            if (bytes[i] != image[offset + i]) {
                TPS_DEBUG_WARN(DEBUG_CAT_TASK,
                    "EEPROM: compare mismatch at image offset 0x%04lX (EEPROM 0x%02X, image 0x%02X)",
                    static_cast<unsigned long>(offset + i), bytes[i], image[offset + i]);
                if (firstMismatch != nullptr) {
                    *firstMismatch = static_cast<uint32_t>(offset + i);
                }
                return EepromUpdateStatus::CompareMismatch;
            }
        }
        if (report) {
            reportProgress(static_cast<uint32_t>(offset + n), total);
        }
    }
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::dump(uint32_t addr, size_t length, Stream& out)
{
    // "xxxxxxxx:" (9 characters) plus " xx" per byte is at most 57 characters,
    // plus the terminating NUL.
    char line[9 + kFlashReadBytes * 3 + 1];
    uint8_t bytes[kFlashReadBytes];

    for (size_t offset = 0; offset < length; offset += kFlashReadBytes) {
        // Not bounded against the EEPROM size and may wrap; the caller owns the
        // range (see the dump() Doxygen).
        const uint32_t lineAddr = addr + static_cast<uint32_t>(offset);
        const EepromUpdateStatus s = readFlash(lineAddr, bytes);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
        const size_t remaining = length - offset;
        const size_t n = (remaining < kFlashReadBytes) ? remaining : kFlashReadBytes;

        int pos = snprintf(line, sizeof(line), "%08lx:", static_cast<unsigned long>(lineAddr));
        for (size_t i = 0; i < n; ++i) {
            pos += snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos), " %02x", bytes[i]);
        }
        out.println(line);
    }
    return EepromUpdateStatus::Success;
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::checkImage(const uint8_t* image, size_t length,
                                                     uint32_t regionCapacity)
{
    if (image == nullptr || length < kMinImageLength || length > regionCapacity) {
        return EepromUpdateStatus::BadImageLength;
    }
    if (getLe32(image) != kBundleHeaderId) {
        return EepromUpdateStatus::BadImageHeader;
    }
    return EepromUpdateStatus::Success;
}

uint32_t TPS25751EepromUpdater::imageVersion(const uint8_t* image, size_t length)
{
    if (image == nullptr || length < kMinImageLength) {
        return 0;
    }
    return getLe32(image + kImageVersionOffset);
}

const char* TPS25751EepromUpdater::toString(EepromUpdateStatus s)
{
    switch (s) {
        case EepromUpdateStatus::Success:                 return "Success";
        case EepromUpdateStatus::NotInAppMode:            return "NotInAppMode";
        case EepromUpdateStatus::NoEepromPresent:         return "NoEepromPresent";
        case EepromUpdateStatus::BadImageHeader:          return "BadImageHeader";
        case EepromUpdateStatus::BadImageLength:          return "BadImageLength";
        case EepromUpdateStatus::NoBootableRegion:        return "NoBootableRegion";
        case EepromUpdateStatus::EepromNotBlank:          return "EepromNotBlank";
        case EepromUpdateStatus::PointerEraseFailed:      return "PointerEraseFailed";
        case EepromUpdateStatus::SetAddressFailed:        return "SetAddressFailed";
        case EepromUpdateStatus::WriteFailed:             return "WriteFailed";
        case EepromUpdateStatus::FlashBusy:               return "FlashBusy";
        case EepromUpdateStatus::VerifyFailed:            return "VerifyFailed";
        case EepromUpdateStatus::CompareMismatch:         return "CompareMismatch";
        case EepromUpdateStatus::PointerRestoreFailed:    return "PointerRestoreFailed";
        case EepromUpdateStatus::OldPointerEraseFailed:   return "OldPointerEraseFailed";
        case EepromUpdateStatus::PointerNeighborsChanged: return "PointerNeighborsChanged";
        case EepromUpdateStatus::ResetTimeout:            return "ResetTimeout";
        case EepromUpdateStatus::CommandRejected:         return "CommandRejected";
        case EepromUpdateStatus::CommandTimeout:          return "CommandTimeout";
        case EepromUpdateStatus::I2cError:                return "I2cError";
        default:                                          return "Unknown";
    }
}

const char* TPS25751EepromUpdater::toString(EepromRegion r)
{
    return (r == EepromRegion::Low) ? "Low" : "High";
}

const char* TPS25751EepromUpdater::toString(EepromUpdateStep step)
{
    switch (step) {
        case EepromUpdateStep::None:            return "None";
        case EepromUpdateStep::Preflight:       return "Preflight";
        case EepromUpdateStep::ErasePointer:    return "ErasePointer";
        case EepromUpdateStep::WriteBundle:     return "WriteBundle";
        case EepromUpdateStep::VerifyBundle:    return "VerifyBundle";
        case EepromUpdateStep::CompareBundle:   return "CompareBundle";
        case EepromUpdateStep::RestorePointer:  return "RestorePointer";
        case EepromUpdateStep::EraseOldPointer: return "EraseOldPointer";
        case EepromUpdateStep::Done:            return "Done";
        default:                                return "Unknown";
    }
}
