#include "TPS25751Pdo.h"

#include "TPS25751Register.h"

// Field positions below are from USB PD R3.3 V1.0, Tables 6.6 to 6.15 (pages 131 to
// 138), served by the usb-pd-docs MCP server.

const char *toString(PDOType type)
{
    switch (type) {
        case PDOType::FixedSupply: return "Fixed Supply";
        case PDOType::Battery: return "Battery";
        case PDOType::VariableSupply: return "Variable Supply";
        case PDOType::Augmented: return "Augmented";
        default: return "Unknown";
    }
}

const char *toString(PdoKind kind)
{
    switch (kind) {
        case PdoKind::Fixed: return "Fixed Supply";
        case PdoKind::Battery: return "Battery";
        case PdoKind::Variable: return "Variable Supply";
        case PdoKind::SprPps: return "SPR PPS APDO";
        case PdoKind::EprAvs: return "EPR AVS APDO (unexpected on an SPR port, not decoded)";
        case PdoKind::SprAvs: return "SPR AVS APDO";
        case PdoKind::InvalidApdo: return "Invalid APDO (APDO type 11b)";
        default: return "Unknown";
    }
}

const char *toString(PdoPeakCurrent peak)
{
    switch (peak) {
        case PdoPeakCurrent::IoC: return "IoC (default)";
        case PdoPeakCurrent::Overload1: return "Overload capabilities 1";
        case PdoPeakCurrent::Overload2: return "Overload capabilities 2";
        case PdoPeakCurrent::Overload3: return "Overload capabilities 3";
        default: return "Unknown";
    }
}

const char *toString(FastRoleSwapCurrent current)
{
    switch (current) {
        case FastRoleSwapCurrent::NotSupported: return "Not supported";
        case FastRoleSwapCurrent::DefaultUsb: return "Default USB Port";
        case FastRoleSwapCurrent::Current1_5A: return "1.5 A at 5 V";
        case FastRoleSwapCurrent::Current3_0A: return "3.0 A at 5 V";
        default: return "Unknown";
    }
}

// --- Type -------------------------------------------------------------------------

PDOType TPS25751Pdo::type() const
{
    return static_cast<PDOType>(bits(30, 2));
}

uint8_t TPS25751Pdo::apdoType() const
{
    if (type() != PDOType::Augmented) return 0;
    return static_cast<uint8_t>(bits(28, 2));
}

PdoKind TPS25751Pdo::kind() const
{
    switch (type()) {
        case PDOType::FixedSupply: return PdoKind::Fixed;
        case PDOType::Battery: return PdoKind::Battery;
        case PDOType::VariableSupply: return PdoKind::Variable;
        case PDOType::Augmented:
        default:
            break;
    }
    switch (apdoType()) {
        case kApdoSubtypeSprPps: return PdoKind::SprPps;
        case kApdoSubtypeEprAvs: return PdoKind::EprAvs;
        case kApdoSubtypeSprAvs: return PdoKind::SprAvs;
        default: return PdoKind::InvalidApdo;
    }
}

// --- Fixed ------------------------------------------------------------------------

uint16_t TPS25751Pdo::rawFixedVoltage() const
{
    return kind() == PdoKind::Fixed ? static_cast<uint16_t>(bits(10, 10)) : 0;
}

uint16_t TPS25751Pdo::fixedVoltage_mV() const
{
    return static_cast<uint16_t>(rawFixedVoltage() * 50u);
}

uint16_t TPS25751Pdo::rawFixedCurrent() const
{
    return kind() == PdoKind::Fixed ? static_cast<uint16_t>(bits(0, 10)) : 0;
}

uint16_t TPS25751Pdo::fixedCurrent_mA() const
{
    return static_cast<uint16_t>(rawFixedCurrent() * 10u);
}

bool TPS25751Pdo::dualRolePower() const
{
    return isFixedPdo1() && bits(29, 1);
}

bool TPS25751Pdo::usbSuspendSupported() const
{
    return isFixedPdo1() && role_ == PdoRole::Source && bits(28, 1);
}

