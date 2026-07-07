#include <Stream.h>

#include "TPS25751InterruptClear.h"

namespace {

/// @brief {bit, name} table for the 36 named clearable sources, used only by
/// debugPrint() to dump which bits are currently staged in the local buffer.
struct NamedBit {
    uint8_t bit;
    const char* name;
};

// Order mirrors TPS25751InterruptEvent's debugPrint() grouping.
constexpr NamedBit kNamedBits[] = {
    {82, "I2C Controller NACKed"},
    {81, "Ready for Patch"},
    {80, "Patch Loaded"},
    {66, "MBRD Buffer Ready"},
    {65, "TX Memory Buffer Empty"},
    {60, "Liquid Detection"},
    {57, "Ext DCDC Source Safe State"},
    {56, "Ext DCDC Sink Safe State"},
    {46, "Unable to Source Error"},
    {39, "Message Data Error"},
    {38, "Protocol Error"},
    {36, "Missing Get Capabilities Error"},
    {35, "Power Event Occurred Error"},
    {34, "Can Provide Voltage/Current Later"},
    {33, "Cannot Provide Voltage/Current"},
    {32, "Device Incompatible Error"},
    {43, "Plug Early Notification"},
    {42, "Sink Transition Completed"},
    {3,  "Plug Insert or Removal"},
    {30, "CMD1 Complete"},
    {27, "PD Status Updated"},
    {26, "Status Updated"},
    {24, "Power Status Updated"},
    {23, "Power Path Switch Changed"},
    {21, "USB Host No Longer Present"},
    {20, "USB Host Present"},
    {18, "Data Swap Requested"},
    {17, "Power Swap Requested"},
    {15, "Sink Cap Message Received"},
    {14, "Source Cap Message Received"},
    {13, "New Contract as Provider"},
    {12, "New Contract as Consumer"},
    {9,  "Overcurrent"},
    {5,  "Data Swap Complete"},
    {4,  "Power Swap Complete"},
    {1,  "PD Hard Reset"},
};

constexpr size_t kNumNamedBits = sizeof(kNamedBits) / sizeof(kNamedBits[0]);

}  // namespace

// --- Per-source clear setters: each a single setBits() call; setBits() -----
// itself guards !isValid().

void TPS25751InterruptClear::clearI2cControllerNacked() { setBits(82, 1, 1u); }
void TPS25751InterruptClear::clearReadyForPatch() { setBits(81, 1, 1u); }
void TPS25751InterruptClear::clearPatchLoaded() { setBits(80, 1, 1u); }

void TPS25751InterruptClear::clearMbrdBufferReady() { setBits(66, 1, 1u); }
void TPS25751InterruptClear::clearTxMemoryBufferEmpty() { setBits(65, 1, 1u); }

void TPS25751InterruptClear::clearLiquidDetection() { setBits(60, 1, 1u); }

void TPS25751InterruptClear::clearExtDcdcSourceSafeState() { setBits(57, 1, 1u); }
void TPS25751InterruptClear::clearExtDcdcSinkSafeState() { setBits(56, 1, 1u); }

void TPS25751InterruptClear::clearUnableToSourceError() { setBits(46, 1, 1u); }
void TPS25751InterruptClear::clearMessageDataError() { setBits(39, 1, 1u); }
void TPS25751InterruptClear::clearProtocolError() { setBits(38, 1, 1u); }
void TPS25751InterruptClear::clearMissingGetCapabilitiesError() { setBits(36, 1, 1u); }
void TPS25751InterruptClear::clearPowerEventOccurredError() { setBits(35, 1, 1u); }
void TPS25751InterruptClear::clearCanProvideVoltageLaterError() { setBits(34, 1, 1u); }
void TPS25751InterruptClear::clearCannotProvideVoltageError() { setBits(33, 1, 1u); }
void TPS25751InterruptClear::clearDeviceIncompatibleError() { setBits(32, 1, 1u); }

