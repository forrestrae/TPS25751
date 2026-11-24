/**
 * @file test_TPS25751Typed.cpp
 * @brief Native tests of the typed read and write wrappers and the
 *        factory-based read functions of TPS25751 (src/TPS25751.cpp), driven
 *        through the TwoWire fake (Wire.h) and the scripted target
 *        (FakeTps25751Target.h).
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751Typed"
 *
 * Each wrapper is checked on the bus: the register address in the pointer
 * write, the read of size + 1 bytes (the length byte), the bytes the decoded
 * object holds, and, for writes, the [register, length, data...] frame sent
 * with a STOP. Every wrapper also gets its failure path: a NACK on the
 * pointer write or on the write, which must return false (or nullptr) and not
 * a half-filled object.
 */

#include <unity.h>
#include <cstring>
#include <memory>
#include <vector>

#include "TPS25751.h"
#include "TPS25751Debug.h"
#include "FakeTps25751Target.h"

namespace {

using Address = TPS25751Registers::Address;
using Txn = FakeTps25751Target::Txn;
using Kind = FakeTps25751Target::Kind;
using Fault = FakeTps25751Target::Fault;

struct Rig
{
    FakeTps25751Target dev;
    TPS25751 pd;

    Rig() : pd(Wire, 0x20)
    {
        fake_arduino::reset();
        Wire = TwoWire();
        Wire.target = &dev;
    }
    ~Rig() { Wire.target = nullptr; }

    // Load register @p a with a pattern that differs per register and per byte.
    std::vector<uint8_t> load(Address a)
    {
        const size_t n = TPS25751Registers::getRegisterSize(a);
        std::vector<uint8_t> v(n);
        for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(static_cast<uint8_t>(a) * 3 + i * 7 + 1);
        dev.setReg(static_cast<uint8_t>(a), v);
        return v;
    }