bool TPS25751Pdo::higherCapability() const
{
    return isFixedPdo1() && role_ == PdoRole::Sink && bits(28, 1);
}

bool TPS25751Pdo::unconstrainedPower() const
{
    return isFixedPdo1() && bits(27, 1);
}

bool TPS25751Pdo::usbCommunicationsCapable() const
{
    return isFixedPdo1() && bits(26, 1);
}

bool TPS25751Pdo::dualRoleData() const
{
    return isFixedPdo1() && bits(25, 1);
}

bool TPS25751Pdo::unchunkedExtendedMessages() const
{
    return isFixedPdo1() && role_ == PdoRole::Source && bits(24, 1);
}

bool TPS25751Pdo::eprCapable() const
{
    return isFixedPdo1() && role_ == PdoRole::Source && bits(23, 1);
}

uint8_t TPS25751Pdo::rawFastRoleSwapCurrent() const
{
    if (!isFixedPdo1() || role_ != PdoRole::Sink) return 0;
    return static_cast<uint8_t>(bits(23, 2));
}

FastRoleSwapCurrent TPS25751Pdo::fastRoleSwapCurrent() const
{
    return static_cast<FastRoleSwapCurrent>(rawFastRoleSwapCurrent());
}

uint8_t TPS25751Pdo::rawPeakCurrent() const
{
    // Table 6.8 (PDO 1) and Table 6.10 (above 5 V): a Source sends Peak Current in
    // bits 21:20 of every Fixed PDO. A Sink leaves them reserved (Tables 6.9, 6.10).
    if (kind() != PdoKind::Fixed || role_ != PdoRole::Source) return 0;
    return static_cast<uint8_t>(bits(20, 2));
}

PdoPeakCurrent TPS25751Pdo::peakCurrent() const
{
    return static_cast<PdoPeakCurrent>(rawPeakCurrent());
}

// --- Battery ----------------------------------------------------------------------

uint16_t TPS25751Pdo::rawBatteryMaxVoltage() const
{
    return kind() == PdoKind::Battery ? static_cast<uint16_t>(bits(20, 10)) : 0;
}

uint16_t TPS25751Pdo::batteryMaxVoltage_mV() const
{
    return static_cast<uint16_t>(rawBatteryMaxVoltage() * 50u);
}

uint16_t TPS25751Pdo::rawBatteryMinVoltage() const
{
    return kind() == PdoKind::Battery ? static_cast<uint16_t>(bits(10, 10)) : 0;
}

uint16_t TPS25751Pdo::batteryMinVoltage_mV() const
{
    return static_cast<uint16_t>(rawBatteryMinVoltage() * 50u);
}

uint16_t TPS25751Pdo::rawBatteryPower() const
{
    return kind() == PdoKind::Battery ? static_cast<uint16_t>(bits(0, 10)) : 0;
}

uint32_t TPS25751Pdo::batteryPower_mW() const
{
    return static_cast<uint32_t>(rawBatteryPower()) * 250u;
}

// --- Variable ---------------------------------------------------------------------

uint16_t TPS25751Pdo::rawVariableMaxVoltage() const
{
    return kind() == PdoKind::Variable ? static_cast<uint16_t>(bits(20, 10)) : 0;
}

uint16_t TPS25751Pdo::variableMaxVoltage_mV() const
{
    return static_cast<uint16_t>(rawVariableMaxVoltage() * 50u);
}

uint16_t TPS25751Pdo::rawVariableMinVoltage() const
{
    return kind() == PdoKind::Variable ? static_cast<uint16_t>(bits(10, 10)) : 0;
}

uint16_t TPS25751Pdo::variableMinVoltage_mV() const
{
    return static_cast<uint16_t>(rawVariableMinVoltage() * 50u);
}

uint16_t TPS25751Pdo::rawVariableCurrent() const
{
    return kind() == PdoKind::Variable ? static_cast<uint16_t>(bits(0, 10)) : 0;
}

uint16_t TPS25751Pdo::variableCurrent_mA() const
{
    return static_cast<uint16_t>(rawVariableCurrent() * 10u);
}

