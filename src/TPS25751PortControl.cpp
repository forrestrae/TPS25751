#include "TPS25751PortControl.h"

ChargerDetectEnable TPS25751PortControl::chargerDetectEnable() const {
    return extractEnum<ChargerDetectEnable>(30, 2, static_cast<uint8_t>(ChargerDetectEnable::BC12AndProprietary), ChargerDetectEnable::None);
}

ChargerAdvertiseEnable TPS25751PortControl::chargerAdvertiseEnable() const {
    return extractEnum<ChargerAdvertiseEnable>(26, 3, static_cast<uint8_t>(ChargerAdvertiseEnable::Reserved7), ChargerAdvertiseEnable::SDP);
}

bool TPS25751PortControl::resistor15kPresent() const {
    return extractBits(24, 1);
}

bool TPS25751PortControl::dcdEnable() const {
    return extractBits(25, 1);
}

bool TPS25751PortControl::automaticIdRequest() const {
    return extractBits(16, 1);
}

bool TPS25751PortControl::enableCurrentMonitor() const {
    return extractBits(20, 1);
}

bool TPS25751PortControl::unconstrainedPower() const {
    return extractBits(19, 1);
}

bool TPS25751PortControl::initiateSwapToDFP() const {
    return extractBits(15, 1);
}

bool TPS25751PortControl::processSwapToDFP() const {
    return extractBits(14, 1);
}

bool TPS25751PortControl::initiateSwapToUFP() const {
    return extractBits(13, 1);
}

bool TPS25751PortControl::processSwapToUFP() const {
    return extractBits(12, 1);
}

bool TPS25751PortControl::initiateSwapToSource() const {
    return extractBits(7, 1);
}

bool TPS25751PortControl::processSwapToSource() const {
    return extractBits(6, 1);
}

bool TPS25751PortControl::initiateSwapToSink() const {
    return extractBits(5, 1);
}

bool TPS25751PortControl::processSwapToSink() const {
    return extractBits(4, 1);
}

