#ifndef TPS25751INTERRUPTMASK_H
#define TPS25751INTERRUPTMASK_H

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @file TPS25751InterruptMask.h
 * @brief INT_MASK1 register (0x16) class implementation
 *
 * The INT_MASK1 register controls which INT_EVENT1 (0x14) interrupt sources
 * are permitted to assert: "A bit in INT_EVENT1 cannot be set if it is
 * cleared in this register." A mask bit of 1 means the corresponding source
 * is enabled (allowed to set its INT_EVENT1 bit); a mask bit of 0 disables
 * (blocks) that source. This class mirrors TPS25751InterruptEvent's bit
 * layout and accessor names exactly -- see that class for the read-only
 * INT_EVENT1 register this one gates.
 *
 * Register Details:
 * - Address: 0x16
 * - Size: 11 bytes (88 bits)
 * - Access: Read/Write
 *
 * @see TI TPS25751 Technical Reference Manual, Table 3-27 family (INT_MASK1)
 */
class TPS25751InterruptMask : public TPS25751Register
{
public:
    explicit TPS25751InterruptMask() : TPS25751Register(TPS25751Registers::Registers::INT_MASK1.size) {};
    explicit TPS25751InterruptMask(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::INT_MASK1.size) {};
    TPS25751InterruptMask(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751InterruptMask(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    /// @brief Register identity for the typed TPS25751::writeRegister<T>() write path.
    static constexpr TPS25751Registers::Address kAddress = TPS25751Registers::Address::INT_MASK1;

    // High-priority system interrupts (bits 80-82)
    /** @brief I2C Controller NACKed source enabled. @see bit 82 */
    bool i2cControllerNacked() const;
    /** @brief Ready for Patch source enabled. @see bit 81 */
    bool readyForPatch() const;
    /** @brief Patch Loaded source enabled. @see bit 80 */
    bool patchLoaded() const;

    // Memory buffer interrupts (bits 65-66)
    /** @brief MBRD Buffer Ready source enabled. @see bit 66 */
    bool mbrdBufferReady() const;
    /** @brief TX Memory Buffer Empty source enabled. @see bit 65 */
    bool txMemoryBufferEmpty() const;

    // Liquid detection (bit 60)
    /** @brief Liquid Detection source enabled. @see bit 60 */
    bool liquidDetection() const;

    // External DC/DC control (bits 56-57)
    /** @brief Ext DCDC Source Safe State source enabled. @see bit 57 */
    bool extDcdcSourceSafeState() const;
    /** @brief Ext DCDC Sink Safe State source enabled. @see bit 56 */
    bool extDcdcSinkSafeState() const;

    // Error conditions (bits 32-39, 46)
    /** @brief Unable to Source Error source enabled. @see bit 46 */
    bool unableToSourceError() const;
    /** @brief Message Data Error source enabled. @see bit 39 */
    bool messageDataError() const;
    /** @brief Protocol Error source enabled. @see bit 38 */
    bool protocolError() const;
    /** @brief Missing Get Capabilities Error source enabled. TRM: "Missing Get Capabalities Message Error" (SLVUCR8B p.21). @see bit 36 */
    bool missingGetCapabilitiesError() const;
    /** @brief Power Event Occurred Error source enabled. @see bit 35 */
    bool powerEventOccurredError() const;
    /** @brief Can Provide Voltage/Current Later Error source enabled. @see bit 34 */
    bool canProvideVoltageLaterError() const;
    /** @brief Cannot Provide Voltage/Current Error source enabled. @see bit 33 */
    bool cannotProvideVoltageError() const;
    /** @brief Device Incompatible Error source enabled. @see bit 32 */
    bool deviceIncompatibleError() const;

    // Plug detection (bits 3, 42-43)
    /** @brief Plug Early Notification source enabled. @see bit 43 */
    bool plugEarlyNotification() const;
    /** @brief Sink Transition Completed source enabled. @see bit 42 */
    bool sinkTransitionCompleted() const;
    /** @brief Plug Insert or Removal source enabled. @see bit 3 */
    bool plugInsertOrRemoval() const;

    // Command completion (bit 30)
    /** @brief CMD1 Complete source enabled. @see bit 30 */
    bool cmd1Complete() const;

    // Register update notifications (bits 23-27)
    /** @brief PD Status Updated source enabled. @see bit 27 */
    bool pdStatusUpdated() const;
    /** @brief Status Updated source enabled. @see bit 26 */
    bool statusUpdated() const;
    /** @brief Power Status Updated source enabled. @see bit 24 */
    bool powerStatusUpdated() const;
    /** @brief Power Path Switch Changed source enabled. @see bit 23 */
    bool powerPathSwitchChanged() const;

    // USB host detection (bits 20-21)
    /** @brief USB Host No Longer Present source enabled. @see bit 21 */
    bool usbHostNoLongerPresent() const;
    /** @brief USB Host Present source enabled. @see bit 20 */
    bool usbHostPresent() const;

    // Power Delivery swap requests (bits 17-18)
    /** @brief Data Swap Requested source enabled. @see bit 18 */
    bool dataSwapRequested() const;
    /** @brief Power Swap Requested source enabled. @see bit 17 */
    bool powerSwapRequested() const;

    // PD message reception (bits 14-15)
    /** @brief Sink Cap Message Received source enabled. @see bit 15 */
    bool sinkCapMessageReceived() const;
    /** @brief Source Cap Message Received source enabled. TRM: "Source Capabalities Message Received" (SLVUCR8B p.21). @see bit 14 */
    bool sourceCapMessageReceived() const;

    // Contract negotiation (bits 12-13)
    /** @brief New Contract as Provider source enabled. @see bit 13 */
    bool newContractAsProvider() const;
    /** @brief New Contract as Consumer source enabled. @see bit 12 */
    bool newContractAsConsumer() const;

    // Overcurrent detection (bit 9)
    /** @brief Overcurrent source enabled. @see bit 9 */
    bool overcurrent() const;

    // Role swap completion (bits 4-5)
    /** @brief Data Swap Complete source enabled. @see bit 5 */
    bool dataSwapComplete() const;
    /** @brief Power Swap Complete source enabled. @see bit 4 */
    bool powerSwapComplete() const;

    // Hard reset (bit 1)
    /** @brief PD Hard Reset source enabled. @see bit 1 */
    bool pdHardreset() const;

    // --- Setters (1:1 with the accessors above; each a single setBits() call) --

    void setI2cControllerNacked(bool enabled);
    void setReadyForPatch(bool enabled);
    void setPatchLoaded(bool enabled);

    void setMbrdBufferReady(bool enabled);
    void setTxMemoryBufferEmpty(bool enabled);

    void setLiquidDetection(bool enabled);

    void setExtDcdcSourceSafeState(bool enabled);
    void setExtDcdcSinkSafeState(bool enabled);

    void setUnableToSourceError(bool enabled);
    void setMessageDataError(bool enabled);
    void setProtocolError(bool enabled);
    void setMissingGetCapabilitiesError(bool enabled);
    void setPowerEventOccurredError(bool enabled);
    void setCanProvideVoltageLaterError(bool enabled);
    void setCannotProvideVoltageError(bool enabled);
    void setDeviceIncompatibleError(bool enabled);

    void setPlugEarlyNotification(bool enabled);
    void setSinkTransitionCompleted(bool enabled);
    void setPlugInsertOrRemoval(bool enabled);

    void setCmd1Complete(bool enabled);

    void setPdStatusUpdated(bool enabled);
    void setStatusUpdated(bool enabled);
    void setPowerStatusUpdated(bool enabled);
    void setPowerPathSwitchChanged(bool enabled);

    void setUsbHostNoLongerPresent(bool enabled);
    void setUsbHostPresent(bool enabled);

    void setDataSwapRequested(bool enabled);
    void setPowerSwapRequested(bool enabled);

    void setSinkCapMessageReceived(bool enabled);
    void setSourceCapMessageReceived(bool enabled);

    void setNewContractAsProvider(bool enabled);
    void setNewContractAsConsumer(bool enabled);

    void setOvercurrent(bool enabled);

    void setDataSwapComplete(bool enabled);
    void setPowerSwapComplete(bool enabled);

    void setPdHardreset(bool enabled);

    /**
     * @brief Check if any interrupt source is enabled (unmasked)
     * @return true if any named mask bit is set
     */
    bool hasAnyEnabled() const;

    /**
     * @brief Get count of enabled (unmasked) interrupt sources
     * @return Number of named mask bits currently set
     */
    uint8_t getEnabledCount() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates interrupt mask register data
     *
     * Any bit combination is a legal mask (there are no inter-field logical
     * constraints between which sources are enabled/disabled, unlike
     * INT_EVENT1's isSemanticallyValid()), so this only validates structure:
     * a valid, correctly-sized buffer that is not the all-0xFF bus-error
     * signature.
     *
     * @return true if the register is valid, correctly sized, and not the
     *         all-ones bus-error pattern
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751INTERRUPTMASK_H
