#include "TPS25751.h"
#include "TPS25751Status.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751Debug.h"
#include "TPS25751InterruptEvent.h"

TPS25751::TPS25751(TwoWire &wire, const uint8_t address)
    : _wire(wire), _address(address)
{
}

bool TPS25751::begin() const
{
    _wire.begin();
    // optionally test connection
    return true;
}

bool TPS25751::readRegister(const uint8_t regAddr, uint8_t *buffer, const size_t length) const
{
    TPS_DEBUG_TRACE(DEBUG_CAT_I2C, "Reading register 0x%02X (addr=0x%02X, len=%u)", regAddr, _address, (unsigned)length);
    
    _wire.beginTransmission(_address);
    _wire.write(regAddr);
    uint8_t result = _wire.endTransmission(false);
    if (result != 0) {
        TPS_REPORT_I2C_ERROR(_address, regAddr, "beginTransmission", result);
        return false;
    }

    // Register read responses are prefixed with the number of bytes in the response from the TPS25751, so request register length+1
    size_t received = _wire.requestFrom(_address, length + 1);
    if (received != length + 1)
    {
        TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "requestFrom failed: expected %u bytes, got %u", (unsigned)(length + 1), (unsigned)received);
        TPS_REPORT_I2C_ERROR(_address, regAddr, "requestFrom", received);
        return false;
    }

    if (!_wire.available())
    {
        TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "No data available after successful requestFrom");
        return false;
    }

    // Get the length of the response and check that it matches the expected length of the register.
    uint8_t byte_count = _wire.read();
    TPS_DEBUG(DEBUG_CAT_I2C, "Register response byte count: %u (expected %u)", byte_count, (unsigned)length);
    
    if (byte_count != length)
    {
        TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "Byte count mismatch: expected %u, got %u", (unsigned)length, byte_count);
        TPS_REPORT_I2C_ERROR(_address, regAddr, "byte_count_mismatch", byte_count);
        return false;
    }

    for (size_t i = 0; i < length; ++i)
    {
        if (!_wire.available())
        {
            TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "No data available when reading byte %u of %u", (unsigned)i, (unsigned)length);
            return false;
        }
        buffer[i] = _wire.read();
    }
    
    TPS_DEBUG_INFO(DEBUG_CAT_I2C, "Successfully read register 0x%02X", regAddr);
    TPS_DEBUG_HEXDUMP(DEBUG_CAT_I2C, buffer, length, "Register data");
    return true;
}


bool TPS25751::readMode(TPS25751Mode &modeOut) const
{
    return readRegister(TPS25751Registers::Registers::MODE, modeOut);
}

bool TPS25751::readCustomerUse(TPS25751CustomerUse &customerUse) const
{
    return readRegister(TPS25751Registers::Registers::CUSTOMER_USE, customerUse);
}

bool TPS25751::readCommand(TPS25751Command &command) const
{
    return readRegister(TPS25751Registers::Registers::COMMAND, command);
}

bool TPS25751::readData(TPS25751Data &data) const
{
    return readRegister(TPS25751Registers::Registers::DATA, data);
}

bool TPS25751::readVersion(TPS25751Version &version) const
{
    return readRegister(TPS25751Registers::Registers::VERSION, version);
}

bool TPS25751::readInterruptEvent(TPS25751InterruptEvent &intEvent) const
{
    return readRegister(TPS25751Registers::Registers::INT_EVENT1, intEvent);
}

bool TPS25751::readInterruptMask(TPS25751InterruptMask &interruptMask) const
{
    return readRegister(TPS25751Registers::Registers::INT_MASK1, interruptMask);
}

bool TPS25751::readInterruptClear(TPS25751InterruptClear &interruptClear) const
{
    return readRegister(TPS25751Registers::Registers::INT_CLEAR1, interruptClear);
}

bool TPS25751::readStatus(TPS25751Status &statusOut) const
{
    return readRegister(TPS25751Registers::Registers::STATUS, statusOut);
}

bool TPS25751::readPowerPathStatus(TPS25751PowerPathStatus &pps) const
{
    return readRegister(TPS25751Registers::Registers::POWER_PATH_STATUS, pps);
}