// --- SPR PPS ----------------------------------------------------------------------

uint8_t TPS25751Pdo::rawPpsMaxVoltage() const
{
    return kind() == PdoKind::SprPps ? static_cast<uint8_t>(bits(17, 8)) : 0;
}

uint16_t TPS25751Pdo::ppsMaxVoltage_mV() const
{
    return static_cast<uint16_t>(rawPpsMaxVoltage() * 100u);
}

uint8_t TPS25751Pdo::rawPpsMinVoltage() const
{
    return kind() == PdoKind::SprPps ? static_cast<uint8_t>(bits(8, 8)) : 0;
}

uint16_t TPS25751Pdo::ppsMinVoltage_mV() const
{
    return static_cast<uint16_t>(rawPpsMinVoltage() * 100u);
}

uint8_t TPS25751Pdo::rawPpsCurrent() const
{
    return kind() == PdoKind::SprPps ? static_cast<uint8_t>(bits(0, 7)) : 0;
}

uint16_t TPS25751Pdo::ppsCurrent_mA() const
{
    return static_cast<uint16_t>(rawPpsCurrent() * 50u);
}

bool TPS25751Pdo::ppsPowerLimited() const
{
    return kind() == PdoKind::SprPps && role_ == PdoRole::Source && bits(27, 1);
}

// --- SPR AVS Source ---------------------------------------------------------------

namespace {
bool isAvsSource(const TPS25751Pdo &pdo)
{
    return pdo.kind() == PdoKind::SprAvs && pdo.role() == PdoRole::Source;
}
}  // namespace

uint8_t TPS25751Pdo::rawAvsPeakCurrent() const
{
    return isAvsSource(*this) ? static_cast<uint8_t>(bits(26, 2)) : 0;
}

PdoPeakCurrent TPS25751Pdo::avsPeakCurrent() const
{
    return static_cast<PdoPeakCurrent>(rawAvsPeakCurrent());
}

uint16_t TPS25751Pdo::rawAvsCurrent15V() const
{
    return isAvsSource(*this) ? static_cast<uint16_t>(bits(10, 10)) : 0;
}

uint16_t TPS25751Pdo::avsCurrent15V_mA() const
{
    return static_cast<uint16_t>(rawAvsCurrent15V() * 10u);
}

uint16_t TPS25751Pdo::rawAvsCurrent20V() const
{
    return isAvsSource(*this) ? static_cast<uint16_t>(bits(0, 10)) : 0;
}

uint16_t TPS25751Pdo::avsCurrent20V_mA() const
{
    return static_cast<uint16_t>(rawAvsCurrent20V() * 10u);
}

// --- Receiver validity ------------------------------------------------------------

bool TPS25751Pdo::isValidForReceiver() const
{
    switch (kind()) {
        case PdoKind::Fixed:
            return rawFixedVoltage() != 0 && rawFixedCurrent() <= kMaxCurrent10mA;

        case PdoKind::Battery:
            return rawBatteryMaxVoltage() != 0 && rawBatteryMinVoltage() != 0 &&
                   rawBatteryPower() <= kMaxBatteryPower250mW;

        case PdoKind::Variable:
            return rawVariableMaxVoltage() != 0 && rawVariableMinVoltage() != 0 &&
                   rawVariableCurrent() <= kMaxCurrent10mA;

        case PdoKind::SprPps: {
            if (rawPpsCurrent() > kMaxPpsCurrent50mA) return false;
            const uint8_t maxV = rawPpsMaxVoltage();
            const uint8_t minV = rawPpsMinVoltage();
            if (role_ == PdoRole::Source) {
                // Table 6.13: Maximum Voltage 110, 160, or 210; Minimum Voltage 33
                // (deprecated) or 50, in 100 mV units.
                const bool maxOk = maxV == 110 || maxV == 160 || maxV == 210;
                const bool minOk = minV == 33 || minV == 50;
                return maxOk && minOk;
            }
            // Table 6.14: both voltages 50 to 210 (5 to 21 V).
            return maxV >= 50 && maxV <= 210 && minV >= 50 && minV <= 210;
        }

        case PdoKind::SprAvs:
            // The deprecated Sink form is raw only; the Source form's currents are
            // 0 to 500 (Table 6.15).
            if (role_ == PdoRole::Sink) return true;
            return rawAvsCurrent15V() <= kMaxCurrent10mA && rawAvsCurrent20V() <= kMaxCurrent10mA;

        case PdoKind::EprAvs:
            return true;

        case PdoKind::InvalidApdo:
        default:
            return false;
    }
}

