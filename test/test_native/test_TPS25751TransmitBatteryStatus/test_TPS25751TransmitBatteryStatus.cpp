/**
 * @file test_TPS25751TransmitBatteryStatus.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitBatteryStatus.
 *
 * Run with: pio test -e native
 *
 * TRANSMITTED_BATTERY_STATUS_DATA_OBJECTS_(BSDO) (0x7B) is a 16-byte R/W register
 * made of four 4-byte entries (entry i at byte 4 * i). In each entry byte 0 is
 * reserved, byte 1 is Battery Info, and bytes 2-3 are Present Capacity
 * (little-endian). The raw accessors return field bits unchanged; the typed
 * accessors decode them per USB PD R3.3 Table 6.24 (see the Typed decode section).
 *
 * Reset value 000000000000000000000000FFFF0200h:
 *   00 02 FF FF 00 00 00 00 00 00 00 00 00 00 00 00
 *
 * Distinct fixture:
 *   entry 0: reserved 00, Info 0x11, Capacity bytes 34 12 -> 0x1234
 *   entry 1: reserved 00, Info 0x22, Capacity bytes 78 56 -> 0x5678
 *   entry 2: reserved 00, Info 0x33, Capacity bytes BC 9A -> 0x9ABC
 *   entry 3: reserved 00, Info 0x44, Capacity bytes F0 DE -> 0xDEF0
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitBatteryStatus.h"
#include "CaptureStdout.h"

namespace {

using Status = TPS25751TransmitBatteryStatus;

constexpr size_t STATUS_SIZE = TPS25751Registers::Registers::TRANSMIT_BATTERY_STATUS.size;  // 16

constexpr uint8_t ZERO_BUFFER[STATUS_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// FFFF0200h in the low word: byte 1 = 0x02 (Battery 0 Battery Info), bytes 2-3 = 0xFF 0xFF.
constexpr uint8_t RESET_BUFFER[STATUS_SIZE] = {0x00, 0x02, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[STATUS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint8_t DISTINCT_BUFFER[STATUS_SIZE] = {
    0x00, 0x11, 0x34, 0x12,  // entry 0: reserved, Info 0x11, Capacity 0x1234
    0x00, 0x22, 0x78, 0x56,  // entry 1: reserved, Info 0x22, Capacity 0x5678
    0x00, 0x33, 0xBC, 0x9A,  // entry 2: reserved, Info 0x33, Capacity 0x9ABC
    0x00, 0x44, 0xF0, 0xDE,  // entry 3: reserved, Info 0x44, Capacity 0xDEF0
};
constexpr uint8_t kInfo[4] = {0x11, 0x22, 0x33, 0x44};
constexpr uint16_t kCapacity[4] = {0x1234, 0x5678, 0x9ABC, 0xDEF0};

}  // namespace

void setUp() {}
void tearDown() {}

void test_address_identity()
{
    TEST_ASSERT_TRUE(Status::kAddress == TPS25751Registers::Address::TRANSMIT_BATTERY_STATUS);
    TEST_ASSERT_EQUAL_UINT8(0x7B, static_cast<uint8_t>(Status::kAddress));
    TEST_ASSERT_EQUAL_UINT8(16, TPS25751Registers::getRegisterSize(Status::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(Status::kAddress));
}

void test_default_constructor_allocates_zeroed_buffer()
{
    Status reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(STATUS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(STATUS_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BUFFER, reg.raw(), STATUS_SIZE);
}

void test_data_constructor_copies_buffer()
{
    Status reg(DISTINCT_BUFFER);

    TEST_ASSERT_NOT_EQUAL(DISTINCT_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, reg.raw(), STATUS_SIZE);
}

void test_explicit_length_constructor()
{
    Status reg(DISTINCT_BUFFER, STATUS_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(STATUS_SIZE));
    TEST_ASSERT_EQUAL_HEX16(0x1234, reg.rawPresentCapacity(0));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    Status reg(TPS25751Registers::Registers::TRANSMIT_BATTERY_STATUS);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(STATUS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ZERO_BUFFER, reg.raw(), STATUS_SIZE);
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[4] = {0, 0, 0, 0};
    Status reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(STATUS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_ones_rejected_at_data_tier()
{
    Status reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_reset_fixture_is_valid_and_decodes()
{
    Status reg(RESET_BUFFER);

    // The reset holds 0xFF bytes but is not all 0xFF, so the data tier accepts it.
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());

    TEST_ASSERT_EQUAL_HEX8(0x02, reg.batteryInfo(0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, reg.rawPresentCapacity(0));
    for (uint8_t battery = 1; battery < 4; battery++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, reg.batteryInfo(battery));
        TEST_ASSERT_EQUAL_UINT16(0, reg.rawPresentCapacity(battery));
    }
}

void test_all_zero_buffer_is_valid_and_decodes_to_zero()
{
    Status reg(ZERO_BUFFER);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    for (uint8_t battery = 0; battery < 4; battery++)
    {
        TEST_ASSERT_EQUAL_UINT8(0, reg.batteryInfo(battery));
        TEST_ASSERT_EQUAL_UINT16(0, reg.rawPresentCapacity(battery));
    }
}

void test_distinct_fixture_decodes_every_field()
{
    Status reg(DISTINCT_BUFFER);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());

    TEST_ASSERT_EQUAL_HEX8(0x11, reg.batteryInfo(0));
    TEST_ASSERT_EQUAL_HEX8(0x22, reg.batteryInfo(1));
    TEST_ASSERT_EQUAL_HEX8(0x33, reg.batteryInfo(2));
    TEST_ASSERT_EQUAL_HEX8(0x44, reg.batteryInfo(3));

    TEST_ASSERT_EQUAL_HEX16(0x1234, reg.rawPresentCapacity(0));
    TEST_ASSERT_EQUAL_HEX16(0x5678, reg.rawPresentCapacity(1));
    TEST_ASSERT_EQUAL_HEX16(0x9ABC, reg.rawPresentCapacity(2));
    TEST_ASSERT_EQUAL_HEX16(0xDEF0, reg.rawPresentCapacity(3));
}

void test_per_entry_round_trip_changes_only_that_entry()
{
    // Start from all zero and set each entry from the distinct fixture. Only that
    // entry's three field bytes (1, 2, 3 of the entry) may change; the reserved
    // byte and every other entry stay zero.
    for (uint8_t battery = 0; battery < 4; battery++)
    {
        Status reg(ZERO_BUFFER);

        reg.setBatteryInfo(battery, kInfo[battery]);
        reg.setRawPresentCapacity(battery, kCapacity[battery]);

        uint8_t expected[STATUS_SIZE] = {};
        memcpy(&expected[4 * battery], &DISTINCT_BUFFER[4 * battery], 4);

        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, reg.raw(), STATUS_SIZE);
        TEST_ASSERT_EQUAL_UINT8(kInfo[battery], reg.batteryInfo(battery));
        TEST_ASSERT_EQUAL_UINT16(kCapacity[battery], reg.rawPresentCapacity(battery));
    }
}

void test_setters_overwrite_from_distinct_fixture_leaving_other_entries()
{
    for (uint8_t battery = 0; battery < 4; battery++)
    {
        Status reg(DISTINCT_BUFFER);

        reg.setBatteryInfo(battery, 0xA5);
        reg.setRawPresentCapacity(battery, 0xC3E1);

        uint8_t expected[STATUS_SIZE];
        memcpy(expected, DISTINCT_BUFFER, STATUS_SIZE);
        expected[4 * battery + 1] = 0xA5;
        expected[4 * battery + 2] = 0xE1;  // low byte first
        expected[4 * battery + 3] = 0xC3;

        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, reg.raw(), STATUS_SIZE);
    }
}

void test_setters_never_touch_reserved_bytes()
{
    Status reg(ZERO_BUFFER);

    for (uint8_t battery = 0; battery < 4; battery++)
    {
        reg.setBatteryInfo(battery, 0xFF);
        reg.setRawPresentCapacity(battery, 0xFFFF);
    }

    TEST_ASSERT_EQUAL_UINT8(0, reg.raw()[0]);
    TEST_ASSERT_EQUAL_UINT8(0, reg.raw()[4]);
    TEST_ASSERT_EQUAL_UINT8(0, reg.raw()[8]);
    TEST_ASSERT_EQUAL_UINT8(0, reg.raw()[12]);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_out_of_range_index_reads_zero()
{
    Status reg(DISTINCT_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0, reg.batteryInfo(4));
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawPresentCapacity(4));
    TEST_ASSERT_EQUAL_UINT8(0, reg.batteryInfo(255));
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawPresentCapacity(255));
}

void test_out_of_range_index_setters_leave_buffer_unchanged()
{
    Status reg(DISTINCT_BUFFER);

    reg.setBatteryInfo(4, 0xEE);
    reg.setRawPresentCapacity(4, 0xEEEE);
    reg.setBatteryInfo(255, 0xEE);
    reg.setRawPresentCapacity(255, 0xEEEE);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, reg.raw(), STATUS_SIZE);
}

void test_setters_no_op_on_invalid_register()
{
    Status original(DISTINCT_BUFFER);
    Status moved(static_cast<Status &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    original.setBatteryInfo(0, 0x55);
    original.setRawPresentCapacity(0, 0x1234);
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, original.batteryInfo(0));
    TEST_ASSERT_EQUAL_UINT16(0, original.rawPresentCapacity(0));
}

void test_reserved_byte_nonzero_is_invalid()
{
    const size_t reservedBytes[] = {0, 4, 8, 12};
    for (size_t index : reservedBytes)
    {
        uint8_t buf[STATUS_SIZE];
        memcpy(buf, DISTINCT_BUFFER, STATUS_SIZE);
        buf[index] = 0x01;

        TEST_ASSERT_FALSE_MESSAGE(Status(buf).isSemanticallyValid(), "reserved byte set");
    }
}

void test_every_reserved_bit_individually_invalid()
{
    const size_t reservedBytes[] = {0, 4, 8, 12};
    for (size_t index : reservedBytes)
    {
        for (unsigned bit = 0; bit < 8; bit++)
        {
            uint8_t buf[STATUS_SIZE];
            memcpy(buf, DISTINCT_BUFFER, STATUS_SIZE);
            buf[index] = static_cast<uint8_t>(1u << bit);

            TEST_ASSERT_FALSE_MESSAGE(Status(buf).isSemanticallyValid(), "reserved bit set");
        }
    }
}

void test_reserved_bit_7_and_0_invalid()
{
    uint8_t low[STATUS_SIZE];
    memcpy(low, DISTINCT_BUFFER, STATUS_SIZE);
    low[4] = 0x01;  // bit 32
    TEST_ASSERT_FALSE(Status(low).isSemanticallyValid());

    uint8_t high[STATUS_SIZE];
    memcpy(high, DISTINCT_BUFFER, STATUS_SIZE);
    high[4] = 0x80;  // bit 39
    TEST_ASSERT_FALSE(Status(high).isSemanticallyValid());
}

void test_debug_print_distinct_fixture()
{
    const std::string out = captureDebugPrint(Status(DISTINCT_BUFFER));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("TRANSMITTED_BATTERY_STATUS (0x7B)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw bytes (byte 0 first): 00 11 34 12 00 22 78 56 00 33 BC 9A 00 44 F0 DE"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 0: Battery Info 0x11, Present Capacity 0x1234 (4660)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 3: Battery Info 0x44, Present Capacity 0xDEF0 (57072)"));
}

void test_debug_print_reset_zero_pads()
{
    const std::string out = captureDebugPrint(Status(RESET_BUFFER));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw bytes (byte 0 first): 00 02 FF FF 00 00 00 00 00 00 00 00 00 00 00 00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 0: Battery Info 0x02, Present Capacity 0xFFFF (65535)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 1: Battery Info 0x00, Present Capacity 0x0000 (0)"));
}

// --- Typed decode (USB PD R3.3 Table 6.24) ---------------------------------------------
//
// Each 4-byte entry is one BSDO: Battery Info byte bit 0 Invalid Battery Reference,
// bit 1 Battery Present, bits 3:2 Charging Status, bits 7:4 reserved. Present
// Capacity counts 0.1 Wh (100 mWh) steps; FFFFh means unknown.

void test_typed_reset_value_battery_0()
{
    // Reset bytes 00 02 FF FF: Info 0x02 is present (bit 1), not an invalid
    // reference (bit 0 clear), status code 0 (Charging), capacity FFFFh unknown.
    Status reg(RESET_BUFFER);

    TEST_ASSERT_TRUE(reg.batteryPresent(0));
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(0));
    TEST_ASSERT_TRUE(reg.chargingStatus(0) == Status::ChargingStatus::Charging);
    TEST_ASSERT_FALSE(reg.presentCapacityKnown(0));
    TEST_ASSERT_EQUAL_UINT32(0, reg.presentCapacity_mWh(0));

    // Batteries 1 to 3 are all zero: not present, capacity 0 is known and equals 0 mWh.
    TEST_ASSERT_FALSE(reg.batteryPresent(1));
    TEST_ASSERT_TRUE(reg.presentCapacityKnown(1));
    TEST_ASSERT_EQUAL_UINT32(0, reg.presentCapacity_mWh(1));
}

// Battery 2 Battery Info 0x07: bit 0 invalid reference (0x01), bit 1 present (0x02),
// bits 3:2 = 01b Discharging (0x04); 0x01 + 0x02 + 0x04 = 0x07. Capacity 0x01F4 is
// 500 steps, 500 * 100 = 50,000 mWh, in bytes 10 and 11 as F4 01.
void test_typed_battery_2_fixture()
{
    uint8_t buf[STATUS_SIZE] = {0};
    buf[9] = 0x07;
    buf[10] = 0xF4;
    buf[11] = 0x01;
    Status reg(buf);

    TEST_ASSERT_TRUE(reg.invalidBatteryReference(2));
    TEST_ASSERT_TRUE(reg.batteryPresent(2));
    TEST_ASSERT_TRUE(reg.chargingStatus(2) == Status::ChargingStatus::Discharging);
    TEST_ASSERT_TRUE(reg.presentCapacityKnown(2));
    TEST_ASSERT_EQUAL_UINT32(50000, reg.presentCapacity_mWh(2));

    // The other entries stay clear.
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(0));
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(1));
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(3));
    TEST_ASSERT_EQUAL_UINT32(0, reg.presentCapacity_mWh(3));
}

void test_typed_charging_status_codes()
{
    // Info bits 3:2 hold the status, so code n is n << 2; bit 1 (0x02) keeps the battery present.
    Status reg(RESET_BUFFER);
    reg.setBatteryInfo(1, 0x02 | (0 << 2));
    TEST_ASSERT_TRUE(reg.chargingStatus(1) == Status::ChargingStatus::Charging);
    reg.setBatteryInfo(1, 0x02 | (1 << 2));  // 0x06
    TEST_ASSERT_TRUE(reg.chargingStatus(1) == Status::ChargingStatus::Discharging);
    reg.setBatteryInfo(1, 0x02 | (2 << 2));  // 0x0A
    TEST_ASSERT_TRUE(reg.chargingStatus(1) == Status::ChargingStatus::Idle);
    reg.setBatteryInfo(1, 0x02 | (3 << 2));  // 0x0E
    TEST_ASSERT_TRUE(reg.chargingStatus(1) == Status::ChargingStatus::Invalid);

    TEST_ASSERT_EQUAL_STRING("Charging", Status::toString(Status::ChargingStatus::Charging));
    TEST_ASSERT_EQUAL_STRING("Discharging", Status::toString(Status::ChargingStatus::Discharging));
    TEST_ASSERT_EQUAL_STRING("Idle", Status::toString(Status::ChargingStatus::Idle));
    TEST_ASSERT_EQUAL_STRING("Invalid", Status::toString(Status::ChargingStatus::Invalid));
    TEST_ASSERT_EQUAL_STRING("Undefined", Status::toString(static_cast<Status::ChargingStatus>(4)));
}

void test_typed_reserved_info_bits_do_not_affect_flags()
{
    Status reg(RESET_BUFFER);
    reg.setBatteryInfo(3, 0xF0);  // reserved bits 7:4 only
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(3));
    TEST_ASSERT_FALSE(reg.batteryPresent(3));
    TEST_ASSERT_TRUE(reg.chargingStatus(3) == Status::ChargingStatus::Charging);
}

void test_typed_capacity_boundaries()
{
    Status reg(RESET_BUFFER);
    reg.setRawPresentCapacity(1, 0xFFFE);  // largest known value: 65534 * 100
    TEST_ASSERT_TRUE(reg.presentCapacityKnown(1));
    TEST_ASSERT_EQUAL_UINT32(6553400, reg.presentCapacity_mWh(1));

    reg.setRawPresentCapacity(1, 0xFFFF);  // unknown
    TEST_ASSERT_FALSE(reg.presentCapacityKnown(1));
    TEST_ASSERT_EQUAL_UINT32(0, reg.presentCapacity_mWh(1));

    reg.setRawPresentCapacity(1, 0x0001);  // one step: 100 mWh
    TEST_ASSERT_EQUAL_UINT32(100, reg.presentCapacity_mWh(1));
}

void test_typed_out_of_range_battery_index()
{
    Status reg(DISTINCT_BUFFER);
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(4));
    TEST_ASSERT_FALSE(reg.batteryPresent(4));
    TEST_ASSERT_TRUE(reg.chargingStatus(4) == Status::ChargingStatus::Charging);
    TEST_ASSERT_FALSE(reg.presentCapacityKnown(4));
    TEST_ASSERT_EQUAL_UINT32(0, reg.presentCapacity_mWh(4));
    TEST_ASSERT_FALSE(reg.batteryPresent(255));
    TEST_ASSERT_EQUAL_UINT32(0, reg.presentCapacity_mWh(255));
}

void test_debug_print_typed_decode_lines()
{
    const std::string resetOut = captureDebugPrint(Status(RESET_BUFFER));
    // Capacity FFFFh: Table 6.24 marks Charging Status reserved, so no status is printed.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Battery 0: present, capacity unknown\n"));
    // Absent battery: neither status nor capacity is printed.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Battery 1: not present\n"));
    TEST_ASSERT_EQUAL(std::string::npos, resetOut.find("Charging"));

    uint8_t buf[STATUS_SIZE] = {0};
    buf[9] = 0x07;
    buf[10] = 0xF4;
    buf[11] = 0x01;
    const std::string out = captureDebugPrint(Status(buf));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 2: present, invalid battery reference, Discharging, capacity 50000 mWh"));
    // The existing raw line remains.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 2: Battery Info 0x07, Present Capacity 0x01F4 (500)"));
}

void test_copy_constructor_deep_copies()
{
    Status source(DISTINCT_BUFFER);
    Status copy(source);

    TEST_ASSERT_NOT_EQUAL(source.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, copy.raw(), STATUS_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    Status original(DISTINCT_BUFFER);
    Status moved(static_cast<Status &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, moved.raw(), STATUS_SIZE);
}

void test_copy_assignment_deep_copies()
{
    Status source(DISTINCT_BUFFER);
    Status target(ZERO_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, target.raw(), STATUS_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    Status source(DISTINCT_BUFFER);
    Status target(ZERO_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<Status &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, target.raw(), STATUS_SIZE);
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_address_identity);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);
    RUN_TEST(test_all_ones_rejected_at_data_tier);

    RUN_TEST(test_reset_fixture_is_valid_and_decodes);
    RUN_TEST(test_all_zero_buffer_is_valid_and_decodes_to_zero);
    RUN_TEST(test_distinct_fixture_decodes_every_field);

    RUN_TEST(test_per_entry_round_trip_changes_only_that_entry);
    RUN_TEST(test_setters_overwrite_from_distinct_fixture_leaving_other_entries);
    RUN_TEST(test_setters_never_touch_reserved_bytes);
    RUN_TEST(test_out_of_range_index_reads_zero);
    RUN_TEST(test_out_of_range_index_setters_leave_buffer_unchanged);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_reserved_byte_nonzero_is_invalid);
    RUN_TEST(test_every_reserved_bit_individually_invalid);
    RUN_TEST(test_reserved_bit_7_and_0_invalid);

    RUN_TEST(test_debug_print_distinct_fixture);
    RUN_TEST(test_debug_print_reset_zero_pads);

    RUN_TEST(test_typed_reset_value_battery_0);
    RUN_TEST(test_typed_battery_2_fixture);
    RUN_TEST(test_typed_charging_status_codes);
    RUN_TEST(test_typed_reserved_info_bits_do_not_affect_flags);
    RUN_TEST(test_typed_capacity_boundaries);
    RUN_TEST(test_typed_out_of_range_battery_index);
    RUN_TEST(test_debug_print_typed_decode_lines);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
