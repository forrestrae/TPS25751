/**
 * @file test_TPS25751RegisterFactory.cpp
 * @brief Native (host-run) unit tests for TPS25751RegisterFactoryImpl.
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751RegisterFactory"
 *
 * The factory has three createRegister() overloads (by Address, by Address with
 * data, and by RegisterInfo), each a switch over TPS25751Registers::Address that
 * ends in a default returning nullptr. The compiler does not check those switches
 * for completeness, so a missing or wrong case shows up only at run time, as a
 * null object from TPS25751::readRegisterByAddress().
 *
 * For every address in TRM SLVUCR8B (Rev. B) these tests check that each path
 * returns a non-null object of the size in getRegisterInfo(), and that the object
 * is the expected class. The platform has no RTTI, so class identity is checked by
 * behavior: the factory object's debugPrint() output must equal that of the
 * expected class constructed directly from the same bytes. A sweep over all 256
 * address byte values checks that the factory and getRegisterInfo() agree on which
 * addresses exist, so an Address enumerator added without a factory case fails.
 */

#include <unity.h>

#include <cstdio>
#include <memory>
#include <string>

#include "TPS25751RegisterAddress.h"
#include "TPS25751RegisterFactory.h"

#include "TPS25751ADCResults.h"
#include "TPS25751ActivePDOContract.h"
#include "TPS25751ActiveRDOContract.h"
#include "TPS25751AutoNegotiateSink.h"
#include "TPS25751BootFlags.h"
#include "TPS25751Command.h"
#include "TPS25751CustomerUse.h"
#include "TPS25751Data.h"
#include "TPS25751GPIOStatus.h"
#include "TPS25751GlobalSystemConfig.h"
#include "TPS25751IOConfig.h"
#include "TPS25751InterruptClear.h"
#include "TPS25751InterruptEvent.h"
#include "TPS25751InterruptMask.h"
#include "TPS25751LiquidDetectionConfig.h"
#include "TPS25751Mode.h"
#include "TPS25751PD3Config.h"
#include "TPS25751PDStatus.h"
#include "TPS25751PortConfig.h"
#include "TPS25751PortControl.h"
#include "TPS25751PowerPathStatus.h"
#include "TPS25751PowerStatus.h"
#include "TPS25751ReceivedSOPIdentity.h"
#include "TPS25751ReceivedSinkCaps.h"
#include "TPS25751ReceivedSourceCaps.h"
#include "TPS25751SleepControl.h"
#include "TPS25751Status.h"
#include "TPS25751TransmitBatteryCapabilities.h"
#include "TPS25751TransmitBatteryStatus.h"
#include "CaptureStdout.h"
#include "TPS25751TransmitPPSStatus.h"
#include "TPS25751TransmitSinkCaps.h"
#include "TPS25751TransmitSinkCapsExtended.h"
#include "TPS25751TransmitSourceCaps.h"
#include "TPS25751TransmitSourceCapsExtended.h"
#include "TPS25751TransmitSourceInfo.h"
#include "TPS25751TypeCState.h"
#include "TPS25751Version.h"

