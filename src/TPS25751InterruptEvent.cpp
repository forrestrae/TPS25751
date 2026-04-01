#include <Stream.h>
#include <string>

#include "TPS25751InterruptEvent.h"

// High-priority system interrupts (bits 80-82)
bool TPS25751InterruptEvent::i2cControllerNacked() const
{
    return extractBits(82, 1);
}

bool TPS25751InterruptEvent::readyForPatch() const
{
    return extractBits(81, 1);
}

bool TPS25751InterruptEvent::patchLoaded() const
{
    return extractBits(80, 1);
}

// Memory buffer interrupts (bits 65-66)
bool TPS25751InterruptEvent::mbrdBufferReady() const
{
    return extractBits(66, 1);
}

bool TPS25751InterruptEvent::txMemoryBufferEmpty() const
{
    return extractBits(65, 1);
}

// Liquid detection (bit 60)
bool TPS25751InterruptEvent::liquidDetection() const
{
    return extractBits(60, 1);
}

// External DC/DC control (bits 56-57)
bool TPS25751InterruptEvent::extDcdcSourceSafeState() const
{
    return extractBits(57, 1);
}

bool TPS25751InterruptEvent::extDcdcSinkSafeState() const
{
    return extractBits(56, 1);
}

// Error conditions (bits 32-39, 46)
bool TPS25751InterruptEvent::unableToSourceError() const
{
    return extractBits(46, 1);
}

bool TPS25751InterruptEvent::messageDataError() const
{
    return extractBits(39, 1);
}

bool TPS25751InterruptEvent::protocolError() const
{
    return extractBits(38, 1);
}

bool TPS25751InterruptEvent::missingGetCapabilitiesError() const
{
    return extractBits(36, 1);
}

bool TPS25751InterruptEvent::powerEventOccurredError() const
{
    return extractBits(35, 1);
}

bool TPS25751InterruptEvent::canProvideVoltageLaterError() const
{
    return extractBits(34, 1);
}

bool TPS25751InterruptEvent::cannotProvideVoltageError() const
{
    return extractBits(33, 1);
}

bool TPS25751InterruptEvent::deviceIncompatibleError() const
{
    return extractBits(32, 1);
}

// Plug detection (bits 3, 42, 43)
bool TPS25751InterruptEvent::plugEarlyNotification() const
{
    return extractBits(43, 1);
}

bool TPS25751InterruptEvent::sinkTransitionCompleted() const
{
    return extractBits(42, 1);
}

bool TPS25751InterruptEvent::plugInsertOrRemoval() const
{
    return extractBits(3, 1);
}

// Command completion (bit 30)
bool TPS25751InterruptEvent::cmd1Complete() const
{
    return extractBits(30, 1);
}

// Register update notifications (bits 23-27)
bool TPS25751InterruptEvent::pdStatusUpdated() const
{
    return extractBits(27, 1);
}

bool TPS25751InterruptEvent::statusUpdated() const
{
    return extractBits(26, 1);
}

bool TPS25751InterruptEvent::powerStatusUpdated() const
{
    return extractBits(24, 1);
}

bool TPS25751InterruptEvent::powerPathSwitchChanged() const
{
    return extractBits(23, 1);
}

// USB host detection (bits 20-21)
bool TPS25751InterruptEvent::usbHostNoLongerPresent() const
{
    return extractBits(21, 1);
}

bool TPS25751InterruptEvent::usbHostPresent() const
{
    return extractBits(20, 1);
}

// Power Delivery swap requests (bits 17-18)
bool TPS25751InterruptEvent::dataSwapRequested() const
{
    return extractBits(18, 1);
}

bool TPS25751InterruptEvent::powerSwapRequested() const
{
    return extractBits(17, 1);
}

// PD message reception (bits 14-15)
bool TPS25751InterruptEvent::sinkCapMessageReceived() const
{
    return extractBits(15, 1);
}

bool TPS25751InterruptEvent::sourceCapMessageReceived() const
{
    return extractBits(14, 1);
}