bool TPS25751::readGlobalSystemConfig(TPS25751GlobalSystemConfig &globalSystemConfig) const
{
    return readRegister(TPS25751Registers::Registers::GLOBAL_SYSTEM_CONFIGURATION, globalSystemConfig);
}

bool TPS25751::readPortConfig(TPS25751PortConfig &portConfig) const
{
    return readRegister(TPS25751Registers::Registers::PORT_CONFIGURATION, portConfig);
}

bool TPS25751::readPortControl(TPS25751PortControl &portControl) const
{
    return readRegister(TPS25751Registers::Registers::PORT_CONTROL, portControl);
}

bool TPS25751::readBootFlags(TPS25751BootFlags &bootFlags) const
{
    return readRegister(TPS25751Registers::Registers::BOOT_FLAGS, bootFlags);
}

bool TPS25751::readReceivedSourceCaps(TPS25751ReceivedSourceCaps &sourceCaps) const
{
    return readRegister(TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES, sourceCaps);
}

bool TPS25751::readReceivedSinkCaps(TPS25751ReceivedSinkCaps &sinkCaps) const
{
    return readRegister(TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES, sinkCaps);
}

bool TPS25751::readTransmitSourceCaps(TPS25751TransmitSourceCaps &sourceCaps) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES, sourceCaps);
}

bool TPS25751::readTransmitSinkCaps(TPS25751TransmitSinkCaps &sinkCaps) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES, sinkCaps);
}

bool TPS25751::readActivePDOContract(TPS25751ActivePDOContract &pdoContract) const
{
    return readRegister(TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT, pdoContract);
}

bool TPS25751::readActiveRDOContract(TPS25751ActiveRDOContract &rdoContract) const
{
    return readRegister(TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT, rdoContract);
}

bool TPS25751::readAutoNegotiateSink(TPS25751AutoNegotiateSink &autoNegSink) const
{
    return readRegister(TPS25751Registers::Registers::AUTONEGOTIATE_SINK, autoNegSink);
}

bool TPS25751::readPowerStatus(TPS25751PowerStatus &powerStatus) const
{
    return readRegister(TPS25751Registers::Registers::POWER_STATUS, powerStatus);
}

bool TPS25751::readPDStatus(TPS25751PDStatus &pdStatus) const
{
    return readRegister(TPS25751Registers::Registers::PD_STATUS, pdStatus);
}

bool TPS25751::readPD3Config(TPS25751PD3Config &pd3Config) const
{
    return readRegister(TPS25751Registers::Registers::PD3_CONFIGURATION, pd3Config);
}

bool TPS25751::readReceivedSOPIdentity(TPS25751ReceivedSOPIdentity &sopIdentity) const
{
    return readRegister(TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY, sopIdentity);
}

bool TPS25751::readIOConfig(TPS25751IOConfig &ioConfig) const
{
    return readRegister(TPS25751Registers::Registers::IO_CONFIG, ioConfig);
}

bool TPS25751::readTypeCState(TPS25751TypeCState &typeCState) const
{
    return readRegister(TPS25751Registers::Registers::TYPEC_STATE, typeCState);
}

bool TPS25751::readADCResults(TPS25751ADCResults &adcResults) const
{
    return readRegister(TPS25751Registers::Registers::ADC_RESULTS, adcResults);
}

bool TPS25751::readSleepControl(TPS25751SleepControl &sleepControl) const
{
    return readRegister(TPS25751Registers::Registers::SLEEP_CONTROL, sleepControl);
}

bool TPS25751::readGPIOStatus(TPS25751GPIOStatus &gpioStatus) const
{
    return readRegister(TPS25751Registers::Registers::GPIO_STATUS, gpioStatus);
}

bool TPS25751::readTransmitSourceCapsExtended(TPS25751TransmitSourceCapsExtended &caps) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED, caps);
}

bool TPS25751::readTransmitSourceInfo(TPS25751TransmitSourceInfo &sourceInfo) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_SOURCE_INFO, sourceInfo);
}

bool TPS25751::readTransmitPPSStatus(TPS25751TransmitPPSStatus &ppsStatus) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_PPS_STATUS, ppsStatus);
}

bool TPS25751::readTransmitBatteryStatus(TPS25751TransmitBatteryStatus &status) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_BATTERY_STATUS, status);
}

