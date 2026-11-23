#include <Stream.h>

#include "TPS25751InterruptMask.h"

// High-priority system interrupts (bits 80-82)
bool TPS25751InterruptMask::i2cControllerNacked() const
{
    return extractBits(82, 1);
}

bool TPS25751InterruptMask::readyForPatch() const
{
    return extractBits(81, 1);
}

bool TPS25751InterruptMask::patchLoaded() const
{
    return extractBits(80, 1);
}

// Memory buffer interrupts (bits 65-66)
bool TPS25751InterruptMask::mbrdBufferReady() const
{
    return extractBits(66, 1);
}

bool TPS25751InterruptMask::txMemoryBufferEmpty() const
{
    return extractBits(65, 1);
}

// Liquid detection (bit 60)
bool TPS25751InterruptMask::liquidDetection() const
{
    return extractBits(60, 1);
}

// External DC/DC control (bits 56-57)
bool TPS25751InterruptMask::extDcdcSourceSafeState() const
{
    return extractBits(57, 1);
}

bool TPS25751InterruptMask::extDcdcSinkSafeState() const
{
    return extractBits(56, 1);
}

// Error conditions (bits 32-39, 46)
bool TPS25751InterruptMask::unableToSourceError() const
{
    return extractBits(46, 1);
}

bool TPS25751InterruptMask::messageDataError() const
{
    return extractBits(39, 1);
}

bool TPS25751InterruptMask::protocolError() const
{
    return extractBits(38, 1);
}

bool TPS25751InterruptMask::missingGetCapabilitiesError() const
{
    return extractBits(36, 1);
}

bool TPS25751InterruptMask::powerEventOccurredError() const
{
    return extractBits(35, 1);
}

bool TPS25751InterruptMask::canProvideVoltageLaterError() const
{
    return extractBits(34, 1);
}

bool TPS25751InterruptMask::cannotProvideVoltageError() const
{
    return extractBits(33, 1);
}

bool TPS25751InterruptMask::deviceIncompatibleError() const
{
    return extractBits(32, 1);
}

// Plug detection (bits 3, 42-43)
bool TPS25751InterruptMask::plugEarlyNotification() const
{
    return extractBits(43, 1);
}

bool TPS25751InterruptMask::sinkTransitionCompleted() const
{
    return extractBits(42, 1);
}

bool TPS25751InterruptMask::plugInsertOrRemoval() const
{
    return extractBits(3, 1);
}

// Command completion (bit 30)
bool TPS25751InterruptMask::cmd1Complete() const
{
    return extractBits(30, 1);
}

// Manufacturer Info message received (bit 29). Source: SLVUCR8B (Rev. B, 2026-08), page 21.
bool TPS25751InterruptMask::midbReceived() const
{
    return extractBits(29, 1);
}

// Discover Modes process completed (bit 51). Source: SLVUCR8B (Rev. B, 2026-08), page 21.
bool TPS25751InterruptMask::discoverModeCompleted() const
{
    return extractBits(51, 1);
}

// Register update notifications (bits 23-27)
bool TPS25751InterruptMask::pdStatusUpdated() const
{
    return extractBits(27, 1);
}

bool TPS25751InterruptMask::statusUpdated() const
{
    return extractBits(26, 1);
}

bool TPS25751InterruptMask::powerStatusUpdated() const
{
    return extractBits(24, 1);
}

bool TPS25751InterruptMask::powerPathSwitchChanged() const
{
    return extractBits(23, 1);
}

// USB host detection (bits 20-21)
bool TPS25751InterruptMask::usbHostNoLongerPresent() const
{
    return extractBits(21, 1);
}

bool TPS25751InterruptMask::usbHostPresent() const
{
    return extractBits(20, 1);
}

// Power Delivery swap requests (bits 17-18)
bool TPS25751InterruptMask::dataSwapRequested() const
{
    return extractBits(18, 1);
}

bool TPS25751InterruptMask::powerSwapRequested() const
{
    return extractBits(17, 1);
}

// PD message reception (bits 14-15)
bool TPS25751InterruptMask::sinkCapMessageReceived() const
{
    return extractBits(15, 1);
}

bool TPS25751InterruptMask::sourceCapMessageReceived() const
{
    return extractBits(14, 1);
}