    void nackPointerWrite(Address a)
    {
        Fault f;
        f.reg = static_cast<int>(a);
        f.kind = 0;
        f.writeError = 2;
        dev.faults.push_back(f);
    }
};

// The two transactions of a register read.
void assertRegisterRead(const std::vector<Txn> &log, Address a, const char *msg)
{
    const uint8_t reg = static_cast<uint8_t>(a);
    const size_t size = TPS25751Registers::getRegisterSize(a);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(2, log.size(), msg);
    TEST_ASSERT_TRUE_MESSAGE(log[0].kind == Kind::Write, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x20, log[0].addr, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, log[0].bytes.size(), msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(reg, log[0].bytes[0], msg);
    TEST_ASSERT_FALSE_MESSAGE(log[0].stop, msg);
    TEST_ASSERT_TRUE_MESSAGE(log[1].kind == Kind::Read, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(reg, log[1].reg, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(size + 1, log[1].requested, msg);
}

// A default-constructed register is zero-filled; a failed read must leave it so.
void assertUntouched(const TPS25751Register &reg, const char *msg)
{
    TEST_ASSERT_TRUE_MESSAGE(reg.isValid(), msg);
    for (size_t i = 0; i < reg.size(); ++i) TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, reg.raw()[i], msg);
}

void assertHolds(const TPS25751Register &reg, const std::vector<uint8_t> &expected, const char *msg)
{
    TEST_ASSERT_TRUE_MESSAGE(reg.isValid(), msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), reg.size(), msg);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected.data(), reg.raw(), expected.size(), msg);
}

// --- Typed readers: readX(T &) ---------------------------------------------------------

template <typename T, typename Fn>
void checkTypedRead(Fn fn, Address a, const char *name)
{
    {
        Rig r;
        const std::vector<uint8_t> data = r.load(a);
        T obj;
        TEST_ASSERT_TRUE_MESSAGE((r.pd.*fn)(obj), name);
        assertRegisterRead(r.dev.log, a, name);
        assertHolds(obj, data, name);
    }
    {
        Rig r;
        r.load(a);
        r.nackPointerWrite(a);
        T obj;
        TEST_ASSERT_FALSE_MESSAGE((r.pd.*fn)(obj), name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, r.dev.log.size(), name);  // no read after a failed pointer write
        assertUntouched(obj, name);                                    // the object was not filled
    }
    {
        // The controller answers with a length byte other than the register size.
        Rig r;
        std::vector<uint8_t> wrong(TPS25751Registers::getRegisterSize(a) + 1, 0x55);
        r.dev.setReg(static_cast<uint8_t>(a), wrong);
        T obj;
        TEST_ASSERT_FALSE_MESSAGE((r.pd.*fn)(obj), name);
        assertUntouched(obj, name);
    }
}

#define TYPED_READ(Name, Type, Addr) \
    void test_typed_read_##Name() { checkTypedRead<Type>(&TPS25751::read##Name, Address::Addr, #Name); }

TYPED_READ(Mode, TPS25751Mode, MODE)
TYPED_READ(CustomerUse, TPS25751CustomerUse, CUSTOMER_USE)
TYPED_READ(InterruptEvent, TPS25751InterruptEvent, INT_EVENT1)
TYPED_READ(InterruptMask, TPS25751InterruptMask, INT_MASK1)
TYPED_READ(Status, TPS25751Status, STATUS)
TYPED_READ(PowerPathStatus, TPS25751PowerPathStatus, POWER_PATH_STATUS)
TYPED_READ(GlobalSystemConfig, TPS25751GlobalSystemConfig, GLOBAL_SYSTEM_CONFIGURATION)
TYPED_READ(PortConfig, TPS25751PortConfig, PORT_CONFIGURATION)
TYPED_READ(PortControl, TPS25751PortControl, PORT_CONTROL)
TYPED_READ(BootFlags, TPS25751BootFlags, BOOT_FLAGS)
TYPED_READ(ReceivedSourceCaps, TPS25751ReceivedSourceCaps, RECEIVED_SOURCE_CAPABILITIES)
TYPED_READ(ReceivedSinkCaps, TPS25751ReceivedSinkCaps, RECEIVED_SINK_CAPABILITIES)
TYPED_READ(TransmitSourceCaps, TPS25751TransmitSourceCaps, TRANSMIT_SOURCE_CAPABILITIES)
TYPED_READ(TransmitSinkCaps, TPS25751TransmitSinkCaps, TRANSMIT_SINK_CAPABILITIES)
TYPED_READ(ActivePDOContract, TPS25751ActivePDOContract, ACTIVE_PDO_CONTRACT)
TYPED_READ(ActiveRDOContract, TPS25751ActiveRDOContract, ACTIVE_RDO_CONTRACT)
TYPED_READ(AutoNegotiateSink, TPS25751AutoNegotiateSink, AUTONEGOTIATE_SINK)
TYPED_READ(PowerStatus, TPS25751PowerStatus, POWER_STATUS)
TYPED_READ(PDStatus, TPS25751PDStatus, PD_STATUS)
TYPED_READ(PD3Config, TPS25751PD3Config, PD3_CONFIGURATION)
TYPED_READ(IOConfig, TPS25751IOConfig, IO_CONFIG)
TYPED_READ(TypeCState, TPS25751TypeCState, TYPEC_STATE)
TYPED_READ(ADCResults, TPS25751ADCResults, ADC_RESULTS)
TYPED_READ(SleepControl, TPS25751SleepControl, SLEEP_CONTROL)
TYPED_READ(GPIOStatus, TPS25751GPIOStatus, GPIO_STATUS)
TYPED_READ(TransmitSourceCapsExtended, TPS25751TransmitSourceCapsExtended, TRANSMIT_SOURCE_CAPABILITIES_EXTENDED)
TYPED_READ(TransmitSourceInfo, TPS25751TransmitSourceInfo, TRANSMIT_SOURCE_INFO)
TYPED_READ(TransmitPPSStatus, TPS25751TransmitPPSStatus, TRANSMIT_PPS_STATUS)
TYPED_READ(TransmitBatteryStatus, TPS25751TransmitBatteryStatus, TRANSMIT_BATTERY_STATUS)
TYPED_READ(TransmitBatteryCapabilities, TPS25751TransmitBatteryCapabilities, TRANSMIT_BATTERY_CAPABILITIES)
TYPED_READ(TransmitSinkCapsExtended, TPS25751TransmitSinkCapsExtended, TRANSMIT_SINK_CAPABILITIES_EXTENDED)
TYPED_READ(LiquidDetectionConfig, TPS25751LiquidDetectionConfig, LIQUID_DETECTION_CONFIG)

// --- Typed writers: writeX(const T &) ---------------------------------------------------

template <typename T, typename Fn>
void checkTypedWrite(Fn fn, Address a, const char *name)
{
    const size_t size = TPS25751Registers::getRegisterSize(a);
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) data[i] = static_cast<uint8_t>(0xA0 + i * 5);
    const T obj(data.data(), size);

