#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

enum class PatchConfigSource {
    NoneLoaded = 0,           // No configuration has been loaded
    Reserved1 = 1,
    Reserved2 = 2,
    Reserved3 = 3,
    Reserved4 = 4,            // Reserved (not HostLoaded)
    EEPROMLoaded = 5,         // Configuration loaded from EEPROM
    I2CLoaded = 6,            // Configuration loaded from I2C
    Reserved7 = 7
};

class TPS25751BootFlags : public TPS25751Register
{
public:
    explicit TPS25751BootFlags() : TPS25751Register(TPS25751Registers::Registers::BOOT_STATUS.size) {};
    explicit TPS25751BootFlags(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::BOOT_STATUS.size) {};
    TPS25751BootFlags(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751BootFlags(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    uint8_t revisionId() const;
    PatchConfigSource patchConfigSource() const;
    bool systemTSD() const;
    bool region1CrcFail() const;
    bool region0CrcFail() const;
    bool patchDownloadError() const;
    bool region1EepromError() const;
    bool region0EepromError() const;
    bool region1Invalid() const;
    bool region0Invalid() const;
    bool region1Attempted() const;
    bool region0Attempted() const;
    bool i2cEepromPresent() const;
    bool deadBatteryFlag() const;
    bool patchHeaderError() const;

    void debugPrint(Stream &s = Serial) const override;
    
    /**
     * @brief Validates boot flags register data semantically
     * @return true if boot flags are logically consistent
     */
    bool isSemanticallyValid() const override;
};

const char* toString(PatchConfigSource src);

