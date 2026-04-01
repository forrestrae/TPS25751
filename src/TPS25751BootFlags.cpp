#include "TPS25751BootFlags.h"

uint8_t TPS25751BootFlags::revisionId() const {
    return extractBits(32, 8);
}

PatchConfigSource TPS25751BootFlags::patchConfigSource() const {
    return extractEnum<PatchConfigSource>(29, 3, static_cast<uint8_t>(PatchConfigSource::Reserved7), PatchConfigSource::NoneLoaded);
}

bool TPS25751BootFlags::systemTSD() const {
    return extractBits(19, 1);
}

bool TPS25751BootFlags::region1CrcFail() const {
    return extractBits(13, 1);
}

bool TPS25751BootFlags::region0CrcFail() const {
    return extractBits(12, 1);
}

bool TPS25751BootFlags::patchDownloadError() const {
    return extractBits(10, 1);
}

bool TPS25751BootFlags::region1EepromError() const {
    return extractBits(9, 1);
}

bool TPS25751BootFlags::region0EepromError() const {
    return extractBits(8, 1);
}

bool TPS25751BootFlags::region1Invalid() const {
    return extractBits(7, 1);
}

bool TPS25751BootFlags::region0Invalid() const {
    return extractBits(6, 1);
}

bool TPS25751BootFlags::region1Attempted() const {
    return extractBits(5, 1);
}

bool TPS25751BootFlags::region0Attempted() const {
    return extractBits(4, 1);
}

bool TPS25751BootFlags::i2cEepromPresent() const {
    return extractBits(3, 1);
}

bool TPS25751BootFlags::deadBatteryFlag() const {
    return extractBits(2, 1);
}

bool TPS25751BootFlags::patchHeaderError() const {
    return extractBits(0, 1);
}

void TPS25751BootFlags::debugPrint(Stream &s) const {
    s.println(F("=== Boot Flags Register (0x2D) ==="));
    s.print(F("  Revision ID: ")); s.println(revisionId());
    s.print(F("  Patch Config Source: ")); s.println(toString(patchConfigSource()));
    s.print(F("  System TSD: ")); s.println(systemTSD());
    s.print(F("  Region 1 CRC Fail: ")); s.println(region1CrcFail());
    s.print(F("  Region 0 CRC Fail: ")); s.println(region0CrcFail());
    s.print(F("  Patch Download Error: ")); s.println(patchDownloadError());
    s.print(F("  Region 1 EEPROM Error: ")); s.println(region1EepromError());
    s.print(F("  Region 0 EEPROM Error: ")); s.println(region0EepromError());
    s.print(F("  Region 1 Invalid: ")); s.println(region1Invalid());
    s.print(F("  Region 0 Invalid: ")); s.println(region0Invalid());
    s.print(F("  Region 1 Attempted: ")); s.println(region1Attempted());
    s.print(F("  Region 0 Attempted: ")); s.println(region0Attempted());
    s.print(F("  I2C EEPROM Present: ")); s.println(i2cEepromPresent());
    s.print(F("  Dead Battery Flag: ")); s.println(deadBatteryFlag());
    s.print(F("  Patch Header Error: ")); s.println(patchHeaderError());
    s.println();
}

const char* toString(PatchConfigSource src) {
    switch (src) {
        case PatchConfigSource::NoneLoaded:   return "None Loaded";
        case PatchConfigSource::Reserved1:     return "Reserved (1)";
        case PatchConfigSource::Reserved2:     return "Reserved (2)";
        case PatchConfigSource::Reserved3:     return "Reserved (3)";
        case PatchConfigSource::Reserved4:     return "Reserved (4)";
        case PatchConfigSource::EEPROMLoaded:  return "EEPROM Loaded";
        case PatchConfigSource::I2CLoaded:     return "I2C Loaded";
        case PatchConfigSource::Reserved7:     return "Reserved (7)";
        default:                               return "Unknown";
    }
}

bool TPS25751BootFlags::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;
    
    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::BOOT_STATUS.size;
    if (!isValidSize(expectedSize)) return false;
    
    // Validate logical consistency of boot flags
    PatchConfigSource patchSrc = patchConfigSource();
    
    // If EEPROM loaded, I2C EEPROM should be present
    if (patchSrc == PatchConfigSource::EEPROMLoaded && !i2cEepromPresent()) {
        return false;
    }
    
    // If I2C loaded, I2C EEPROM should be present
    if (patchSrc == PatchConfigSource::I2CLoaded && !i2cEepromPresent()) {
        return false;
    }
    
    // If regions attempted but failed CRC, should have error flags
    if (region0Attempted() && region0CrcFail() && !region0Invalid()) {
        // CRC fail should typically mark region as invalid
        // This could be a warning rather than hard failure
    }
    
    if (region1Attempted() && region1CrcFail() && !region1Invalid()) {
        // CRC fail should typically mark region as invalid
        // This could be a warning rather than hard failure  
    }
    
    // Check that patch source enum is within valid range
    uint8_t patchSrcRaw = static_cast<uint8_t>(patchSrc);
    if (patchSrcRaw > 7) {  // PatchConfigSource uses 3 bits, max value 7
        return false;
    }
    
    return true;
}
