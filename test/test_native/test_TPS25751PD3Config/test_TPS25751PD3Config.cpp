/**
 * @file test_TPS25751PD3Config.cpp
 * @brief Native (host-run) unit tests for TPS25751PD3Config.
 *
 * Run with: pio test -e native
 *
 * PD3_CONFIGURATION (0x42) is a 4-byte R/W register, reset 00080010h. Bit n
 * lives in byte n / 8, bit n % 8, so the reset value is {0x10, 0x00, 0x08, 0x00}.
 *
 * Defined bits: 20, 19, 18, 17, 11, 10, 8, 7, 4  -> 0x001E0D90
 *   bits 20:17 = 0x001E0000, bits 11:10 = 0x00000C00, bit 8 = 0x100,
 *   bit 7 = 0x80, bit 4 = 0x10
 * Reserved bits: 31:21, 16:12, 9, 6:5, 3:0       -> 0xFFE1F26F
 *   (the two masks are complements of each other)
 * Validation checks reserved bits 31:21, 16:12, 9, 6:5 -> 0xFFE1F260. Bits 3:0
 * are set by the firmware (hardware read 0x0008001A on 2026-11-18) and are
 * not checked.
 */

#include <unity.h>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "TPS25751PD3Config.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t PD3_SIZE = TPS25751Registers::Registers::PD3_CONFIGURATION.size;  // 4

constexpr uint32_t DEFINED_MASK = 0x001E0D90u;
constexpr uint32_t RESERVED_MASK = 0xFFE1F26Fu;          // every reserved bit in Table 3-26
constexpr uint32_t CHECKED_RESERVED_MASK = 0xFFE1F260u;  // reserved bits validation rejects
constexpr uint32_t FIRMWARE_RESERVED_MASK = 0x0000000Fu; // bits 3:0, set by firmware

constexpr uint8_t RESET_BUFFER[PD3_SIZE] = {0x10, 0x00, 0x08, 0x00};
constexpr uint8_t ALL_ONES_DATA[PD3_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
// Every reserved bit set, every defined bit clear: 0xFFE1F26F little-endian.
constexpr uint8_t RESERVED_ONLY[PD3_SIZE] = {0x6F, 0xF2, 0xE1, 0xFF};
// Every defined bit set, reserved clear: 0x001E0D90 little-endian.
constexpr uint8_t DEFINED_ONLY[PD3_SIZE] = {0x90, 0x0D, 0x1E, 0x00};

void packLE(uint32_t value, uint8_t *out)
{
    for (int i = 0; i < 4; i++) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t unpackLE(const uint8_t *in)
{
    return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

// Buffer equal to the reset value with a single extra bit set.
TPS25751PD3Config resetWithBit(uint8_t bit)
{
    uint8_t buf[PD3_SIZE];
    packLE(0x00080010u | (1u << bit), buf);
    return TPS25751PD3Config(buf);
}

std::string captureDebugPrint(const TPS25751PD3Config &reg)
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

// One row per defined bit, for table-driven setter tests.
struct BitCase
{
    uint8_t bit;
    bool (TPS25751PD3Config::*get)() const;
    void (TPS25751PD3Config::*set)(bool);
};

const BitCase kCases[] = {
    {20, &TPS25751PD3Config::supportPpsStatus, &TPS25751PD3Config::setSupportPpsStatus},
    {19, &TPS25751PD3Config::supportGetRevision, &TPS25751PD3Config::setSupportGetRevision},
    {18, &TPS25751PD3Config::supportGetSourceInfo, &TPS25751PD3Config::setSupportGetSourceInfo},
    {17, &TPS25751PD3Config::supportSinkCapExtended, &TPS25751PD3Config::setSupportSinkCapExtended},
    {11, &TPS25751PD3Config::supportBatteryStatus, &TPS25751PD3Config::setSupportBatteryStatus},
    {10, &TPS25751PD3Config::supportBatteryCapabilities, &TPS25751PD3Config::setSupportBatteryCapabilities},
    {8, &TPS25751PD3Config::supportSourceExtended, &TPS25751PD3Config::setSupportSourceExtended},
    {7, &TPS25751PD3Config::autoGetMidb, &TPS25751PD3Config::setAutoGetMidb},
    {4, &TPS25751PD3Config::unchunkedSupported, &TPS25751PD3Config::setUnchunkedSupported},
};
constexpr size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

}  // namespace

void setUp() {}
void tearDown() {}

// --- Constants -------------------------------------------------------------------

void test_masks_are_complements()
{
    TEST_ASSERT_EQUAL_UINT32(CHECKED_RESERVED_MASK, TPS25751PD3Config::kCheckedReservedMask);
    TEST_ASSERT_EQUAL_UINT32(FIRMWARE_RESERVED_MASK, TPS25751PD3Config::kFirmwareReservedMask);
    TEST_ASSERT_EQUAL_UINT32(RESERVED_MASK, CHECKED_RESERVED_MASK | FIRMWARE_RESERVED_MASK);
    TEST_ASSERT_EQUAL_UINT32(0u, CHECKED_RESERVED_MASK & FIRMWARE_RESERVED_MASK);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, DEFINED_MASK | RESERVED_MASK);
    TEST_ASSERT_EQUAL_UINT32(0u, DEFINED_MASK & RESERVED_MASK);

    uint32_t fromBits = 0;
    for (size_t i = 0; i < kCaseCount; i++) fromBits |= (1u << kCases[i].bit);
    TEST_ASSERT_EQUAL_UINT32(DEFINED_MASK, fromBits);

    TEST_ASSERT_EQUAL_UINT32(DEFINED_MASK, unpackLE(DEFINED_ONLY));
    TEST_ASSERT_EQUAL_UINT32(RESERVED_MASK, unpackLE(RESERVED_ONLY));
}

void test_address_identity()
{
    TEST_ASSERT_TRUE(TPS25751PD3Config::kAddress == TPS25751Registers::Address::PD3_CONFIGURATION);
    TEST_ASSERT_EQUAL_UINT8(0x42, static_cast<uint8_t>(TPS25751PD3Config::kAddress));
    TEST_ASSERT_EQUAL_UINT8(4, TPS25751Registers::getRegisterSize(TPS25751Registers::Address::PD3_CONFIGURATION));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(TPS25751Registers::Address::PD3_CONFIGURATION));
    TEST_ASSERT_EQUAL_UINT32(0x00080010u, unpackLE(RESET_BUFFER));
}