bool TPS25751::readTransmitBatteryCapabilities(TPS25751TransmitBatteryCapabilities &caps) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_BATTERY_CAPABILITIES, caps);
}

bool TPS25751::readTransmitSinkCapsExtended(TPS25751TransmitSinkCapsExtended &caps) const
{
    return readRegister(TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES_EXTENDED, caps);
}

bool TPS25751::readLiquidDetectionConfig(TPS25751LiquidDetectionConfig &liquidDetectionConfig) const
{
    return readRegister(TPS25751Registers::Registers::LIQUID_DETECTION_CONFIG, liquidDetectionConfig);
}

bool TPS25751::writeInterruptMask(const TPS25751InterruptMask &interruptMask) const
{
    // Delegates to the typed writeRegister<T>() (keyed on kAddress) so there is
    // a single host write path; the size/validity guard lives there.
    return writeRegister(interruptMask);
}

bool TPS25751::writeInterruptClear(const TPS25751InterruptClear &interruptClear) const
{
    return writeRegister(interruptClear);
}

bool TPS25751::writeGlobalSystemConfig(const TPS25751GlobalSystemConfig &globalSystemConfig) const
{
    return writeRegister(globalSystemConfig);
}

bool TPS25751::writeTransmitSourceCaps(const TPS25751TransmitSourceCaps &sourceCaps) const
{
    return writeRegister(sourceCaps);
}

bool TPS25751::writeTransmitSinkCaps(const TPS25751TransmitSinkCaps &sinkCaps) const
{
    return writeRegister(sinkCaps);
}

bool TPS25751::writeAutoNegotiateSink(const TPS25751AutoNegotiateSink &autoNegSink) const
{
    // Delegates to the typed writeRegister<T>() (keyed on kAddress) so there is
    // a single host write path; the size/validity guard lives there.
    return writeRegister(autoNegSink);
}

bool TPS25751::writePD3Config(const TPS25751PD3Config &pd3Config) const
{
    // Delegates to the typed writeRegister<T>() (keyed on kAddress) so there is
    // a single host write path; the size/validity guard lives there.
    return writeRegister(pd3Config);
}

bool TPS25751::writeSleepControl(const TPS25751SleepControl &sleepControl) const
{
    // Delegates to the typed writeRegister<T>() (keyed on kAddress) so there is
    // a single host write path; the size/validity guard lives there.
    return writeRegister(sleepControl);
}

bool TPS25751::writeTransmitSourceCapsExtended(const TPS25751TransmitSourceCapsExtended &caps) const
{
    return writeRegister(caps);
}

bool TPS25751::writeTransmitSourceInfo(const TPS25751TransmitSourceInfo &sourceInfo) const
{
    return writeRegister(sourceInfo);
}

bool TPS25751::writeTransmitPPSStatus(const TPS25751TransmitPPSStatus &ppsStatus) const
{
    return writeRegister(ppsStatus);
}

bool TPS25751::writeTransmitBatteryStatus(const TPS25751TransmitBatteryStatus &status) const
{
    return writeRegister(status);
}

bool TPS25751::writeTransmitBatteryCapabilities(const TPS25751TransmitBatteryCapabilities &caps) const
{
    return writeRegister(caps);
}

bool TPS25751::writeTransmitSinkCapsExtended(const TPS25751TransmitSinkCapsExtended &caps) const
{
    return writeRegister(caps);
}

bool TPS25751::writeLiquidDetectionConfig(const TPS25751LiquidDetectionConfig &liquidDetectionConfig) const
{
    // Delegates to the typed writeRegister<T>() (keyed on kAddress) so there is
    // a single host write path; the size/validity guard lives there.
    return writeRegister(liquidDetectionConfig);
}

bool TPS25751::writePortConfig(const TPS25751PortConfig &portConfig) const
{
    return writeRegister(portConfig);
}

bool TPS25751::writePortControl(const TPS25751PortControl &portControl) const
{
    return writeRegister(portControl);
}

// Type-safe register reading methods
bool TPS25751::readRegister(TPS25751Registers::RegisterInfo regInfo, uint8_t *buffer) const
{
    return readRegister(static_cast<uint8_t>(regInfo.address), buffer, regInfo.size);
}