TypeCCurrent TPS25751PortControl::typeCCurrent() const {
    return extractEnum<TypeCCurrent>(0, 2, static_cast<uint8_t>(TypeCCurrent::Reserved), TypeCCurrent::USBDefault);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751PortControl::setChargerDetectEnable(ChargerDetectEnable value)
{
    setBits(30, 2, static_cast<uint32_t>(value));
}

void TPS25751PortControl::setChargerAdvertiseEnable(ChargerAdvertiseEnable value)
{
    setBits(26, 3, static_cast<uint32_t>(value));
}

void TPS25751PortControl::setTypeCCurrent(TypeCCurrent value)
{
    setBits(0, 2, static_cast<uint32_t>(value));
}

void TPS25751PortControl::setResistor15kPresent(bool present)
{
    setBits(24, 1, present ? 1u : 0u);
}

void TPS25751PortControl::setDcdEnable(bool enable)
{
    setBits(25, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setAutomaticIdRequest(bool enable)
{
    setBits(16, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setEnableCurrentMonitor(bool enable)
{
    setBits(20, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setUnconstrainedPower(bool enable)
{
    setBits(19, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setInitiateSwapToDFP(bool enable)
{
    setBits(15, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setProcessSwapToDFP(bool enable)
{
    setBits(14, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setInitiateSwapToUFP(bool enable)
{
    setBits(13, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setProcessSwapToUFP(bool enable)
{
    setBits(12, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setInitiateSwapToSource(bool enable)
{
    setBits(7, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setProcessSwapToSource(bool enable)
{
    setBits(6, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setInitiateSwapToSink(bool enable)
{
    setBits(5, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::setProcessSwapToSink(bool enable)
{
    setBits(4, 1, enable ? 1u : 0u);
}

void TPS25751PortControl::debugPrint(Stream &s) const {
    s.println(F("=== Port Control Register (0x29) ==="));

    s.println(F("  Charger Detection:"));
    s.print(F("    Detect Enable: ")); s.println(toString(chargerDetectEnable()));
    s.print(F("    Advertise Enable: ")); s.println(toString(chargerAdvertiseEnable()));
    s.print(F("    15k Resistor Present: ")); s.println(yesNo(resistor15kPresent()));
    s.print(F("    DCD Enable: ")); s.println(dcdEnable() ? F("Enabled") : F("Disabled"));
    s.print(F("    Automatic ID Request: ")); s.println(automaticIdRequest() ? F("Enabled") : F("Disabled"));

    s.println(F("  Power Configuration:"));
    s.print(F("    Current Monitor: ")); s.println(enableCurrentMonitor() ? F("Enabled") : F("Disabled"));
    s.print(F("    Unconstrained Power: ")); s.println(yesNo(unconstrainedPower()));

    s.println(F("  Data Role Swap:"));
    s.print(F("    Initiate to DFP: ")); s.println(yesNo(initiateSwapToDFP()));
    s.print(F("    Process to DFP: ")); s.println(processSwapToDFP() ? F("Accept") : F("Reject"));
    s.print(F("    Initiate to UFP: ")); s.println(yesNo(initiateSwapToUFP()));
    s.print(F("    Process to UFP: ")); s.println(processSwapToUFP() ? F("Accept") : F("Reject"));

    s.println(F("  Power Role Swap:"));
    s.print(F("    Initiate to Source: ")); s.println(yesNo(initiateSwapToSource()));
    s.print(F("    Process to Source: ")); s.println(processSwapToSource() ? F("Accept") : F("Reject"));
    s.print(F("    Initiate to Sink: ")); s.println(yesNo(initiateSwapToSink()));
    s.print(F("    Process to Sink: ")); s.println(processSwapToSink() ? F("Accept") : F("Reject"));

    s.print(F("Type-C Current: ")); s.println(toString(typeCCurrent()));

    s.println();
}

const char* toString(ChargerDetectEnable val) {
    switch (val) {
        case ChargerDetectEnable::None: return "None";
        case ChargerDetectEnable::BC12Only: return "BC 1.2 only";
        case ChargerDetectEnable::ProprietaryOnly: return "Proprietary only";  // SLVUCR8B p.31
        case ChargerDetectEnable::BC12AndProprietary: return "BC 1.2 + Proprietary";
        default: return "Unknown";
    }
}

const char* toString(ChargerAdvertiseEnable val) {
    switch (val) {
        case ChargerAdvertiseEnable::SDP: return "BC 1.2 SDP only";  // SLVUCR8B p.31
        case ChargerAdvertiseEnable::CDP: return "CDP";
        case ChargerAdvertiseEnable::DCP: return "DCP";
        case ChargerAdvertiseEnable::Reserved3: return "Reserved (3)";
        case ChargerAdvertiseEnable::Reserved4: return "Reserved (4)";
        case ChargerAdvertiseEnable::DCP_Auto1: return "DCP Auto 1";
        case ChargerAdvertiseEnable::DCP_Auto2: return "DCP Auto 2";
        case ChargerAdvertiseEnable::Reserved7: return "Reserved (7)";
        default: return "Unknown";
    }
}

const char* toString(TypeCCurrent val) {
    switch (val) {
        case TypeCCurrent::USBDefault: return "USB Default";
        case TypeCCurrent::Current_1_5A: return "1.5A";
        case TypeCCurrent::Current_3_0A: return "3.0A";
        case TypeCCurrent::Reserved: return "Reserved";
        default: return "Unknown";
    }
}

bool TPS25751PortControl::isSemanticallyValid() const {
    // Basic validity checks first
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::PORT_CONTROL.size;
    if (!isValidSize(expectedSize)) return false;

    // Validate enum ranges
    ChargerDetectEnable detectEn = chargerDetectEnable();
    ChargerAdvertiseEnable advertiseEn = chargerAdvertiseEnable();
    TypeCCurrent current = typeCCurrent();

    uint8_t detectRaw = static_cast<uint8_t>(detectEn);
    uint8_t advertiseRaw = static_cast<uint8_t>(advertiseEn);
    uint8_t currentRaw = static_cast<uint8_t>(current);

    if (detectRaw > 3 || advertiseRaw > 7 || currentRaw > 3) {
        return false;
    }

    // Every Charger Detect Enable value (0h-3h) is defined in Rev. B; 2h is
    // "Detect proprietary chargers only" (SLVUCR8B p.31), so no rejection here.

    // Logical consistency checks
    // If 15k resistor is present, certain charger modes shouldn't be used
    if (resistor15kPresent()) {
        if (advertiseEn == ChargerAdvertiseEnable::DCP ||
            advertiseEn == ChargerAdvertiseEnable::DCP_Auto1 ||
            advertiseEn == ChargerAdvertiseEnable::DCP_Auto2) {
            return false;  // Documentation says this shouldn't be used for DCP modes
        }
    }

    return true;
}