// --- Construction ----------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751PD3Config reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(PD3_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(PD3_SIZE));
    for (size_t i = 0; i < kCaseCount; i++) TEST_ASSERT_FALSE((reg.*kCases[i].get)());
}

void test_data_constructor_copies_buffer()
{
    TPS25751PD3Config reg(RESET_BUFFER);

    TEST_ASSERT_NOT_EQUAL(RESET_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), PD3_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751PD3Config reg(RESET_BUFFER, PD3_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(PD3_SIZE));
    TEST_ASSERT_TRUE(reg.supportGetRevision());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751PD3Config reg(TPS25751Registers::Registers::PD3_CONFIGURATION);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(PD3_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.unchunkedSupported());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[2] = {0x10, 0x00};
    TPS25751PD3Config reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(PD3_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Reset fixture ---------------------------------------------------------------

void test_reset_value_decodes()
{
    TPS25751PD3Config reg(RESET_BUFFER);

    TEST_ASSERT_TRUE(reg.supportGetRevision());
    TEST_ASSERT_TRUE(reg.unchunkedSupported());

    TEST_ASSERT_FALSE(reg.supportPpsStatus());
    TEST_ASSERT_FALSE(reg.supportGetSourceInfo());
    TEST_ASSERT_FALSE(reg.supportSinkCapExtended());
    TEST_ASSERT_FALSE(reg.supportBatteryStatus());
    TEST_ASSERT_FALSE(reg.supportBatteryCapabilities());
    TEST_ASSERT_FALSE(reg.supportSourceExtended());
    TEST_ASSERT_FALSE(reg.autoGetMidb());

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_each_defined_bit_decodes_alone()
{
    for (size_t i = 0; i < kCaseCount; i++)
    {
        uint8_t buf[PD3_SIZE];
        packLE(1u << kCases[i].bit, buf);
        TPS25751PD3Config reg(buf);

        for (size_t j = 0; j < kCaseCount; j++)
        {
            TEST_ASSERT_EQUAL_MESSAGE(i == j, (reg.*kCases[j].get)(), "wrong accessor state");
        }
        TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    }
}

// --- Setters ---------------------------------------------------------------------

void test_each_setter_round_trips_and_touches_only_its_bit()
{
    for (size_t i = 0; i < kCaseCount; i++)
    {
        TPS25751PD3Config reg(RESET_BUFFER);
        const uint32_t before = unpackLE(reg.raw());
        const uint32_t bitMask = 1u << kCases[i].bit;

        (reg.*kCases[i].set)(true);
        TEST_ASSERT_TRUE((reg.*kCases[i].get)());
        TEST_ASSERT_EQUAL_UINT32(before | bitMask, unpackLE(reg.raw()));

        (reg.*kCases[i].set)(false);
        TEST_ASSERT_FALSE((reg.*kCases[i].get)());
        TEST_ASSERT_EQUAL_UINT32(before & ~bitMask, unpackLE(reg.raw()));
    }
}

void test_setters_leave_other_bytes_unchanged()
{
    // Bit 20 lives in byte 2 only; bytes 0, 1 and 3 must be untouched.
    TPS25751PD3Config reg(RESET_BUFFER);
    reg.setSupportPpsStatus(true);

    TEST_ASSERT_EQUAL_UINT8(0x10, reg.raw()[0]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[1]);
    TEST_ASSERT_EQUAL_UINT8(0x18, reg.raw()[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[3]);
}

void test_setters_preserve_reserved_bits_set_to_one()
{
    for (size_t i = 0; i < kCaseCount; i++)
    {
        TPS25751PD3Config reg(RESERVED_ONLY);

        (reg.*kCases[i].set)(true);
        TEST_ASSERT_EQUAL_UINT32(RESERVED_MASK, unpackLE(reg.raw()) & RESERVED_MASK);
        TEST_ASSERT_TRUE((reg.*kCases[i].get)());

        (reg.*kCases[i].set)(false);
        TEST_ASSERT_EQUAL_UINT32(RESERVED_MASK, unpackLE(reg.raw()) & RESERVED_MASK);
        TEST_ASSERT_FALSE((reg.*kCases[i].get)());
    }
}

void test_setting_all_defined_bits_from_zero()
{
    TPS25751PD3Config reg;
    for (size_t i = 0; i < kCaseCount; i++) (reg.*kCases[i].set)(true);

    TEST_ASSERT_EQUAL_UINT32(DEFINED_MASK, unpackLE(reg.raw()));
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_setters_no_op_on_invalid_register()
{
    TPS25751PD3Config original(RESET_BUFFER);
    TPS25751PD3Config moved(static_cast<TPS25751PD3Config &&>(original));

    // original has been moved from and holds no buffer.
    TEST_ASSERT_FALSE(original.isValid());
    original.setAutoGetMidb(true);
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_FALSE(original.autoGetMidb());
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    TPS25751PD3Config reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_zeros_is_valid()
{
    TPS25751PD3Config reg;

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_defined_bits_set_is_valid()
{
    TPS25751PD3Config reg(DEFINED_ONLY);

    TEST_ASSERT_TRUE(reg.supportPpsStatus());
    TEST_ASSERT_TRUE(reg.supportGetRevision());
    TEST_ASSERT_TRUE(reg.supportGetSourceInfo());
    TEST_ASSERT_TRUE(reg.supportSinkCapExtended());
    TEST_ASSERT_TRUE(reg.supportBatteryStatus());
    TEST_ASSERT_TRUE(reg.supportBatteryCapabilities());
    TEST_ASSERT_TRUE(reg.supportSourceExtended());
    TEST_ASSERT_TRUE(reg.autoGetMidb());
    TEST_ASSERT_TRUE(reg.unchunkedSupported());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_reserved_high_range_31_21_invalid()
{
    TEST_ASSERT_FALSE(resetWithBit(31).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(26).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(21).isSemanticallyValid());
}

void test_reserved_range_16_12_invalid()
{
    TEST_ASSERT_FALSE(resetWithBit(16).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(14).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(12).isSemanticallyValid());
}

void test_reserved_bit_9_invalid()
{
    TEST_ASSERT_FALSE(resetWithBit(9).isSemanticallyValid());
}

void test_reserved_range_6_5_invalid()
{
    TEST_ASSERT_FALSE(resetWithBit(6).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(5).isSemanticallyValid());
}

// Bits 3:0 are reserved in the TRM, but the firmware sets them, so
// validation does not check them (see kFirmwareReservedMask).
void test_reserved_range_3_0_not_checked()
{
    TEST_ASSERT_TRUE(resetWithBit(3).isSemanticallyValid());
    TEST_ASSERT_TRUE(resetWithBit(2).isSemanticallyValid());
    TEST_ASSERT_TRUE(resetWithBit(1).isSemanticallyValid());
    TEST_ASSERT_TRUE(resetWithBit(0).isSemanticallyValid());
}

// Value read from hardware on 2026-11-18 (APP mode, EEPROM configuration):
// the documented reset 0x00080010 plus bits 3 and 1.
void test_hardware_value_0x0008001A_is_valid()
{
    uint8_t buf[PD3_SIZE];
    packLE(0x0008001Au, buf);
    TPS25751PD3Config reg(buf);

    TEST_ASSERT_TRUE(reg.supportGetRevision());
    TEST_ASSERT_TRUE(reg.unchunkedSupported());
    TEST_ASSERT_FALSE(reg.supportPpsStatus());
    TEST_ASSERT_FALSE(reg.autoGetMidb());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_every_reserved_bit_individually_invalid()
{
    for (uint8_t bit = 0; bit < 32; bit++)
    {
        const bool reserved = (CHECKED_RESERVED_MASK >> bit) & 1u;
        uint8_t buf[PD3_SIZE];
        packLE(1u << bit, buf);
        TPS25751PD3Config reg(buf);
        TEST_ASSERT_EQUAL_MESSAGE(!reserved, reg.isSemanticallyValid(), "reserved/defined classification mismatch");
    }
}

// --- Debug print -----------------------------------------------------------------

void test_debug_print_lists_fields_without_reserved_warning()
{
    TPS25751PD3Config reg(RESET_BUFFER);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("PD3_CONFIGURATION (0x42)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw: 0x00080010"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Support Get Revision: Enabled"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Support PPS Status: Disabled"));
    TEST_ASSERT_EQUAL(std::string::npos, out.find("Reserved bits set"));
}

void test_debug_print_reports_reserved_bits()
{
    TPS25751PD3Config reg(RESERVED_ONLY);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits set: 0xFFE1F260"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits 3:0 (firmware-set, not checked): 0xF"));
}

void test_debug_print_reports_firmware_bits_without_reserved_warning()
{
    uint8_t buf[PD3_SIZE];
    packLE(0x0008001Au, buf);
    TPS25751PD3Config reg(buf);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw: 0x0008001A"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits 3:0 (firmware-set, not checked): 0xA"));
    TEST_ASSERT_EQUAL(std::string::npos, out.find("Reserved bits set"));
}

// --- Rule of Five ----------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751PD3Config original(DEFINED_ONLY);
    TPS25751PD3Config copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DEFINED_ONLY, copy.raw(), PD3_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    TPS25751PD3Config original(DEFINED_ONLY);
    const uint8_t *buf = original.raw();

    TPS25751PD3Config moved(static_cast<TPS25751PD3Config &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DEFINED_ONLY, moved.raw(), PD3_SIZE);
}

void test_copy_assignment_deep_copies()
{
    TPS25751PD3Config source(DEFINED_ONLY);
    TPS25751PD3Config target(RESET_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DEFINED_ONLY, target.raw(), PD3_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    TPS25751PD3Config source(DEFINED_ONLY);
    TPS25751PD3Config target(RESET_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<TPS25751PD3Config &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DEFINED_ONLY, target.raw(), PD3_SIZE);
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_masks_are_complements);
    RUN_TEST(test_address_identity);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_reset_value_decodes);
    RUN_TEST(test_each_defined_bit_decodes_alone);

    RUN_TEST(test_each_setter_round_trips_and_touches_only_its_bit);
    RUN_TEST(test_setters_leave_other_bytes_unchanged);
    RUN_TEST(test_setters_preserve_reserved_bits_set_to_one);
    RUN_TEST(test_setting_all_defined_bits_from_zero);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_all_zeros_is_valid);
    RUN_TEST(test_all_defined_bits_set_is_valid);
    RUN_TEST(test_reserved_high_range_31_21_invalid);
    RUN_TEST(test_reserved_range_16_12_invalid);
    RUN_TEST(test_reserved_bit_9_invalid);
    RUN_TEST(test_reserved_range_6_5_invalid);
    RUN_TEST(test_reserved_range_3_0_not_checked);
    RUN_TEST(test_hardware_value_0x0008001A_is_valid);
    RUN_TEST(test_every_reserved_bit_individually_invalid);

    RUN_TEST(test_debug_print_lists_fields_without_reserved_warning);
    RUN_TEST(test_debug_print_reports_reserved_bits);
    RUN_TEST(test_debug_print_reports_firmware_bits_without_reserved_warning);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
