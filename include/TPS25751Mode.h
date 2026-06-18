#pragma once

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

    enum class ModeType : uint8_t {
    Application = 0,
    Boot = 1,
    Patch = 2,
    Unknown = 3
};

class TPS25751Mode : public TPS25751Register
{
public:
    explicit TPS25751Mode() : TPS25751Register(TPS25751Registers::Registers::MODE.size) {};
    explicit TPS25751Mode(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::MODE.size) {};
    TPS25751Mode(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751Mode(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};


    const char *modeString() const;
    
    ModeType getModeType() const;

    bool isApplicationMode() const;

    bool isBootMode() const;

    bool isPatchMode() const;

    void debugPrint(Stream &s = Serial) const override;
    
    /**
     * @brief Validates mode register data semantically
     * @return true if mode data is valid according to TPS25751 specification
     */
    bool isSemanticallyValid() const override;

};

// String conversion helper for ModeType
const char* toString(ModeType mode);
