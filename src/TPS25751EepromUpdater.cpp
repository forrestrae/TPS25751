#include "TPS25751EepromUpdater.h"
#include "TPS25751BootFlags.h"
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

// ---------------------------------------------------------------------------
// Task primitives
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::readFlash(uint32_t addr, uint8_t out[kFlashReadBytes])
{
    uint8_t payload[4];
    putLe32(payload, addr);

    // FLrd returns the 16 memory bytes from DATA byte 0; there is no return-code
    // byte (TRM Table 4-19), so result.returnCode is the first memory byte and is
    // not interpreted.
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

    // Both valid: SLVAFL1 Figure 2-2 decides from BOOT_STATUS.
    TPS25751BootFlags flags;
    if (!_exec.readBootFlags(flags)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "EEPROM: failed to read BOOT_STATUS");
        return EepromUpdateStatus::I2cError;
    }
    target = (flags.region0Invalid() || flags.region0EepromError())
                 ? EepromRegion::Low
                 : EepromRegion::High;
    return EepromUpdateStatus::Success;
}

// ---------------------------------------------------------------------------
// Read-back
// ---------------------------------------------------------------------------

EepromUpdateStatus TPS25751EepromUpdater::compareRegion(EepromRegion r, const uint8_t* image,
                                                        size_t length, uint32_t* firstMismatch)
{
    if (image == nullptr || length == 0 || length > regionCapacity(r)) {
        return EepromUpdateStatus::BadImageLength;
    }

    const uint32_t base = bundleStart(r);
    uint8_t bytes[kFlashReadBytes];

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
