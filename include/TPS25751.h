#ifndef TPS25751_H
#define TPS25751_H

#include <Wire.h>
#include "TPS25751.h"  // guard prevents re-entry (intentional no-op self-include)
#include "TPS25751Mode.h"
#include "TPS25751CustomerUse.h"
#include "TPS25751Command.h"
#include "TPS25751Data.h"
#include "TPS25751Version.h"
#include "TPS25751InterruptEvent.h"
#include "TPS25751InterruptMask.h"
#include "TPS25751InterruptClear.h"
#include "TPS25751Status.h"
#include "TPS25751PowerPathStatus.h"
#include "TPS25751GlobalSystemConfig.h"
#include "TPS25751PortConfig.h"
#include "TPS25751PortControl.h"
#include "TPS25751BootFlags.h"
#include "TPS25751ReceivedSourceCaps.h"
#include "TPS25751ReceivedSinkCaps.h"
#include "TPS25751TransmitSourceCaps.h"
#include "TPS25751TransmitSinkCaps.h"
#include "TPS25751ActivePDOContract.h"
#include "TPS25751ActiveRDOContract.h"
#include "TPS25751AutoNegotiateSink.h"
#include "TPS25751PowerStatus.h"
#include "TPS25751PDStatus.h"
#include "TPS25751PD3Config.h"
#include "TPS25751ReceivedSOPIdentity.h"
#include "TPS25751IOConfig.h"
#include "TPS25751TypeCState.h"
#include "TPS25751ADCResults.h"
#include "TPS25751SleepControl.h"
#include "TPS25751GPIOStatus.h"
#include "TPS25751TransmitSourceCapsExtended.h"
#include "TPS25751TransmitSourceInfo.h"
#include "TPS25751TransmitPPSStatus.h"
#include "TPS25751TransmitBatteryStatus.h"
#include "TPS25751TransmitBatteryCapabilities.h"
#include "TPS25751TransmitSinkCapsExtended.h"
#include "TPS25751LiquidDetectionConfig.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751RegisterFactory.h"
#include "TPS25751Task.h"


/**
 * @brief Default chunk size (bytes) for TPS25751::burstWrite() PBM bursts.
 *
 * The TPS25751 Patch Burst Mode pointer auto-increments across I2C START/STOP
 * boundaries (SLVAFV8A footnote 3), so a single large image can be split into
 * many small transactions.  This constant sets the default number of image bytes
 * per transaction.  32 bytes matches the stock Arduino/Teensy Wire TX buffer
 * (BUFFER_LENGTH); raise it here if your project sets a larger buffer.
 *
 * Used as the default for the @p chunk parameter of TPS25751::burstWrite() and
 * referenced by TPS25751PatchLoader.
 */