namespace {

using TPS25751Registers::Address;
using MakeFn = std::unique_ptr<TPS25751Register> (*)(const uint8_t *, size_t);

template <typename T>
std::unique_ptr<TPS25751Register> make(const uint8_t *data, size_t len)
{
    return std::unique_ptr<TPS25751Register>(new T(data, len));
}

struct Entry
{
    Address addr;
    MakeFn makeExpected;
};

// One entry per register in TRM SLVUCR8B (Rev. B), in address order, with the
// class the factory must create for it.
const Entry kAll[] = {
    {Address::MODE, make<TPS25751Mode>},
    {Address::CUSTOMER_USE, make<TPS25751CustomerUse>},
    {Address::COMMAND, make<TPS25751Command>},
    {Address::DATA, make<TPS25751Data>},
    {Address::VERSION, make<TPS25751Version>},
    {Address::INT_EVENT1, make<TPS25751InterruptEvent>},
    {Address::INT_MASK1, make<TPS25751InterruptMask>},
    {Address::INT_CLEAR1, make<TPS25751InterruptClear>},
    {Address::STATUS, make<TPS25751Status>},
    {Address::POWER_PATH_STATUS, make<TPS25751PowerPathStatus>},
    {Address::GLOBAL_SYSTEM_CONFIGURATION, make<TPS25751GlobalSystemConfig>},
    {Address::PORT_CONFIGURATION, make<TPS25751PortConfig>},
    {Address::PORT_CONTROL, make<TPS25751PortControl>},
    {Address::BOOT_STATUS, make<TPS25751BootFlags>},
    {Address::RECEIVED_SOURCE_CAPABILITIES, make<TPS25751ReceivedSourceCaps>},
    {Address::RECEIVED_SINK_CAPABILITIES, make<TPS25751ReceivedSinkCaps>},
    {Address::TRANSMIT_SOURCE_CAPABILITIES, make<TPS25751TransmitSourceCaps>},
    {Address::TRANSMIT_SINK_CAPABILITIES, make<TPS25751TransmitSinkCaps>},
    {Address::ACTIVE_PDO_CONTRACT, make<TPS25751ActivePDOContract>},
    {Address::ACTIVE_RDO_CONTRACT, make<TPS25751ActiveRDOContract>},
    {Address::AUTONEGOTIATE_SINK, make<TPS25751AutoNegotiateSink>},
    {Address::POWER_STATUS, make<TPS25751PowerStatus>},
    {Address::PD_STATUS, make<TPS25751PDStatus>},
    {Address::PD3_CONFIGURATION, make<TPS25751PD3Config>},
    {Address::RECEIVED_SOP_IDENTITY, make<TPS25751ReceivedSOPIdentity>},
    {Address::IO_CONFIG, make<TPS25751IOConfig>},
    {Address::TYPEC_STATE, make<TPS25751TypeCState>},
    {Address::ADC_RESULTS, make<TPS25751ADCResults>},
    {Address::SLEEP_CONTROL, make<TPS25751SleepControl>},
    {Address::GPIO_STATUS, make<TPS25751GPIOStatus>},
    {Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED, make<TPS25751TransmitSourceCapsExtended>},
    {Address::TRANSMIT_SOURCE_INFO, make<TPS25751TransmitSourceInfo>},
    {Address::TRANSMIT_PPS_STATUS, make<TPS25751TransmitPPSStatus>},
    {Address::TRANSMIT_BATTERY_STATUS, make<TPS25751TransmitBatteryStatus>},
    {Address::TRANSMIT_BATTERY_CAPABILITIES, make<TPS25751TransmitBatteryCapabilities>},
    {Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED, make<TPS25751TransmitSinkCapsExtended>},
    {Address::LIQUID_DETECTION_CONFIG, make<TPS25751LiquidDetectionConfig>},
};

constexpr size_t kCount = sizeof(kAll) / sizeof(kAll[0]);
constexpr size_t kMaxSize = 64;  // DATA, the largest register

TPS25751RegisterFactoryImpl factory;

// A byte pattern that differs per register and per byte, so classes of the same
// size print different decodes.
void fillPattern(uint8_t *buf, size_t len, uint8_t seed)
{
    for (size_t i = 0; i < len; i++)
    {
        buf[i] = static_cast<uint8_t>(seed * 31u + i * 7u + 1u);
    }
}

void messageFor(char *msg, size_t n, const char *path, Address addr)
{
    snprintf(msg, n, "%s(0x%02X)", path, static_cast<unsigned>(static_cast<uint8_t>(addr)));
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_table_lists_all_37_registers()
{
    TEST_ASSERT_EQUAL_size_t(37, kCount);
}

void test_create_by_address_returns_the_right_size_and_class()
{
    const uint8_t zeros[kMaxSize] = {};
    for (size_t i = 0; i < kCount; i++)
    {
        char msg[48];
        messageFor(msg, sizeof msg, "createRegister(addr)", kAll[i].addr);
        const TPS25751Registers::RegisterInfo *info = TPS25751Registers::getRegisterInfo(kAll[i].addr);
        TEST_ASSERT_NOT_NULL_MESSAGE(info, msg);

        std::unique_ptr<TPS25751Register> reg = factory.createRegister(kAll[i].addr);
        TEST_ASSERT_NOT_NULL_MESSAGE(reg.get(), msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(info->size, reg->size(), msg);

        std::unique_ptr<TPS25751Register> expected = kAll[i].makeExpected(zeros, info->size);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(captureDebugPrint(*expected).c_str(),
                                         captureDebugPrint(*reg).c_str(), msg);
    }
}

void test_create_with_data_returns_the_right_size_and_class()
{
    for (size_t i = 0; i < kCount; i++)
    {
        char msg[48];
        messageFor(msg, sizeof msg, "createRegister(addr, data)", kAll[i].addr);
        const TPS25751Registers::RegisterInfo *info = TPS25751Registers::getRegisterInfo(kAll[i].addr);
        TEST_ASSERT_NOT_NULL_MESSAGE(info, msg);

        uint8_t data[kMaxSize];
        fillPattern(data, info->size, static_cast<uint8_t>(i));

        std::unique_ptr<TPS25751Register> reg = factory.createRegister(kAll[i].addr, data, info->size);
        TEST_ASSERT_NOT_NULL_MESSAGE(reg.get(), msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(info->size, reg->size(), msg);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(data, reg->raw(), info->size, msg);

        std::unique_ptr<TPS25751Register> expected = kAll[i].makeExpected(data, info->size);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(captureDebugPrint(*expected).c_str(),
                                         captureDebugPrint(*reg).c_str(), msg);
    }
}

void test_create_by_register_info_returns_the_right_size_and_class()
{
    const uint8_t zeros[kMaxSize] = {};
    for (size_t i = 0; i < kCount; i++)
    {
        char msg[48];
        messageFor(msg, sizeof msg, "createRegister(regInfo)", kAll[i].addr);
        const TPS25751Registers::RegisterInfo *info = TPS25751Registers::getRegisterInfo(kAll[i].addr);
        TEST_ASSERT_NOT_NULL_MESSAGE(info, msg);

        std::unique_ptr<TPS25751Register> reg = factory.createRegister(*info);
        TEST_ASSERT_NOT_NULL_MESSAGE(reg.get(), msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(info->size, reg->size(), msg);

        std::unique_ptr<TPS25751Register> expected = kAll[i].makeExpected(zeros, info->size);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(captureDebugPrint(*expected).c_str(),
                                         captureDebugPrint(*reg).c_str(), msg);
    }
}

void test_factory_and_register_info_agree_on_every_address_byte()
{
    // Every byte value the Address enum could hold: the factory must create an
    // object exactly when getRegisterInfo() knows the address.
    const uint8_t zeros[kMaxSize] = {};
    size_t known = 0;
    for (unsigned v = 0; v <= 0xFF; v++)
    {
        const Address addr = static_cast<Address>(v);
        const TPS25751Registers::RegisterInfo *info = TPS25751Registers::getRegisterInfo(addr);
        char msg[48];
        snprintf(msg, sizeof msg, "address 0x%02X", v);

        std::unique_ptr<TPS25751Register> byAddr = factory.createRegister(addr);
        std::unique_ptr<TPS25751Register> withData = factory.createRegister(addr, zeros, info ? info->size : 4);
        if (info)
        {
            known++;
            TEST_ASSERT_NOT_NULL_MESSAGE(byAddr.get(), msg);
            TEST_ASSERT_NOT_NULL_MESSAGE(withData.get(), msg);
        }
        else
        {
            TEST_ASSERT_NULL_MESSAGE(byAddr.get(), msg);
            TEST_ASSERT_NULL_MESSAGE(withData.get(), msg);
            const TPS25751Registers::RegisterInfo unknown{addr, 4};
            TEST_ASSERT_NULL_MESSAGE(factory.createRegister(unknown).get(), msg);
        }
    }
    TEST_ASSERT_EQUAL_size_t(kCount, known);
}

void test_unknown_address_returns_null_on_every_path()
{
    // 0x00 is not a TPS25751 register address (the TRM starts at 0x03 MODE).
    const Address unknown = static_cast<Address>(0x00);
    const uint8_t data[4] = {1, 2, 3, 4};
    const TPS25751Registers::RegisterInfo info{unknown, 4};

    TEST_ASSERT_NULL(TPS25751Registers::getRegisterInfo(unknown));
    TEST_ASSERT_NULL(factory.createRegister(unknown).get());
    TEST_ASSERT_NULL(factory.createRegister(unknown, data, sizeof data).get());
    TEST_ASSERT_NULL(factory.createRegister(info).get());
}

void test_create_with_null_data_returns_null()
{
    TEST_ASSERT_NULL(factory.createRegister(Address::STATUS, nullptr, 5).get());
}

void test_singleton_and_convenience_creators_use_the_factory()
{
    std::unique_ptr<TPS25751Register> reg = TPS25751Factory::getInstance().createRegister(Address::STATUS);
    TEST_ASSERT_NOT_NULL(reg.get());
    TEST_ASSERT_EQUAL_size_t(TPS25751Registers::Registers::STATUS.size, reg->size());

    TEST_ASSERT_NOT_NULL(TPS25751RegisterCreator::create(Address::MODE).get());
    const uint8_t data[4] = {'A', 'P', 'P', ' '};
    TEST_ASSERT_NOT_NULL(TPS25751RegisterCreator::create(Address::MODE, data, sizeof data).get());
    TEST_ASSERT_NOT_NULL(TPS25751RegisterCreator::create(TPS25751Registers::Registers::MODE).get());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_table_lists_all_37_registers);
    RUN_TEST(test_create_by_address_returns_the_right_size_and_class);
    RUN_TEST(test_create_with_data_returns_the_right_size_and_class);
    RUN_TEST(test_create_by_register_info_returns_the_right_size_and_class);
    RUN_TEST(test_factory_and_register_info_agree_on_every_address_byte);
    RUN_TEST(test_unknown_address_returns_null_on_every_path);
    RUN_TEST(test_create_with_null_data_returns_null);
    RUN_TEST(test_singleton_and_convenience_creators_use_the_factory);
    return UNITY_END();
}