bool TPS25751::readRegister(TPS25751Registers::Address addr, uint8_t *buffer, size_t length) const
{
    uint8_t expectedSize = TPS25751Registers::getRegisterSize(addr);
    if (expectedSize != 0 && expectedSize != length) {
        // Size mismatch detected
        return false;
    }
    return readRegister(static_cast<uint8_t>(addr), buffer, length);
}

bool TPS25751::writeRegister(const uint8_t regAddr, const uint8_t *data, const size_t length) const
{
    TPS_DEBUG_TRACE(DEBUG_CAT_I2C, "Writing register 0x%02X (addr=0x%02X, len=%u)", regAddr, _address, (unsigned)length);

    _wire.beginTransmission(_address);
    _wire.write(regAddr);
    _wire.write(static_cast<uint8_t>(length));
    size_t written = _wire.write(data, length);
    if (written != length)
    {
        TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "write() failed: expected %u bytes, wrote %u", (unsigned)length, (unsigned)written);
        TPS_REPORT_I2C_ERROR(_address, regAddr, "write", written);
        return false;
    }

    uint8_t result = _wire.endTransmission(true);
    if (result != 0) {
        TPS_REPORT_I2C_ERROR(_address, regAddr, "endTransmission", result);
        return false;
    }

    TPS_DEBUG_INFO(DEBUG_CAT_I2C, "Successfully wrote register 0x%02X", regAddr);
    TPS_DEBUG_HEXDUMP(DEBUG_CAT_I2C, data, length, "Register data written");
    return true;
}

bool TPS25751::readRegisterRaw(const uint8_t regAddr, uint8_t *buffer, const size_t capacity, uint8_t &lengthByte) const
{
    if (buffer == nullptr || capacity == 0 || capacity > TPS25751Registers::Registers::DATA.size) {
        TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "readRegisterRaw: capacity %u not in 1 to %u",
                        (unsigned)capacity, (unsigned)TPS25751Registers::Registers::DATA.size);
        return false;
    }
    TPS_DEBUG_TRACE(DEBUG_CAT_I2C, "Reading register 0x%02X raw (addr=0x%02X, capacity=%u)", regAddr, _address, (unsigned)capacity);

    _wire.beginTransmission(_address);
    _wire.write(regAddr);
    uint8_t result = _wire.endTransmission(false);
    if (result != 0) {
        TPS_REPORT_I2C_ERROR(_address, regAddr, "beginTransmission", result);
        return false;
    }

    // The first byte of the response is the register's byte count, as in
    // readRegister(); here it is reported instead of compared.
    size_t received = _wire.requestFrom(_address, capacity + 1);
    if (received != capacity + 1 || !_wire.available()) {
        TPS_REPORT_I2C_ERROR(_address, regAddr, "requestFrom", received);
        return false;
    }

    lengthByte = _wire.read();
    const size_t wanted = (lengthByte < capacity) ? lengthByte : capacity;
    for (size_t i = 0; i < wanted; ++i) {
        if (!_wire.available()) {
            return false;
        }
        buffer[i] = _wire.read();
    }
    return true;
}

// Type-safe register writing methods
bool TPS25751::writeRegister(TPS25751Registers::Address addr, const uint8_t *data, size_t length) const
{
    uint8_t expectedSize = TPS25751Registers::getRegisterSize(addr);
    if (expectedSize != 0 && expectedSize != length) {
        // Size mismatch detected
        return false;
    }
    return writeRegister(static_cast<uint8_t>(addr), data, length);
}

bool TPS25751::writeRegister(TPS25751Registers::RegisterInfo regInfo, const uint8_t *data) const
{
    return writeRegister(static_cast<uint8_t>(regInfo.address), data, regInfo.size);
}

// Debug configuration implementations
void TPS25751::setDebugStream(Stream* stream) {
    TPS25751Debug::setDebugStream(stream);
}

void TPS25751::setDebugLevel(uint8_t level) {
    TPS25751Debug::setDebugLevel(level);
}

void TPS25751::setDebugCategories(uint8_t categories) {
    TPS25751Debug::setDebugCategories(categories);
}