constexpr size_t PBM_BURST_CHUNK = 32;

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
    bool readCustomerUse(TPS25751CustomerUse &customerUse) const;

    /**
     * @brief Read the COMMAND register (0x08)
     *
     * COMMAND is transient: executeCommand() writes a 4CC and polls it until it
     * clears or reads "!CMD". This read is for inspection and debugging.
     *
     * @param command Register object to populate (4 bytes)
     * @return true if the read succeeded
     */
    bool readCommand(TPS25751Command &command) const;

    /**
     * @brief Read the DATA register (0x09)
     * @param data Register object to populate (64 bytes)
     * @return true if the read succeeded
     */
    bool readData(TPS25751Data &data) const;

    /**
     * @brief Read the VERSION register (0x0F)
     * @param version Register object to populate (4 bytes)
     * @return true if the read succeeded
     */
    bool readVersion(TPS25751Version &version) const;

    bool readInterruptEvent(TPS25751InterruptEvent &intEvent) const;

    /**
     * @brief Read the Interrupt Mask register (0x16)
     * @param interruptMask Register object to populate
     * @return true if the read succeeded
     */
    bool readInterruptMask(TPS25751InterruptMask &interruptMask) const;

    /**
     * @brief Read the Interrupt Clear register (0x18)
     *
     * The library writes INT_CLEAR1 to acknowledge INT_EVENT1 sources; the TRM
     * marks it R/W, so it can be read back.
     *
     * @param interruptClear Register object to populate (11 bytes)
     * @return true if the read succeeded
     */
    bool readInterruptClear(TPS25751InterruptClear &interruptClear) const;

    bool readStatus(TPS25751Status &statusOut) const;
    bool readPowerPathStatus(TPS25751PowerPathStatus &pps) const;

    /**
     * @brief Read the Global System Configuration register (0x27)
     * @param globalSystemConfig Register object to populate (14 bytes)
     * @return true if the read succeeded
     */
    bool readGlobalSystemConfig(TPS25751GlobalSystemConfig &globalSystemConfig) const;

    bool readPortConfig(TPS25751PortConfig &portConfig) const;
    bool readPortControl(TPS25751PortControl &portControl) const;
    bool readBootFlags(TPS25751BootFlags &bootFlags) const;
    bool readReceivedSourceCaps(TPS25751ReceivedSourceCaps &sourceCaps) const;
    bool readReceivedSinkCaps(TPS25751ReceivedSinkCaps &sinkCaps) const;

    /**
     * @brief Read the Transmit Source Capabilities register (0x32)
     * @param sourceCaps Register object to populate
     * @return true if the read succeeded
     */
    bool readTransmitSourceCaps(TPS25751TransmitSourceCaps &sourceCaps) const;

    /**
     * @brief Read the Transmit Sink Capabilities register (0x33)
     * @param sinkCaps Register object to populate
     * @return true if the read succeeded
     */
    bool readTransmitSinkCaps(TPS25751TransmitSinkCaps &sinkCaps) const;

    bool readActivePDOContract(TPS25751ActivePDOContract &pdoContract) const;
    bool readActiveRDOContract(TPS25751ActiveRDOContract &rdoContract) const;

    /**
     * @brief Read the Autonegotiate Sink register (0x37)
     * @param autoNegSink Register object to populate
     * @return true if the read succeeded
     */
    bool readAutoNegotiateSink(TPS25751AutoNegotiateSink &autoNegSink) const;

    bool readPowerStatus(TPS25751PowerStatus &powerStatus) const;
    bool readPDStatus(TPS25751PDStatus &pdStatus) const;

    /**
     * @brief Read the PD3 Configuration register (0x42)
     * @param pd3Config Register object to populate
     * @return true if the read succeeded
     */
    bool readPD3Config(TPS25751PD3Config &pd3Config) const;

    /**
     * @brief Read the Received SOP Identity Data Object register (0x48)
     * @param sopIdentity Register object to populate (25 bytes)
     * @return true if the read succeeded
     */
    bool readReceivedSOPIdentity(TPS25751ReceivedSOPIdentity &sopIdentity) const;

    /**
     * @brief Read the IO Config register (0x5C)
     *
     * IO_CONFIG cannot be modified at run-time -- there is no corresponding
     * write method.
     *
     * @param ioConfig Register object to populate
     * @return true if the read succeeded
     */
    bool readIOConfig(TPS25751IOConfig &ioConfig) const;

    bool readTypeCState(TPS25751TypeCState &typeCState) const;

    /**
     * @brief Read the ADC Results register (0x6A)
     *
     * Note: the PD controller does not update ADC_RESULTS while in
     * Unconnected Sleep mode, so a stale/all-zero read in that state is
     * expected and not, by itself, an error.
     *
     * @param adcResults Register object to populate
     * @return true if the read succeeded
     */
    bool readADCResults(TPS25751ADCResults &adcResults) const;

    bool readSleepControl(TPS25751SleepControl &sleepControl) const;
    bool readGPIOStatus(TPS25751GPIOStatus &gpioStatus) const;

    /**
     * @brief Read the TX Source Capabilities Extended register (0x77)
     * @param caps Register object to populate (15 bytes)
     * @return true if the read succeeded
     */
    bool readTransmitSourceCapsExtended(TPS25751TransmitSourceCapsExtended &caps) const;

    /**
     * @brief Read the TX Source Info register (0x78)
     * @param sourceInfo Register object to populate (8 bytes)
     * @return true if the read succeeded
     */
    bool readTransmitSourceInfo(TPS25751TransmitSourceInfo &sourceInfo) const;

    /**
     * @brief Read the Transmitted PPS Status Data Block register (0x7A)
     * @param ppsStatus Register object to populate (4 bytes)
     * @return true if the read succeeded
     */
    bool readTransmitPPSStatus(TPS25751TransmitPPSStatus &ppsStatus) const;

    /**
     * @brief Read the Transmitted Battery Status Data Objects register (0x7B)
     * @param status Register object to populate (16 bytes)
     * @return true if the read succeeded
     */
    bool readTransmitBatteryStatus(TPS25751TransmitBatteryStatus &status) const;

    /**
     * @brief Read the TX Battery Capabilities register (0x7D)
     * @param caps Register object to populate (36 bytes)
     * @return true if the read succeeded
     */
    bool readTransmitBatteryCapabilities(TPS25751TransmitBatteryCapabilities &caps) const;

    /**
     * @brief Read the Transmit Sink Capabilities Extended register (0x7E)
     * @param caps Register object to populate (14 bytes)
     * @return true if the read succeeded
     */
    bool readTransmitSinkCapsExtended(TPS25751TransmitSinkCapsExtended &caps) const;

    /**
     * @brief Read the Liquid Detection Config register (0x98)
     * @param liquidDetectionConfig Register object to populate
     * @return true if the read succeeded
     */
    bool readLiquidDetectionConfig(TPS25751LiquidDetectionConfig &liquidDetectionConfig) const;

    /**
     * @brief Write the Interrupt Mask register (0x16)
     * @param interruptMask Register object holding the bytes to write
     * @return false if @p interruptMask is not valid or not the expected
     *         size; otherwise the result of the underlying I2C write
     */
    bool writeInterruptMask(const TPS25751InterruptMask &interruptMask) const;

    /**
     * @brief Write the Interrupt Clear register (0x18)
     *
     * The TRM marks INT_CLEAR1 R/W (SLVUCR8B p.23), but the library only writes
     * it: there is no readInterruptClear(), because read-back carries no event
     * state.
     *
     * @param interruptClear Register object holding the staged clear bits to write
     * @return false if @p interruptClear is not valid or not the expected
     *         size; otherwise the result of the underlying I2C write
     */
    bool writeInterruptClear(const TPS25751InterruptClear &interruptClear) const;

    /**
     * @brief Write the Global System Configuration register (0x27)
     *
     * TRM: "Any modifications to this register will cause a port disconnect and
     * reconnect with the new settings." PP1 and PP3 describe the board's
     * power-switch wiring, and a wrong PP3 role can back-drive the power path.
     * The firmware stores undocumented values in bits the TRM lists as
     * reserved, so start from a fresh readGlobalSystemConfig() and change only
     * the fields you mean to. Writing a default-constructed object would clear
     * those bits as well as rewrite every field.
     *
     * @param globalSystemConfig Register object holding the bytes to write
     * @return false if @p globalSystemConfig is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeGlobalSystemConfig(const TPS25751GlobalSystemConfig &globalSystemConfig) const;

    /**
     * @brief Write the Transmit Source Capabilities register (0x32)
     * @param sourceCaps Register object holding the bytes to write
     * @return false if @p sourceCaps is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitSourceCaps(const TPS25751TransmitSourceCaps &sourceCaps) const;

    /**
     * @brief Write the Transmit Sink Capabilities register (0x33)
     * @param sinkCaps Register object holding the bytes to write
     * @return false if @p sinkCaps is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitSinkCaps(const TPS25751TransmitSinkCaps &sinkCaps) const;

    /**
     * @brief Write the Autonegotiate Sink register (0x37)
     * @param autoNegSink Register object holding the bytes to write
     * @return false if @p autoNegSink is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeAutoNegotiateSink(const TPS25751AutoNegotiateSink &autoNegSink) const;

    /**
     * @brief Write the PD3 Configuration register (0x42)
     * @param pd3Config Register object holding the bytes to write
     * @return false if @p pd3Config is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writePD3Config(const TPS25751PD3Config &pd3Config) const;

    /**
     * @brief Write the Sleep Control register (0x70)
     * @param sleepControl Register object holding the bytes to write
     * @return false if @p sleepControl is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeSleepControl(const TPS25751SleepControl &sleepControl) const;

    /**
     * @brief Write the TX Source Capabilities Extended (0x77) register
     *
     * The controller builds its Get_Source_Cap_Extended reply from this register
     * only while PD3_CONFIGURATION (0x42) bit 8
     * (TPS25751PD3Config::supportSourceExtended()) is set; with it clear the controller
     * answers Get_Source_Cap_Extended with Not_Supported.
     *
     * @param caps Register object holding the bytes to write
     * @return false if @p caps is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitSourceCapsExtended(const TPS25751TransmitSourceCapsExtended &caps) const;

    /**
     * @brief Write the TX Source Info register (0x78)
     *
     * TRM: "This register is automatically updated by the PD firmware and does
     * not require any EC implementation." The firmware may overwrite what you
     * write: it has rewritten the lower word when the port attached as source.
     * The configuration EEPROM writes it at boot, and it does not follow a
     * runtime write to TRANSMIT_SOURCE_CAPABILITIES (0x32); CONSTRAINTS.md,
     * TX_SOURCE_INFO, has the bench evidence. The controller sends this register's contents in reply to
     * Get_Source_Info only while PD3_CONFIGURATION (0x42) bit 18
     * (TPS25751PD3Config::supportGetSourceInfo()) is set; with it clear the
     * controller answers Not_Supported.
     *
     * @param sourceInfo Register object holding the bytes to write
     * @return false if @p sourceInfo is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitSourceInfo(const TPS25751TransmitSourceInfo &sourceInfo) const;

    /**
     * @brief Write the Transmitted PPS Status Data Block register (0x7A)
     *
     * The controller sends this register's contents in reply to Get_PPS_Status
     * only while PD3_CONFIGURATION (0x42) bit 20
     * (TPS25751PD3Config::supportPpsStatus()) is set; with it clear the
     * controller answers Not_Supported. The TRM does not say the firmware
     * updates this register, so the host supplies the PPS status it wants
     * reported.
     *
     * @param ppsStatus Register object holding the bytes to write
     * @return false if @p ppsStatus is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitPPSStatus(const TPS25751TransmitPPSStatus &ppsStatus) const;

    /**
     * @brief Write the Transmitted Battery Status Data Objects (BSDO) (0x7B) register
     *
     * The controller builds its Get_Battery_Status reply from this register
     * only while PD3_CONFIGURATION (0x42) bit 11
     * (TPS25751PD3Config::supportBatteryStatus()) is set; with it clear the controller
     * answers Get_Battery_Status with Not_Supported. The controller takes no automatic
     * action when this register is written.
     *
     * @param status Register object holding the bytes to write
     * @return false if @p status is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitBatteryStatus(const TPS25751TransmitBatteryStatus &status) const;

    /**
     * @brief Write the Tx Battery Capabilities Data Block (0x7D) register
     *
     * The controller builds its Get_Battery_Capabilities reply from this register
     * only while PD3_CONFIGURATION (0x42) bit 10
     * (TPS25751PD3Config::supportBatteryCapabilities()) is set; with it clear the controller
     * answers Get_Battery_Capabilities with Not_Supported. The controller takes no automatic
     * action when this register is written.
     *
     * @param caps Register object holding the bytes to write
     * @return false if @p caps is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitBatteryCapabilities(const TPS25751TransmitBatteryCapabilities &caps) const;

    /**
     * @brief Write the Transmit Sink Capabilities Extended (0x7E) register
     *
     * The controller builds its Get_Sink_Cap_Extended reply from this register
     * only while PD3_CONFIGURATION (0x42) bit 17
     * (TPS25751PD3Config::supportSinkCapExtended()) is set; with it clear the controller
     * answers Get_Sink_Cap_Extended with Not_Supported.
     *
     * @param caps Register object holding the bytes to write
     * @return false if @p caps is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writeTransmitSinkCapsExtended(const TPS25751TransmitSinkCapsExtended &caps) const;

    /**
     * @brief Write the Liquid Detection Config register (0x98)
     * @param liquidDetectionConfig Register object holding the bytes to write
     * @return false if @p liquidDetectionConfig is not valid or not the
     *         expected size; otherwise the result of the underlying I2C write
     */
    bool writeLiquidDetectionConfig(const TPS25751LiquidDetectionConfig &liquidDetectionConfig) const;

    /**
     * @brief Write the Port Configuration register (0x28)
     *
     * TRM SLVUCR8B p.28/29: "Any modifications to this register will cause a
     * port disconnect and reconnect with the new settings." The register
     * configures port-specific hardware (Type-C state machine, OVP and UVP
     * thresholds), so a wrong value can drop the contract or change the port's
     * role. The firmware stores undocumented values in bits the TRM lists as
     * reserved, so start from a fresh readPortConfig() and change only the
     * fields you mean to.
     *
     * @param portConfig Register object holding the bytes to write
     * @return false if @p portConfig is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writePortConfig(const TPS25751PortConfig &portConfig) const;

    /**
     * @brief Write the Port Control register (0x29)
     *
     * TRM SLVUCR8B p.31: the controller takes no immediate action on a write;
     * the change takes effect the next time the governing policy runs (for
     * example the next swap request or the next Type-C current advertisement).
     * Start from a fresh readPortControl() and change only the fields you mean
     * to, because the firmware may set bits the TRM lists as reserved.
     *
     * @param portControl Register object holding the bytes to write
     * @return false if @p portControl is not valid or not the expected size;
     *         otherwise the result of the underlying I2C write
     */
    bool writePortControl(const TPS25751PortControl &portControl) const;

    // Type-safe register reading methods
    bool readRegister(TPS25751Registers::RegisterInfo regInfo, uint8_t *buffer) const;
    bool readRegister(TPS25751Registers::Address addr, uint8_t *buffer, size_t length) const;

    /**
     * @brief Read a register and report the length byte the controller sent
     *
     * Every other read overload requires the controller's length byte to equal
     * the size the caller expects and fails when it does not. This one returns
     * the length byte instead, so a caller can record a register exactly as the
     * controller presented it, or read a register the library has no size for.
     * The bench console uses it for the `tps25751-registers` snapshot format,
     * which stores the length byte as read (test/fixtures/pd-captures/README.md).
     *
     * The transaction is the library's usual one: the register address, a
     * repeated start, then @p capacity + 1 bytes, of which the first is the
     * length byte. When the length byte is larger than @p capacity, the buffer
     * holds the first @p capacity bytes and the caller can read again with a
     * larger buffer. The TRM allows the short read: "during a register read, the
     * controller can issue the stop bit before reading all N bytes" (SLVUCR8B
     * section 1.3.1, p.9).
     *
     * @param regAddr    Register address, 0x00 to 0xFF; it need not be in
     *                   TPS25751Registers::Address
     * @param buffer     Receives min(lengthByte, capacity) data bytes
     * @param capacity   Data bytes to request after the length byte, 1 to 64
     * @param lengthByte Receives the length byte the controller returned
     * @return true if the controller answered; false on a NAK, a short read, a
     *         null buffer, or a capacity outside 1 to 64
     *
     * @see SLVUCR8B section 1.3.1, Unique Address Interface Protocol, Figure 1-3 (p.9)
     */
    bool readRegisterRaw(uint8_t regAddr, uint8_t *buffer, size_t capacity, uint8_t &lengthByte) const;

    // Type-safe register writing methods (mirror the read overloads above)
    bool writeRegister(TPS25751Registers::Address addr, const uint8_t *data, size_t length) const;
    bool writeRegister(TPS25751Registers::RegisterInfo regInfo, const uint8_t *data) const;

    /**
     * @brief Type-safe register write keyed on the class's own @c kAddress.
     *
     * Writes @p reg 's raw bytes back to the register identified by
     * @c RegisterT::kAddress. Because the target address comes from the register
     * class itself, a class can only ever be written to its own register — the
     * whole class of "same-size register mis-pairing" bugs (e.g. INT_MASK1 vs
     * INT_CLEAR1, both 11 bytes) that the untyped
     * @c writeRegister(RegisterInfo,...) path cannot catch is unrepresentable
     * here. Validates @c isValid() and size before touching the bus.
     *
     * @tparam RegisterT A host R/W register class exposing @c kAddress
     * @param reg The register object holding the bytes to write
     * @return false if @p reg is invalid/wrong-size or the address is unknown;
     *         otherwise the result of the underlying I2C write
     */
    template<typename RegisterT>
    bool writeRegister(const RegisterT &reg) const
    {
        const TPS25751Registers::RegisterInfo *info =
            TPS25751Registers::getRegisterInfo(RegisterT::kAddress);
        if (!info) {
            return false;
        }
        if (!reg.isValid() || !reg.isValidSize(info->size)) {
            return false;
        }
        return writeRegister(*info, reg.raw());
    }

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
     * @return Unique pointer to specific register type (nullptr if the read failed)
     * @warning T must be the class the factory creates for @p addr. The cast is a
     *          static_cast (no RTTI), so a mismatched T is not detected.
     */
    template<typename T>
    std::unique_ptr<T> readRegisterAs(TPS25751Registers::Address addr) const {
        // The factory creates the class for addr, so static_cast is safe; a null
        // pointer casts to null.
        return std::unique_ptr<T>(static_cast<T*>(readRegisterByAddress(addr).release()));
    }
    
    // Type-safe factory-based convenience methods
    /**
     * @brief Read Mode register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Mode register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Mode> readModeRegister(bool validate = true) const;

    /**
     * @brief Read Customer Use register (0x06) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Customer Use register (nullptr if failed)
     */
    std::unique_ptr<TPS25751CustomerUse> readCustomerUseRegister(bool validate = true) const;
    
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
     * @brief Read Version register (0x0F) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Version register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Version> readVersionRegister(bool validate = true) const;
    
    /**
     * @brief Read Interrupt Event register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Interrupt Event register (nullptr if failed)
     */
    std::unique_ptr<TPS25751InterruptEvent> readInterruptEventRegister(bool validate = true) const;

    /**
     * @brief Read Interrupt Mask register (0x16) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Interrupt Mask register (nullptr if failed)
     */
    std::unique_ptr<TPS25751InterruptMask> readInterruptMaskRegister(bool validate = true) const;

    /**
     * @brief Read Interrupt Clear register (0x18) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Interrupt Clear register (nullptr if failed)
     */
    std::unique_ptr<TPS25751InterruptClear> readInterruptClearRegister(bool validate = true) const;

    /**
     * @brief Read Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751Status> readStatusRegister(bool validate = true) const;

    /**
     * @brief Read Power Path Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Power Path Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PowerPathStatus> readPowerPathStatusRegister(bool validate = true) const;

    /**
     * @brief Read Global System Configuration (0x27) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751GlobalSystemConfig> readGlobalSystemConfigRegister(bool validate = true) const;

    /**
     * @brief Read Port Configuration register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Port Configuration register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PortConfig> readPortConfigRegister(bool validate = true) const;

    /**
     * @brief Read Port Control register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Port Control register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PortControl> readPortControlRegister(bool validate = true) const;

    /**
     * @brief Read Boot Flags register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Boot Flags register (nullptr if failed)
     */
    std::unique_ptr<TPS25751BootFlags> readBootFlagsRegister(bool validate = true) const;

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
     * @brief Read Transmit Source Capabilities register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Transmit Source Capabilities register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitSourceCaps> readTransmitSourceCapsRegister(bool validate = true) const;

    /**
     * @brief Read Transmit Sink Capabilities register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Transmit Sink Capabilities register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitSinkCaps> readTransmitSinkCapsRegister(bool validate = true) const;

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
     * @brief Read Autonegotiate Sink register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Autonegotiate Sink register (nullptr if failed)
     */
    std::unique_ptr<TPS25751AutoNegotiateSink> readAutoNegotiateSinkRegister(bool validate = true) const;

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
     * @brief Read PD3 Configuration register (0x42) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to PD3 Configuration register (nullptr if failed)
     */
    std::unique_ptr<TPS25751PD3Config> readPD3ConfigRegister(bool validate = true) const;

    /**
     * @brief Read Received SOP Identity Data Object (0x48) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751ReceivedSOPIdentity> readReceivedSOPIdentityRegister(bool validate = true) const;

    /**
     * @brief Read IO Config register using factory pattern
     *
     * IO_CONFIG cannot be modified at run-time -- there is no corresponding
     * write method.
     *
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to IO Config register (nullptr if failed)
     */
    std::unique_ptr<TPS25751IOConfig> readIOConfigRegister(bool validate = true) const;

    /**
     * @brief Read Type C State register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Type C State register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TypeCState> readTypeCStateRegister(bool validate = true) const;

    /**
     * @brief Read ADC Results register using factory pattern
     *
     * Note: the PD controller does not update ADC_RESULTS while in
     * Unconnected Sleep mode, so a stale/all-zero read in that state is
     * expected and not, by itself, an error.
     *
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to ADC Results register (nullptr if failed)
     */
    std::unique_ptr<TPS25751ADCResults> readADCResultsRegister(bool validate = true) const;

    /**
     * @brief Read Sleep Control register (0x70) using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Sleep Control register (nullptr if failed)
     */
    std::unique_ptr<TPS25751SleepControl> readSleepControlRegister(bool validate = true) const;

    /**
     * @brief Read GPIO Status register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to GPIO Status register (nullptr if failed)
     */
    std::unique_ptr<TPS25751GPIOStatus> readGPIOStatusRegister(bool validate = true) const;

    /**
     * @brief Read TX Source Capabilities Extended (0x77) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitSourceCapsExtended> readTransmitSourceCapsExtendedRegister(bool validate = true) const;

    /**
     * @brief Read TX Source Info (0x78) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitSourceInfo> readTransmitSourceInfoRegister(bool validate = true) const;

    /**
     * @brief Read Transmitted PPS Status (0x7A) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitPPSStatus> readTransmitPPSStatusRegister(bool validate = true) const;

    /**
     * @brief Read Transmitted Battery Status (0x7B) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitBatteryStatus> readTransmitBatteryStatusRegister(bool validate = true) const;

    /**
     * @brief Read TX Battery Capabilities (0x7D) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitBatteryCapabilities> readTransmitBatteryCapabilitiesRegister(bool validate = true) const;

    /**
     * @brief Read Transmit Sink Capabilities Extended (0x7E) register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to the register (nullptr if failed)
     */
    std::unique_ptr<TPS25751TransmitSinkCapsExtended> readTransmitSinkCapsExtendedRegister(bool validate = true) const;

    /**
     * @brief Read Liquid Detection Config register using factory pattern
     * @param validate Whether to validate the register after reading
     * @return Unique pointer to Liquid Detection Config register (nullptr if failed)
     */
    std::unique_ptr<TPS25751LiquidDetectionConfig> readLiquidDetectionConfigRegister(bool validate = true) const;

    /**
     * @brief Write a 4CC task's input to DATA (0x09) at its own length
     *
     * One register write of @p length bytes, with @p length as the byte count:
     * `[0x09, length, data...]`. The typed writeRegister(Address, ...) accepts
     * DATA's full 64 bytes only; a task input such as the 6-byte PBMs
     * configuration goes out at its own length, as SLVAFV8A step 4 (page 8)
     * shows. issueCommand() and executeCommand() stage their input through
     * this call. Use it on its own to stage DATA and check it before the
     * COMMAND write, then issue the task with no input.
     *
     * @param data   Bytes for DATA from byte 0
     * @param length 1 to 64
     * @return true if the write was acknowledged; false for a null @p data, a
     *         @p length of 0 or above 64 (no transaction), or a failed write
     */
    bool writeCommandData(const uint8_t* data, size_t length) const;

    /**
     * @brief Start a 4CC task and wait for it to complete, without reading DATA
     *
     * Writes @p inData to DATA when @p inData is not nullptr and @p inLen is not
     * 0, writes @p cmd to COMMAND, then polls COMMAND every 5 ms until the
     * controller clears it (Success), replaces it with "!CMD" (Rejected), or
     * @p timeoutMs passes (Timeout). A failed I2C transaction returns I2CError
     * at once. DATA is not read: the caller reads it, when and if the task
     * requires.
     *
     * For tasks whose output is read right after COMMAND clears, use
     * executeCommand(), which is this call followed by the DATA read. This
     * call exists for PBMc, after which TI's documents wait 20 ms before
     * reading DATA (SLVAFV8A, step 15 on page 11 and the code on page 18; TRM
     * SLVUCR8B Figure 5-1, page 88).
     *
     * @param cmd       4-character task code
     * @param inData    Input payload for DATA, or nullptr for none
     * @param inLen     Length of @p inData in bytes (0 to 64)
     * @param timeoutMs Longest wait for COMMAND to clear or reject
     * @return Success, Rejected, Timeout, or I2CError
     */
    TPS25751TaskStatus issueCommand(TPS25751FourCC cmd,
                                    const uint8_t* inData = nullptr, size_t inLen = 0,
                                    uint32_t timeoutMs = 200) const;

    /**
     * @brief Execute a 4CC command-task via the COMMAND/DATA interface (TRM Sec 4.4.2)
     *
     * issueCommand(), then, on Success, a read of the full 64-byte DATA register
     * at once. @p outData is filled from DATA byte 0 (so outData[0] is the task
     * return code) up to min(outLen, 64) bytes. Task-specific output layouts
     * (e.g. I2Cr's read bytes starting at DATA offset 1) are unpacked by the
     * caller. A failed DATA read returns I2CError.
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

    /**
     * @brief Raw chunked burst write to an arbitrary I2C target address (PBM path)
     *
     * Sends @p data to @p targetAddr in chunks of at most @p chunk bytes.  Each
     * chunk is its own `beginTransmission / write / endTransmission(true)` cycle.
     * There is NO register-number byte and NO byte-count (length) byte emitted —
     * this is the "Table 3-3" raw burst shape from SLVAFV8A, which differs from
     * every other TPS25751 write (see CONSTRAINTS.md "Patch Burst Mode (PBM)").
     *
     * The TPS25751's PBM pointer auto-increments across I2C START/STOP boundaries
     * (SLVAFV8A footnote 3), so splitting the image across many small transactions
     * is correct and expected.
     *
     * @param targetAddr 7-bit I2C address of the PBM receive port (0x30).
     *                   This is an I2C device address, not a TPS25751 register offset.
     * @param data       Pointer to the byte array to send
     * @param length     Total number of bytes to send
     * @param chunk      Maximum bytes per transaction (default PBM_BURST_CHUNK)
     * @param interChunkDelayUs  Optional settle delay between chunks, in microseconds
     * @return true if every chunk was ACKed successfully; false on first error
     *
     * @see TPS25751PatchLoader::loadPatchBundle() for the complete PBM flow
     * @see SLVAFV8A Table 3-3 and §4.1 step 10
     */
    bool burstWrite(uint8_t targetAddr, const uint8_t* data, size_t length,
                    size_t chunk = PBM_BURST_CHUNK,
                    uint16_t interChunkDelayUs = 0) const;

private:
    TwoWire &_wire;
    uint8_t _address;

    /**
     * @brief readAndValidateRegisterByAddress() cast to the class for @p addr
     *
     * Shared by the typed read*Register() functions. T must be the class the
     * factory creates for @p addr; a null result casts to null.
     */
    template<typename T>
    std::unique_ptr<T> readAndValidateAs(TPS25751Registers::Address addr, bool validate) const {
        return std::unique_ptr<T>(static_cast<T*>(readAndValidateRegisterByAddress(addr, validate).release()));
    }

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

// Included at the bottom (after TPS25751 is fully defined) so that consumers
// of TPS25751.h also pick up the patch-loader API without an extra #include.
// TPS25751PatchLoader.h forward-declares TPS25751 to avoid a circular include.
#include "TPS25751PatchLoader.h"

// The executor seam, its TPS25751 adapter, and the EEPROM updater built on the
// seam, included here for the same reason. TPS25751HostExecutor.h
// forward-declares TPS25751; TPS25751EepromUpdater.h does not depend on it.
#include "TPS25751HostExecutor.h"
#include "TPS25751EepromUpdater.h"

// The patch bundle app config editor, which needs no controller (ADR-016).
#include "TPS25751AppConfigImage.h"

#endif // TPS25751_H