// --- Debug ------------------------------------------------------------------------

namespace {

void line(Stream &s, const char *indent, const __FlashStringHelper *label)
{
    if (indent) s.print(indent);
    s.print(F("  "));
    s.print(label);
}

void printPeak(Stream &s, PdoPeakCurrent peak, uint8_t raw)
{
    s.print(toString(peak));
    s.print(F(" ("));
    s.print(raw);
    s.println(F(")"));
}

}  // namespace

void TPS25751Pdo::debugPrint(Stream &s, const char *indent) const
{
    if (indent) s.print(indent);
    s.print(F("Type: "));
    s.println(toString(kind()));

    switch (kind()) {
        case PdoKind::Fixed:
            line(s, indent, F("Voltage: ")); s.print(fixedVoltage_mV()); s.println(F(" mV"));
            // Table 6.10 names a Sink's Fixed PDO above 5 V "Operational Current";
            // Tables 6.8 to 6.10 otherwise call the field Maximum Current.
            line(s, indent, (role_ == PdoRole::Sink && !isFirst()) ? F("Operational Current: ") : F("Max Current: "));
            s.print(fixedCurrent_mA()); s.println(F(" mA"));
            if (role_ == PdoRole::Source) {
                line(s, indent, F("Peak Current: ")); printPeak(s, peakCurrent(), rawPeakCurrent());
            }
            if (isFirst()) {
                line(s, indent, F("Dual-Role Power: ")); s.println(TPS25751Register::yesNo(dualRolePower()));
                if (role_ == PdoRole::Source) {
                    line(s, indent, F("USB Suspend Supported: ")); s.println(TPS25751Register::yesNo(usbSuspendSupported()));
                } else {
                    line(s, indent, F("Higher Capability: ")); s.println(TPS25751Register::yesNo(higherCapability()));
                }
                line(s, indent, F("Unconstrained Power: ")); s.println(TPS25751Register::yesNo(unconstrainedPower()));
                line(s, indent, F("USB Communications Capable: ")); s.println(TPS25751Register::yesNo(usbCommunicationsCapable()));
                line(s, indent, F("Dual-Role Data: ")); s.println(TPS25751Register::yesNo(dualRoleData()));
                if (role_ == PdoRole::Source) {
                    line(s, indent, F("Unchunked Extended Messages: ")); s.println(TPS25751Register::yesNo(unchunkedExtendedMessages()));
                    line(s, indent, F("EPR Capable: ")); s.println(TPS25751Register::yesNo(eprCapable()));
                } else {
                    line(s, indent, F("Fast Role Swap Current: ")); s.print(toString(fastRoleSwapCurrent()));
                    s.print(F(" (")); s.print(rawFastRoleSwapCurrent()); s.println(F(")"));
                }
            }
            break;

        case PdoKind::Battery:
            line(s, indent, F("Max Voltage: ")); s.print(batteryMaxVoltage_mV()); s.println(F(" mV"));
            line(s, indent, F("Min Voltage: ")); s.print(batteryMinVoltage_mV()); s.println(F(" mV"));
            line(s, indent, F("Max Power: "));
            s.print(static_cast<unsigned long>(batteryPower_mW())); s.println(F(" mW"));
            break;

        case PdoKind::Variable:
            line(s, indent, F("Max Voltage: ")); s.print(variableMaxVoltage_mV()); s.println(F(" mV"));
            line(s, indent, F("Min Voltage: ")); s.print(variableMinVoltage_mV()); s.println(F(" mV"));
            line(s, indent, F("Max Current: "));
            s.print(variableCurrent_mA()); s.println(F(" mA"));
            break;

        case PdoKind::SprPps:
            line(s, indent, F("Max Voltage: ")); s.print(ppsMaxVoltage_mV()); s.println(F(" mV"));
            line(s, indent, F("Min Voltage: ")); s.print(ppsMinVoltage_mV()); s.println(F(" mV"));
            line(s, indent, F("Max Current: ")); s.print(ppsCurrent_mA()); s.println(F(" mA"));
            if (role_ == PdoRole::Source) {
                line(s, indent, F("Power Limited: ")); s.println(TPS25751Register::yesNo(ppsPowerLimited()));
            }
            break;

        case PdoKind::SprAvs:
            if (role_ == PdoRole::Sink) {
                line(s, indent, F("Deprecated SPR AVS Sink APDO, not decoded: 0x"));
                TPS25751Register::printHex(s, word_, 8);
                s.println();
                break;
            }
            line(s, indent, F("Max Current 9 to 15 V: ")); s.print(avsCurrent15V_mA()); s.println(F(" mA"));
            line(s, indent, F("Max Current 15 to 20 V: ")); s.print(avsCurrent20V_mA()); s.println(F(" mA"));
            line(s, indent, F("Peak Current: ")); printPeak(s, avsPeakCurrent(), rawAvsPeakCurrent());
            break;

        case PdoKind::EprAvs:
        case PdoKind::InvalidApdo:
        default:
            line(s, indent, F("Raw: 0x"));
            TPS25751Register::printHex(s, word_, 8);
            s.println();
            break;
    }
}