    {
        Rig r;
        TEST_ASSERT_TRUE_MESSAGE((r.pd.*fn)(obj), name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, r.dev.log.size(), name);
        const Txn &t = r.dev.log[0];
        TEST_ASSERT_TRUE_MESSAGE(t.kind == Kind::Write, name);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x20, t.addr, name);
        TEST_ASSERT_TRUE_MESSAGE(t.stop, name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(size + 2, t.bytes.size(), name);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(static_cast<uint8_t>(a), t.bytes[0], name);
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(static_cast<uint8_t>(size), t.bytes[1], name);  // the length byte
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(data.data(), t.bytes.data() + 2, size, name);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(data.data(), r.dev.regs[static_cast<uint8_t>(a)].data(), size, name);
    }
    {
        // A NACKed write returns false.
        Rig r;
        Fault f;
        f.reg = static_cast<int>(a);
        f.kind = 0;
        f.writeError = 2;
        r.dev.faults.push_back(f);
        TEST_ASSERT_FALSE_MESSAGE((r.pd.*fn)(obj), name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, r.dev.log.size(), name);
    }
    {
        // A register object of the wrong size is refused before any bus traffic.
        Rig r;
        const T shortObj(data.data(), size - 1);
        TEST_ASSERT_FALSE_MESSAGE((r.pd.*fn)(shortObj), name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, r.dev.log.size(), name);
    }
    {
        // A default-constructed object is zero-filled at the register size and is written as such.
        Rig r;
        const T blank;
        TEST_ASSERT_TRUE_MESSAGE((r.pd.*fn)(blank), name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(size + 2, r.dev.log[0].bytes.size(), name);
        for (size_t i = 2; i < size + 2; ++i) TEST_ASSERT_EQUAL_HEX8_MESSAGE(0, r.dev.log[0].bytes[i], name);
    }
}

#define TYPED_WRITE(Name, Type, Addr) \
    void test_typed_write_##Name() { checkTypedWrite<Type>(&TPS25751::write##Name, Address::Addr, #Name); }

TYPED_WRITE(InterruptMask, TPS25751InterruptMask, INT_MASK1)
TYPED_WRITE(InterruptClear, TPS25751InterruptClear, INT_CLEAR1)
TYPED_WRITE(GlobalSystemConfig, TPS25751GlobalSystemConfig, GLOBAL_SYSTEM_CONFIGURATION)
TYPED_WRITE(TransmitSourceCaps, TPS25751TransmitSourceCaps, TRANSMIT_SOURCE_CAPABILITIES)
TYPED_WRITE(TransmitSinkCaps, TPS25751TransmitSinkCaps, TRANSMIT_SINK_CAPABILITIES)
TYPED_WRITE(AutoNegotiateSink, TPS25751AutoNegotiateSink, AUTONEGOTIATE_SINK)
TYPED_WRITE(PD3Config, TPS25751PD3Config, PD3_CONFIGURATION)
TYPED_WRITE(SleepControl, TPS25751SleepControl, SLEEP_CONTROL)
TYPED_WRITE(TransmitSourceCapsExtended, TPS25751TransmitSourceCapsExtended, TRANSMIT_SOURCE_CAPABILITIES_EXTENDED)
TYPED_WRITE(TransmitSourceInfo, TPS25751TransmitSourceInfo, TRANSMIT_SOURCE_INFO)
TYPED_WRITE(TransmitPPSStatus, TPS25751TransmitPPSStatus, TRANSMIT_PPS_STATUS)
TYPED_WRITE(TransmitBatteryStatus, TPS25751TransmitBatteryStatus, TRANSMIT_BATTERY_STATUS)
TYPED_WRITE(TransmitBatteryCapabilities, TPS25751TransmitBatteryCapabilities, TRANSMIT_BATTERY_CAPABILITIES)
TYPED_WRITE(TransmitSinkCapsExtended, TPS25751TransmitSinkCapsExtended, TRANSMIT_SINK_CAPABILITIES_EXTENDED)
TYPED_WRITE(LiquidDetectionConfig, TPS25751LiquidDetectionConfig, LIQUID_DETECTION_CONFIG)

// --- Factory reads: readXRegister(validate) ---------------------------------------------

template <typename Fn>
void checkFactoryRead(Fn fn, Address a, const char *name)
{
    {
        Rig r;
        const std::vector<uint8_t> data = r.load(a);
        auto reg = (r.pd.*fn)(false);
        TEST_ASSERT_NOT_NULL_MESSAGE(reg.get(), name);
        assertRegisterRead(r.dev.log, a, name);
        assertHolds(*reg, data, name);
    }
    {
        Rig r;
        r.load(a);
        r.nackPointerWrite(a);
        TEST_ASSERT_NULL_MESSAGE((r.pd.*fn)(false).get(), name);
        TEST_ASSERT_NULL_MESSAGE((r.pd.*fn)(true).get(), name);
    }
}

#define FACTORY_READ(Name, Addr) \
    void test_factory_read_##Name() { checkFactoryRead(&TPS25751::read##Name##Register, Address::Addr, #Name); }

FACTORY_READ(Mode, MODE)
FACTORY_READ(Command, COMMAND)
FACTORY_READ(Data, DATA)
FACTORY_READ(Version, VERSION)
FACTORY_READ(InterruptEvent, INT_EVENT1)
FACTORY_READ(Status, STATUS)
FACTORY_READ(PowerPathStatus, POWER_PATH_STATUS)
FACTORY_READ(GlobalSystemConfig, GLOBAL_SYSTEM_CONFIGURATION)
FACTORY_READ(PortConfig, PORT_CONFIGURATION)
FACTORY_READ(PortControl, PORT_CONTROL)
FACTORY_READ(BootFlags, BOOT_FLAGS)
FACTORY_READ(ReceivedSourceCaps, RECEIVED_SOURCE_CAPABILITIES)
FACTORY_READ(ReceivedSinkCaps, RECEIVED_SINK_CAPABILITIES)
FACTORY_READ(TransmitSourceCaps, TRANSMIT_SOURCE_CAPABILITIES)
FACTORY_READ(TransmitSinkCaps, TRANSMIT_SINK_CAPABILITIES)
FACTORY_READ(ActivePDOContract, ACTIVE_PDO_CONTRACT)
FACTORY_READ(ActiveRDOContract, ACTIVE_RDO_CONTRACT)
FACTORY_READ(AutoNegotiateSink, AUTONEGOTIATE_SINK)
FACTORY_READ(PowerStatus, POWER_STATUS)
FACTORY_READ(PDStatus, PD_STATUS)
FACTORY_READ(PD3Config, PD3_CONFIGURATION)
FACTORY_READ(ReceivedSOPIdentity, RECEIVED_SOP_IDENTITY)
FACTORY_READ(IOConfig, IO_CONFIG)
FACTORY_READ(TypeCState, TYPEC_STATE)
FACTORY_READ(ADCResults, ADC_RESULTS)
FACTORY_READ(GPIOStatus, GPIO_STATUS)
FACTORY_READ(TransmitSourceCapsExtended, TRANSMIT_SOURCE_CAPABILITIES_EXTENDED)
FACTORY_READ(TransmitSourceInfo, TRANSMIT_SOURCE_INFO)
FACTORY_READ(TransmitPPSStatus, TRANSMIT_PPS_STATUS)
FACTORY_READ(TransmitBatteryStatus, TRANSMIT_BATTERY_STATUS)
FACTORY_READ(TransmitBatteryCapabilities, TRANSMIT_BATTERY_CAPABILITIES)
FACTORY_READ(TransmitSinkCapsExtended, TRANSMIT_SINK_CAPABILITIES_EXTENDED)
FACTORY_READ(LiquidDetectionConfig, LIQUID_DETECTION_CONFIG)

}  // namespace

