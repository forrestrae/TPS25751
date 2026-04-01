#include "TPS25751RegisterFactory.h"
#include "TPS25751Status.h"
#include "TPS25751Mode.h"
#include "TPS25751BootFlags.h"
#include "TPS25751PowerPathStatus.h"
#include "TPS25751PortConfig.h"
#include "TPS25751TypeCState.h"
#include "TPS25751InterruptEvent.h"
#include "TPS25751PowerStatus.h"
#include "TPS25751PDStatus.h"
#include "TPS25751GPIOStatus.h"
#include "TPS25751PortControl.h"
#include "TPS25751ActivePDOContract.h"
#include "TPS25751ActiveRDOContract.h"
#include "TPS25751ReceivedSourceCaps.h"
#include "TPS25751ReceivedSinkCaps.h"
#include "TPS25751Debug.h"
#include <memory>

// Static factory instance
std::unique_ptr<TPS25751RegisterFactory> TPS25751Factory::instance_;

// Static mapping functions
RegisterType TPS25751RegisterFactory::getRegisterType(TPS25751Registers::Address addr) {
    switch (addr) {
        case TPS25751Registers::Address::STATUS:
            return RegisterType::STATUS;
        case TPS25751Registers::Address::MODE:
            return RegisterType::MODE;
        case TPS25751Registers::Address::BOOT_STATUS:
            return RegisterType::BOOT_FLAGS;
        case TPS25751Registers::Address::POWER_PATH_STATUS:
            return RegisterType::POWER_PATH_STATUS;
        case TPS25751Registers::Address::PORT_CONFIGURATION:
            return RegisterType::PORT_CONFIG;
        case TPS25751Registers::Address::TYPEC_STATE:
            return RegisterType::TYPEC_STATE;
        case TPS25751Registers::Address::INT_EVENT1:
            return RegisterType::INTERRUPT_EVENT;
        case TPS25751Registers::Address::POWER_STATUS:
            return RegisterType::POWER_STATUS;
        case TPS25751Registers::Address::PD_STATUS:
            return RegisterType::PD_STATUS;
        case TPS25751Registers::Address::GPIO_STATUS:
            return RegisterType::GPIO_STATUS;
        case TPS25751Registers::Address::PORT_CONTROL:
            return RegisterType::PORT_CONTROL;
        case TPS25751Registers::Address::ACTIVE_PDO_CONTRACT:
            return RegisterType::ACTIVE_PDO_CONTRACT;
        case TPS25751Registers::Address::ACTIVE_RDO_CONTRACT:
            return RegisterType::ACTIVE_RDO_CONTRACT;
        case TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES:
            return RegisterType::RECEIVED_SOURCE_CAPS;
        case TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES:
            return RegisterType::RECEIVED_SINK_CAPS;
        default:
            return RegisterType::UNKNOWN;
    }
}

RegisterType TPS25751RegisterFactory::getRegisterType(uint8_t addr) {
    return getRegisterType(static_cast<TPS25751Registers::Address>(addr));
}

TPS25751Registers::Address TPS25751RegisterFactory::getAddress(RegisterType type) {
    switch (type) {
        case RegisterType::STATUS:
            return TPS25751Registers::Address::STATUS;
        case RegisterType::MODE:
            return TPS25751Registers::Address::MODE;
        case RegisterType::BOOT_FLAGS:
            return TPS25751Registers::Address::BOOT_STATUS;
        case RegisterType::POWER_PATH_STATUS:
            return TPS25751Registers::Address::POWER_PATH_STATUS;
        case RegisterType::PORT_CONFIG:
            return TPS25751Registers::Address::PORT_CONFIGURATION;
        case RegisterType::TYPEC_STATE:
            return TPS25751Registers::Address::TYPEC_STATE;
        case RegisterType::INTERRUPT_EVENT:
            return TPS25751Registers::Address::INT_EVENT1;
        case RegisterType::POWER_STATUS:
            return TPS25751Registers::Address::POWER_STATUS;
        case RegisterType::PD_STATUS:
            return TPS25751Registers::Address::PD_STATUS;
        case RegisterType::GPIO_STATUS:
            return TPS25751Registers::Address::GPIO_STATUS;
        case RegisterType::PORT_CONTROL:
            return TPS25751Registers::Address::PORT_CONTROL;
        case RegisterType::ACTIVE_PDO_CONTRACT:
            return TPS25751Registers::Address::ACTIVE_PDO_CONTRACT;
        case RegisterType::ACTIVE_RDO_CONTRACT:
            return TPS25751Registers::Address::ACTIVE_RDO_CONTRACT;
        case RegisterType::RECEIVED_SOURCE_CAPS:
            return TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES;
        case RegisterType::RECEIVED_SINK_CAPS:
            return TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES;
        default:
            return static_cast<TPS25751Registers::Address>(0xFF); // Invalid
    }
}