// Factory-based register creation implementations
std::unique_ptr<TPS25751Register> TPS25751::readRegisterByAddress(TPS25751Registers::Address addr) const {
    // Look up register info (size). A null result means the address is unknown.
    auto regInfo = TPS25751Registers::getRegisterInfo(addr);
    if (!regInfo) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "No register info for address 0x%02X", static_cast<uint8_t>(addr));
        return nullptr;
    }

    // Confirm the address has a decoder class before reading from the bus.
    auto reg = TPS25751Factory::getInstance().createRegister(addr);
    if (!reg) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Unsupported register address: 0x%02X", static_cast<uint8_t>(addr));
        return nullptr;
    }

    // Read data into a temporary buffer
    auto buffer = std::make_unique<uint8_t[]>(regInfo->size);
    if (!readRegister(static_cast<uint8_t>(addr), buffer.get(), regInfo->size)) {
        TPS_DEBUG_ERROR(DEBUG_CAT_REGISTER, "Failed to read register data for address 0x%02X", static_cast<uint8_t>(addr));
        return nullptr;
    }

    // Create register with actual data
    return TPS25751Factory::getInstance().createRegister(addr, buffer.get(), regInfo->size);
}

std::unique_ptr<TPS25751Register> TPS25751::readAndValidateRegisterByAddress(TPS25751Registers::Address addr, bool validate) const {
    auto reg = readRegisterByAddress(addr);
    if (!reg) {
        return nullptr;
    }

    if (validate && !validateRegister(*reg)) {
        TPS_DEBUG_WARN(DEBUG_CAT_REGISTER, "Register validation failed for address 0x%02X", static_cast<uint8_t>(addr));
        return nullptr;
    }

    TPS_DEBUG_INFO(DEBUG_CAT_REGISTER, "Successfully read and validated register at address 0x%02X", static_cast<uint8_t>(addr));
    return reg;
}

// Type-safe factory-based convenience method implementations
std::unique_ptr<TPS25751Mode> TPS25751::readModeRegister(bool validate) const {
    return readAndValidateAs<TPS25751Mode>(TPS25751Registers::Address::MODE, validate);
}

std::unique_ptr<TPS25751CustomerUse> TPS25751::readCustomerUseRegister(bool validate) const {
    return readAndValidateAs<TPS25751CustomerUse>(TPS25751Registers::Address::CUSTOMER_USE, validate);
}

std::unique_ptr<TPS25751Command> TPS25751::readCommandRegister(bool validate) const {
    return readAndValidateAs<TPS25751Command>(TPS25751Registers::Address::COMMAND, validate);
}

std::unique_ptr<TPS25751Data> TPS25751::readDataRegister(bool validate) const {
    return readAndValidateAs<TPS25751Data>(TPS25751Registers::Address::DATA, validate);
}

std::unique_ptr<TPS25751Version> TPS25751::readVersionRegister(bool validate) const {
    return readAndValidateAs<TPS25751Version>(TPS25751Registers::Address::VERSION, validate);
}

std::unique_ptr<TPS25751InterruptEvent> TPS25751::readInterruptEventRegister(bool validate) const {
    return readAndValidateAs<TPS25751InterruptEvent>(TPS25751Registers::Address::INT_EVENT1, validate);
}

std::unique_ptr<TPS25751InterruptMask> TPS25751::readInterruptMaskRegister(bool validate) const {
    return readAndValidateAs<TPS25751InterruptMask>(TPS25751Registers::Address::INT_MASK1, validate);
}

std::unique_ptr<TPS25751InterruptClear> TPS25751::readInterruptClearRegister(bool validate) const {
    return readAndValidateAs<TPS25751InterruptClear>(TPS25751Registers::Address::INT_CLEAR1, validate);
}

std::unique_ptr<TPS25751Status> TPS25751::readStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751Status>(TPS25751Registers::Address::STATUS, validate);
}

std::unique_ptr<TPS25751PowerPathStatus> TPS25751::readPowerPathStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751PowerPathStatus>(TPS25751Registers::Address::POWER_PATH_STATUS, validate);
}

std::unique_ptr<TPS25751GlobalSystemConfig> TPS25751::readGlobalSystemConfigRegister(bool validate) const {
    return readAndValidateAs<TPS25751GlobalSystemConfig>(TPS25751Registers::Address::GLOBAL_SYSTEM_CONFIGURATION, validate);
}

