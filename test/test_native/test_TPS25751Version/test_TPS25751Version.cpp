/**
 * @file test_TPS25751Version.cpp
 * @brief Native (host-run) unit tests for TPS25751Version.
 *
 * Run with: pio test -e native
 *
 * VERSION (0x0F) is a 4-byte read-only register, returned little-endian:
 * byte 0 = RR (bits 7:0), byte 1 = MM (bits 15:8), bytes 2 and 3 = VVVV
 * (bits 31:16). VVVV in bytes 2 and 3 is an inference from the TRM wording
 * (SLVUCR8B p.18) pending hardware confirmation.
 */

#include <unity.h>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "TPS25751Version.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t VER_SIZE = TPS25751Registers::Registers::VERSION.size;  // 4

// VVVV.MM.RR = 0001.02.03
constexpr uint8_t SMALL_VERSION[VER_SIZE] = {0x03, 0x02, 0x01, 0x00};
// VVVV.MM.RR = 1234.56.78 (non-zero high byte)
constexpr uint8_t HIGH_BYTE_VERSION[VER_SIZE] = {0x78, 0x56, 0x34, 0x12};
constexpr uint8_t RESET_VALUE[VER_SIZE] = {0x00, 0x00, 0x00, 0x00};
constexpr uint8_t ALL_ONES_DATA[VER_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};

// Runs reg.debugPrint() against the stdout-backed native Stream stub and
// returns what it printed.
std::string captureDebugPrint(const TPS25751Version &reg)
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

// --- Construction ----------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751Version reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(VER_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(VER_SIZE));
    TEST_ASSERT_EQUAL_UINT32(0, reg.rawVersion());
}

void test_data_constructor_copies_buffer()
{
    TPS25751Version reg(SMALL_VERSION);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_NOT_EQUAL(SMALL_VERSION, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SMALL_VERSION, reg.raw(), VER_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751Version reg(SMALL_VERSION, VER_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(VER_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0x03, reg.versionRevision());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751Version reg(TPS25751Registers::Registers::VERSION);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(VER_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT32(0, reg.rawVersion());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[2] = {0x03, 0x02};
    TPS25751Version reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(VER_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_address_table_entry()
{
    TEST_ASSERT_EQUAL_UINT8(0x0F, static_cast<uint8_t>(TPS25751Registers::Address::VERSION));
    TEST_ASSERT_EQUAL_UINT8(4, TPS25751Registers::getRegisterSize(TPS25751Registers::Address::VERSION));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(TPS25751Registers::Address::VERSION));
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    TPS25751Version reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_reset_value_is_valid()
{
    TPS25751Version reg(RESET_VALUE);

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    TEST_ASSERT_EQUAL_UINT32(0, reg.rawVersion());
    TEST_ASSERT_EQUAL_UINT16(0, reg.versionMajor());
    TEST_ASSERT_EQUAL_UINT8(0, reg.versionMinor());
    TEST_ASSERT_EQUAL_UINT8(0, reg.versionRevision());
}

// --- Decode ----------------------------------------------------------------------

void test_decode_small_version()
{
    TPS25751Version reg(SMALL_VERSION);

    TEST_ASSERT_EQUAL_UINT8(0x03, reg.versionRevision());
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.versionMinor());
    TEST_ASSERT_EQUAL_UINT16(0x0001, reg.versionMajor());
    TEST_ASSERT_EQUAL_UINT32(0x00010203u, reg.rawVersion());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_decode_version_with_high_byte()
{
    TPS25751Version reg(HIGH_BYTE_VERSION);

    TEST_ASSERT_EQUAL_UINT8(0x78, reg.versionRevision());
    TEST_ASSERT_EQUAL_UINT8(0x56, reg.versionMinor());
    TEST_ASSERT_EQUAL_UINT16(0x1234, reg.versionMajor());
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, reg.rawVersion());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Debug print -----------------------------------------------------------------

void test_debug_print_shows_padded_hex_version_and_raw()
{
    TPS25751Version reg(SMALL_VERSION);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("VERSION (0x"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("0001.02.03"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("0x00010203"));
}

void test_debug_print_zero_pads_each_part()
{
    TPS25751Version reg(RESET_VALUE);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("0000.00.00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("0x00000000"));
}

// --- Rule of Five ----------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751Version original(HIGH_BYTE_VERSION);
    TPS25751Version copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HIGH_BYTE_VERSION, copy.raw(), VER_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    TPS25751Version original(HIGH_BYTE_VERSION);
    const uint8_t *buf = original.raw();

    TPS25751Version moved(static_cast<TPS25751Version &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, moved.rawVersion());
}

void test_copy_assignment_deep_copies()
{
    TPS25751Version source(HIGH_BYTE_VERSION);
    TPS25751Version target(SMALL_VERSION);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HIGH_BYTE_VERSION, target.raw(), VER_SIZE);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HIGH_BYTE_VERSION, source.raw(), VER_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    TPS25751Version source(HIGH_BYTE_VERSION);
    TPS25751Version target(SMALL_VERSION);
    const uint8_t *buf = source.raw();

    target = static_cast<TPS25751Version &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(HIGH_BYTE_VERSION, target.raw(), VER_SIZE);
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);
    RUN_TEST(test_address_table_entry);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_reset_value_is_valid);

    RUN_TEST(test_decode_small_version);
    RUN_TEST(test_decode_version_with_high_byte);

    RUN_TEST(test_debug_print_shows_padded_hex_version_and_raw);
    RUN_TEST(test_debug_print_zero_pads_each_part);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
