/**
 * @file test_TPS25751RegisterAddress.cpp
 * @brief Native (host-run) unit tests for the address table in TPS25751RegisterAddress.h.
 *
 * Run with: pio test -e native
 *
 * Every Address enumerator needs a case in both getRegisterSize() and
 * getRegisterInfo(). Neither switch is checked for completeness by the compiler
 * (both end in a default), so a missing case shows up only at run time:
 * getRegisterInfo() returning nullptr makes TPS25751::readRegisterByAddress()
 * return null and TPS25751::writeRegister<T>() return false without writing.
 * CUSTOMER_USE (0x06) had a getRegisterSize() case but no getRegisterInfo() case
 * until 2026-11-19, which a bench walk over all 37 addresses found.
 *
 * The list below has one entry per register in TRM SLVUCR8B (Rev. B), with the
 * byte count the library uses (the hardware length byte where it differs from
 * the TRM width: 0x27 is 14, 0x35 is 16, 0x98 is 12).
 */

#include <unity.h>

#include <cstdio>

#include "TPS25751RegisterAddress.h"

namespace {

using TPS25751Registers::Address;

struct Entry
{
    Address addr;
    uint8_t value;
    uint8_t size;
};

const Entry kAll[] = {
    {Address::MODE, 0x03, 4},
    {Address::CUSTOMER_USE, 0x06, 8},
    {Address::COMMAND, 0x08, 4},
    {Address::DATA, 0x09, 64},
    {Address::VERSION, 0x0F, 4},
    {Address::INT_EVENT1, 0x14, 11},
    {Address::INT_MASK1, 0x16, 11},
    {Address::INT_CLEAR1, 0x18, 11},
    {Address::STATUS, 0x1A, 5},
    {Address::POWER_PATH_STATUS, 0x26, 5},
    {Address::GLOBAL_SYSTEM_CONFIGURATION, 0x27, 14},
    {Address::PORT_CONFIGURATION, 0x28, 17},
    {Address::PORT_CONTROL, 0x29, 4},
    {Address::BOOT_STATUS, 0x2D, 5},
    {Address::RECEIVED_SOURCE_CAPABILITIES, 0x30, 53},
    {Address::RECEIVED_SINK_CAPABILITIES, 0x31, 53},
    {Address::TRANSMIT_SOURCE_CAPABILITIES, 0x32, 63},
    {Address::TRANSMIT_SINK_CAPABILITIES, 0x33, 53},
    {Address::ACTIVE_PDO_CONTRACT, 0x34, 6},
    {Address::ACTIVE_RDO_CONTRACT, 0x35, 16},
    {Address::AUTONEGOTIATE_SINK, 0x37, 24},
    {Address::POWER_STATUS, 0x3F, 2},
    {Address::PD_STATUS, 0x40, 4},
    {Address::PD3_CONFIGURATION, 0x42, 4},
    {Address::RECEIVED_SOP_IDENTITY, 0x48, 25},
    {Address::IO_CONFIG, 0x5C, 49},
    {Address::TYPEC_STATE, 0x69, 4},
    {Address::ADC_RESULTS, 0x6A, 13},
    {Address::SLEEP_CONTROL, 0x70, 1},
    {Address::GPIO_STATUS, 0x72, 8},
    {Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED, 0x77, 15},
    {Address::TRANSMIT_SOURCE_INFO, 0x78, 8},
    {Address::TRANSMIT_PPS_STATUS, 0x7A, 4},
    {Address::TRANSMIT_BATTERY_STATUS, 0x7B, 16},
    {Address::TRANSMIT_BATTERY_CAPABILITIES, 0x7D, 36},
    {Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED, 0x7E, 14},
    {Address::LIQUID_DETECTION_CONFIG, 0x98, 12},
};

constexpr size_t kCount = sizeof(kAll) / sizeof(kAll[0]);

}  // namespace

void setUp() {}
void tearDown() {}

void test_table_lists_all_37_registers()
{
    TEST_ASSERT_EQUAL_size_t(37, kCount);
}

void test_enumerator_values_match_the_trm_offsets()
{
    for (size_t i = 0; i < kCount; i++)
    {
        TEST_ASSERT_EQUAL_HEX8(kAll[i].value, static_cast<uint8_t>(kAll[i].addr));
    }
}

void test_every_address_has_a_size()
{
    for (size_t i = 0; i < kCount; i++)
    {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(kAll[i].size, TPS25751Registers::getRegisterSize(kAll[i].addr),
                                        "getRegisterSize");
    }
}

void test_every_address_has_register_info()
{
    for (size_t i = 0; i < kCount; i++)
    {
        const TPS25751Registers::RegisterInfo *info = TPS25751Registers::getRegisterInfo(kAll[i].addr);
        char msg[48];
        snprintf(msg, sizeof msg, "getRegisterInfo(0x%02X)", static_cast<unsigned>(kAll[i].value));
        TEST_ASSERT_NOT_NULL_MESSAGE(info, msg);
        TEST_ASSERT_TRUE_MESSAGE(info->address == kAll[i].addr, msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(kAll[i].size, info->size, msg);
    }
}

void test_customer_use_has_register_info()
{
    const TPS25751Registers::RegisterInfo *info = TPS25751Registers::getRegisterInfo(Address::CUSTOMER_USE);

    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_UINT8(8, info->size);
}

void test_unknown_address_has_no_info()
{
    const Address unknown = static_cast<Address>(0x01);

    TEST_ASSERT_NULL(TPS25751Registers::getRegisterInfo(unknown));
    TEST_ASSERT_EQUAL_UINT8(0, TPS25751Registers::getRegisterSize(unknown));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_table_lists_all_37_registers);
    RUN_TEST(test_enumerator_values_match_the_trm_offsets);
    RUN_TEST(test_every_address_has_a_size);
    RUN_TEST(test_every_address_has_register_info);
    RUN_TEST(test_customer_use_has_register_info);
    RUN_TEST(test_unknown_address_has_no_info);

    return UNITY_END();
}
