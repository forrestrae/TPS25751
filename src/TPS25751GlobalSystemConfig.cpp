#include <Stream.h>

#include "TPS25751GlobalSystemConfig.h"

// --- Decode ---------------------------------------------------------------------

RcpThreshold TPS25751GlobalSystemConfig::rcpThreshold() const
{
    return static_cast<RcpThreshold>(extractBits(kRcpThresholdBit, 2));
}

Pp3Config TPS25751GlobalSystemConfig::pp3Config() const
{
    return static_cast<Pp3Config>(rawPp3Config());
}

uint8_t TPS25751GlobalSystemConfig::rawPp3Config() const
{
    return extractBits(kPp3ConfigBit, 3);
}

IlimOvershoot TPS25751GlobalSystemConfig::ilimOvershoot() const
{
    return static_cast<IlimOvershoot>(extractBits(kIlimOvershootBit, 2));
}

Pp1Config TPS25751GlobalSystemConfig::pp1Config() const
{
    return static_cast<Pp1Config>(rawPp1Config());
}

uint8_t TPS25751GlobalSystemConfig::rawPp1Config() const
{
    return extractBits(kPp1ConfigBit, 3);
}

bool TPS25751GlobalSystemConfig::ppCable1SwitchEnabled() const
{
    return extractBits(kPpCable1Bit, 1);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751GlobalSystemConfig::setRcpThreshold(RcpThreshold value)
{
    setBits(kRcpThresholdBit, 2, static_cast<uint32_t>(value));
}

void TPS25751GlobalSystemConfig::setPp3Config(Pp3Config value)
{
    setBits(kPp3ConfigBit, 3, static_cast<uint32_t>(value));
}

void TPS25751GlobalSystemConfig::setIlimOvershoot(IlimOvershoot value)
{
    setBits(kIlimOvershootBit, 2, static_cast<uint32_t>(value));
}

void TPS25751GlobalSystemConfig::setPp1Config(Pp1Config value)
{
    setBits(kPp1ConfigBit, 3, static_cast<uint32_t>(value));
}

void TPS25751GlobalSystemConfig::setPpCable1SwitchEnabled(bool enabled)
{
    setBits(kPpCable1Bit, 1, enabled ? 1u : 0u);
}

// --- String conversion ------------------------------------------------------------

const char *toString(RcpThreshold val)
{
    switch (val) {
        case RcpThreshold::mV6: return "6 mV (nominal)";
        case RcpThreshold::mV8: return "8 mV (nominal)";
        case RcpThreshold::mV10: return "10 mV (nominal)";
        case RcpThreshold::mV12: return "12 mV (nominal)";
        default: return "Undefined";
    }
}

const char *toString(Pp3Config val)
{
    switch (val) {
        case Pp3Config::Disabled: return "PP3 not used and disabled";
        case Pp3Config::Source: return "PP3 is a Source (output)";
        case Pp3Config::Sink: return "PP3 is a Sink (input)";
        case Pp3Config::SinkWaitSrdy: return "PP3 is sink but waits for 'SRDY'";
        case Pp3Config::Bidirectional: return "PP3 is bi-directional";
        case Pp3Config::BidirectionalWaitSrdy: return "PP3 is bi-directional but waits for 'SRDY'";
        default: return "Undefined";
    }
}

const char *toString(IlimOvershoot val)
{
    switch (val) {
        case IlimOvershoot::None: return "No additional overshoot margin";
        case IlimOvershoot::mA100: return "Overshoot margin of at least 100 mA";
        case IlimOvershoot::mA200: return "Overshoot margin of at least 200 mA";
        case IlimOvershoot::mA500: return "Overshoot margin of at least 500 mA";
        default: return "Undefined";
    }
}

const char *toString(Pp1Config val)
{
    switch (val) {
        case Pp1Config::Disabled: return "Not used (disabled)";
        case Pp1Config::Source: return "PP1 configured as source";
        default: return "Undefined";
    }
}

// --- Debug ------------------------------------------------------------------------

void TPS25751GlobalSystemConfig::debugPrint(Stream &s) const
{
    s.println(F("========================================"));
    s.print(F("GLOBAL_SYSTEM_CONFIGURATION (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::GLOBAL_SYSTEM_CONFIGURATION), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("RCP Threshold: "));
    s.print(toString(rcpThreshold()));
    s.print(F(" (raw "));
    s.print(static_cast<uint8_t>(rcpThreshold()));
    s.println(F(")"));

    s.print(F("PP3 Config: "));
    s.print(toString(pp3Config()));
    s.print(F(" (raw "));
    s.print(rawPp3Config());
    s.println(F(")"));

    s.print(F("ILIM Over Shoot (PP_5V): "));
    s.print(toString(ilimOvershoot()));
    s.print(F(" (raw "));
    s.print(static_cast<uint8_t>(ilimOvershoot()));
    s.println(F(")"));

    s.print(F("PP1 Config: "));
    s.print(toString(pp1Config()));
    s.print(F(" (raw "));
    s.print(rawPp1Config());
    s.println(F(")"));

    s.print(F("PP Cable1 Switch: "));
    s.println(ppCable1SwitchEnabled() ? F("Enabled") : F("Disabled"));

    s.println(F("========================================"));
}

// --- Validation -------------------------------------------------------------------

bool TPS25751GlobalSystemConfig::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::GLOBAL_SYSTEM_CONFIGURATION.size;
    if (!isValidSize(expectedSize)) return false;

    if (rawPp3Config() > kMaxPp3Code)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "GLOBAL_SYSTEM_CONFIGURATION: undefined PP3 Config code %u", rawPp3Config());
        return false;
    }
    if (rawPp1Config() > kMaxPp1Code)
    {
        TPS_DEBUG_WARN(DEBUG_CAT_VALIDATION, "GLOBAL_SYSTEM_CONFIGURATION: undefined PP1 Config code %u", rawPp1Config());
        return false;
    }

    // Reserved bits are deliberately not checked. A TPS25751 in APP mode read
    // 05 09 14 00 1C 03 00... on 2026-11-18: the firmware sets bit 2 (7:1),
    // bit 11 (13:11), bit 20 (21:19), and bits 34, 35, 36, 40, 41 (115:24).

    return true;
}
