/**
 * @file test_TPS25751Data.cpp
 * @brief Native (host-run) unit tests for TPS25751Data.
 *
 * Run with: pio test -e native
 *
 * DATA (0x09) is the 64-byte scratch register for 4CC task payloads. It has
 * no fixed field layout, so the class exposes raw bytes, the generic bitfield
 * helper, and a hex-dump debugPrint().
 */

#include <unity.h>

#include <cstdio>
#include <string>

#include "TPS25751Data.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t DATA_SIZE = TPS25751Registers::Registers::DATA.size;  // 64

// Fills buf[i] = i + 1 (1..64), so every byte differs and none is 0xFF.
void fillRamp(uint8_t *buf)
{
    for (size_t i = 0; i < DATA_SIZE; i++) buf[i] = static_cast<uint8_t>(i + 1);
}

bool contains(const std::string &s, const char *needle)
{
    return s.find(needle) != std::string::npos;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction ------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751Data reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(DATA_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(DATA_SIZE));
    for (size_t i = 0; i < DATA_SIZE; i++) TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(i));
}

void test_data_constructor_copies_buffer()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data reg(src);

    TEST_ASSERT_NOT_EQUAL(src, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, reg.raw(), DATA_SIZE);
}

void test_explicit_length_constructor()
{
    const uint8_t shortData[3] = {0xAA, 0xBB, 0xCC};
    TPS25751Data reg(shortData, sizeof(shortData));

    TEST_ASSERT_EQUAL_size_t(3, reg.size());
    TEST_ASSERT_FALSE(reg.isValidSize(DATA_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0xCC, reg.byteAt(2));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751Data reg(TPS25751Registers::Registers::DATA);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(DATA_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(63));
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751Data reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(0));
}

// --- byteAt / extractByteField ----------------------------------------------

void test_byte_at_returns_each_byte()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data reg(src);

    TEST_ASSERT_EQUAL_UINT8(1, reg.byteAt(0));
    TEST_ASSERT_EQUAL_UINT8(33, reg.byteAt(32));
    TEST_ASSERT_EQUAL_UINT8(64, reg.byteAt(63));
}

void test_byte_at_out_of_range_returns_zero()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data reg(src);

    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(DATA_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(1000));
}

void test_byte_at_zero_when_invalid()
{
    TPS25751Data original;
    TPS25751Data moved(static_cast<TPS25751Data &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, original.byteAt(0));
}

void test_extract_byte_field_reads_bits_little_endian()
{
    // Byte 1 = 0xA5 = 1010 0101b; byte 2 = 0x3C.
    uint8_t src[DATA_SIZE] = {0};
    src[1] = 0xA5;
    src[2] = 0x3C;
    TPS25751Data reg(src);

    TEST_ASSERT_EQUAL_UINT8(0xA5, reg.extractByteField(8, 8));  // whole byte 1
    TEST_ASSERT_EQUAL_UINT8(0x5, reg.extractByteField(8, 4));   // low nibble of byte 1
    TEST_ASSERT_EQUAL_UINT8(0xA, reg.extractByteField(12, 4));  // high nibble of byte 1
    TEST_ASSERT_EQUAL_UINT8(0x1, reg.extractByteField(8, 1));   // bit 8
    TEST_ASSERT_EQUAL_UINT8(0x0, reg.extractByteField(9, 1));   // bit 9
    TEST_ASSERT_EQUAL_UINT8(0x3C, reg.extractByteField(16, 8));
}

void test_extract_byte_field_reaches_last_addressable_byte()
{
    // The bit position is 16 bits wide, so the whole 64-byte register is
    // reachable: bit 248 is byte 31, bit 256 byte 32, and bit 504 byte 63.
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data reg(src);

    TEST_ASSERT_EQUAL_UINT8(32, reg.extractByteField(248, 8));
    TEST_ASSERT_EQUAL_UINT8(33, reg.extractByteField(256, 8));
    TEST_ASSERT_EQUAL_UINT8(64, reg.extractByteField(504, 8));
}

void test_extract_byte_field_past_end_returns_zero_without_wrapping()
{
    // Bit 512 is past the 64-byte register. With an 8-bit position it wrapped
    // to bit 0 and read byte 0.
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data reg(src);

    TEST_ASSERT_EQUAL_UINT8(0, reg.extractByteField(512, 8));
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractByteField(508, 8));  // straddles the end
}

void test_extract_byte_field_past_short_buffer_returns_zero()
{
    const uint8_t shortData[2] = {0xFF, 0xFF};
    TPS25751Data reg(shortData, sizeof(shortData));

    TEST_ASSERT_EQUAL_UINT8(0, reg.extractByteField(16, 8));
}