std::unique_ptr<TPS25751PortConfig> TPS25751::readPortConfigRegister(bool validate) const {
    return readAndValidateAs<TPS25751PortConfig>(TPS25751Registers::Address::PORT_CONFIGURATION, validate);
}

std::unique_ptr<TPS25751PortControl> TPS25751::readPortControlRegister(bool validate) const {
    return readAndValidateAs<TPS25751PortControl>(TPS25751Registers::Address::PORT_CONTROL, validate);
}

std::unique_ptr<TPS25751BootFlags> TPS25751::readBootFlagsRegister(bool validate) const {
    return readAndValidateAs<TPS25751BootFlags>(TPS25751Registers::Address::BOOT_FLAGS, validate);
}

std::unique_ptr<TPS25751ReceivedSourceCaps> TPS25751::readReceivedSourceCapsRegister(bool validate) const {
    return readAndValidateAs<TPS25751ReceivedSourceCaps>(TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES, validate);
}

std::unique_ptr<TPS25751ReceivedSinkCaps> TPS25751::readReceivedSinkCapsRegister(bool validate) const {
    return readAndValidateAs<TPS25751ReceivedSinkCaps>(TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES, validate);
}

std::unique_ptr<TPS25751TransmitSourceCaps> TPS25751::readTransmitSourceCapsRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitSourceCaps>(TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES, validate);
}

std::unique_ptr<TPS25751TransmitSinkCaps> TPS25751::readTransmitSinkCapsRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitSinkCaps>(TPS25751Registers::Address::TRANSMIT_SINK_CAPABILITIES, validate);
}

std::unique_ptr<TPS25751ActivePDOContract> TPS25751::readActivePDOContractRegister(bool validate) const {
    return readAndValidateAs<TPS25751ActivePDOContract>(TPS25751Registers::Address::ACTIVE_PDO_CONTRACT, validate);
}

std::unique_ptr<TPS25751ActiveRDOContract> TPS25751::readActiveRDOContractRegister(bool validate) const {
    return readAndValidateAs<TPS25751ActiveRDOContract>(TPS25751Registers::Address::ACTIVE_RDO_CONTRACT, validate);
}

std::unique_ptr<TPS25751AutoNegotiateSink> TPS25751::readAutoNegotiateSinkRegister(bool validate) const {
    return readAndValidateAs<TPS25751AutoNegotiateSink>(TPS25751Registers::Address::AUTONEGOTIATE_SINK, validate);
}

std::unique_ptr<TPS25751PowerStatus> TPS25751::readPowerStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751PowerStatus>(TPS25751Registers::Address::POWER_STATUS, validate);
}

std::unique_ptr<TPS25751PDStatus> TPS25751::readPDStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751PDStatus>(TPS25751Registers::Address::PD_STATUS, validate);
}

std::unique_ptr<TPS25751PD3Config> TPS25751::readPD3ConfigRegister(bool validate) const {
    return readAndValidateAs<TPS25751PD3Config>(TPS25751Registers::Address::PD3_CONFIGURATION, validate);
}

std::unique_ptr<TPS25751ReceivedSOPIdentity> TPS25751::readReceivedSOPIdentityRegister(bool validate) const {
    return readAndValidateAs<TPS25751ReceivedSOPIdentity>(TPS25751Registers::Address::RECEIVED_SOP_IDENTITY, validate);
}

std::unique_ptr<TPS25751IOConfig> TPS25751::readIOConfigRegister(bool validate) const {
    return readAndValidateAs<TPS25751IOConfig>(TPS25751Registers::Address::IO_CONFIG, validate);
}

std::unique_ptr<TPS25751TypeCState> TPS25751::readTypeCStateRegister(bool validate) const {
    return readAndValidateAs<TPS25751TypeCState>(TPS25751Registers::Address::TYPEC_STATE, validate);
}

std::unique_ptr<TPS25751ADCResults> TPS25751::readADCResultsRegister(bool validate) const {
    return readAndValidateAs<TPS25751ADCResults>(TPS25751Registers::Address::ADC_RESULTS, validate);
}