void TPS25751InterruptClear::clearPlugEarlyNotification() { setBits(43, 1, 1u); }
void TPS25751InterruptClear::clearSinkTransitionCompleted() { setBits(42, 1, 1u); }
void TPS25751InterruptClear::clearPlugInsertOrRemoval() { setBits(3, 1, 1u); }

void TPS25751InterruptClear::clearCmd1Complete() { setBits(30, 1, 1u); }

void TPS25751InterruptClear::clearPdStatusUpdated() { setBits(27, 1, 1u); }
void TPS25751InterruptClear::clearStatusUpdated() { setBits(26, 1, 1u); }
void TPS25751InterruptClear::clearPowerStatusUpdated() { setBits(24, 1, 1u); }
void TPS25751InterruptClear::clearPowerPathSwitchChanged() { setBits(23, 1, 1u); }

void TPS25751InterruptClear::clearUsbHostNoLongerPresent() { setBits(21, 1, 1u); }
void TPS25751InterruptClear::clearUsbHostPresent() { setBits(20, 1, 1u); }

void TPS25751InterruptClear::clearDataSwapRequested() { setBits(18, 1, 1u); }
void TPS25751InterruptClear::clearPowerSwapRequested() { setBits(17, 1, 1u); }

void TPS25751InterruptClear::clearSinkCapMessageReceived() { setBits(15, 1, 1u); }
void TPS25751InterruptClear::clearSourceCapMessageReceived() { setBits(14, 1, 1u); }

void TPS25751InterruptClear::clearNewContractAsProvider() { setBits(13, 1, 1u); }
void TPS25751InterruptClear::clearNewContractAsConsumer() { setBits(12, 1, 1u); }

void TPS25751InterruptClear::clearOvercurrent() { setBits(9, 1, 1u); }

void TPS25751InterruptClear::clearDataSwapComplete() { setBits(5, 1, 1u); }
void TPS25751InterruptClear::clearPowerSwapComplete() { setBits(4, 1, 1u); }

void TPS25751InterruptClear::clearPdHardreset() { setBits(1, 1, 1u); }

void TPS25751InterruptClear::clear(uint8_t bitPos)
{
    setBits(bitPos, 1, 1u);
}

void TPS25751InterruptClear::clearAll()
{
    // Set exactly the 36 named bits to 1 via setBits() (never memset(0xFF),
    // which would also set undocumented reserved bits). Idempotent: setting
    // an already-set bit is a no-op change.
    for (size_t i = 0; i < kNumNamedBits; ++i) {
        setBits(kNamedBits[i].bit, 1, 1u);
    }
}

void TPS25751InterruptClear::debugPrint(Stream &s) const
{
    s.println(F("=== Interrupt Clear Register (0x18) [write-only] ==="));

    if (!isValid()) {
        s.println(F("  <invalid>"));
        s.println();
        return;
    }

    bool anyStaged = false;
    for (size_t i = 0; i < kNumNamedBits; ++i) {
        if (extractBits(kNamedBits[i].bit, 1)) {
            if (!anyStaged) {
                s.println(F("Staged clears:"));
                anyStaged = true;
            }
            s.print(F("  [x] "));
            s.println(kNamedBits[i].name);
        }
    }

    if (!anyStaged) {
        s.println(F("No clears staged (buffer is all-zero)"));
    }

    s.println();
}

bool TPS25751InterruptClear::isSemanticallyValid() const
{
    // INT_CLEAR1 is write-only: read-back is not meaningful, and the all-zero
    // "clear nothing" buffer is a valid state (unlike field-structured
    // registers), so this deliberately does NOT call hasValidData() -- see
    // TPS25751CustomerUse for the same reasoning.
    if (!isValid()) return false;

    constexpr size_t expectedSize = TPS25751Registers::Registers::INT_CLEAR1.size;
    return isValidSize(expectedSize);
}
