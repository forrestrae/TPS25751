#ifndef TPS25751INTERRUPTCLEAR_H
#define TPS25751INTERRUPTCLEAR_H

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751InterruptClear.h
 * @brief INT_CLEAR1 register (0x18) class implementation
 *
 * INT_CLEAR1 is a write-only staging buffer: "Writing 1 to a specific bit
 * will clear that specific event in INT_EVENT1." A caller sets the bits
 * corresponding to the INT_EVENT1 sources it wants acknowledged/cleared,
 * then writes the buffer via TPS25751::writeInterruptClear(). This class
 * mirrors TPS25751InterruptEvent's bit layout and accessor names exactly
 * (as clear<Name>() setters rather than read accessors) -- see that class
 * for the read-only INT_EVENT1 register this one acknowledges.
 *
 * @note This register has no meaningful read-back semantics: it is a
 * write-only clear-request buffer, not a mirror of INT_EVENT1's state.
 * There are intentionally no decode accessors, no hasAny*()/count helpers,
 * and isSemanticallyValid() does not gate on hasValidData() (see that
 * method's doc comment and TPS25751CustomerUse for the same pattern) --
 * an all-zero "clear nothing" buffer is a perfectly valid state to write.
 *
 * Register Details:
 * - Address: 0x18
 * - Size: 11 bytes (88 bits)
 * - Access: Write-only
 *
 * @see TI TPS25751 Technical Reference Manual, Table 3-27 family (INT_CLEAR1)
 */
class TPS25751InterruptClear : public TPS25751Register
{
public:
    explicit TPS25751InterruptClear() : TPS25751Register(TPS25751Registers::Registers::INT_CLEAR1.size) {};
    explicit TPS25751InterruptClear(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::INT_CLEAR1.size) {};
    TPS25751InterruptClear(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751InterruptClear(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::INT_CLEAR1;

    // --- Per-source clear setters (same names/bits as TPS25751InterruptEvent) --

    /** @brief Stage a clear of I2C Controller NACKed. @see bit 82 */
    void clearI2cControllerNacked();
    /** @brief Stage a clear of Ready for Patch. @see bit 81 */
    void clearReadyForPatch();
    /** @brief Stage a clear of Patch Loaded. @see bit 80 */
    void clearPatchLoaded();

    /** @brief Stage a clear of MBRD Buffer Ready. @see bit 66 */
    void clearMbrdBufferReady();
    /** @brief Stage a clear of TX Memory Buffer Empty. @see bit 65 */
    void clearTxMemoryBufferEmpty();

    /** @brief Stage a clear of Liquid Detection. @see bit 60 */
    void clearLiquidDetection();

    /** @brief Stage a clear of Ext DCDC Source Safe State. @see bit 57 */
    void clearExtDcdcSourceSafeState();
    /** @brief Stage a clear of Ext DCDC Sink Safe State. @see bit 56 */
    void clearExtDcdcSinkSafeState();

    /** @brief Stage a clear of Unable to Source Error. @see bit 46 */
    void clearUnableToSourceError();
    /** @brief Stage a clear of Message Data Error. @see bit 39 */
    void clearMessageDataError();
    /** @brief Stage a clear of Protocol Error. @see bit 38 */
    void clearProtocolError();
    /** @brief Stage a clear of Missing Get Capabilities Error. TRM: "Missing Get Capabalities Message Error" (SLVUCR8B p.23). @see bit 36 */
    void clearMissingGetCapabilitiesError();
    /** @brief Stage a clear of Power Event Occurred Error. @see bit 35 */
    void clearPowerEventOccurredError();
    /** @brief Stage a clear of Can Provide Voltage/Current Later Error. @see bit 34 */
    void clearCanProvideVoltageLaterError();
    /** @brief Stage a clear of Cannot Provide Voltage/Current Error. @see bit 33 */
    void clearCannotProvideVoltageError();
    /** @brief Stage a clear of Device Incompatible Error. @see bit 32 */
    void clearDeviceIncompatibleError();

    /** @brief Stage a clear of Plug Early Notification. @see bit 43 */
    void clearPlugEarlyNotification();
    /** @brief Stage a clear of Sink Transition Completed. @see bit 42 */
    void clearSinkTransitionCompleted();
    /** @brief Stage a clear of Plug Insert or Removal. @see bit 3 */
    void clearPlugInsertOrRemoval();

    /** @brief Stage a clear of CMD1 Complete. @see bit 30 */
    void clearCmd1Complete();

    /** @brief Stage a clear of PD Status Updated. @see bit 27 */
    void clearPdStatusUpdated();
    /** @brief Stage a clear of Status Updated. @see bit 26 */
    void clearStatusUpdated();
    /** @brief Stage a clear of Power Status Updated. @see bit 24 */
    void clearPowerStatusUpdated();
    /** @brief Stage a clear of Power Path Switch Changed. @see bit 23 */
    void clearPowerPathSwitchChanged();

    /** @brief Stage a clear of USB Host No Longer Present. @see bit 21 */
    void clearUsbHostNoLongerPresent();
    /** @brief Stage a clear of USB Host Present. @see bit 20 */
    void clearUsbHostPresent();

    /** @brief Stage a clear of Data Swap Requested. @see bit 18 */
    void clearDataSwapRequested();
    /** @brief Stage a clear of Power Swap Requested. @see bit 17 */
    void clearPowerSwapRequested();

    /** @brief Stage a clear of Sink Cap Message Received. @see bit 15 */
    void clearSinkCapMessageReceived();
    /** @brief Stage a clear of Source Cap Message Received. @see bit 14 */
    void clearSourceCapMessageReceived();

    /** @brief Stage a clear of New Contract as Provider. @see bit 13 */
    void clearNewContractAsProvider();
    /** @brief Stage a clear of New Contract as Consumer. @see bit 12 */
    void clearNewContractAsConsumer();

    /** @brief Stage a clear of Overcurrent. @see bit 9 */
    void clearOvercurrent();

    /** @brief Stage a clear of Data Swap Complete. @see bit 5 */
    void clearDataSwapComplete();
    /** @brief Stage a clear of Power Swap Complete. @see bit 4 */
    void clearPowerSwapComplete();

    /** @brief Stage a clear of PD Hard Reset. @see bit 1 */
    void clearPdHardreset();

    /**
     * @brief Stage a clear of an arbitrary bit position
     * @param bitPos Bit offset within the 88-bit register (0-87)
     */
    void clear(uint8_t bitPos);

    /**
     * @brief Stage a clear of every named source (all 36 named bits set to 1)
     *
     * Reserved bits are left untouched (0) -- this does NOT memset the buffer
     * to 0xFF, which would also set reserved bits the TRM does not document
     * as clearable. Idempotent: calling it repeatedly leaves the same bits set.
     */
    void clearAll();

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates the interrupt-clear register structurally
     *
     * INT_CLEAR1 is a write-only staging buffer, so read-back is not
     * meaningful and there is no logical state to validate beyond structure.
     * Unlike most registers, this deliberately does NOT call the inherited
     * hasValidData() -- an all-zero "clear nothing" buffer is the common,
     * valid case (mirrors TPS25751CustomerUse, which has no field structure
     * to validate either).
     *
     * @return true if the register is valid and correctly sized
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751INTERRUPTCLEAR_H
