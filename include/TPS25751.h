#ifndef TPS25751_H
#define TPS25751_H

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
#include "TPS25751Command.h"
#include "TPS25751Data.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751RegisterFactory.h"
#include "TPS25751Task.h"


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

    // Type-safe register writing methods (mirror the read overloads above)
    bool writeRegister(TPS25751Registers::Address addr, const uint8_t *data, size_t length) const;
    bool writeRegister(TPS25751Registers::RegisterInfo regInfo, const uint8_t *data) const;

    template<typename RegisterT>
    bool readRegister(TPS25751Registers::RegisterInfo regInfo, RegisterT &registerOut) const
    {
        uint8_t buffer[regInfo.size];
        if (readRegister(regInfo, buffer)) {
            registerOut = RegisterT(buffer, regInfo.size);
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
     * @brief Create and read a register by address using the factory pattern
     * @param addr Register address
     * @return Unique pointer to populated register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Register> readRegisterByAddress(TPS25751Registers::Address addr) const;

    /**
     * @brief Read register data into a factory-created object with validation
     * @param addr Register address to create and read
     * @param validate Whether to perform validation after reading
     * @return Unique pointer to validated register (nullptr if failed or invalid)
     */
    std::unique_ptr<TPS25751Register> readAndValidateRegisterByAddress(TPS25751Registers::Address addr, bool validate = true) const;

    /**
     * @brief Generic register reader that creates appropriate register objects
     * @tparam T Target register type (will be cast from base)
     * @param addr Register address to create
     * @return Unique pointer to specific register type (nullptr if failed or wrong type)
     */
    template<typename T>
    std::unique_ptr<T> readRegisterAs(TPS25751Registers::Address addr) const {
        auto reg = readRegisterByAddress(addr);
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

    /**
     * @brief Read Command register (0x08) using factory pattern
     *
     * Note: COMMAND is typically transient (write a 4CC, poll until it clears
     * or rejects via executeCommand()). This convenience read is provided for
     * symmetry/debugging — under normal use prefer executeCommand().
     *
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Command register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Command> readCommandRegister(bool validate = true) const;

    /**
     * @brief Read Data register (0x09) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Data register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Data> readDataRegister(bool validate = true) const;

    /**
     * @brief Execute a 4CC command-task via the COMMAND/DATA interface (TRM Sec 4.4.2)
     *
     * Writes @p inData to DATA (if provided), writes @p cmd to COMMAND, then polls
     * COMMAND until the TPS25751 clears it (success), replaces it with "!CMD"
     * (rejected/unrecognized), or @p timeoutMs elapses. On success, the full 64-byte
     * DATA register is read back; @p outData is filled from DATA byte 0 (so
     * outData[0] is the task return code) up to min(outLen, 64) bytes. Task-specific
     * output layouts (e.g. I2Cr's read bytes starting at DATA offset 1) are unpacked
     * by the caller.
     *
     * @param cmd       4-character command code (e.g. TPS25751FourCC::of("I2Cr"))
     * @param inData    Optional input payload written to DATA before COMMAND (nullptr to skip)
     * @param inLen     Length of inData in bytes (0-64)
     * @param outData   Optional buffer to receive DATA after completion (nullptr to skip)
     * @param outLen    Size of outData in bytes
     * @param timeoutMs Maximum time to wait for COMMAND to clear/reject
     * @return Result containing the completion status and the task return code
     */
    TPS25751TaskResult executeCommand(
        TPS25751FourCC cmd,
        const uint8_t* inData = nullptr, size_t inLen = 0,
        uint8_t* outData = nullptr, size_t outLen = 0,
        uint32_t timeoutMs = 200) const;

private:
    TwoWire &_wire;
    uint8_t _address;

    bool readRegister(uint8_t regAddr, uint8_t *buffer, size_t length) const;
    bool writeRegister(uint8_t regAddr, const uint8_t *data, size_t length) const;

    /**
     * @brief Poll COMMAND until it clears (success) or rejects ("!CMD")
     *
     * Isolated from executeCommand() so a future interrupt-driven variant can
     * replace just this helper without touching the DATA staging/encode-decode
     * logic (see ARCHITECTURE.md ADR-007 / the IRQ deferral rationale).
     *
     * @param timeoutMs Maximum time to wait
     * @return Success, Rejected, Timeout, or I2CError
     */
    TPS25751TaskStatus waitForCommandClear(uint32_t timeoutMs) const;
};

#endif // TPS25751_H