// Contract negotiation (bits 12-13)
bool TPS25751InterruptEvent::newContractAsProvider() const
{
    return extractBits(13, 1);
}

bool TPS25751InterruptEvent::newContractAsConsumer() const
{
    return extractBits(12, 1);
}

// Overcurrent detection (bit 9)
bool TPS25751InterruptEvent::overcurrent() const
{
    return extractBits(9, 1);
}

// Role swap completion (bits 4-5)
bool TPS25751InterruptEvent::dataSwapComplete() const
{
    return extractBits(5, 1);
}

bool TPS25751InterruptEvent::powerSwapComplete() const
{
    return extractBits(4, 1);
}

// Hard reset (bit 1)
bool TPS25751InterruptEvent::pdHardreset() const
{
    return extractBits(1, 1);
}

bool TPS25751InterruptEvent::hasAnyInterrupt() const
{
    // Check if any byte in the register is non-zero
    for (size_t i = 0; i < TPS25751Registers::Registers::INT_EVENT1.size; i++) {
        if (_raw[i] != 0) {
            return true;
        }
    }
    return false;
}

uint8_t TPS25751InterruptEvent::getActiveInterruptCount() const
{
    uint8_t count = 0;

    // Check all non-reserved interrupt flags
    if (i2cControllerNacked()) count++;
    if (readyForPatch()) count++;
    if (patchLoaded()) count++;
    if (mbrdBufferReady()) count++;
    if (txMemoryBufferEmpty()) count++;
    if (liquidDetection()) count++;
    if (extDcdcSourceSafeState()) count++;
    if (extDcdcSinkSafeState()) count++;
    if (unableToSourceError()) count++;
    if (messageDataError()) count++;
    if (protocolError()) count++;
    if (missingGetCapabilitiesError()) count++;
    if (powerEventOccurredError()) count++;
    if (canProvideVoltageLaterError()) count++;
    if (cannotProvideVoltageError()) count++;
    if (deviceIncompatibleError()) count++;
    if (plugEarlyNotification()) count++;
    if (sinkTransitionCompleted()) count++;
    if (plugInsertOrRemoval()) count++;
    if (cmd1Complete()) count++;
    if (pdStatusUpdated()) count++;
    if (statusUpdated()) count++;
    if (powerStatusUpdated()) count++;
    if (powerPathSwitchChanged()) count++;
    if (usbHostNoLongerPresent()) count++;
    if (usbHostPresent()) count++;
    if (dataSwapRequested()) count++;
    if (powerSwapRequested()) count++;
    if (sinkCapMessageReceived()) count++;
    if (sourceCapMessageReceived()) count++;
    if (newContractAsProvider()) count++;
    if (newContractAsConsumer()) count++;
    if (overcurrent()) count++;
    if (dataSwapComplete()) count++;
    if (powerSwapComplete()) count++;
    if (pdHardreset()) count++;

    return count;
}

