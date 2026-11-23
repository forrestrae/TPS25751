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

EepromUpdateStatus TPS25751EepromUpdater::writeWordChecked(uint32_t addr, uint32_t value,
                                                           uint8_t* readBack)
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
    if (readBack != nullptr) {
        memcpy(readBack, after, sizeof(after));
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

EepromUpdateStatus TPS25751EepromUpdater::checkHostAndImage(const uint8_t* image, size_t length,
                                                            uint32_t capacity)
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

    // 3. The image is a bundle and fits the region or regions to be written.
    const EepromUpdateStatus s = checkImage(image, length, capacity);
    if (s != EepromUpdateStatus::Success) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: image rejected (%s)", toString(s));
    }
    return s;
}

EepromUpdateStatus TPS25751EepromUpdater::preflight(EepromRegion target, const uint8_t* image,
                                                    size_t length)
{
    // 1 to 3: MODE, BOOT_FLAGS, and the image against the target region.
    EepromUpdateStatus s = checkHostAndImage(image, length, regionCapacity(target));
    if (s != EepromUpdateStatus::Success) {
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

EepromUpdateStatus TPS25751EepromUpdater::preflightBlank(const uint8_t* image, size_t length)
{
    // 1 to 3: MODE, BOOT_FLAGS, and the image against the smaller region,
    // since both regions receive it.
    const uint32_t lowCapacity = regionCapacity(EepromRegion::Low);
    const uint32_t highCapacity = regionCapacity(EepromRegion::High);
    EepromUpdateStatus s = checkHostAndImage(image, length,
                                             (lowCapacity < highCapacity) ? lowCapacity : highCapacity);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // 4. No pointer may be valid: an EEPROM that can boot is updated with
    //    updateRegion(), never rewritten from scratch.
    uint32_t lowPointer = 0;
    uint32_t highPointer = 0;
    s = readRegionPointer(EepromRegion::Low, lowPointer);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    s = readRegionPointer(EepromRegion::High, highPointer);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    if (lowPointer == bundleStart(EepromRegion::Low) || highPointer == bundleStart(EepromRegion::High)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                        "EEPROM: pointers low=0x%08lX high=0x%08lX, one is valid; blank programming refused",
                        static_cast<unsigned long>(lowPointer), static_cast<unsigned long>(highPointer));
        return EepromUpdateStatus::EepromNotBlank;
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

EepromUpdateStatus TPS25751EepromUpdater::writeChunks(uint32_t base, const uint8_t* image,
                                                      size_t from, size_t to, uint32_t total)
{
    // The write address advances after each successful FLwd (Table 4-21), so the
    // chunks need no FLad between them. Both bundle starts are multiples of 32,
    // so no chunk crosses a 32-byte page. The last chunk goes out at its true
    // length.
    size_t offset = from;
    while (offset < to) {
        const size_t remaining = to - offset;
        const size_t n = (remaining < kFlashWriteChunk) ? remaining : kFlashWriteChunk;
        const EepromUpdateStatus s = writeChunk(base + static_cast<uint32_t>(offset), image + offset, n);
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

EepromUpdateStatus TPS25751EepromUpdater::checkEarlyChunks(EepromRegion target, const uint8_t* image,
                                                           size_t written, const uint8_t* pointerRow)
{
    // Read order: the pointer row first, the bundle start last. The last FLrd
    // before the second FLad is then inside the target region. Should an FLrd
    // move the controller's write address and the second FLad not take effect,
    // the remaining chunks would start near that last read, in the region being
    // written, and not at the pointer row in front of the other region.
    const uint32_t pointerAddr = regionPointerAddress(target);
    uint8_t now[kFlashReadBytes];
    EepromUpdateStatus s = readFlash(pointerAddr, now);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // The first chunks must read back from the bundle start. A write address
    // that was not where FLad was told to put it shows here, unless the region
    // already held the same bytes. This result is judged first.
    s = compareImage(target, image, written, nullptr, /*report=*/false);
    if (s != EepromUpdateStatus::Success) {
        if (s == EepromUpdateStatus::CompareMismatch) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                            "EEPROM: the first %lu bytes did not read back at 0x%08lX; write stopped",
                            static_cast<unsigned long>(written),
                            static_cast<unsigned long>(bundleStart(target)));
        }
        return s;
    }

    // The 16 bytes at the target pointer must be as step 1 left them. If step 1
    // left the controller's write address just past the pointer word, an FLad
    // that reported success without taking effect puts the chunks there, and
    // the check above cannot see that when the region already held the image.
    if (getLe32(now) != 0 || memcmp(now + 4, pointerRow + 4, kFlashReadBytes - 4) != 0) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                        "EEPROM: the 16 bytes at 0x%08lX changed while the first chunks were written; write stopped",
                        static_cast<unsigned long>(pointerAddr));
        return EepromUpdateStatus::PointerNeighborsChanged;
    }
    return EepromUpdateStatus::Success;
}

EepromUpdateStatus TPS25751EepromUpdater::writeBundle(EepromRegion target, const uint8_t* image,
                                                      size_t length, const uint8_t* pointerRow)
{
    const uint32_t base = bundleStart(target);
    const uint32_t total = static_cast<uint32_t>(length);

    // Step 1 is complete and no task of step 2 has been issued yet.
    reportProgress(0, total);

    // If the 12 bytes after the target pointer already are the first 12 bytes
    // of the image, an earlier attempt put them there: a bundle write that went
    // to the pointer row and was stopped by the check below. That check
    // compares against these same bytes, so it could not see the same thing
    // happen again. Nothing of step 2 is issued.
    if (memcmp(pointerRow + 4, image, kFlashReadBytes - 4) == 0) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                        "EEPROM: the bytes after the pointer at 0x%08lX hold the start of the image; "
                        "dump and repair the EEPROM before another update",
                        static_cast<unsigned long>(regionPointerAddress(target)));
        return EepromUpdateStatus::PointerNeighborsChanged;
    }

    EepromUpdateStatus s = setWriteAddress(base);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // The first two chunks, then proof that they are where they belong, before
    // the other chunks follow them on the strength of the same write address.
    const size_t early = (length < kEarlyReadBackBytes) ? length : kEarlyReadBackBytes;
    s = writeChunks(base, image, 0, early, total);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    s = checkEarlyChunks(target, image, early, pointerRow);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    if (early == length) {
        return EepromUpdateStatus::Success;
    }

    // Whether an FLrd disturbs the controller's write address is not
    // documented, so the address of the next unwritten byte is set again, as
    // the busy retry does.
    s = setWriteAddress(base + static_cast<uint32_t>(early));
    if (s != EepromUpdateStatus::Success) {
        return s;
    }
    return writeChunks(base, image, early, length, total);
}

