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

// Factory Implementation
std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(TPS25751Registers::Address addr) {
    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register from address 0x%02X", static_cast<uint8_t>(addr));

    switch (addr) {
        case TPS25751Registers::Address::STATUS:
            return createStatusRegister();
        case TPS25751Registers::Address::MODE:
            return createModeRegister();
        case TPS25751Registers::Address::BOOT_STATUS:
            return createBootFlagsRegister();
        case TPS25751Registers::Address::POWER_PATH_STATUS:
            return createPowerPathStatusRegister();
        case TPS25751Registers::Address::PORT_CONFIGURATION:
            return createPortConfigRegister();
        case TPS25751Registers::Address::TYPEC_STATE:
            return createTypeCStateRegister();
        case TPS25751Registers::Address::INT_EVENT1:
            return createInterruptEventRegister();
        case TPS25751Registers::Address::POWER_STATUS:
            return createPowerStatusRegister();
        case TPS25751Registers::Address::PD_STATUS:
            return createPDStatusRegister();
        case TPS25751Registers::Address::GPIO_STATUS:
            return createGPIOStatusRegister();
        case TPS25751Registers::Address::PORT_CONTROL:
            return createPortControlRegister();
        case TPS25751Registers::Address::ACTIVE_PDO_CONTRACT:
            return createActivePDOContractRegister();
        case TPS25751Registers::Address::ACTIVE_RDO_CONTRACT:
            return createActiveRDOContractRegister();
        case TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES:
            return createReceivedSourceCapsRegister();
        case TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES:
            return createReceivedSinkCapsRegister();
        default:
            TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unsupported register address: 0x%02X", static_cast<uint8_t>(addr));
            return nullptr;
    }
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(TPS25751Registers::Address addr, const uint8_t* data, size_t length) {
    if (!data) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Cannot create register with null data");
        return nullptr;
    }

    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register from address 0x%02X with %u bytes of data",
                   static_cast<uint8_t>(addr), static_cast<unsigned>(length));

    switch (addr) {
        case TPS25751Registers::Address::STATUS:
            return createStatusRegister(data, length);
        case TPS25751Registers::Address::MODE:
            return createModeRegister(data, length);
        case TPS25751Registers::Address::BOOT_STATUS:
            return createBootFlagsRegister(data, length);
        case TPS25751Registers::Address::POWER_PATH_STATUS:
            return createPowerPathStatusRegister(data, length);
        case TPS25751Registers::Address::PORT_CONFIGURATION:
            return createPortConfigRegister(data, length);
        case TPS25751Registers::Address::TYPEC_STATE:
            return createTypeCStateRegister(data, length);
        case TPS25751Registers::Address::INT_EVENT1:
            return createInterruptEventRegister(data, length);
        case TPS25751Registers::Address::POWER_STATUS:
            return createPowerStatusRegister(data, length);
        case TPS25751Registers::Address::PD_STATUS:
            return createPDStatusRegister(data, length);
        case TPS25751Registers::Address::GPIO_STATUS:
            return createGPIOStatusRegister(data, length);
        case TPS25751Registers::Address::PORT_CONTROL:
            return createPortControlRegister(data, length);
        case TPS25751Registers::Address::ACTIVE_PDO_CONTRACT:
            return createActivePDOContractRegister(data, length);
        case TPS25751Registers::Address::ACTIVE_RDO_CONTRACT:
            return createActiveRDOContractRegister(data, length);
        case TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES:
            return createReceivedSourceCapsRegister(data, length);
        case TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES:
            return createReceivedSinkCapsRegister(data, length);
        default:
            TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unsupported register address: 0x%02X", static_cast<uint8_t>(addr));
            return nullptr;
    }
}

std::unique_ptr<TPS25751Register> TPS25751RegisterFactoryImpl::createRegister(const TPS25751Registers::RegisterInfo& regInfo) {
    TPS_DEBUG_TRACE(DEBUG_CAT_REGISTER, "Creating register from RegisterInfo (addr 0x%02X, size %u)",
                   static_cast<uint8_t>(regInfo.address), regInfo.size);

    switch (regInfo.address) {
        case TPS25751Registers::Address::STATUS:
            return std::make_unique<TPS25751Status>(regInfo);
        case TPS25751Registers::Address::MODE:
            return std::make_unique<TPS25751Mode>(regInfo);
        case TPS25751Registers::Address::BOOT_STATUS:
            return std::make_unique<TPS25751BootFlags>(regInfo);
        case TPS25751Registers::Address::POWER_PATH_STATUS:
            return std::make_unique<TPS25751PowerPathStatus>(regInfo);
        case TPS25751Registers::Address::PORT_CONFIGURATION:
            return std::make_unique<TPS25751PortConfig>(regInfo);
        case TPS25751Registers::Address::TYPEC_STATE:
            return std::make_unique<TPS25751TypeCState>(regInfo);
        case TPS25751Registers::Address::INT_EVENT1:
            return std::make_unique<TPS25751InterruptEvent>(regInfo);
        case TPS25751Registers::Address::POWER_STATUS:
            return std::make_unique<TPS25751PowerStatus>(regInfo);
        case TPS25751Registers::Address::PD_STATUS:
            return std::make_unique<TPS25751PDStatus>(regInfo);
        case TPS25751Registers::Address::GPIO_STATUS:
            return std::make_unique<TPS25751GPIOStatus>(regInfo);
        case TPS25751Registers::Address::PORT_CONTROL:
            return std::make_unique<TPS25751PortControl>(regInfo);
        case TPS25751Registers::Address::ACTIVE_PDO_CONTRACT:
            return std::make_unique<TPS25751ActivePDOContract>(regInfo);
        case TPS25751Registers::Address::ACTIVE_RDO_CONTRACT:
            return std::make_unique<TPS25751ActiveRDOContract>(regInfo);
        case TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES:
            return std::make_unique<TPS25751ReceivedSourceCaps>(regInfo);
        case TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES:
            return std::make_unique<TPS25751ReceivedSinkCaps>(regInfo);
        default:
            TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unsupported register address: 0x%02X", static_cast<uint8_t>(regInfo.address));
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
std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(TPS25751Registers::Address addr) {
    return TPS25751Factory::getInstance().createRegister(addr);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(TPS25751Registers::Address addr, const uint8_t* data, size_t length) {
    return TPS25751Factory::getInstance().createRegister(addr, data, length);
}

std::unique_ptr<TPS25751Register> TPS25751RegisterCreator::create(const TPS25751Registers::RegisterInfo& regInfo) {
    return TPS25751Factory::getInstance().createRegister(regInfo);
}