#ifndef TPS25751INTERRUPTEVENT_H
#define TPS25751INTERRUPTEVENT_H

#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

/**
 * @brief TPS25751 Interrupt Event Register (INT_EVENT1, Address: 0x14)
 *
 * This register contains interrupt event bits for the IRQ pin. If any bit in
 * this register is 1, the IRQ pin is pulled low. The register contains 88 bits
 * (11 bytes) with various interrupt flags.
 *
 * Register size: 11 bytes (88 bits)
 * Access: Read-only
 *
 * This class provides accessor methods for all non-reserved interrupt flags.
 * Use the corresponding INTERRUPT_MASK and INTERRUPT_CLEAR registers (0x16, 0x18)
 * to control interrupt behavior.
 */
class TPS25751InterruptEvent : public TPS25751Register
{
public:
    explicit TPS25751InterruptEvent() : TPS25751Register(TPS25751Registers::Registers::INT_EVENT1.size) {};
    explicit TPS25751InterruptEvent(const uint8_t *data) : TPS25751Register(data, TPS25751Registers::Registers::INT_EVENT1.size) {};
    TPS25751InterruptEvent(const uint8_t *data, const size_t len) : TPS25751Register(data, len) {};
    explicit TPS25751InterruptEvent(TPS25751Registers::RegisterInfo regInfo) : TPS25751Register(regInfo.size) {};

    // High-priority system interrupts (bits 80-82)
    bool i2cControllerNacked() const;      // bit 82
    bool readyForPatch() const;            // bit 81
    bool patchLoaded() const;              // bit 80

    // Memory buffer interrupts (bits 65-66)
    bool mbrdBufferReady() const;          // bit 66
    bool txMemoryBufferEmpty() const;      // bit 65

    // Liquid detection (bit 60)
    bool liquidDetection() const;          // bit 60

    // External DC/DC control (bits 56-57)
    bool extDcdcSourceSafeState() const;   // bit 57
    bool extDcdcSinkSafeState() const;     // bit 56

    // Error conditions (bits 32-39, 46)
    bool unableToSourceError() const;      // bit 46
    bool messageDataError() const;         // bit 39
    bool protocolError() const;            // bit 38
    bool missingGetCapabilitiesError() const; // bit 36
    bool powerEventOccurredError() const;  // bit 35
    bool canProvideVoltageLaterError() const; // bit 34
    bool cannotProvideVoltageError() const; // bit 33
    bool deviceIncompatibleError() const;  // bit 32

    // Plug detection (bit 43, 42, 3)
    bool plugEarlyNotification() const;    // bit 43
    bool sinkTransitionCompleted() const;  // bit 42
    bool plugInsertOrRemoval() const;      // bit 3

    // Command completion (bit 30)
    bool cmd1Complete() const;             // bit 30

    // Register update notifications (bits 23-27)
    bool pdStatusUpdated() const;          // bit 27
    bool statusUpdated() const;            // bit 26
    bool powerStatusUpdated() const;       // bit 24
    bool powerPathSwitchChanged() const;   // bit 23

    // USB host detection (bits 20-21)
    bool usbHostNoLongerPresent() const;   // bit 21
    bool usbHostPresent() const;           // bit 20

    // Power Delivery swap requests (bits 17-18)
    bool dataSwapRequested() const;        // bit 18
    bool powerSwapRequested() const;       // bit 17

    // PD message reception (bits 14-15)
    bool sinkCapMessageReceived() const;   // bit 15
    bool sourceCapMessageReceived() const; // bit 14

    // Contract negotiation (bits 12-13)
    bool newContractAsProvider() const;    // bit 13
    bool newContractAsConsumer() const;    // bit 12

    // Overcurrent detection (bit 9)
    bool overcurrent() const;              // bit 9

    // Role swap completion (bits 4-5)
    bool dataSwapComplete() const;         // bit 5
    bool powerSwapComplete() const;        // bit 4

    // Hard reset (bit 1)
    bool pdHardreset() const;              // bit 1

    /**
     * @brief Check if any interrupt flag is set
     * @return true if any non-reserved interrupt bit is set
     */
    bool hasAnyInterrupt() const;

    /**
     * @brief Get count of active interrupt flags
     * @return Number of interrupt flags currently set
     */
    uint8_t getActiveInterruptCount() const;

    void debugPrint(Stream &s = Serial) const override;

    /**
     * @brief Validates interrupt event register data semantically
     * @return true if interrupt flags are logically consistent
     */
    bool isSemanticallyValid() const override;
};

#endif // TPS25751INTERRUPTEVENT_H