EepromUpdateStatus TPS25751EepromUpdater::eraseTargetPointer(EepromRegion target, uint8_t* pointerRow)
{
    // Step 1: erase the target pointer, so that the controller cannot load the
    // region while it is being written. The 16 bytes its read-back saw are
    // kept: step 2 checks them again.
    _lastStep = EepromUpdateStep::ErasePointer;
    return mapPointerWriteFailure(writeWordChecked(regionPointerAddress(target), 0, pointerRow),
                                  EepromUpdateStatus::PointerEraseFailed);
}

EepromUpdateStatus TPS25751EepromUpdater::installBundle(EepromRegion target, const uint8_t* image,
                                                        size_t length, const uint8_t* pointerRow,
                                                        bool compare)
{
    const uint32_t start = bundleStart(target);

    // Step 2: write the bundle, with a read-back after the first two chunks.
    _lastStep = EepromUpdateStep::WriteBundle;
    EepromUpdateStatus s = writeBundle(target, image, length, pointerRow);
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

    if (compare) {
        _lastStep = EepromUpdateStep::CompareBundle;
        s = compareImage(target, image, length, nullptr, /*report=*/true);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
    }

    _lastStep = EepromUpdateStep::RestorePointer;
    return mapPointerWriteFailure(writeRegionPointer(target, start), EepromUpdateStatus::PointerRestoreFailed);
}

