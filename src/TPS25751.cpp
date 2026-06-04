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

bool TPS25751::readStatus(TPS25751Status &statusOut) const
{
    return readRegister(TPS25751Registers::Registers::STATUS, statusOut);
}

bool TPS25751::readPowerPathStatus(TPS25751PowerPathStatus &pps) const
{
    return readRegister(TPS25751Registers::Registers::POWER_PATH_STATUS, pps);
}

bool TPS25751::readBootFlags(TPS25751BootFlags &bootFlags) const
{
    return readRegister(TPS25751Registers::Registers::BOOT_STATUS, bootFlags);
}

bool TPS25751::readPortConfig(TPS25751PortConfig &portConfig) const
{
    return readRegister(TPS25751Registers::Registers::PORT_CONFIGURATION, portConfig);
}

bool TPS25751::readTypeCState(TPS25751TypeCState &typeCState) const
{
    return readRegister(TPS25751Registers::Registers::TYPEC_STATE, typeCState);
}

bool TPS25751::readInterruptEvent(TPS25751InterruptEvent &intEvent) const
{
    return readRegister(TPS25751Registers::Registers::INT_EVENT1, intEvent);
}

bool TPS25751::readPowerStatus(TPS25751PowerStatus &powerStatus) const
{
    return readRegister(TPS25751Registers::Registers::POWER_STATUS, powerStatus);
}

bool TPS25751::readPDStatus(TPS25751PDStatus &pdStatus) const
{
    return readRegister(TPS25751Registers::Registers::PD_STATUS, pdStatus);
}

bool TPS25751::readGPIOStatus(TPS25751GPIOStatus &gpioStatus) const
{
    return readRegister(TPS25751Registers::Registers::GPIO_STATUS, gpioStatus);
}

bool TPS25751::readPortControl(TPS25751PortControl &portControl) const
{
    return readRegister(TPS25751Registers::Registers::PORT_CONTROL, portControl);
}

bool TPS25751::readActivePDOContract(TPS25751ActivePDOContract &pdoContract) const
{
    return readRegister(TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT, pdoContract);
}

bool TPS25751::readActiveRDOContract(TPS25751ActiveRDOContract &rdoContract) const
{
    return readRegister(TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT, rdoContract);
}

bool TPS25751::readReceivedSourceCaps(TPS25751ReceivedSourceCaps &sourceCaps) const
{
    return readRegister(TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES, sourceCaps);
}

bool TPS25751::readReceivedSinkCaps(TPS25751ReceivedSinkCaps &sinkCaps) const
{
    return readRegister(TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES, sinkCaps);
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
std::unique_ptr<TPS25751Status> TPS25751::readStatusRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::STATUS, validate);
    if (!reg) return nullptr;
    
    // Since we control the factory creation, we can use static_cast
    if (reg) {
        TPS25751Status* statusReg = static_cast<TPS25751Status*>(reg.release());
        return std::unique_ptr<TPS25751Status>(statusReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751Mode> TPS25751::readModeRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::MODE, validate);
    if (!reg) return nullptr;
    
    if (reg) {
        TPS25751Mode* modeReg = static_cast<TPS25751Mode*>(reg.release());
        return std::unique_ptr<TPS25751Mode>(modeReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751BootFlags> TPS25751::readBootFlagsRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::BOOT_STATUS, validate);
    if (!reg) return nullptr;
    
    if (reg) {
        TPS25751BootFlags* bootReg = static_cast<TPS25751BootFlags*>(reg.release());
        return std::unique_ptr<TPS25751BootFlags>(bootReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751PowerPathStatus> TPS25751::readPowerPathStatusRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::POWER_PATH_STATUS, validate);
    if (!reg) return nullptr;
    
    if (reg) {
        TPS25751PowerPathStatus* powerReg = static_cast<TPS25751PowerPathStatus*>(reg.release());
        return std::unique_ptr<TPS25751PowerPathStatus>(powerReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751PortConfig> TPS25751::readPortConfigRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::PORT_CONFIGURATION, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751PortConfig* configReg = static_cast<TPS25751PortConfig*>(reg.release());
        return std::unique_ptr<TPS25751PortConfig>(configReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751TypeCState> TPS25751::readTypeCStateRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::TYPEC_STATE, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751TypeCState* typeCReg = static_cast<TPS25751TypeCState*>(reg.release());
        return std::unique_ptr<TPS25751TypeCState>(typeCReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751InterruptEvent> TPS25751::readInterruptEventRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::INT_EVENT1, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751InterruptEvent* intEventReg = static_cast<TPS25751InterruptEvent*>(reg.release());
        return std::unique_ptr<TPS25751InterruptEvent>(intEventReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751PowerStatus> TPS25751::readPowerStatusRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::POWER_STATUS, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751PowerStatus* powerStatusReg = static_cast<TPS25751PowerStatus*>(reg.release());
        return std::unique_ptr<TPS25751PowerStatus>(powerStatusReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751PDStatus> TPS25751::readPDStatusRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::PD_STATUS, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751PDStatus* pdStatusReg = static_cast<TPS25751PDStatus*>(reg.release());
        return std::unique_ptr<TPS25751PDStatus>(pdStatusReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751GPIOStatus> TPS25751::readGPIOStatusRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::GPIO_STATUS, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751GPIOStatus* gpioStatusReg = static_cast<TPS25751GPIOStatus*>(reg.release());
        return std::unique_ptr<TPS25751GPIOStatus>(gpioStatusReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751PortControl> TPS25751::readPortControlRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::PORT_CONTROL, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751PortControl* portControlReg = static_cast<TPS25751PortControl*>(reg.release());
        return std::unique_ptr<TPS25751PortControl>(portControlReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751ActivePDOContract> TPS25751::readActivePDOContractRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::ACTIVE_PDO_CONTRACT, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751ActivePDOContract* pdoContractReg = static_cast<TPS25751ActivePDOContract*>(reg.release());
        return std::unique_ptr<TPS25751ActivePDOContract>(pdoContractReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751ActiveRDOContract> TPS25751::readActiveRDOContractRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::ACTIVE_RDO_CONTRACT, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751ActiveRDOContract* rdoContractReg = static_cast<TPS25751ActiveRDOContract*>(reg.release());
        return std::unique_ptr<TPS25751ActiveRDOContract>(rdoContractReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751ReceivedSourceCaps> TPS25751::readReceivedSourceCapsRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::RECEIVED_SOURCE_CAPABILITIES, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751ReceivedSourceCaps* sourceCapsReg = static_cast<TPS25751ReceivedSourceCaps*>(reg.release());
        return std::unique_ptr<TPS25751ReceivedSourceCaps>(sourceCapsReg);
    }
    return nullptr;
}

std::unique_ptr<TPS25751ReceivedSinkCaps> TPS25751::readReceivedSinkCapsRegister(bool validate) const {
    auto reg = readAndValidateRegisterByAddress(TPS25751Registers::Address::RECEIVED_SINK_CAPABILITIES, validate);
    if (!reg) return nullptr;

    if (reg) {
        TPS25751ReceivedSinkCaps* sinkCapsReg = static_cast<TPS25751ReceivedSinkCaps*>(reg.release());
        return std::unique_ptr<TPS25751ReceivedSinkCaps>(sinkCapsReg);
    }
    return nullptr;
}