bool TPS25751RegisterFactory::validateTypeAddress(RegisterType type, TPS25751Registers::Address addr) {
    return getAddress(type) == addr;
}

// Factory Implementation
std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(RegisterType type) {
    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register of type %u", static_cast<uint8_t>(type));

    switch (type) {
        case RegisterType::STATUS:
            return createStatusRegister();
        case RegisterType::MODE:
            return createModeRegister();
        case RegisterType::BOOT_FLAGS:
            return createBootFlagsRegister();
        case RegisterType::POWER_PATH_STATUS:
            return createPowerPathStatusRegister();
        case RegisterType::PORT_CONFIG:
            return createPortConfigRegister();
        case RegisterType::TYPEC_STATE:
            return createTypeCStateRegister();
        case RegisterType::INTERRUPT_EVENT:
            return createInterruptEventRegister();
        case RegisterType::POWER_STATUS:
            return createPowerStatusRegister();
        case RegisterType::PD_STATUS:
            return createPDStatusRegister();
        case RegisterType::GPIO_STATUS:
            return createGPIOStatusRegister();
        case RegisterType::PORT_CONTROL:
            return createPortControlRegister();
        case RegisterType::ACTIVE_PDO_CONTRACT:
            return createActivePDOContractRegister();
        case RegisterType::ACTIVE_RDO_CONTRACT:
            return createActiveRDOContractRegister();
        case RegisterType::RECEIVED_SOURCE_CAPS:
            return createReceivedSourceCapsRegister();
        case RegisterType::RECEIVED_SINK_CAPS:
            return createReceivedSinkCapsRegister();
        default:
            TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unknown register type: %u", static_cast<uint8_t>(type));
            return nullptr;
    }
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(TPS25751Registers::Address addr) {
    RegisterType type = getRegisterType(addr);
    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register from address 0x%02X (type %u)", 
                   static_cast<uint8_t>(addr), static_cast<uint8_t>(type));
    
    if (type == RegisterType::UNKNOWN) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unknown register address: 0x%02X", static_cast<uint8_t>(addr));
        return nullptr;
    }
    
    return createRegister(type);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(RegisterType type, const uint8_t* data, size_t length) {
    if (!data) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Cannot create register with null data");
        return nullptr;
    }

    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register of type %u with %u bytes of data",
                   static_cast<uint8_t>(type), static_cast<unsigned>(length));

    switch (type) {
        case RegisterType::STATUS:
            return createStatusRegister(data, length);
        case RegisterType::MODE:
            return createModeRegister(data, length);
        case RegisterType::BOOT_FLAGS:
            return createBootFlagsRegister(data, length);
        case RegisterType::POWER_PATH_STATUS:
            return createPowerPathStatusRegister(data, length);
        case RegisterType::PORT_CONFIG:
            return createPortConfigRegister(data, length);
        case RegisterType::TYPEC_STATE:
            return createTypeCStateRegister(data, length);
        case RegisterType::INTERRUPT_EVENT:
            return createInterruptEventRegister(data, length);
        case RegisterType::POWER_STATUS:
            return createPowerStatusRegister(data, length);
        case RegisterType::PD_STATUS:
            return createPDStatusRegister(data, length);
        case RegisterType::GPIO_STATUS:
            return createGPIOStatusRegister(data, length);
        case RegisterType::PORT_CONTROL:
            return createPortControlRegister(data, length);
        case RegisterType::ACTIVE_PDO_CONTRACT:
            return createActivePDOContractRegister(data, length);
        case RegisterType::ACTIVE_RDO_CONTRACT:
            return createActiveRDOContractRegister(data, length);
        case RegisterType::RECEIVED_SOURCE_CAPS:
            return createReceivedSourceCapsRegister(data, length);
        case RegisterType::RECEIVED_SINK_CAPS:
            return createReceivedSinkCapsRegister(data, length);
        default:
            TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unknown register type: %u", static_cast<uint8_t>(type));
            return nullptr;
    }
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(RegisterType type, const TPS25751Registers::RegisterInfo& regInfo) {
    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register from RegisterInfo (type %u, addr 0x%02X, size %u)",
                   static_cast<uint8_t>(type), static_cast<uint8_t>(regInfo.address), regInfo.size);

    // Validate type matches address
    if (!validateTypeAddress(type, regInfo.address)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Type/address mismatch: type %u, addr 0x%02X",
                       static_cast<uint8_t>(type), static_cast<uint8_t>(regInfo.address));
        return nullptr;
    }

    switch (type) {
        case RegisterType::STATUS:
            return std::make_unique<TPS25751Status>(regInfo);
        case RegisterType::MODE:
            return std::make_unique<TPS25751Mode>(regInfo);
        case RegisterType::BOOT_FLAGS:
            return std::make_unique<TPS25751BootFlags>(regInfo);
        case RegisterType::POWER_PATH_STATUS:
            return std::make_unique<TPS25751PowerPathStatus>(regInfo);
        case RegisterType::PORT_CONFIG:
            return std::make_unique<TPS25751PortConfig>(regInfo);
        case RegisterType::TYPEC_STATE:
            return std::make_unique<TPS25751TypeCState>(regInfo);
        case RegisterType::INTERRUPT_EVENT:
            return std::make_unique<TPS25751InterruptEvent>(regInfo);
        case RegisterType::POWER_STATUS:
            return std::make_unique<TPS25751PowerStatus>(regInfo);
        case RegisterType::PD_STATUS:
            return std::make_unique<TPS25751PDStatus>(regInfo);
        case RegisterType::GPIO_STATUS:
            return std::make_unique<TPS25751GPIOStatus>(regInfo);
        case RegisterType::PORT_CONTROL:
            return std::make_unique<TPS25751PortControl>(regInfo);
        case RegisterType::ACTIVE_PDO_CONTRACT:
            return std::make_unique<TPS25751ActivePDOContract>(regInfo);
        case RegisterType::ACTIVE_RDO_CONTRACT:
            return std::make_unique<TPS25751ActiveRDOContract>(regInfo);
        case RegisterType::RECEIVED_SOURCE_CAPS:
            return std::make_unique<TPS25751ReceivedSourceCaps>(regInfo);
        case RegisterType::RECEIVED_SINK_CAPS:
            return std::make_unique<TPS25751ReceivedSinkCaps>(regInfo);
        default:
            TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unknown register type: %u", static_cast<uint8_t>(type));
            return nullptr;
    }
}

