#pragma once

#include <Arduino.h>
#include <memory>
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

// Forward declarations
class TPS25751Status;
class TPS25751Mode;
class TPS25751BootFlags;
class TPS25751PowerPathStatus;
class TPS25751PortConfig;
class TPS25751TypeCState;
class TPS25751InterruptEvent;
class TPS25751PowerStatus;
class TPS25751PDStatus;
class TPS25751GPIOStatus;
class TPS25751PortControl;
class TPS25751ActivePDOContract;
class TPS25751ActiveRDOContract;
class TPS25751ReceivedSourceCaps;
class TPS25751ReceivedSinkCaps;

/**
 * @brief Abstract factory interface for creating register objects
 *
 * Registers are identified by their hardware address (TPS25751Registers::Address),
 * which is the single source of truth for register identity. Addresses without a
 * decoder class yield a nullptr from createRegister().
 */
class TPS25751RegisterFactory {
public:
    virtual ~TPS25751RegisterFactory() = default;

    /**
     * @brief Create a register object from address
     * @param addr Register address
     * @return Unique pointer to created register (nullptr if unsupported address)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr) = 0;

    /**
     * @brief Create a register object with data
     * @param addr Register address
     * @param data Raw data to initialize with
     * @param length Data length
     * @return Unique pointer to created register (nullptr if failed)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr, const uint8_t* data, size_t length) = 0;

    /**
     * @brief Create a register object with RegisterInfo
     * @param regInfo Register information structure (address + size)
     * @return Unique pointer to created register (nullptr if failed)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(const TPS25751Registers::RegisterInfo& regInfo) = 0;
};

/**
 * @brief Concrete factory for creating TPS25751 register objects
 */
class TPS25751RegisterFactoryImpl : public TPS25751RegisterFactory {
public:
    std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr) override;
    std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr, const uint8_t* data, size_t length) override;
    std::unique_ptr<TPS25751Register> createRegister(const TPS25751Registers::RegisterInfo& regInfo) override;
    
private:
    // Helper methods for creating specific register types
    std::unique_ptr<TPS25751Register> createStatusRegister() const;
    std::unique_ptr<TPS25751Register> createModeRegister() const;
    std::unique_ptr<TPS25751Register> createBootFlagsRegister() const;
    std::unique_ptr<TPS25751Register> createPowerPathStatusRegister() const;
    std::unique_ptr<TPS25751Register> createPortConfigRegister() const;
    std::unique_ptr<TPS25751Register> createTypeCStateRegister() const;
    std::unique_ptr<TPS25751Register> createInterruptEventRegister() const;
    std::unique_ptr<TPS25751Register> createPowerStatusRegister() const;
    std::unique_ptr<TPS25751Register> createPDStatusRegister() const;
    std::unique_ptr<TPS25751Register> createGPIOStatusRegister() const;
    std::unique_ptr<TPS25751Register> createPortControlRegister() const;
    std::unique_ptr<TPS25751Register> createActivePDOContractRegister() const;
    std::unique_ptr<TPS25751Register> createActiveRDOContractRegister() const;
    std::unique_ptr<TPS25751Register> createReceivedSourceCapsRegister() const;
    std::unique_ptr<TPS25751Register> createReceivedSinkCapsRegister() const;

    std::unique_ptr<TPS25751Register> createStatusRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createModeRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createBootFlagsRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createPowerPathStatusRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createPortConfigRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createTypeCStateRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createInterruptEventRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createPowerStatusRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createPDStatusRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createGPIOStatusRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createPortControlRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createActivePDOContractRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createActiveRDOContractRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createReceivedSourceCapsRegister(const uint8_t* data, size_t length) const;
    std::unique_ptr<TPS25751Register> createReceivedSinkCapsRegister(const uint8_t* data, size_t length) const;
};

/**
 * @brief Singleton factory instance for convenient access
 */
class TPS25751Factory {
public:
    /**
     * @brief Get singleton factory instance
     * @return Reference to factory instance
     */
    static TPS25751RegisterFactory& getInstance();
    
    /**
     * @brief Set custom factory implementation
     * @param factory Custom factory instance (takes ownership)
     */
    static void setFactory(std::unique_ptr<TPS25751RegisterFactory> factory);
    
private:
    static std::unique_ptr<TPS25751RegisterFactory> instance_;
};

// Convenience functions for register creation
namespace TPS25751RegisterCreator {
    /**
     * @brief Create register by address
     */
    std::unique_ptr<TPS25751Register> create(TPS25751Registers::Address addr);

    /**
     * @brief Create register with data
     */
    std::unique_ptr<TPS25751Register> create(TPS25751Registers::Address addr, const uint8_t* data, size_t length);

    /**
     * @brief Create register with RegisterInfo
     */
    std::unique_ptr<TPS25751Register> create(const TPS25751Registers::RegisterInfo& regInfo);
}