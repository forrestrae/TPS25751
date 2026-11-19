/**
 * @file test_TPS25751TransmitBatteryStatus.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitBatteryStatus.
 *
 * Run with: pio test -e native
 *
 * TRANSMITTED_BATTERY_STATUS_DATA_OBJECTS_(BSDO) (0x7B) is a 16-byte R/W register
 * made of four 4-byte entries (entry i at byte 4 * i). In each entry byte 0 is
 * reserved, byte 1 is Battery Info, and bytes 2-3 are Present Capacity
 * (little-endian). Every field is raw: the encodings belong to the USB PD
 * specification, so these tests assert raw values only.
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
#include <unistd.h>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitBatteryStatus.h"

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

std::string captureDebugPrint(const Status &reg)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    const int saved = dup(fileno(stdout));
    dup2(fileno(tmp), fileno(stdout));

    reg.debugPrint();

    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    std::string out;
    rewind(tmp);
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), tmp)) > 0) out.append(buf, n);
    fclose(tmp);
    return out;
}

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

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
