#ifndef TPS25751REGISTERS_H
#define TPS25751REGISTERS_H

#include <cstdint>

// Type-safe register definitions
namespace TPS25751Registers {

/**
 * @brief Strongly typed enum for TPS25751 register addresses
 *
 * This is the single source of truth for register identity throughout the
 * library; the factory dispatches register-object creation directly on it.
 */
enum class Address : uint8_t {
    MODE = 0x03,
    CUSTOMER_USE = 0x06,
    COMMAND = 0x08,
    DATA = 0x09,
    INT_EVENT1 = 0x14,
    INT_MASK1 = 0x16,
    INT_CLEAR1 = 0x18,
    STATUS = 0x1A,
    POWER_PATH_STATUS = 0x26,
    PORT_CONFIGURATION = 0x28,
    PORT_CONTROL = 0x29,
    BOOT_STATUS = 0x2D,
    RECEIVED_SOURCE_CAPABILITIES = 0x30,
    RECEIVED_SINK_CAPABILITIES = 0x31,
    TRANSMIT_SOURCE_CAPABILITIES = 0x32,
    TRANSMIT_SINK_CAPABILITIES = 0x33,
    ACTIVE_PDO_CONTRACT = 0x34,
    ACTIVE_RDO_CONTRACT = 0x35,
    AUTONEGOTIATE_SINK = 0x37,
    POWER_STATUS = 0x3F,
    PD_STATUS = 0x40,
    IO_CONFIG = 0x5C,
    TYPEC_STATE = 0x69,
    ADC_RESULTS = 0x6A,
    SLEEP_CONTROL = 0x70,
    GPIO_STATUS = 0x72,
    LIQUID_DETECTION_CONFIG = 0x98
};

/**
 * @brief Register information structure containing address and size
 */
struct RegisterInfo {
    Address address;
    uint8_t size;
    
    constexpr RegisterInfo(Address addr, uint8_t sz) : address(addr), size(sz) {}
    