std::unique_ptr<TPS25751SleepControl> TPS25751::readSleepControlRegister(bool validate) const {
    return readAndValidateAs<TPS25751SleepControl>(TPS25751Registers::Address::SLEEP_CONTROL, validate);
}

std::unique_ptr<TPS25751GPIOStatus> TPS25751::readGPIOStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751GPIOStatus>(TPS25751Registers::Address::GPIO_STATUS, validate);
}

std::unique_ptr<TPS25751TransmitSourceCapsExtended> TPS25751::readTransmitSourceCapsExtendedRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitSourceCapsExtended>(TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED, validate);
}

std::unique_ptr<TPS25751TransmitSourceInfo> TPS25751::readTransmitSourceInfoRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitSourceInfo>(TPS25751Registers::Address::TRANSMIT_SOURCE_INFO, validate);
}

std::unique_ptr<TPS25751TransmitPPSStatus> TPS25751::readTransmitPPSStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitPPSStatus>(TPS25751Registers::Address::TRANSMIT_PPS_STATUS, validate);
}

std::unique_ptr<TPS25751TransmitBatteryStatus> TPS25751::readTransmitBatteryStatusRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitBatteryStatus>(TPS25751Registers::Address::TRANSMIT_BATTERY_STATUS, validate);
}

std::unique_ptr<TPS25751TransmitBatteryCapabilities> TPS25751::readTransmitBatteryCapabilitiesRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitBatteryCapabilities>(TPS25751Registers::Address::TRANSMIT_BATTERY_CAPABILITIES, validate);
}

std::unique_ptr<TPS25751TransmitSinkCapsExtended> TPS25751::readTransmitSinkCapsExtendedRegister(bool validate) const {
    return readAndValidateAs<TPS25751TransmitSinkCapsExtended>(TPS25751Registers::Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED, validate);
}

std::unique_ptr<TPS25751LiquidDetectionConfig> TPS25751::readLiquidDetectionConfigRegister(bool validate) const {
    return readAndValidateAs<TPS25751LiquidDetectionConfig>(TPS25751Registers::Address::LIQUID_DETECTION_CONFIG, validate);
}

// PBM raw burst write (SLVAFV8A Table 3-3 / §4.1 step 10)
bool TPS25751::burstWrite(uint8_t targetAddr, const uint8_t* data, size_t length,
                          size_t chunk, uint16_t interChunkDelayUs) const
{
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK,
        "burstWrite: target=0x%02X, length=%u, chunk=%u, delay=%u us",
        targetAddr, (unsigned)length, (unsigned)chunk, (unsigned)interChunkDelayUs);

    size_t offset = 0;
    while (offset < length) {
        size_t thisChunk = (chunk < (length - offset)) ? chunk : (length - offset);

        _wire.beginTransmission(targetAddr);
        size_t written = _wire.write(data + offset, thisChunk);
        if (written != thisChunk) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                "burstWrite: write() short at offset %u: expected %u, wrote %u",
                (unsigned)offset, (unsigned)thisChunk, (unsigned)written);
            TPS_REPORT_I2C_ERROR(targetAddr, 0x00, "burstWrite::write", (int)written);
            _wire.endTransmission(true);  // release bus
            return false;
        }

        uint8_t result = _wire.endTransmission(true);
        if (result != 0) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK,
                "burstWrite: endTransmission failed at offset %u (err=%u)",
                (unsigned)offset, (unsigned)result);
            TPS_REPORT_I2C_ERROR(targetAddr, 0x00, "burstWrite::endTransmission", result);
            return false;
        }

        offset += thisChunk;

        if (interChunkDelayUs > 0 && offset < length) {
            delayMicroseconds(interChunkDelayUs);
        }
    }

    TPS_DEBUG_INFO(DEBUG_CAT_TASK,
        "burstWrite: complete, %u bytes sent to 0x%02X", (unsigned)length, targetAddr);
    return true;
}

