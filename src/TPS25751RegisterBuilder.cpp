/*#include "TPS25751RegisterBuilder.h"

// Status register creation methods
std::unique_ptr<TPS25751Status> TPS25751RegisterBuilder::createStatus() {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::STATUS);
    return safeCast<TPS25751Status>(std::move(reg));
}

std::unique_ptr<TPS25751Status> TPS25751RegisterBuilder::createStatus(const uint8_t* data, size_t length) {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::STATUS, data, length);
    return safeCast<TPS25751Status>(std::move(reg));
}

std::unique_ptr<TPS25751Status> TPS25751RegisterBuilder::createStatus(const TPS25751Registers::RegisterInfo& regInfo) {
    auto reg = TPS25751Factory::getInstance().createRegister(regInfo);
    return safeCast<TPS25751Status>(std::move(reg));
}

// Mode register creation methods
std::unique_ptr<TPS25751Mode> TPS25751RegisterBuilder::createMode() {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::MODE);
    return safeCast<TPS25751Mode>(std::move(reg));
}

std::unique_ptr<TPS25751Mode> TPS25751RegisterBuilder::createMode(const uint8_t* data, size_t length) {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::MODE, data, length);
    return safeCast<TPS25751Mode>(std::move(reg));
}

std::unique_ptr<TPS25751Mode> TPS25751RegisterBuilder::createMode(const TPS25751Registers::RegisterInfo& regInfo) {
    auto reg = TPS25751Factory::getInstance().createRegister(regInfo);
    return safeCast<TPS25751Mode>(std::move(reg));
}

// Boot Flags register creation methods
std::unique_ptr<TPS25751BootFlags> TPS25751RegisterBuilder::createBootFlags() {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::BOOT_STATUS);
    return safeCast<TPS25751BootFlags>(std::move(reg));
}

std::unique_ptr<TPS25751BootFlags> TPS25751RegisterBuilder::createBootFlags(const uint8_t* data, size_t length) {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::BOOT_STATUS, data, length);
    return safeCast<TPS25751BootFlags>(std::move(reg));
}

std::unique_ptr<TPS25751BootFlags> TPS25751RegisterBuilder::createBootFlags(const TPS25751Registers::RegisterInfo& regInfo) {
    auto reg = TPS25751Factory::getInstance().createRegister(regInfo);
    return safeCast<TPS25751BootFlags>(std::move(reg));
}

// Power Path Status register creation methods
std::unique_ptr<TPS25751PowerPathStatus> TPS25751RegisterBuilder::createPowerPathStatus() {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::POWER_PATH_STATUS);
    return safeCast<TPS25751PowerPathStatus>(std::move(reg));
}

std::unique_ptr<TPS25751PowerPathStatus> TPS25751RegisterBuilder::createPowerPathStatus(const uint8_t* data, size_t length) {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::POWER_PATH_STATUS, data, length);
    return safeCast<TPS25751PowerPathStatus>(std::move(reg));
}

std::unique_ptr<TPS25751PowerPathStatus> TPS25751RegisterBuilder::createPowerPathStatus(const TPS25751Registers::RegisterInfo& regInfo) {
    auto reg = TPS25751Factory::getInstance().createRegister(regInfo);
    return safeCast<TPS25751PowerPathStatus>(std::move(reg));
}

// Port Configuration register creation methods
std::unique_ptr<TPS25751PortConfig> TPS25751RegisterBuilder::createPortConfig() {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::PORT_CONFIGURATION);
    return safeCast<TPS25751PortConfig>(std::move(reg));
}

std::unique_ptr<TPS25751PortConfig> TPS25751RegisterBuilder::createPortConfig(const uint8_t* data, size_t length) {
    auto reg = TPS25751Factory::getInstance().createRegister(TPS25751Registers::Address::PORT_CONFIGURATION, data, length);
    return safeCast<TPS25751PortConfig>(std::move(reg));
}

std::unique_ptr<TPS25751PortConfig> TPS25751RegisterBuilder::createPortConfig(const TPS25751Registers::RegisterInfo& regInfo) {
    auto reg = TPS25751Factory::getInstance().createRegister(regInfo);
    return safeCast<TPS25751PortConfig>(std::move(reg));
}*/