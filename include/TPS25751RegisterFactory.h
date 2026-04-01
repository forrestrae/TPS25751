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
 * @brief Register type enumeration for factory creation
 */
enum class RegisterType : uint8_t {
    STATUS = 0,
    MODE = 1,
    BOOT_FLAGS = 2,
    POWER_PATH_STATUS = 3,
    PORT_CONFIG = 4,
    TYPEC_STATE = 5,
    INTERRUPT_EVENT = 6,
    POWER_STATUS = 7,
    PD_STATUS = 8,
    GPIO_STATUS = 9,
    PORT_CONTROL = 10,
    ACTIVE_PDO_CONTRACT = 11,
    ACTIVE_RDO_CONTRACT = 12,
    RECEIVED_SOURCE_CAPS = 13,
    RECEIVED_SINK_CAPS = 14,
    UNKNOWN = 255
};

/**
 * @brief Abstract factory interface for creating register objects
 */
class TPS25751RegisterFactory {
public:
    virtual ~TPS25751RegisterFactory() = default;
    
    /**
     * @brief Create a register object of the specified type
     * @param type Register type to create
     * @return Unique pointer to created register (nullptr if failed)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(RegisterType type) = 0;
    
    /**
     * @brief Create a register object from address
     * @param addr Register address
     * @return Unique pointer to created register (nullptr if unknown address)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr) = 0;
    
    /**
     * @brief Create a register object with data
     * @param type Register type
     * @param data Raw data to initialize with
     * @param length Data length
     * @return Unique pointer to created register (nullptr if failed)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(RegisterType type, const uint8_t* data, size_t length) = 0;
    
    /**
     * @brief Create a register object with RegisterInfo
     * @param type Register type
     * @param regInfo Register information structure
     * @return Unique pointer to created register (nullptr if failed)
     */
    virtual std::unique_ptr<TPS25751Register> createRegister(RegisterType type, const TPS25751Registers::RegisterInfo& regInfo) = 0;
    
    /**
     * @brief Get register type from address
     * @param addr Register address
     * @return Register type (UNKNOWN if not found)
     */
    static RegisterType getRegisterType(TPS25751Registers::Address addr);
    
    /**
     * @brief Get register type from address value
     * @param addr Register address as uint8_t
     * @return Register type (UNKNOWN if not found)
     */
    static RegisterType getRegisterType(uint8_t addr);
    
    /**
     * @brief Get address from register type
     * @param type Register type
     * @return Register address (returns first address if multiple exist)
     */
    static TPS25751Registers::Address getAddress(RegisterType type);
    
    /**
     * @brief Validate that register type matches address
     * @param type Expected register type
     * @param addr Register address
     * @return true if type matches address
     */
    static bool validateTypeAddress(RegisterType type, TPS25751Registers::Address addr);
};

/**
 * @brief Concrete factory for creating TPS25751 register objects
 */
class TPS25751RegisterFactoryImpl : public TPS25751RegisterFactory {
public:
    std::unique_ptr<TPS25751Register> createRegister(RegisterType type) override;
    std::unique_ptr<TPS25751Register> createRegister(TPS25751Registers::Address addr) override;
    std::unique_ptr<TPS25751Register> createRegister(RegisterType type, const uint8_t* data, size_t length) override;
    std::unique_ptr<TPS25751Register> createRegister(RegisterType type, const TPS25751Registers::RegisterInfo& regInfo) override;
    
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
     * @brief Create register by type
     */
    std::unique_ptr<TPS25751Register> create(RegisterType type);
    
    /**
     * @brief Create register by address
     */
    std::unique_ptr<TPS25751Register> create(TPS25751Registers::Address addr);
    
    /**
     * @brief Create register with data
     */
    std::unique_ptr<TPS25751Register> create(RegisterType type, const uint8_t* data, size_t length);
    
    /**
     * @brief Create register with RegisterInfo
     */
    std::unique_ptr<TPS25751Register> create(RegisterType type, const TPS25751Registers::RegisterInfo& regInfo);
}