// Contract negotiation (bits 12-13)
bool TPS25751InterruptMask::newContractAsProvider() const
{
    return extractBits(13, 1);
}

bool TPS25751InterruptMask::newContractAsConsumer() const
{
    return extractBits(12, 1);
}

// Overcurrent detection (bit 9)
bool TPS25751InterruptMask::overcurrent() const
{
    return extractBits(9, 1);
}

// Role swap completion (bits 4-5)
bool TPS25751InterruptMask::dataSwapComplete() const
{
    return extractBits(5, 1);
}

bool TPS25751InterruptMask::powerSwapComplete() const
{
    return extractBits(4, 1);
}

// Hard reset (bit 1)
bool TPS25751InterruptMask::pdHardreset() const
{
    return extractBits(1, 1);
}

// --- Setters: each a single setBits() call; setBits() itself guards !isValid() ---

void TPS25751InterruptMask::setI2cControllerNacked(bool enabled) { setBits(82, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setReadyForPatch(bool enabled) { setBits(81, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPatchLoaded(bool enabled) { setBits(80, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setMbrdBufferReady(bool enabled) { setBits(66, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setTxMemoryBufferEmpty(bool enabled) { setBits(65, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setLiquidDetection(bool enabled) { setBits(60, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setExtDcdcSourceSafeState(bool enabled) { setBits(57, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setExtDcdcSinkSafeState(bool enabled) { setBits(56, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setUnableToSourceError(bool enabled) { setBits(46, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setMessageDataError(bool enabled) { setBits(39, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setProtocolError(bool enabled) { setBits(38, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setMissingGetCapabilitiesError(bool enabled) { setBits(36, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPowerEventOccurredError(bool enabled) { setBits(35, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setCanProvideVoltageLaterError(bool enabled) { setBits(34, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setCannotProvideVoltageError(bool enabled) { setBits(33, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setDeviceIncompatibleError(bool enabled) { setBits(32, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setPlugEarlyNotification(bool enabled) { setBits(43, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setSinkTransitionCompleted(bool enabled) { setBits(42, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPlugInsertOrRemoval(bool enabled) { setBits(3, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setCmd1Complete(bool enabled) { setBits(30, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setMidbReceived(bool enabled) { setBits(29, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setDiscoverModeCompleted(bool enabled) { setBits(51, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setPdStatusUpdated(bool enabled) { setBits(27, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setStatusUpdated(bool enabled) { setBits(26, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPowerStatusUpdated(bool enabled) { setBits(24, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPowerPathSwitchChanged(bool enabled) { setBits(23, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setUsbHostNoLongerPresent(bool enabled) { setBits(21, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setUsbHostPresent(bool enabled) { setBits(20, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setDataSwapRequested(bool enabled) { setBits(18, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPowerSwapRequested(bool enabled) { setBits(17, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setSinkCapMessageReceived(bool enabled) { setBits(15, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setSourceCapMessageReceived(bool enabled) { setBits(14, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setNewContractAsProvider(bool enabled) { setBits(13, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setNewContractAsConsumer(bool enabled) { setBits(12, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setOvercurrent(bool enabled) { setBits(9, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setDataSwapComplete(bool enabled) { setBits(5, 1, enabled ? 1u : 0u); }
void TPS25751InterruptMask::setPowerSwapComplete(bool enabled) { setBits(4, 1, enabled ? 1u : 0u); }

void TPS25751InterruptMask::setPdHardreset(bool enabled) { setBits(1, 1, enabled ? 1u : 0u); }

bool TPS25751InterruptMask::hasAnyEnabled() const
{
    for (size_t i = 0; i < TPS25751Registers::Registers::INT_MASK1.size; i++) {
        if (_raw[i] != 0) {
            return true;
        }
    }
    return false;
}

uint8_t TPS25751InterruptMask::getEnabledCount() const
{
    uint8_t count = 0;

    if (i2cControllerNacked()) count++;
    if (readyForPatch()) count++;
    if (patchLoaded()) count++;
    if (mbrdBufferReady()) count++;
    if (txMemoryBufferEmpty()) count++;
    if (liquidDetection()) count++;
    if (extDcdcSourceSafeState()) count++;
    if (extDcdcSinkSafeState()) count++;
    if (discoverModeCompleted()) count++;
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
    if (midbReceived()) count++;
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

void TPS25751InterruptMask::debugPrint(Stream &s) const
{
    s.println(F("=== Interrupt Mask Register (0x16) ==="));

    if (!hasAnyEnabled()) {
        s.println(F("No interrupt sources enabled (all masked)"));
        s.println();
        return;
    }

    s.print(F("Enabled Sources: "));
    s.println(getEnabledCount());
    s.println();

    if (i2cControllerNacked()) s.println(F("  [+] I2C Controller NACKed"));
    if (readyForPatch()) s.println(F("  [+] Ready for Patch"));
    if (patchLoaded()) s.println(F("  [+] Patch Loaded"));

    if (mbrdBufferReady()) s.println(F("  [+] MBRD Buffer Ready"));
    if (txMemoryBufferEmpty()) s.println(F("  [+] TX Memory Buffer Empty"));

    if (liquidDetection()) s.println(F("  [+] Liquid Detection"));

    if (extDcdcSourceSafeState()) s.println(F("  [+] Ext DCDC Source Safe State"));
    if (extDcdcSinkSafeState()) s.println(F("  [+] Ext DCDC Sink Safe State"));
    if (discoverModeCompleted()) s.println(F("  [+] Discover Mode Completed"));

    if (unableToSourceError()) s.println(F("  [+] Unable to Source Error"));
    if (messageDataError()) s.println(F("  [+] Message Data Error"));
    if (protocolError()) s.println(F("  [+] Protocol Error"));
    if (missingGetCapabilitiesError()) s.println(F("  [+] Missing Get Capabilities Error"));
    if (powerEventOccurredError()) s.println(F("  [+] Power Event Occurred Error"));
    if (canProvideVoltageLaterError()) s.println(F("  [+] Can Provide Voltage/Current Later"));
    if (cannotProvideVoltageError()) s.println(F("  [+] Cannot Provide Voltage/Current"));
    if (deviceIncompatibleError()) s.println(F("  [+] Device Incompatible Error"));

    if (plugEarlyNotification()) s.println(F("  [+] Plug Early Notification"));
    if (sinkTransitionCompleted()) s.println(F("  [+] Sink Transition Completed"));
    if (plugInsertOrRemoval()) s.println(F("  [+] Plug Insert or Removal"));

    if (cmd1Complete()) s.println(F("  [+] CMD1 Complete"));
    if (midbReceived()) s.println(F("  [+] MIDB Received"));

    if (pdStatusUpdated()) s.println(F("  [+] PD Status Updated"));
    if (statusUpdated()) s.println(F("  [+] Status Updated"));
    if (powerStatusUpdated()) s.println(F("  [+] Power Status Updated"));
    if (powerPathSwitchChanged()) s.println(F("  [+] Power Path Switch Changed"));

    if (usbHostNoLongerPresent()) s.println(F("  [+] USB Host No Longer Present"));
    if (usbHostPresent()) s.println(F("  [+] USB Host Present"));

    if (dataSwapRequested()) s.println(F("  [+] Data Swap Requested"));
    if (powerSwapRequested()) s.println(F("  [+] Power Swap Requested"));

    if (sinkCapMessageReceived()) s.println(F("  [+] Sink Cap Message Received"));
    if (sourceCapMessageReceived()) s.println(F("  [+] Source Cap Message Received"));

    if (newContractAsProvider()) s.println(F("  [+] New Contract as Provider"));
    if (newContractAsConsumer()) s.println(F("  [+] New Contract as Consumer"));

    if (overcurrent()) s.println(F("  [+] Overcurrent"));

    if (dataSwapComplete()) s.println(F("  [+] Data Swap Complete"));
    if (powerSwapComplete()) s.println(F("  [+] Power Swap Complete"));

    if (pdHardreset()) s.println(F("  [+] PD Hard Reset"));

    s.println();
}

bool TPS25751InterruptMask::isSemanticallyValid() const
{
    if (!isValid() || !hasValidData()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::INT_MASK1.size;
    if (!isValidSize(expectedSize)) return false;

    // No inter-field logical constraints apply to a mask register -- any
    // combination of enabled/disabled sources is legal (unlike INT_EVENT1's
    // isSemanticallyValid(), which flags mutually-exclusive event states).
    return true;
}