// Helper methods for creating specific register types (empty constructors)
std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createStatusRegister() const {
    return std::make_unique<TPS25751Status>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createModeRegister() const {
    return std::make_unique<TPS25751Mode>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createBootFlagsRegister() const {
    return std::make_unique<TPS25751BootFlags>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPowerPathStatusRegister() const {
    return std::make_unique<TPS25751PowerPathStatus>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPortConfigRegister() const {
    return std::make_unique<TPS25751PortConfig>();
}

// Helper methods for creating with data
std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createStatusRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751Status>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createModeRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751Mode>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createBootFlagsRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751BootFlags>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPowerPathStatusRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751PowerPathStatus>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPortConfigRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751PortConfig>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createTypeCStateRegister() const {
    return std::make_unique<TPS25751TypeCState>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createTypeCStateRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751TypeCState>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createInterruptEventRegister() const {
    return std::make_unique<TPS25751InterruptEvent>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createInterruptEventRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751InterruptEvent>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPowerStatusRegister() const {
    return std::make_unique<TPS25751PowerStatus>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPowerStatusRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751PowerStatus>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPDStatusRegister() const {
    return std::make_unique<TPS25751PDStatus>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPDStatusRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751PDStatus>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createGPIOStatusRegister() const {
    return std::make_unique<TPS25751GPIOStatus>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createGPIOStatusRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751GPIOStatus>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPortControlRegister() const {
    return std::make_unique<TPS25751PortControl>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createPortControlRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751PortControl>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createActivePDOContractRegister() const {
    return std::make_unique<TPS25751ActivePDOContract>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createActivePDOContractRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751ActivePDOContract>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createActiveRDOContractRegister() const {
    return std::make_unique<TPS25751ActiveRDOContract>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createActiveRDOContractRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751ActiveRDOContract>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createReceivedSourceCapsRegister() const {
    return std::make_unique<TPS25751ReceivedSourceCaps>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createReceivedSourceCapsRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751ReceivedSourceCaps>(data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createReceivedSinkCapsRegister() const {
    return std::make_unique<TPS25751ReceivedSinkCaps>();
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createReceivedSinkCapsRegister(const uint8_t* data, size_t length) const {
    return std::make_unique<TPS25751ReceivedSinkCaps>(data, length);
}

// Singleton factory instance management
TPS25751RegisterFactory& TPS25751Factory::getInstance() {
    if (!instance_) {
        instance_ = std::make_unique<TPS25751RegisterFactoryImpl>();
    }
    return *instance_;
}

void TPS25751Factory::setFactory(std::unique_ptr<TPS25751RegisterFactory> factory) {
    instance_ = std::move(factory);
}

// Convenience functions
std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(RegisterType type) {
    return TPS25751Factory::getInstance().createRegister(type);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(TPS25751Registers::Address addr) {
    return TPS25751Factory::getInstance().createRegister(addr);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(RegisterType type, const uint8_t* data, size_t length) {
    return TPS25751Factory::getInstance().createRegister(type, data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(RegisterType type, const TPS25751Registers::RegisterInfo& regInfo) {
    return TPS25751Factory::getInstance().createRegister(type, regInfo);
}