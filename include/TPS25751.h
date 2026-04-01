#ifndef TPS25751_H
#define TPS25751_H

#include <Arduino.h>
#include <Wire.h>
#include "TPS25751.h"
#include "TPS25751Mode.h"
#include "TPS25751Status.h"
#include "TPS25751PowerPathStatus.h"
#include "TPS25751PowerStatus.h"
#include "TPS25751PDStatus.h"
#include "TPS25751BootFlags.h"
#include "TPS25751PortConfig.h"
#include "TPS25751TypeCState.h"
#include "TPS25751InterruptEvent.h"
#include "TPS25751GPIOStatus.h"
#include "TPS25751PortControl.h"
#include "TPS25751ActivePDOContract.h"
#include "TPS25751ActiveRDOContract.h"
#include "TPS25751ReceivedSourceCaps.h"
#include "TPS25751ReceivedSinkCaps.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751RegisterFactory.h"
// #include "TPS25751RegisterBuilder.h"


/*
 * I2C Default Target Address for I2Ct_SCL/SDA
 *
 * Calculation to illustrate address.  See "Table 8-5. I2C Default Target Address for I2Ct_SCL/SDA" in TPS27571 datasheet for more information
 */
constexpr uint8_t TPS25751_I2CT_ADDRESS_IDX[4] = {
    0b0100'0000 >> 1, //  0x20
    0b0100'0010 >> 1, //  0x21
    0b0100'0100 >> 1, //  0x22
    0b0100'0110 >> 1  //  0x23
};

class TPS25751
{
public:
    explicit TPS25751(TwoWire &wire = Wire, uint8_t address = TPS25751_I2CT_ADDRESS_IDX[0]);

    bool begin() const;

    bool readMode(TPS25751Mode &modeOut) const;
    bool readStatus(TPS25751Status &statusOut) const;
    bool readPowerPathStatus(TPS25751PowerPathStatus &pps) const;
    bool readPowerStatus(TPS25751PowerStatus &powerStatus) const;
    bool readPDStatus(TPS25751PDStatus &pdStatus) const;
    bool readBootFlags(TPS25751BootFlags &bootFlags) const;
    bool readPortConfig(TPS25751PortConfig &portConfig) const;
    bool readTypeCState(TPS25751TypeCState &typeCState) const;
    bool readInterruptEvent(TPS25751InterruptEvent &intEvent) const;
    bool readGPIOStatus(TPS25751GPIOStatus &gpioStatus) const;
    bool readPortControl(TPS25751PortControl &portControl) const;
    bool readActivePDOContract(TPS25751ActivePDOContract &pdoContract) const;
    bool readActiveRDOContract(TPS25751ActiveRDOContract &rdoContract) const;
    bool readReceivedSourceCaps(TPS25751ReceivedSourceCaps &sourceCaps) const;
    bool readReceivedSinkCaps(TPS25751ReceivedSinkCaps &sinkCaps) const;

    // Type-safe register reading methods
    bool readRegister(TPS25751Registers::RegisterInfo regInfo, uint8_t *buffer) const;
    bool readRegister(TPS25751Registers::Address addr, uint8_t *buffer, size_t length) const;
    