// --- Decoded values ---------------------------------------------------------------------

void test_read_mode_decodes_the_ascii_mode()
{
    Rig r;
    r.dev.setMode("APP ");
    TPS25751Mode m;
    TEST_ASSERT_TRUE(r.pd.readMode(m));
    TEST_ASSERT_TRUE(m.isApplicationMode());
    TEST_ASSERT_FALSE(m.isPatchMode());
    r.dev.setMode("PTCH");
    TEST_ASSERT_TRUE(r.pd.readMode(m));
    TEST_ASSERT_TRUE(m.isPatchMode());
    TEST_ASSERT_FALSE(m.isApplicationMode());
}

void test_factory_read_version_decodes_little_endian()
{
    Rig r;
    r.dev.setReg(0x0F, {0x66, 0x11, 0x09, 0xF8});  // hardware read of 2026-11-18
    auto v = r.pd.readVersionRegister(true);
    TEST_ASSERT_NOT_NULL(v.get());
    TEST_ASSERT_EQUAL_HEX32(0xF8091166, v->rawVersion());
    TEST_ASSERT_EQUAL_HEX16(0xF809, v->versionMajor());
}

void test_factory_read_mode_validates()
{
    Rig r;
    r.dev.setMode("APP ");
    auto m = r.pd.readModeRegister(true);
    TEST_ASSERT_NOT_NULL(m.get());
    TEST_ASSERT_TRUE(m->isApplicationMode());
}