EepromUpdateStatus TPS25751EepromUpdater::updateRegion(EepromRegion target, const uint8_t* image,
                                                       size_t length, EepromUpdateOptions opt)
{
    const EepromRegion other = otherRegion(target);
    _lastTarget = target;

    // Every failure below returns at once: nothing is written after it.

    _lastStep = EepromUpdateStep::Preflight;
    EepromUpdateStatus s = preflight(target, image, length);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Step 1.
    uint8_t pointerRow[kFlashReadBytes];
    s = eraseTargetPointer(target, pointerRow);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Steps 2 and 3.
    s = installBundle(target, image, length, pointerRow, opt.compareAfterWrite);
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

EepromUpdateStatus TPS25751EepromUpdater::programBlank(const uint8_t* image, size_t length)
{
    _lastTarget = EepromRegion::Low;
    _lastStep = EepromUpdateStep::Preflight;
    EepromUpdateStatus s = preflightBlank(image, length);
    if (s != EepromUpdateStatus::Success) {
        return s;
    }

    // Low first, then High, each pass complete before the next starts. After a
    // failure in the High pass the Low region is the one that boots.
    // Every failure below returns at once: nothing is written after it.
    const EepromRegion order[] = {EepromRegion::Low, EepromRegion::High};
    for (const EepromRegion target : order) {
        _lastTarget = target;

        // Step 1, as in updateRegion().
        uint8_t pointerRow[kFlashReadBytes];
        s = eraseTargetPointer(target, pointerRow);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }

        // The AppConfigOffset word to 0: the controller looks for the region's
        // Header_ID at the region start plus this offset (SLVAFL1 Boot
        // Process). The same checked four-byte write as a pointer, with its
        // failures passed through as raised; lastStep() tells them apart.
        // Its 16-byte read-back runs into the High pointer word (Low pass) or
        // the start of the Low bundle (High pass). The check compares those
        // bytes before and after the write, so bytes the Low pass put there
        // cannot make the High pass refuse.
        _lastStep = EepromUpdateStep::ClearAppConfigOffset;
        s = writeWordChecked(appConfigOffsetAddress(target), 0);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }

        // Steps 2 and 3, as in updateRegion(), with the compare always on.
        s = installBundle(target, image, length, pointerRow, /*compare=*/true);
        if (s != EepromUpdateStatus::Success) {
            return s;
        }
    }

    _lastStep = EepromUpdateStep::Done;
    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "EEPROM: blank programming complete, %lu bytes in each region",
                   static_cast<unsigned long>(length));
    return EepromUpdateStatus::Success;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::coldReset(uint32_t waitForAppMs)
{
    _lastStep = EepromUpdateStep::Reset;

    const TPS25751TaskResult result = _exec.executeCommand(
        CMD_GAID, nullptr, 0, nullptr, 0, GAID_CMD_TIMEOUT_MS);

    // GAID never completes in the normal sense (TRM Table 4-3). Success, Timeout,
    // and I2CError all mean the reset was issued: COMMAND reads 0 after the
    // reboot, and the controller can NAK while it reboots. Only "!CMD" means the
    // controller did not take the task.
    if (result.status == TPS25751TaskStatus::Rejected) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: GAID rejected");
        return EepromUpdateStatus::CommandRejected;
    }

    const uint32_t start = _exec.nowMs();
    _exec.delayMs(GAID_SETTLE_MS);

    TPS25751Mode mode;
    for (;;) {
        // A failed read is expected while the controller reboots.
        if (_exec.readMode(mode) && mode.isApplicationMode()) {
            _lastStep = EepromUpdateStep::Done;
            TPS_DEBUG_INFO(DEBUG_CAT_TASK, "EEPROM: MODE reads APP %lu ms after GAID returned",
                           static_cast<unsigned long>(_exec.nowMs() - start));
            return EepromUpdateStatus::Success;
        }
        // Unsigned subtraction keeps the elapsed time right across a clock wrap.
        if (static_cast<uint32_t>(_exec.nowMs() - start) >= waitForAppMs) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: MODE did not read APP within %lu ms of GAID",
                            static_cast<unsigned long>(waitForAppMs));
            return EepromUpdateStatus::ResetTimeout;
        }
        _exec.delayMs(GAID_MODE_POLL_MS);
    }
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
        case EepromUpdateStep::ClearAppConfigOffset: return "ClearAppConfigOffset";
        case EepromUpdateStep::WriteBundle:     return "WriteBundle";
        case EepromUpdateStep::VerifyBundle:    return "VerifyBundle";
        case EepromUpdateStep::CompareBundle:   return "CompareBundle";
        case EepromUpdateStep::RestorePointer:  return "RestorePointer";
        case EepromUpdateStep::EraseOldPointer: return "EraseOldPointer";
        case EepromUpdateStep::Reset:           return "Reset";
        case EepromUpdateStep::Done:            return "Done";
        default:                                return "Unknown";
    }
}