    template<typename RegisterType>
    bool readRegister(TPS25751Registers::RegisterInfo regInfo, RegisterType &registerOut) const
    {
        uint8_t buffer[regInfo.size];
        if (readRegister(regInfo, buffer)) {
            registerOut = RegisterType(buffer, regInfo.size);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Validates a register after reading it
     * @param reg The register to validate
     * @return true if register data is valid
     */
    template<typename T>
    bool validateRegister(const T& reg) const {
        return reg.isValid() && reg.hasValidData() && reg.isSemanticallyValid();
    }
    
    /**
     * @brief Reads and validates a register in one operation
     * @param regInfo Register information (address and size)
     * @param regOut Register object to populate
     * @return true if read successful and data is valid
     */
    template<typename T>
    bool readAndValidateRegister(TPS25751Registers::RegisterInfo regInfo, T& regOut) const {
        if (readRegister(regInfo, regOut)) {
            if (validateRegister(regOut)) {
                return true;
            }
            // Log validation failure if needed
        }
        return false;
    }
    
    // Debug configuration methods
    /**
     * @brief Configure debug output stream
     * @param stream Output stream for debug messages (nullptr to disable)
     */
    static void setDebugStream(Stream* stream);
    
    /**
     * @brief Set debug verbosity level
     * @param level Debug level (0=none, 1=error, 2=warn, 3=info, 4=debug, 5=trace)
     */
    static void setDebugLevel(uint8_t level);
    
    /**
     * @brief Enable/disable debug categories
     * @param categories Bitfield of categories to enable (see TPS25751Debug.h)
     */
    static void setDebugCategories(uint8_t categories);
    
    // Factory-based register creation methods
    /**
     * @brief Create and read a register using the factory pattern
     * @param type Register type to create and read
     * @return Unique pointer to populated register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Register> readRegisterByType(RegisterType type) const;
    
    /**
     * @brief Create and read a register by address using the factory pattern
     * @param addr Register address
     * @return Unique pointer to populated register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Register> readRegisterByAddress(TPS25751Registers::Address addr) const;
    
    /**
     * @brief Read register data into a factory-created object with validation
     * @param type Register type to create
     * @param validate Whether to perform validation after reading
     * @return Unique pointer to validated register (nullptr if failed or invalid)
     */
    std::unique_ptr<TPS25751Register> readAndValidateRegisterByType(RegisterType type, bool validate = true) const;
    
    /**
     * @brief Generic register reader that creates appropriate register objects
     * @tparam T Target register type (will be cast from base)
     * @param type Register type to create
     * @return Unique pointer to specific register type (nullptr if failed or wrong type)
     */
    template<typename T>
    std::unique_ptr<T> readRegisterAs(RegisterType type) const {
        auto reg = readRegisterByType(type);
        if (!reg) return nullptr;
        
        // Since we control factory creation, we can use static_cast
        if (reg) {
            T* castedReg = static_cast<T*>(reg.release());
            return std::unique_ptr<T>(castedReg);
        }
        return nullptr;
    }
    
    // Type-safe factory-based convenience methods
    /**
     * @brief Read Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Status> readStatusRegister(bool validate = true) const;
    
    /**
     * @brief Read Mode register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Mode register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Mode> readModeRegister(bool validate = true) const;
    
    /**
     * @brief Read Boot Flags register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Boot Flags register (nullptr if failed)
     */
    std::unique_ptr<TPS25751BootFlags> readBootFlagsRegister(bool validate = true) const;
    
    /**
     * @brief Read Power Path Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Power Path Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PowerPathStatus> readPowerPathStatusRegister(bool validate = true) const;
    
    /**
     * @brief Read Port Configuration register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Port Configuration register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PortConfig> readPortConfigRegister(bool validate = true) const;

    /**
     * @brief Read Type C State register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Type C State register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TypeCState> readTypeCStateRegister(bool validate = true) const;

    /**
     * @brief Read Interrupt Event register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Interrupt Event register (nullptr if failed)
     */
    std::unique_ptr<TPS25751InterruptEvent> readInterruptEventRegister(bool validate = true) const;

    /**
     * @brief Read Power Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Power Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PowerStatus> readPowerStatusRegister(bool validate = true) const;

    /**
     * @brief Read PD Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to PD Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PDStatus> readPDStatusRegister(bool validate = true) const;

    /**
     * @brief Read GPIO Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to GPIO Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751GPIOStatus> readGPIOStatusRegister(bool validate = true) const;

    /**
     * @brief Read Port Control register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Port Control register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PortControl> readPortControlRegister(bool validate = true) const;

    /**
     * @brief Read Active PDO Contract register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Active PDO Contract register (nullptr if failed)
     */
    std::unique_ptr<TPS25751ActivePDOContract> readActivePDOContractRegister(bool validate = true) const;

    /**
     * @brief Read Active RDO Contract register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Active RDO Contract register (nullptr if failed)
     */
    std::unique_ptr<TPS25751ActiveRDOContract> readActiveRDOContractRegister(bool validate = true) const;

    /**
     * @brief Read Received Source Capabilities register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Received Source Capabilities register (nullptr if failed)
     */
    std::unique_ptr<TPS25751ReceivedSourceCaps> readReceivedSourceCapsRegister(bool validate = true) const;

    /**
     * @brief Read Received Sink Capabilities register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Received Sink Capabilities register (nullptr if failed)
     */
    std::unique_ptr<TPS25751ReceivedSinkCaps> readReceivedSinkCapsRegister(bool validate = true) const;

private:
    TwoWire &_wire;
    uint8_t _address;

    bool readRegister(uint8_t regAddr, uint8_t *buffer, size_t length) const;
};

#endif // TPS25751_H