void test_factory_read_validation_failure_returns_null_only_when_validating()
{
    // PD3_CONFIGURATION bit 31 is reserved and must read 0 (semantic check).
    Rig r;
    r.dev.setReg(0x42, {0x10, 0x00, 0x08, 0x80});
    TEST_ASSERT_NULL(r.pd.readPD3ConfigRegister(true).get());
    auto unvalidated = r.pd.readPD3ConfigRegister(false);
    TEST_ASSERT_NOT_NULL(unvalidated.get());
    TEST_ASSERT_EQUAL_HEX8(0x80, unvalidated->raw()[3]);
}

void test_write_round_trip_through_the_fake()
{
    Rig r;
    TPS25751SleepControl s;
    TEST_ASSERT_TRUE(r.pd.readSleepControl(s));
    s.setSleepModeAllowed(true);
    TEST_ASSERT_TRUE(r.pd.writeSleepControl(s));
    TPS25751SleepControl back;
    TEST_ASSERT_TRUE(r.pd.readSleepControl(back));
    TEST_ASSERT_TRUE(back.sleepModeAllowed());
}

// --- Address-keyed paths ----------------------------------------------------------------

void test_read_register_by_address_covers_every_register()
{
    const Address all[] = {
        Address::MODE, Address::CUSTOMER_USE, Address::COMMAND, Address::DATA, Address::VERSION,
        Address::INT_EVENT1, Address::INT_MASK1, Address::INT_CLEAR1, Address::STATUS,
        Address::POWER_PATH_STATUS, Address::GLOBAL_SYSTEM_CONFIGURATION, Address::PORT_CONFIGURATION,
        Address::PORT_CONTROL, Address::BOOT_FLAGS, Address::RECEIVED_SOURCE_CAPABILITIES,
        Address::RECEIVED_SINK_CAPABILITIES, Address::TRANSMIT_SOURCE_CAPABILITIES,
        Address::TRANSMIT_SINK_CAPABILITIES, Address::ACTIVE_PDO_CONTRACT, Address::ACTIVE_RDO_CONTRACT,
        Address::AUTONEGOTIATE_SINK, Address::POWER_STATUS, Address::PD_STATUS, Address::PD3_CONFIGURATION,
        Address::RECEIVED_SOP_IDENTITY, Address::IO_CONFIG, Address::TYPEC_STATE, Address::ADC_RESULTS,
        Address::SLEEP_CONTROL, Address::GPIO_STATUS, Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED,
        Address::TRANSMIT_SOURCE_INFO, Address::TRANSMIT_PPS_STATUS, Address::TRANSMIT_BATTERY_STATUS,
        Address::TRANSMIT_BATTERY_CAPABILITIES, Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED,
        Address::LIQUID_DETECTION_CONFIG};
    TEST_ASSERT_EQUAL_size_t(37, sizeof(all) / sizeof(all[0]));
    for (Address a : all) {
        Rig r;
        const std::vector<uint8_t> data = r.load(a);
        auto reg = r.pd.readRegisterByAddress(a);
        char msg[32];
        snprintf(msg, sizeof(msg), "address 0x%02X", static_cast<unsigned>(a));
        TEST_ASSERT_NOT_NULL_MESSAGE(reg.get(), msg);
        assertRegisterRead(r.dev.log, a, msg);
        assertHolds(*reg, data, msg);
    }
}

void test_read_register_by_address_unknown_address_makes_no_bus_traffic()
{
    Rig r;
    TEST_ASSERT_NULL(r.pd.readRegisterByAddress(static_cast<Address>(0x01)).get());
    TEST_ASSERT_NULL(r.pd.readAndValidateRegisterByAddress(static_cast<Address>(0x01), true).get());
    TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
}