void TPS25751InterruptEvent::debugPrint(Stream &s) const
{
    s.println(F("=== Interrupt Event Register (0x14) ==="));

    if (!hasAnyInterrupt()) {
        s.println(F("No interrupts active"));
        s.println();
        return;
    }

    s.print(F("Active Interrupts: "));
    s.println(getActiveInterruptCount());
    s.println();

    // System interrupts
    if (i2cControllerNacked()) s.println(F("  [!] I2C Controller NACKed"));
    if (readyForPatch()) s.println(F("  [+] Ready for Patch"));
    if (patchLoaded()) s.println(F("  [+] Patch Loaded"));

    // Memory buffers
    if (mbrdBufferReady()) s.println(F("  [+] MBRD Buffer Ready"));
    if (txMemoryBufferEmpty()) s.println(F("  [+] TX Memory Buffer Empty"));

    // Liquid detection
    if (liquidDetection()) s.println(F("  [!] Liquid Detection"));

    // DC/DC control
    if (extDcdcSourceSafeState()) s.println(F("  [+] Ext DCDC Source Safe State"));
    if (extDcdcSinkSafeState()) s.println(F("  [+] Ext DCDC Sink Safe State"));

    // Error conditions
    if (unableToSourceError()) s.println(F("  [!] Unable to Source Error"));
    if (messageDataError()) s.println(F("  [!] Message Data Error"));
    if (protocolError()) s.println(F("  [!] Protocol Error"));
    if (missingGetCapabilitiesError()) s.println(F("  [!] Missing Get Capabilities Error"));
    if (powerEventOccurredError()) s.println(F("  [!] Power Event Occurred Error"));
    if (canProvideVoltageLaterError()) s.println(F("  [!] Can Provide Voltage/Current Later"));
    if (cannotProvideVoltageError()) s.println(F("  [!] Cannot Provide Voltage/Current"));
    if (deviceIncompatibleError()) s.println(F("  [!] Device Incompatible Error"));

    // Plug events
    if (plugEarlyNotification()) s.println(F("  [+] Plug Early Notification"));
    if (sinkTransitionCompleted()) s.println(F("  [+] Sink Transition Completed"));
    if (plugInsertOrRemoval()) s.println(F("  [+] Plug Insert or Removal"));

    // Command completion
    if (cmd1Complete()) s.println(F("  [+] CMD1 Complete"));

    // Register updates
    if (pdStatusUpdated()) s.println(F("  [+] PD Status Updated"));
    if (statusUpdated()) s.println(F("  [+] Status Updated"));
    if (powerStatusUpdated()) s.println(F("  [+] Power Status Updated"));
    if (powerPathSwitchChanged()) s.println(F("  [+] Power Path Switch Changed"));

    // USB host
    if (usbHostNoLongerPresent()) s.println(F("  [+] USB Host No Longer Present"));
    if (usbHostPresent()) s.println(F("  [+] USB Host Present"));

    // Swap requests
    if (dataSwapRequested()) s.println(F("  [+] Data Swap Requested"));
    if (powerSwapRequested()) s.println(F("  [+] Power Swap Requested"));

    // PD messages
    if (sinkCapMessageReceived()) s.println(F("  [+] Sink Cap Message Received"));
    if (sourceCapMessageReceived()) s.println(F("  [+] Source Cap Message Received"));

    // Contracts
    if (newContractAsProvider()) s.println(F("  [+] New Contract as Provider"));
    if (newContractAsConsumer()) s.println(F("  [+] New Contract as Consumer"));

    // Overcurrent
    if (overcurrent()) s.println(F("  [!] Overcurrent"));

    // Swaps complete
    if (dataSwapComplete()) s.println(F("  [+] Data Swap Complete"));
    if (powerSwapComplete()) s.println(F("  [+] Power Swap Complete"));

    // Hard reset
    if (pdHardreset()) s.println(F("  [!] PD Hard Reset"));

    s.println();
}

bool TPS25751InterruptEvent::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    // Check register size matches expected
    constexpr size_t expectedSize = TPS25751Registers::Registers::INT_EVENT1.size;
    if (!isValidSize(expectedSize)) return false;

    // Logical consistency checks for interrupt flags

    // If both USB host present and not present are set, that's inconsistent
    if (usbHostPresent() && usbHostNoLongerPresent()) {
        return false;
    }

    // If both data swap requested and complete are set simultaneously, verify
    // (both could be true in edge cases, but let's flag for review)
    // This is more of a warning than an error, so we'll allow it

    // If power swap requested and complete are both set, that's unusual but possible
    // during rapid state transitions

    // Device incompatible should be mutually exclusive with contract negotiations
    if (deviceIncompatibleError() && (newContractAsProvider() || newContractAsConsumer())) {
        return false;
    }

    // If protocol or message data errors occur, contracts should not be successfully negotiated
    // in the same interrupt cycle
    if ((protocolError() || messageDataError()) &&
        (newContractAsProvider() || newContractAsConsumer())) {
        return false;
    }

    return true;
}