// --- Encoders ---------------------------------------------------------------------

uint32_t TPS25751Pdo::encodeFixed(uint16_t voltage_mV, uint16_t current_mA)
{
    // Bits 31:30 = 00 (Fixed Supply).
    return ((static_cast<uint32_t>(voltage_mV) / 50u) & 0x3FFu) << 10 |
           ((static_cast<uint32_t>(current_mA) / 10u) & 0x3FFu);
}

uint32_t TPS25751Pdo::encodeBattery(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint32_t power_mW)
{
    return (static_cast<uint32_t>(PDOType::Battery) << 30) |
           ((static_cast<uint32_t>(maxVoltage_mV) / 50u) & 0x3FFu) << 20 |
           ((static_cast<uint32_t>(minVoltage_mV) / 50u) & 0x3FFu) << 10 |
           ((power_mW / 250u) & 0x3FFu);
}

uint32_t TPS25751Pdo::encodeVariable(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t current_mA)
{
    return (static_cast<uint32_t>(PDOType::VariableSupply) << 30) |
           ((static_cast<uint32_t>(maxVoltage_mV) / 50u) & 0x3FFu) << 20 |
           ((static_cast<uint32_t>(minVoltage_mV) / 50u) & 0x3FFu) << 10 |
           ((static_cast<uint32_t>(current_mA) / 10u) & 0x3FFu);
}

uint32_t TPS25751Pdo::encodeSprPps(uint16_t maxVoltage_mV, uint16_t minVoltage_mV, uint16_t current_mA,
                                   bool powerLimited)
{
    // Bits 31:30 = 11 (Augmented), bits 29:28 = 00 (SPR PPS).
    return (static_cast<uint32_t>(PDOType::Augmented) << 30) |
           (powerLimited ? 1UL : 0UL) << 27 |
           ((static_cast<uint32_t>(maxVoltage_mV) / 100u) & 0xFFu) << 17 |
           ((static_cast<uint32_t>(minVoltage_mV) / 100u) & 0xFFu) << 8 |
           ((static_cast<uint32_t>(current_mA) / 50u) & 0x7Fu);
}