void test_extract_byte_field_zero_when_invalid()
{
    TPS25751Data original;
    TPS25751Data moved(static_cast<TPS25751Data &&>(original));

    TEST_ASSERT_EQUAL_UINT8(0, original.extractByteField(0, 8));
}

// --- Validation --------------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    TPS25751Data reg;

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    uint8_t ones[DATA_SIZE];
    for (size_t i = 0; i < DATA_SIZE; i++) ones[i] = 0xFF;
    TPS25751Data reg(ones);

    TEST_ASSERT_FALSE(reg.hasValidData());
}

void test_semantic_valid_for_correctly_sized_register()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);

    TEST_ASSERT_TRUE(TPS25751Data(src).isSemanticallyValid());
}

void test_semantic_invalid_for_wrong_size()
{
    const uint8_t shortData[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    TEST_ASSERT_FALSE(TPS25751Data(shortData, sizeof(shortData)).isSemanticallyValid());
}

void test_semantic_invalid_when_register_invalid()
{
    TPS25751Data original;
    TPS25751Data moved(static_cast<TPS25751Data &&>(original));

    TEST_ASSERT_FALSE(original.isSemanticallyValid());
}

// --- debugPrint --------------------------------------------------------------

void test_debug_print_shows_length_and_hex_dump()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    const std::string out = captureDebugPrint(TPS25751Data(src));

    TEST_ASSERT_TRUE(contains(out, "=== Data Register (0x09) ==="));
    TEST_ASSERT_TRUE(contains(out, "Length: 64 bytes"));
    TEST_ASSERT_TRUE(contains(out, "Raw (64 bytes):"));
    // 64 bytes at 16 per line: offsets 0x00, 0x10, 0x20, 0x30.
    TEST_ASSERT_TRUE(contains(out, "0x00: 01 02 03 04"));
    TEST_ASSERT_TRUE(contains(out, "0x10: 11 12 13 14"));
    TEST_ASSERT_TRUE(contains(out, "0x30: 31 32 33 34"));
    TEST_ASSERT_TRUE(contains(out, "3D 3E 3F 40"));
}

void test_debug_print_short_register_reports_its_length()
{
    const uint8_t shortData[3] = {0xDE, 0xAD, 0xBE};
    const std::string out = captureDebugPrint(TPS25751Data(shortData, sizeof(shortData)));

    TEST_ASSERT_TRUE(contains(out, "Length: 3 bytes"));
    TEST_ASSERT_TRUE(contains(out, "0x00: DE AD BE"));
}

void test_debug_print_invalid()
{
    TPS25751Data original;
    TPS25751Data moved(static_cast<TPS25751Data &&>(original));

    const std::string out = captureDebugPrint(original);

    TEST_ASSERT_TRUE(contains(out, "=== Data Register (0x09) ==="));
    TEST_ASSERT_TRUE(contains(out, "<invalid>"));
    TEST_ASSERT_FALSE(contains(out, "Length:"));
}

// --- Rule of Five ------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data original(src);
    TPS25751Data copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, copy.raw(), DATA_SIZE);
}

void test_copy_assignment_deep_copies()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data original(src);
    TPS25751Data target;
    target = original;

    TEST_ASSERT_NOT_EQUAL(original.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8(64, target.byteAt(63));
}

void test_move_constructor_transfers_ownership()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data original(src);
    const uint8_t *buf = original.raw();

    TPS25751Data moved(static_cast<TPS25751Data &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(1, moved.byteAt(0));
}

void test_move_assignment_transfers_ownership()
{
    uint8_t src[DATA_SIZE];
    fillRamp(src);
    TPS25751Data original(src);
    const uint8_t *buf = original.raw();
    TPS25751Data target;

    target = static_cast<TPS25751Data &&>(original);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(64, target.byteAt(63));
}

// --- Runner ------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);

    RUN_TEST(test_byte_at_returns_each_byte);
    RUN_TEST(test_byte_at_out_of_range_returns_zero);
    RUN_TEST(test_byte_at_zero_when_invalid);
    RUN_TEST(test_extract_byte_field_reads_bits_little_endian);
    RUN_TEST(test_extract_byte_field_reaches_last_addressable_byte);
    RUN_TEST(test_extract_byte_field_past_end_returns_zero_without_wrapping);
    RUN_TEST(test_extract_byte_field_past_short_buffer_returns_zero);
    RUN_TEST(test_extract_byte_field_zero_when_invalid);

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);
    RUN_TEST(test_semantic_valid_for_correctly_sized_register);
    RUN_TEST(test_semantic_invalid_for_wrong_size);
    RUN_TEST(test_semantic_invalid_when_register_invalid);

    RUN_TEST(test_debug_print_shows_length_and_hex_dump);
    RUN_TEST(test_debug_print_short_register_reports_its_length);
    RUN_TEST(test_debug_print_invalid);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
