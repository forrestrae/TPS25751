#include "TPS25751SleepControl.h"

bool TPS25751SleepControl::sleepModeAllowed() const {
    return extractBits(0, 1);
}

SleepTime TPS25751SleepControl::sleepTime() const {
    return extractEnum<SleepTime>(kSleepTimeShift, kSleepTimeBits,
                                   static_cast<uint8_t>(SleepTime::Reserved3), SleepTime::Reserved0);
}

void TPS25751SleepControl::setSleepModeAllowed(bool allowed) {
    // setBits() guards !isValid(), read-modify-writes only bit 0, and masks the
    // value to the field width -- see the base-class write primitive.
    setBits(kSleepModeAllowedBit, 1, allowed ? 1u : 0u);
}

void TPS25751SleepControl::setSleepTime(SleepTime t) {
    setBits(kSleepTimeShift, kSleepTimeBits, static_cast<uint8_t>(t));
}

void TPS25751SleepControl::debugPrint(Stream &s) const {
    s.println(F("========================================"));
    s.print(F("SLEEP_CONTROL (0x"));
    s.print(static_cast<uint8_t>(TPS25751Registers::Address::SLEEP_CONTROL), HEX);
    s.print(F(") - "));
    s.print(size());
    s.println(F(" bytes"));
    s.println(F("========================================"));

    s.print(F("Sleep Mode Allowed: "));
    s.println(yesNo(sleepModeAllowed()));

    s.print(F("Sleep Time: "));
    s.print(toString(sleepTime()));
    s.print(F(" (0x"));
    s.print(static_cast<uint8_t>(sleepTime()), HEX);
    s.println(F(")"));

    s.println(F("========================================"));
}

bool TPS25751SleepControl::isSemanticallyValid() const {
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::SLEEP_CONTROL.size;
    if (!isValidSize(expectedSize)) return false;

    if (sleepModeAllowed()) {
        const SleepTime t = sleepTime();
        if (t == SleepTime::Reserved0 || t == SleepTime::Reserved3) {
            return false;  // Reserved Sleep Time is meaningless while sleep is allowed
        }
    }

    return true;
}

const char* toString(SleepTime val) {
    switch (val) {
        case SleepTime::Reserved0: return "Reserved (0h)";
        case SleepTime::Time100ms: return "100 ms";
        case SleepTime::Time1000ms: return "1000 ms";  // SLVUCR8B p.52
        case SleepTime::Reserved3: return "Reserved (3h)";
        default: return "Unknown";
    }
}