uint32_t TPS25751Pdo::encodeSprAvsSource(uint16_t current15V_mA, uint16_t current20V_mA, PdoPeakCurrent peak)
{
    // Bits 31:30 = 11 (Augmented), bits 29:28 = 10 (SPR AVS).
    return (static_cast<uint32_t>(PDOType::Augmented) << 30) |
           (static_cast<uint32_t>(kApdoSubtypeSprAvs) << 28) |
           (static_cast<uint32_t>(peak) & 0x3u) << 26 |
           ((static_cast<uint32_t>(current15V_mA) / 10u) & 0x3FFu) << 10 |
           ((static_cast<uint32_t>(current20V_mA) / 10u) & 0x3FFu);
}

// --- List validation --------------------------------------------------------------

namespace {

// Order rank of Section 6.4.1.1: Fixed, Battery, Variable, SPR AVS, PPS.
int orderRank(PdoKind kind)
{
    switch (kind) {
        case PdoKind::Fixed: return 0;
        case PdoKind::Battery: return 1;
        case PdoKind::Variable: return 2;
        case PdoKind::SprAvs: return 3;
        case PdoKind::SprPps: return 4;
        default: return -1;
    }
}

// Maximum Current of the Fixed PDO at the given voltage code, or -1 if none.
int fixedCurrentAt(const TPS25751Pdo *pdos, uint8_t count, uint16_t voltageCode)
{
    for (uint8_t i = 0; i < count; i++) {
        if (pdos[i].kind() == PdoKind::Fixed && pdos[i].rawFixedVoltage() == voltageCode) {
            return pdos[i].rawFixedCurrent();
        }
    }
    return -1;
}

}  // namespace

bool validatePdoList(const TPS25751Pdo *pdos, uint8_t count, PdoListRules rules)
{
    if (count == 0) return true;
    if (pdos == nullptr) return false;

    for (uint8_t i = 0; i < count; i++) {
        if (!pdos[i].isValidForReceiver()) return false;
    }

    // PDO 1 is the vSafe5V Fixed PDO: exactly 100 x 50 mV (Tables 6.8, 6.9).
    if (pdos[0].kind() != PdoKind::Fixed || pdos[0].rawFixedVoltage() != TPS25751Pdo::kVSafe5VCode) {
        return false;
    }

    const bool transmit = rules == PdoListRules::TransmitSource || rules == PdoListRules::TransmitSink;
    if (!transmit) return true;

    int lastRank = 0;
    for (uint8_t i = 0; i < count; i++) {
        const PdoKind kind = pdos[i].kind();
        const int rank = orderRank(kind);
        if (rank < 0) return false;          // EPR AVS or invalid APDO
        if (rank < lastRank) return false;   // out of order
        lastRank = rank;

        if (rules == PdoListRules::TransmitSink && kind == PdoKind::SprAvs) {
            return false;  // a Sink Shall Not send the SPR AVS APDO (page 137)
        }

        if (rules == PdoListRules::TransmitSource && kind == PdoKind::Variable) {
            // Table 6.12: a Source's Minimum Voltage Shall Not be less than 80% of
            // its Maximum Voltage. Compare in integers: 5 * min >= 4 * max.
            const uint32_t minV = pdos[i].rawVariableMinVoltage();
            const uint32_t maxV = pdos[i].rawVariableMaxVoltage();
            if (5u * minV < 4u * maxV) return false;
        }

        if (rules == PdoListRules::TransmitSource && kind == PdoKind::SprAvs) {
            // Table 6.15: each current Shall equal the Maximum Current of the 15 V
            // or 20 V Fixed PDO; the 20 V current Shall be 0 when the maximum
            // voltage supported is 15 V (no 20 V Fixed PDO).
            const int fixed15 = fixedCurrentAt(pdos, count, 300);  // 15 V
            const int fixed20 = fixedCurrentAt(pdos, count, 400);  // 20 V
            if (fixed15 >= 0 && pdos[i].rawAvsCurrent15V() != fixed15) return false;
            if (fixed20 >= 0) {
                if (pdos[i].rawAvsCurrent20V() != fixed20) return false;
            } else if (pdos[i].rawAvsCurrent20V() != 0) {
                return false;
            }
        }
    }

    return true;
}