// 4CC command-task executor (TRM Sec 4.4.2)
TPS25751TaskStatus TPS25751::waitForCommandClear(uint32_t timeoutMs) const
{
    uint32_t start = millis();
    uint8_t buf[TPS25751Registers::Registers::COMMAND.size];

    for (;;) {
        if (!readRegister(static_cast<uint8_t>(TPS25751Registers::Address::COMMAND), buf, sizeof(buf))) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "executeCommand: failed to poll COMMAND");
            return TPS25751TaskStatus::I2CError;
        }

        TPS25751Command cmd(buf, sizeof(buf));
        if (cmd.isClear()) {
            return TPS25751TaskStatus::Success;
        }
        if (cmd.isRejected()) {
            TPS_DEBUG_WARN(DEBUG_CAT_TASK, "executeCommand: rejected (\"!CMD\")");
            return TPS25751TaskStatus::Rejected;
        }

        if (millis() - start > timeoutMs) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "executeCommand: timed out waiting for COMMAND to clear");
            return TPS25751TaskStatus::Timeout;
        }

        delay(5);
    }
}

bool TPS25751::writeCommandData(const uint8_t* data, size_t length) const
{
    if (data == nullptr || length == 0 || length > TPS25751Registers::Registers::DATA.size) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "writeCommandData: length %u not in 1 to %u",
                        (unsigned)length, (unsigned)TPS25751Registers::Registers::DATA.size);
        return false;
    }
    // The private raw overload writes exactly length bytes; the public Address
    // overload requires DATA's full 64 bytes.
    return writeRegister(static_cast<uint8_t>(TPS25751Registers::Address::DATA), data, length);
}

TPS25751TaskStatus TPS25751::issueCommand(TPS25751FourCC cmd, const uint8_t* inData, size_t inLen,
                                          uint32_t timeoutMs) const
{
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "issueCommand: '%c%c%c%c' (inLen=%u)",
                    cmd.code[0], cmd.code[1], cmd.code[2], cmd.code[3], (unsigned)inLen);

    // Stage the input payload in DATA first (if any), at its own length.
    if (inData != nullptr && inLen > 0) {
        if (!writeCommandData(inData, inLen)) {
            TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "executeCommand: failed to write DATA");
            return TPS25751TaskStatus::I2CError;
        }
    }

    // Kick off the task by writing the 4CC to COMMAND.
    if (!writeRegister(TPS25751Registers::Address::COMMAND, reinterpret_cast<const uint8_t*>(cmd.code), sizeof(cmd.code))) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "executeCommand: failed to write COMMAND");
        return TPS25751TaskStatus::I2CError;
    }

    // DATA is not read here: PBMc needs 20 ms between COMMAND clearing and the
    // DATA read (SLVAFV8A page 11 and page 18; TRM Figure 5-1, page 88).
    return waitForCommandClear(timeoutMs);
}

TPS25751TaskResult TPS25751::executeCommand(
    TPS25751FourCC cmd,
    const uint8_t* inData, size_t inLen,
    uint8_t* outData, size_t outLen,
    uint32_t timeoutMs) const
{
    TPS_DEBUG_TRACE(DEBUG_CAT_TASK, "executeCommand: '%c%c%c%c' (inLen=%u, outLen=%u)",
                     cmd.code[0], cmd.code[1], cmd.code[2], cmd.code[3], (unsigned)inLen, (unsigned)outLen);

    // The same DATA write, COMMAND write, and COMMAND polls as issueCommand().
    TPS25751TaskStatus status = issueCommand(cmd, inData, inLen, timeoutMs);
    if (status != TPS25751TaskStatus::Success) {
        return TPS25751TaskResult{status, 0};
    }

    // Read back the full DATA register (exact register size, avoiding partial-read ambiguity).
    uint8_t raw[TPS25751Registers::Registers::DATA.size];
    if (!readRegister(static_cast<uint8_t>(TPS25751Registers::Address::DATA), raw, sizeof(raw))) {
        TPS_DEBUG_ERROR(DEBUG_CAT_TASK, "executeCommand: failed to read DATA after completion");
        return TPS25751TaskResult{TPS25751TaskStatus::I2CError, 0};
    }

    uint8_t returnCode = raw[0];
    if (outData != nullptr && outLen > 0) {
        const size_t copyLen = (outLen < sizeof(raw)) ? outLen : sizeof(raw);
        memcpy(outData, raw, copyLen);
    }

    TPS_DEBUG_INFO(DEBUG_CAT_TASK, "executeCommand: success, returnCode=0x%02X", returnCode);
    return TPS25751TaskResult{TPS25751TaskStatus::Success, returnCode};
}