    // Implicit conversion to address for backward compatibility
    constexpr operator uint8_t() const { 
        return static_cast<uint8_t>(address); 
    }
};

// Type-safe register constants with compile-time address/size pairing
namespace Registers {
    constexpr RegisterInfo MODE{Address::MODE, 4};
    constexpr RegisterInfo CUSTOMER_USE{Address::CUSTOMER_USE, 8};
    constexpr RegisterInfo COMMAND{Address::COMMAND, 4};
    constexpr RegisterInfo DATA{Address::DATA, 64};
    constexpr RegisterInfo INT_EVENT1{Address::INT_EVENT1, 11};
    constexpr RegisterInfo INT_MASK1{Address::INT_MASK1, 11};
    constexpr RegisterInfo INT_CLEAR1{Address::INT_CLEAR1, 11};
    constexpr RegisterInfo STATUS{Address::STATUS, 5};
    constexpr RegisterInfo POWER_PATH_STATUS{Address::POWER_PATH_STATUS, 5};
    constexpr RegisterInfo PORT_CONFIGURATION{Address::PORT_CONFIGURATION, 17};
    constexpr RegisterInfo PORT_CONTROL{Address::PORT_CONTROL, 4};
    constexpr RegisterInfo BOOT_STATUS{Address::BOOT_STATUS, 5};
    constexpr RegisterInfo RECEIVED_SOURCE_CAPABILITIES{Address::RECEIVED_SOURCE_CAPABILITIES, 53};
    constexpr RegisterInfo RECEIVED_SINK_CAPABILITIES{Address::RECEIVED_SINK_CAPABILITIES, 53};
    constexpr RegisterInfo TRANSMIT_SOURCE_CAPABILITIES{Address::TRANSMIT_SOURCE_CAPABILITIES, 63};
    constexpr RegisterInfo TRANSMIT_SINK_CAPABILITIES{Address::TRANSMIT_SINK_CAPABILITIES, 53};
    constexpr RegisterInfo ACTIVE_PDO_CONTRACT{Address::ACTIVE_PDO_CONTRACT, 6};
    constexpr RegisterInfo ACTIVE_RDO_CONTRACT{Address::ACTIVE_RDO_CONTRACT, 12};
    constexpr RegisterInfo AUTONEGOTIATE_SINK{Address::AUTONEGOTIATE_SINK, 24};
    constexpr RegisterInfo POWER_STATUS{Address::POWER_STATUS, 2};
    constexpr RegisterInfo PD_STATUS{Address::PD_STATUS, 4};
    constexpr RegisterInfo IO_CONFIG{Address::IO_CONFIG, 49};
    constexpr RegisterInfo TYPEC_STATE{Address::TYPEC_STATE, 4};
    constexpr RegisterInfo ADC_RESULTS{Address::ADC_RESULTS, 13};
    constexpr RegisterInfo SLEEP_CONTROL{Address::SLEEP_CONTROL, 1};
    constexpr RegisterInfo GPIO_STATUS{Address::GPIO_STATUS, 8};
    constexpr RegisterInfo LIQUID_DETECTION_CONFIG{Address::LIQUID_DETECTION_CONFIG, 11};
}

/**
 * @brief Utility function to get register size from address
 * @param addr Register address
 * @return Register size in bytes, or 0 if unknown
 */
constexpr uint8_t getRegisterSize(Address addr) {
    switch (addr) {
        case Address::MODE: return Registers::MODE.size;
        case Address::CUSTOMER_USE: return Registers::CUSTOMER_USE.size;
        case Address::COMMAND: return Registers::COMMAND.size;
        case Address::DATA: return Registers::DATA.size;
        case Address::INT_EVENT1: return Registers::INT_EVENT1.size;
        case Address::INT_MASK1: return Registers::INT_MASK1.size;
        case Address::INT_CLEAR1: return Registers::INT_CLEAR1.size;
        case Address::STATUS: return Registers::STATUS.size;
        case Address::POWER_PATH_STATUS: return Registers::POWER_PATH_STATUS.size;
        case Address::PORT_CONFIGURATION: return Registers::PORT_CONFIGURATION.size;
        case Address::PORT_CONTROL: return Registers::PORT_CONTROL.size;
        case Address::BOOT_STATUS: return Registers::BOOT_STATUS.size;
        case Address::RECEIVED_SOURCE_CAPABILITIES: return Registers::RECEIVED_SOURCE_CAPABILITIES.size;
        case Address::RECEIVED_SINK_CAPABILITIES: return Registers::RECEIVED_SINK_CAPABILITIES.size;
        case Address::TRANSMIT_SOURCE_CAPABILITIES: return Registers::TRANSMIT_SOURCE_CAPABILITIES.size;
        case Address::TRANSMIT_SINK_CAPABILITIES: return Registers::TRANSMIT_SINK_CAPABILITIES.size;
        case Address::ACTIVE_PDO_CONTRACT: return Registers::ACTIVE_PDO_CONTRACT.size;
        case Address::ACTIVE_RDO_CONTRACT: return Registers::ACTIVE_RDO_CONTRACT.size;
        case Address::AUTONEGOTIATE_SINK: return Registers::AUTONEGOTIATE_SINK.size;
        case Address::POWER_STATUS: return Registers::POWER_STATUS.size;
        case Address::PD_STATUS: return Registers::PD_STATUS.size;
        case Address::IO_CONFIG: return Registers::IO_CONFIG.size;
        case Address::TYPEC_STATE: return Registers::TYPEC_STATE.size;
        case Address::ADC_RESULTS: return Registers::ADC_RESULTS.size;
        case Address::SLEEP_CONTROL: return Registers::SLEEP_CONTROL.size;
        case Address::GPIO_STATUS: return Registers::GPIO_STATUS.size;
        case Address::LIQUID_DETECTION_CONFIG: return Registers::LIQUID_DETECTION_CONFIG.size;
        default: return 0;
    }
}

/**
 * @brief Get register information for a given address
 * @param addr Register address
 * @return Pointer to RegisterInfo if found, nullptr otherwise
 */
inline const RegisterInfo* getRegisterInfo(Address addr) {
    switch (addr) {
        case Address::STATUS: return &Registers::STATUS;
        case Address::MODE: return &Registers::MODE;
        case Address::BOOT_STATUS: return &Registers::BOOT_STATUS;
        case Address::POWER_PATH_STATUS: return &Registers::POWER_PATH_STATUS;
        case Address::PORT_CONFIGURATION: return &Registers::PORT_CONFIGURATION;
        case Address::COMMAND: return &Registers::COMMAND;
        case Address::DATA: return &Registers::DATA;
        case Address::INT_EVENT1: return &Registers::INT_EVENT1;
        case Address::INT_MASK1: return &Registers::INT_MASK1;
        case Address::INT_CLEAR1: return &Registers::INT_CLEAR1;
        case Address::PORT_CONTROL: return &Registers::PORT_CONTROL;
        case Address::RECEIVED_SOURCE_CAPABILITIES: return &Registers::RECEIVED_SOURCE_CAPABILITIES;
        case Address::RECEIVED_SINK_CAPABILITIES: return &Registers::RECEIVED_SINK_CAPABILITIES;
        case Address::TRANSMIT_SOURCE_CAPABILITIES: return &Registers::TRANSMIT_SOURCE_CAPABILITIES;
        case Address::TRANSMIT_SINK_CAPABILITIES: return &Registers::TRANSMIT_SINK_CAPABILITIES;
        case Address::ACTIVE_PDO_CONTRACT: return &Registers::ACTIVE_PDO_CONTRACT;
        case Address::ACTIVE_RDO_CONTRACT: return &Registers::ACTIVE_RDO_CONTRACT;
        case Address::AUTONEGOTIATE_SINK: return &Registers::AUTONEGOTIATE_SINK;
        case Address::POWER_STATUS: return &Registers::POWER_STATUS;
        case Address::PD_STATUS: return &Registers::PD_STATUS;
        case Address::IO_CONFIG: return &Registers::IO_CONFIG;
        case Address::TYPEC_STATE: return &Registers::TYPEC_STATE;
        case Address::ADC_RESULTS: return &Registers::ADC_RESULTS;
        case Address::SLEEP_CONTROL: return &Registers::SLEEP_CONTROL;
        case Address::GPIO_STATUS: return &Registers::GPIO_STATUS;
        case Address::LIQUID_DETECTION_CONFIG: return &Registers::LIQUID_DETECTION_CONFIG;
        default: return nullptr;
    }
}

} // namespace TPS25751Registers


#endif // TPS25751REGISTERS_H