void test_read_register_as_returns_the_typed_object()
{
    Rig r;
    r.dev.setMode("BOOT");
    auto m = r.pd.readRegisterAs<TPS25751Mode>(Address::MODE);
    TEST_ASSERT_NOT_NULL(m.get());
    TEST_ASSERT_TRUE(m->isBootMode());
    r.nackPointerWrite(Address::MODE);
    TEST_ASSERT_NULL(r.pd.readRegisterAs<TPS25751Mode>(Address::MODE).get());
}

void test_address_keyed_read_rejects_a_wrong_length()
{
    Rig r;
    uint8_t buf[8] = {};
    TEST_ASSERT_FALSE(r.pd.readRegister(Address::MODE, buf, 5));      // MODE is 4 bytes
    TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    TEST_ASSERT_TRUE(r.pd.readRegister(Address::MODE, buf, 4));
    TEST_ASSERT_EQUAL_size_t(2, r.dev.log.size());
}

void test_address_keyed_write_rejects_a_wrong_length()
{
    Rig r;
    const uint8_t data[2] = {0x01, 0x02};
    TEST_ASSERT_FALSE(r.pd.writeRegister(Address::PD3_CONFIGURATION, data, 2));  // 4 bytes expected
    TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    const uint8_t ok[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(r.pd.writeRegister(Address::PD3_CONFIGURATION, ok, 4));
    TEST_ASSERT_EQUAL_size_t(1, r.dev.log.size());
    TEST_ASSERT_EQUAL_HEX8(0x42, r.dev.log[0].bytes[0]);
    TEST_ASSERT_EQUAL_HEX8(4, r.dev.log[0].bytes[1]);
}

void test_register_info_paths_use_the_register_size()
{
    Rig r;
    uint8_t buf[4] = {};
    r.dev.setMode("APP ");
    TEST_ASSERT_TRUE(r.pd.readRegister(TPS25751Registers::Registers::MODE, buf));
    TEST_ASSERT_EQUAL_MEMORY("APP ", buf, 4);
    const uint8_t data[4] = {9, 8, 7, 6};
    r.dev.log.clear();
    TEST_ASSERT_TRUE(r.pd.writeRegister(TPS25751Registers::Registers::PD3_CONFIGURATION, data));
    TEST_ASSERT_EQUAL_size_t(1, r.dev.log.size());
    TEST_ASSERT_EQUAL_size_t(6, r.dev.log[0].bytes.size());
    TEST_ASSERT_EQUAL_HEX8(9, r.dev.log[0].bytes[2]);
}

void test_begin_starts_the_wire()
{
    Rig r;
    TEST_ASSERT_TRUE(r.pd.begin());
    TEST_ASSERT_EQUAL_UINT(1, Wire.beginCalls);
}

void test_debug_setters_reach_the_debug_state()
{
    const uint8_t level = TPS25751Debug::currentDebugLevel;
    const uint8_t cats = TPS25751Debug::enabledCategories;
    Rig r;
    r.pd.setDebugLevel(DEBUG_LEVEL_WARN);
    TEST_ASSERT_EQUAL_UINT8(DEBUG_LEVEL_WARN, TPS25751Debug::currentDebugLevel);
    r.pd.setDebugCategories(DEBUG_CAT_I2C);
    TEST_ASSERT_EQUAL_UINT8(DEBUG_CAT_I2C, TPS25751Debug::enabledCategories);
    r.pd.setDebugStream(nullptr);
    r.pd.setDebugLevel(level);
    r.pd.setDebugCategories(cats);
}

void setUp() {}
void tearDown() {}

#define RUN_READ(Name) RUN_TEST(test_typed_read_##Name)
#define RUN_WRITE(Name) RUN_TEST(test_typed_write_##Name)
#define RUN_FACTORY(Name) RUN_TEST(test_factory_read_##Name)

int main()
{
    UNITY_BEGIN();
    RUN_READ(Mode); RUN_READ(CustomerUse); RUN_READ(InterruptEvent); RUN_READ(InterruptMask);
    RUN_READ(Status); RUN_READ(PowerPathStatus); RUN_READ(GlobalSystemConfig); RUN_READ(PortConfig);
    RUN_READ(PortControl); RUN_READ(BootFlags); RUN_READ(ReceivedSourceCaps); RUN_READ(ReceivedSinkCaps);
    RUN_READ(TransmitSourceCaps); RUN_READ(TransmitSinkCaps); RUN_READ(ActivePDOContract);
    RUN_READ(ActiveRDOContract); RUN_READ(AutoNegotiateSink); RUN_READ(PowerStatus); RUN_READ(PDStatus);
    RUN_READ(PD3Config); RUN_READ(IOConfig); RUN_READ(TypeCState); RUN_READ(ADCResults);
    RUN_READ(SleepControl); RUN_READ(GPIOStatus); RUN_READ(TransmitSourceCapsExtended);
    RUN_READ(TransmitSourceInfo); RUN_READ(TransmitPPSStatus); RUN_READ(TransmitBatteryStatus);
    RUN_READ(TransmitBatteryCapabilities); RUN_READ(TransmitSinkCapsExtended); RUN_READ(LiquidDetectionConfig);

    RUN_WRITE(InterruptMask); RUN_WRITE(InterruptClear); RUN_WRITE(GlobalSystemConfig);
    RUN_WRITE(TransmitSourceCaps); RUN_WRITE(TransmitSinkCaps); RUN_WRITE(AutoNegotiateSink);
    RUN_WRITE(PD3Config); RUN_WRITE(SleepControl); RUN_WRITE(TransmitSourceCapsExtended);
    RUN_WRITE(TransmitSourceInfo); RUN_WRITE(TransmitPPSStatus); RUN_WRITE(TransmitBatteryStatus);
    RUN_WRITE(TransmitBatteryCapabilities); RUN_WRITE(TransmitSinkCapsExtended); RUN_WRITE(LiquidDetectionConfig);

    RUN_FACTORY(Mode); RUN_FACTORY(Command); RUN_FACTORY(Data); RUN_FACTORY(Version);
    RUN_FACTORY(InterruptEvent); RUN_FACTORY(Status); RUN_FACTORY(PowerPathStatus);
    RUN_FACTORY(GlobalSystemConfig); RUN_FACTORY(PortConfig); RUN_FACTORY(PortControl);
    RUN_FACTORY(BootFlags); RUN_FACTORY(ReceivedSourceCaps); RUN_FACTORY(ReceivedSinkCaps);
    RUN_FACTORY(TransmitSourceCaps); RUN_FACTORY(TransmitSinkCaps); RUN_FACTORY(ActivePDOContract);
    RUN_FACTORY(ActiveRDOContract); RUN_FACTORY(AutoNegotiateSink); RUN_FACTORY(PowerStatus);
    RUN_FACTORY(PDStatus); RUN_FACTORY(PD3Config); RUN_FACTORY(ReceivedSOPIdentity); RUN_FACTORY(IOConfig);
    RUN_FACTORY(TypeCState); RUN_FACTORY(ADCResults); RUN_FACTORY(GPIOStatus);
    RUN_FACTORY(TransmitSourceCapsExtended); RUN_FACTORY(TransmitSourceInfo); RUN_FACTORY(TransmitPPSStatus);
    RUN_FACTORY(TransmitBatteryStatus); RUN_FACTORY(TransmitBatteryCapabilities);
    RUN_FACTORY(TransmitSinkCapsExtended); RUN_FACTORY(LiquidDetectionConfig);

    RUN_TEST(test_read_mode_decodes_the_ascii_mode);
    RUN_TEST(test_factory_read_version_decodes_little_endian);
    RUN_TEST(test_factory_read_mode_validates);
    RUN_TEST(test_factory_read_validation_failure_returns_null_only_when_validating);
    RUN_TEST(test_write_round_trip_through_the_fake);
    RUN_TEST(test_read_register_by_address_covers_every_register);
    RUN_TEST(test_read_register_by_address_unknown_address_makes_no_bus_traffic);
    RUN_TEST(test_read_register_as_returns_the_typed_object);
    RUN_TEST(test_address_keyed_read_rejects_a_wrong_length);
    RUN_TEST(test_address_keyed_write_rejects_a_wrong_length);
    RUN_TEST(test_register_info_paths_use_the_register_size);
    RUN_TEST(test_begin_starts_the_wire);
    RUN_TEST(test_debug_setters_reach_the_debug_state);
    return UNITY_END();
}
