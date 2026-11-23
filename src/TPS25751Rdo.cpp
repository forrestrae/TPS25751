#include "TPS25751Rdo.h"

#include "TPS25751Register.h"

// Field positions are from USB PD R3.3 V1.0, Tables 6.19 to 6.22 (pages 142 to 144),
// served by the usb-pd-docs MCP server.

const char *toString(RdoKind kind)
{
    switch (kind) {
        case RdoKind::FixedVariable: return "Fixed/Variable";
        case RdoKind::Battery: return "Battery";
        case RdoKind::Pps: return "PPS";
        case RdoKind::Avs: return "AVS";
        case RdoKind::Unknown:
        default: return "Unknown";
    }
}

RdoKind rdoKindFor(const TPS25751Pdo &pdo)
{
    switch (pdo.kind()) {
        case PdoKind::Fixed:
        case PdoKind::Variable: return RdoKind::FixedVariable;
        case PdoKind::Battery: return RdoKind::Battery;
        case PdoKind::SprPps: return RdoKind::Pps;
        case PdoKind::SprAvs:
        case PdoKind::EprAvs: return RdoKind::Avs;
        case PdoKind::InvalidApdo:
        default: return RdoKind::Unknown;
    }
}

RdoKind rdoKindFor(uint32_t pdoWord)
{
    return rdoKindFor(TPS25751Pdo(pdoWord, TPS25751Pdo::kUnknownPosition, PdoRole::Source));
}

// --- Common -----------------------------------------------------------------------

uint8_t TPS25751Rdo::objectPosition() const { return static_cast<uint8_t>(bits(28, 4)); }
bool TPS25751Rdo::giveback() const { return bits(27, 1); }
bool TPS25751Rdo::capabilityMismatch() const { return bits(26, 1); }
bool TPS25751Rdo::usbCommunicationsCapable() const { return bits(25, 1); }
bool TPS25751Rdo::noUsbSuspend() const { return bits(24, 1); }
bool TPS25751Rdo::unchunkedExtendedMessages() const { return bits(23, 1); }
bool TPS25751Rdo::eprCapable() const { return bits(22, 1); }

// --- FixedVariable and Battery ----------------------------------------------------

namespace {
bool hasOperatingField(RdoKind kind)
{
    return kind == RdoKind::FixedVariable || kind == RdoKind::Battery;
}

bool hasOutputVoltage(RdoKind kind)
{
    return kind == RdoKind::Pps || kind == RdoKind::Avs;
}
}  // namespace

uint16_t TPS25751Rdo::rawOperating() const
{
    return hasOperatingField(kind_) ? static_cast<uint16_t>(bits(10, 10)) : 0;
}

uint16_t TPS25751Rdo::rawMaximumOperating() const
{
    return hasOperatingField(kind_) ? static_cast<uint16_t>(bits(0, 10)) : 0;
}

uint16_t TPS25751Rdo::operatingCurrent_mA() const
{
    if (kind_ == RdoKind::FixedVariable) return static_cast<uint16_t>(rawOperating() * 10u);
    if (hasOutputVoltage(kind_)) return static_cast<uint16_t>(rawPpsAvsOperatingCurrent() * 50u);
    return 0;
}

uint16_t TPS25751Rdo::maximumOperatingCurrent_mA() const
{
    return kind_ == RdoKind::FixedVariable ? static_cast<uint16_t>(rawMaximumOperating() * 10u) : 0;
}

uint32_t TPS25751Rdo::operatingPower_mW() const
{
    return kind_ == RdoKind::Battery ? static_cast<uint32_t>(rawOperating()) * 250u : 0;
}

uint32_t TPS25751Rdo::maximumOperatingPower_mW() const
{
    return kind_ == RdoKind::Battery ? static_cast<uint32_t>(rawMaximumOperating()) * 250u : 0;
}

// --- Pps and Avs ------------------------------------------------------------------

uint16_t TPS25751Rdo::rawOutputVoltage() const
{
    return hasOutputVoltage(kind_) ? static_cast<uint16_t>(bits(9, 12)) : 0;
}

uint32_t TPS25751Rdo::outputVoltage_mV() const
{
    if (kind_ == RdoKind::Pps) return static_cast<uint32_t>(rawOutputVoltage()) * 20u;
    if (kind_ == RdoKind::Avs) return static_cast<uint32_t>(rawOutputVoltage()) * 25u;
    return 0;
}

uint8_t TPS25751Rdo::rawPpsAvsOperatingCurrent() const
{
    return hasOutputVoltage(kind_) ? static_cast<uint8_t>(bits(0, 7)) : 0;
}

bool TPS25751Rdo::deprecatedCopyMatches() const
{
    if (!hasOperatingField(kind_)) return true;
    return rawOperating() == rawMaximumOperating();
}

// --- Debug ------------------------------------------------------------------------

namespace {
void flagLine(Stream &s, const char *indent, const __FlashStringHelper *label, bool value)
{
    if (indent) s.print(indent);
    s.print(label);
    s.println(TPS25751Register::yesNo(value));
}
}  // namespace

void TPS25751Rdo::debugPrint(Stream &s, const char *indent) const
{
    if (indent) s.print(indent);
    s.print(F("RDO 0x"));
    TPS25751Register::printHex(s, word_, 8);
    s.print(F(", decoded as "));
    s.println(toString(kind_));

    if (indent) s.print(indent);
    s.print(F("Object Position: "));
    s.println(objectPosition());
    flagLine(s, indent, F("Capability Mismatch: "), capabilityMismatch());
    flagLine(s, indent, F("USB Communications Capable: "), usbCommunicationsCapable());
    flagLine(s, indent, F("No USB Suspend: "), noUsbSuspend());
    flagLine(s, indent, F("Unchunked Extended Messages: "), unchunkedExtendedMessages());
    flagLine(s, indent, F("EPR Capable: "), eprCapable());

    switch (kind_) {
        case RdoKind::FixedVariable:
            if (indent) s.print(indent);
            s.print(F("Operating Current: "));
            s.print(operatingCurrent_mA());
            s.println(F(" mA"));
            if (!deprecatedCopyMatches()) {
                if (indent) s.print(indent);
                s.print(F("Maximum Operating Current (deprecated) differs: "));
                s.print(maximumOperatingCurrent_mA());
                s.println(F(" mA"));
            }
            break;

        case RdoKind::Battery:
            if (indent) s.print(indent);
            s.print(F("Operating Power: "));
            s.print(static_cast<unsigned long>(operatingPower_mW()));
            s.println(F(" mW"));
            if (!deprecatedCopyMatches()) {
                if (indent) s.print(indent);
                s.print(F("Maximum Operating Power (deprecated) differs: "));
                s.print(static_cast<unsigned long>(maximumOperatingPower_mW()));
                s.println(F(" mW"));
            }
            break;

        case RdoKind::Pps:
        case RdoKind::Avs:
            if (indent) s.print(indent);
            s.print(F("Output Voltage: "));
            s.print(static_cast<unsigned long>(outputVoltage_mV()));
            s.println(F(" mV"));
            if (indent) s.print(indent);
            s.print(F("Operating Current: "));
            s.print(operatingCurrent_mA());
            s.println(F(" mA"));
            break;

        case RdoKind::Unknown:
        default:
            if (indent) s.print(indent);
            s.println(F("Kind unknown: the typed fields need the requested PDO's type"));
            break;
    }
}
