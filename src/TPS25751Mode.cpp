#include <cstring>

#include "TPS25751Mode.h"
#include "TPS25751RegisterAddress.h"

/*
 * 'APP ' indicates that the PD controller is fully functioning in the application firmware where all registers are available.
 * 'BOOT' indicates that the PD controller is booting in dead battery condition.
 * 'PTCH' indicates that the PD controller is in patch mode.
 * Any other values indicates that the PD controller is functioning in limited capacity.
 *
 * Note: In 'BOOT' and 'PTCH' only the follow register addresses are accessible, Mode, Command, Data, Int Event, Int Mask, Int Clear, and Boot Flags.
 */




const char* TPS25751Mode::modeString() const {
    constexpr size_t modeSize = TPS25751Registers::Registers::MODE.size;
    static char modeStr[modeSize + 1];
    
    if (!isValid()) {
        modeStr[0] = '\0';
        return modeStr;
    }
    
    memcpy(modeStr, _raw, modeSize);
    modeStr[modeSize] = '\0';
    return modeStr;
}

bool TPS25751Mode::isApplicationMode() const {
    if (!isValid()) return false;
    
    constexpr size_t modeSize = TPS25751Registers::Registers::MODE.size;
    constexpr char APP_MODE[] = "APP ";
    static_assert(sizeof(APP_MODE) - 1 == modeSize, "APP mode string must match register size");
    
    return memcmp(_raw, APP_MODE, modeSize) == 0;
}

bool TPS25751Mode::isBootMode() const {
    if (!isValid()) return false;
    
    constexpr size_t modeSize = TPS25751Registers::Registers::MODE.size;
    constexpr char BOOT_MODE[] = "BOOT";
    static_assert(sizeof(BOOT_MODE) - 1 == modeSize, "BOOT mode string must match register size");
    
    return memcmp(_raw, BOOT_MODE, modeSize) == 0;
}

bool TPS25751Mode::isPatchMode() const {
    if (!isValid()) return false;
    
    constexpr size_t modeSize = TPS25751Registers::Registers::MODE.size;
    constexpr char PATCH_MODE[] = "PTCH";
    static_assert(sizeof(PATCH_MODE) - 1 == modeSize, "PTCH mode string must match register size");
    
    return memcmp(_raw, PATCH_MODE, modeSize) == 0;
}

ModeType TPS25751Mode::getModeType() const {
    if (isApplicationMode()) return ModeType::Application;
    if (isBootMode()) return ModeType::Boot;
    if (isPatchMode()) return ModeType::Patch;
    return ModeType::Unknown;
}

const char* toString(ModeType mode) {
    switch (mode) {
        case ModeType::Application: return "Application";
        case ModeType::Boot: return "Boot";
        case ModeType::Patch: return "Patch";
        case ModeType::Unknown: return "Unknown";
        default: return "Invalid";
    }
}

void TPS25751Mode::debugPrint(Stream &s) const {
    s.println(F("=== Mode Register (0x03) ==="));
    s.print(F("Raw Mode String: ")); s.println(modeString());
    s.print(F("Mode Type: "));
    
    ModeType mode = getModeType();
    switch (mode) {
        case ModeType::Application:
            s.println(F("Application - PD controller is fully functioning, all registers are available."));
            break;
        case ModeType::Boot:
            s.println(F("Boot - Only the Mode, Command, Data, Int Event, Int Mask, Int Clear, and Boot Flags are available."));
            break;
        case ModeType::Patch:
            s.println(F("Patch - Only the Mode, Command, Data, Int Event, Int Mask, Int Clear, and Boot Flags are available."));
            break;
        case ModeType::Unknown:
        default:
            s.println(F("Unknown / Limited Functionality"));
            break;
    }
    s.println();
}

bool TPS25751Mode::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) {
        TPS_REPORT_VALIDATION_ERROR("Mode", "basic validity", "register invalid or no valid data");
        return false;
    }
    
    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::MODE.size;
    if (!isValidSize(expectedSize)) {
        TPS_REPORT_VALIDATION_ERROR("Mode", "size validation", "register size mismatch");
        return false;
    }
    
    // Mode register should contain ASCII characters for valid modes
    // Valid modes are: "APP ", "BOOT", "PTCH", or other ASCII strings
    for (size_t i = 0; i < expectedSize; i++) {
        // Check if characters are printable ASCII (space to tilde: 0x20-0x7E)
        if (_raw[i] < 0x20 || _raw[i] > 0x7E) {
            TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "Mode validation: non-ASCII character at byte %u: 0x%02X", static_cast<unsigned>(i), _raw[i]);
            TPS_REPORT_VALIDATION_ERROR("Mode", "ASCII validation", "non-printable character detected");
            return false;
        }
    }
    
    TPS_DEBUG(DEBUG_CAT_VALIDATION, "Mode register validation passed: '%s'", modeString());
    return true;
}


