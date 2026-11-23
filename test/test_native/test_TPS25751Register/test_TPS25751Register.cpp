/**
 * @file test_TPS25751Register.cpp
 * @brief Native (host-run) unit tests for the TPS25751Register bit helpers.
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751Register"
 *
 * extractBits(), extractBits16/32/64(), setBits(), and the extractEnum*()
 * templates take a 16-bit bit position (ADR-012 in ARCHITECTURE.md). Before
 * ADR-012 the position was 8 bits, so no field could start past bit 255. These
 * tests use a 40-byte buffer (bits 319:0) and check fields that end at bit 255,
 * start past it, and straddle it, plus the bounds checks at the end of the
 * buffer and for positions far past it.
 *
 * Fixture: byte k = 0x10 + k for k = 0..39, so byte 31 = 0x2F, byte 32 = 0x30,
 * byte 33 = 0x31, and byte 39 = 0x37. Expected values below are computed by hand
 * from those bytes, little-endian, bit n in byte n / 8 at bit n % 8.
 *
 * The suite also covers printHex(), the zero-padded hex printer that every
 * debugPrint() shares.
 */

#include <unity.h>

#include <cstdio>
#include <string>

#include "CaptureStdout.h"
#include "TPS25751Register.h"

namespace {

constexpr size_t kSize = 40;  // 320 bits

// TPS25751Register is abstract (debugPrint() is pure virtual).
class TestRegister : public TPS25751Register
{
public:
    explicit TestRegister(const uint8_t *data) : TPS25751Register(data, kSize) {}
    void debugPrint(Stream &) const override {}
};

void fill(uint8_t *buf)
{
    for (size_t k = 0; k < kSize; k++) buf[k] = static_cast<uint8_t>(0x10 + k);
}

enum class Small : uint8_t { A = 0, B = 1, C = 2, Invalid = 0xFF };

// Runs TPS25751Register::printHex() against the stdout-backed native Stream stub
// and returns what it printed.
std::string capturePrintHex(uint32_t value, uint8_t digits)
{
    return captureStdout([&] { TPS25751Register::printHex(Serial, value, digits); });
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_field_ending_at_bit_255()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    // Bit 255 is bit 7 of byte 31 (0x2F = 0010 1111b): 0.
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(255, 1));
    // Bits 255:248 are byte 31 whole.
    TEST_ASSERT_EQUAL_HEX8(0x2F, reg.extractBits(248, 8));
    TEST_ASSERT_EQUAL_HEX16(0x2F2E, reg.extractBits16(240, 16));
}

void test_fields_starting_past_bit_255()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    TEST_ASSERT_EQUAL_HEX8(0x30, reg.extractBits(256, 8));
    TEST_ASSERT_EQUAL_HEX16(0x3130, reg.extractBits16(256, 16));
    TEST_ASSERT_EQUAL_HEX32(0x33323130, reg.extractBits32(256, 32));
    TEST_ASSERT_EQUAL_HEX64(0x3736353433323130ULL, reg.extractBits64(256, 64));
    // Bit 264 is bit 0 of byte 33 (0x31): 1. Bits 266:264 are 001b.
    TEST_ASSERT_EQUAL_UINT8(1, reg.extractBits(264, 1));
    TEST_ASSERT_EQUAL_UINT8(1, reg.extractBits(264, 3));
    // A field at bit 300: bits 303:300 are the high nibble of byte 37 (0x35): 3.
    TEST_ASSERT_EQUAL_UINT8(3, reg.extractBits(300, 4));
}

void test_fields_straddling_bit_255()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    // Bits 259:252 = high nibble of byte 31 (0x2) | low nibble of byte 32 (0x0) << 4.
    TEST_ASSERT_EQUAL_HEX8(0x02, reg.extractBits(252, 8));
    // Bits 263:248 = bytes 31 and 32: 0x302F.
    TEST_ASSERT_EQUAL_HEX16(0x302F, reg.extractBits16(248, 16));
    // Bits 267:252 = (0x31302F >> 4) & 0xFFFF = 0x1302.
    TEST_ASSERT_EQUAL_HEX16(0x1302, reg.extractBits16(252, 16));
    // Bits 283:252 = (0x3332_3130_2F >> 4) & 0xFFFFFFFF = 0x33231302.
    TEST_ASSERT_EQUAL_HEX32(0x33231302, reg.extractBits32(252, 32));
    // Bits 303:240 = bytes 30 to 37.
    TEST_ASSERT_EQUAL_HEX64(0x3534333231302F2EULL, reg.extractBits64(240, 64));
}

void test_field_ending_at_the_last_bit_and_one_past_it()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    TEST_ASSERT_EQUAL_HEX8(0x37, reg.extractBits(312, 8));
    TEST_ASSERT_EQUAL_HEX16(0x3736, reg.extractBits16(304, 16));
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(319, 1));  // 0x37 bit 7
    // One bit past the end: out of bounds, so 0.
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(313, 8));
    TEST_ASSERT_EQUAL_UINT16(0, reg.extractBits16(305, 16));
    TEST_ASSERT_EQUAL_UINT32(0, reg.extractBits32(289, 32));
    // A 64-bit field is two 32-bit halves, each bounds-checked on its own: at
    // bit 257 the lower half (bits 288:257) fits and the upper half does not, so
    // only the lower half comes back. Bits 288:257 = (0x34_33323130 >> 1).
    TEST_ASSERT_EQUAL_HEX64(0x19991898ULL, reg.extractBits64(257, 64));
    TEST_ASSERT_EQUAL_UINT64(0, reg.extractBits64(289, 64));
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(320, 1));
}

void test_positions_far_past_the_buffer_return_zero()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(0xFFFF, 1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.extractBits16(0xFFF8, 16));
    TEST_ASSERT_EQUAL_UINT32(0, reg.extractBits32(0xFFF0, 32));
    // The upper half of a 64-bit field at 0xFFE0 would start at 0x10000, which
    // does not fit in 16 bits; it must not wrap to bit 0 (which would read 0x13121110).
    TEST_ASSERT_EQUAL_UINT64(0, reg.extractBits64(0xFFE0, 64));
    TEST_ASSERT_EQUAL_UINT64(0, TPS25751Register::extractBits64(buf, kSize, 0xFFE0, 64));
    // Same for the static form with a buffer large enough to hold bit 0xFFE0's
    // lower half: an 8 KB buffer, so the lower half is in bounds and the upper
    // half's position overflows. Only the lower half may come back.
    static uint8_t big[8192];
    big[0] = 0xEE;
    big[8188] = 0x44;
    TEST_ASSERT_EQUAL_HEX64(0x44ULL, TPS25751Register::extractBits64(big, sizeof big, 0xFFE0, 64));
}

void test_set_bits_past_bit_255_preserves_neighbors()
{
    uint8_t buf[kSize];
    fill(buf);
    TestRegister reg(buf);

    reg.setBits(256, 16, 0xBEEF);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, reg.extractBits16(256, 16));
    TEST_ASSERT_EQUAL_HEX8(0x2F, reg.raw()[31]);
    TEST_ASSERT_EQUAL_HEX8(0x32, reg.raw()[34]);

    reg.setBits(312, 8, 0xA5);  // the last byte
    TEST_ASSERT_EQUAL_HEX8(0xA5, reg.raw()[39]);
    TEST_ASSERT_EQUAL_HEX8(0x36, reg.raw()[38]);
}

void test_set_bits_straddling_bit_255_preserves_neighbors()
{
    uint8_t buf[kSize];
    fill(buf);
    TestRegister reg(buf);

    // Write 0xAB to bits 259:252: low nibble B into byte 31 bits 7:4, high nibble
    // A into byte 32 bits 3:0. Byte 31 becomes 0xBF, byte 32 becomes 0x3A.
    reg.setBits(252, 8, 0xAB);
    TEST_ASSERT_EQUAL_HEX8(0xAB, reg.extractBits(252, 8));
    TEST_ASSERT_EQUAL_HEX8(0xBF, reg.raw()[31]);
    TEST_ASSERT_EQUAL_HEX8(0x3A, reg.raw()[32]);
    TEST_ASSERT_EQUAL_HEX8(0x2E, reg.raw()[30]);
    TEST_ASSERT_EQUAL_HEX8(0x31, reg.raw()[33]);

    // A 32-bit write straddling bit 255 round-trips.
    reg.setBits(250, 32, 0x12345678);
    TEST_ASSERT_EQUAL_HEX32(0x12345678, reg.extractBits32(250, 32));
}

void test_set_bits_out_of_bounds_is_a_no_op()
{
    uint8_t buf[kSize];
    fill(buf);
    TestRegister reg(buf);

    reg.setBits(313, 8, 0xFF);     // one bit past the end
    reg.setBits(320, 1, 1);        // first bit past the end
    reg.setBits(0xFFFF, 1, 1);     // far past the end
    reg.setBits(0xFFF8, 32, 0xFFFFFFFF);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(buf, reg.raw(), kSize);
}

void test_static_helpers_reach_past_bit_255()
{
    uint8_t buf[kSize];
    fill(buf);

    TEST_ASSERT_EQUAL_HEX8(0x30, TPS25751Register::extractBits(buf, kSize, 256, 8));
    TEST_ASSERT_EQUAL_HEX16(0x3130, TPS25751Register::extractBits16(buf, kSize, 256, 16));
    TEST_ASSERT_EQUAL_HEX32(0x33231302, TPS25751Register::extractBits32(buf, kSize, 252, 32));
    TPS25751Register::setBits(buf, kSize, 280, 8, 0x5A);
    TEST_ASSERT_EQUAL_HEX8(0x5A, buf[35]);
}

void test_extract_enum_past_bit_255()
{
    uint8_t buf[kSize];
    fill(buf);
    buf[36] = 0x02;  // bits 295:288
    const TestRegister reg(buf);

    TEST_ASSERT_TRUE(Small::C == reg.extractEnum<Small>(288, 2, 2, Small::Invalid));
    // Bits 263:256 = 0x30 = 48, above the maximum of 2: the default.
    TEST_ASSERT_TRUE(Small::Invalid == reg.extractEnum<Small>(256, 8, 2, Small::Invalid));
    TEST_ASSERT_EQUAL_UINT16(0x3130, reg.extractEnum16<uint16_t>(256, 16, 0xFFFF, 0));
    TEST_ASSERT_EQUAL_UINT32(0x33323130, reg.extractEnum32<uint32_t>(256, 32, 0xFFFFFFFF, 0));
}

void test_extract_array_across_bit_255()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    // Four 16-bit elements from bit 240: bytes 30-31, 32-33, 34-35, 36-37.
    uint32_t out[6] = {};
    TEST_ASSERT_EQUAL_size_t(4, reg.extractArray32(240, 16, 4, out));
    TEST_ASSERT_EQUAL_HEX32(0x2F2E, out[0]);
    TEST_ASSERT_EQUAL_HEX32(0x3130, out[1]);
    TEST_ASSERT_EQUAL_HEX32(0x3332, out[2]);
    TEST_ASSERT_EQUAL_HEX32(0x3534, out[3]);

    // Asking for six stops at the end of the buffer: bytes 38-39 is the fifth.
    TEST_ASSERT_EQUAL_size_t(5, reg.extractArray32(240, 16, 6, out));
    TEST_ASSERT_EQUAL_HEX32(0x3736, out[4]);

    // A start past the end extracts nothing.
    TEST_ASSERT_EQUAL_size_t(0, reg.extractArray32(400, 8, 2, out));
}

void test_fields_below_bit_255_are_unchanged()
{
    uint8_t buf[kSize];
    fill(buf);
    const TestRegister reg(buf);

    TEST_ASSERT_EQUAL_HEX8(0x10, reg.extractBits(0, 8));
    TEST_ASSERT_EQUAL_HEX8(0x01, reg.extractBits(4, 4));       // 0x10 >> 4
    TEST_ASSERT_EQUAL_HEX8(0x11, reg.extractBits(4, 8));       // 0x1110 >> 4 & 0xFF
    TEST_ASSERT_EQUAL_HEX16(0x1211, reg.extractBits16(8, 16));
    TEST_ASSERT_EQUAL_HEX32(0x13121110, reg.extractBits32(0, 32));
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(0, 9));         // wider than 8 bits
}

void test_print_hex_zero_pads_to_the_digit_count()
{
    TEST_ASSERT_EQUAL_STRING("0A", capturePrintHex(0x0A, 2).c_str());
    TEST_ASSERT_EQUAL_STRING("00", capturePrintHex(0x00, 2).c_str());
    TEST_ASSERT_EQUAL_STRING("FF", capturePrintHex(0xFF, 2).c_str());
    TEST_ASSERT_EQUAL_STRING("05AC", capturePrintHex(0x05AC, 4).c_str());
    TEST_ASSERT_EQUAL_STRING("000ABC", capturePrintHex(0xABC, 6).c_str());
    TEST_ASSERT_EQUAL_STRING("0008001A", capturePrintHex(0x0008001A, 8).c_str());
    TEST_ASSERT_EQUAL_STRING("F8091166", capturePrintHex(0xF8091166, 8).c_str());
}

void test_print_hex_prints_only_the_low_digits()
{
    // Fewer digits than the value needs: only the low digits print.
    TEST_ASSERT_EQUAL_STRING("34", capturePrintHex(0x1234, 2).c_str());
    TEST_ASSERT_EQUAL_STRING("4", capturePrintHex(0x1234, 1).c_str());
}

void test_print_hex_digit_count_limits()
{
    TEST_ASSERT_EQUAL_STRING("", capturePrintHex(0x1234, 0).c_str());
    TEST_ASSERT_EQUAL_STRING("00001234", capturePrintHex(0x1234, 9).c_str());
    TEST_ASSERT_EQUAL_STRING("00001234", capturePrintHex(0x1234, 255).c_str());
}


// --- Invalid registers and argument guards -----------------------------------

namespace {

// A default-constructed register has no buffer (_raw null, _len 0).
class EmptyRegister : public TPS25751Register
{
public:
    EmptyRegister() : TPS25751Register() {}
    void debugPrint(Stream &) const override {}
};

std::string captureDump(const TPS25751Register &reg, const char *prefix)
{
    return captureStdout([&] { reg.debugDumpRaw(Serial, prefix); });
}

}  // namespace

void test_default_constructed_register_is_invalid()
{
    EmptyRegister reg;
    TEST_ASSERT_FALSE(reg.isValid());
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_EQUAL_size_t(0, reg.size());
}

void test_invalid_register_decodes_to_zero()
{
    EmptyRegister reg;
    TEST_ASSERT_EQUAL_UINT8(0, reg.extractBits(0, 8));
    TEST_ASSERT_EQUAL_UINT16(0, reg.extractBits16(0, 16));
    TEST_ASSERT_EQUAL_UINT32(0, reg.extractBits32(0, 32));
    TEST_ASSERT_TRUE(reg.extractBits64(0, 64) == 0);
    uint32_t out[2] = {0xAAAA, 0xBBBB};
    TEST_ASSERT_EQUAL_size_t(0, reg.extractArray32(0, 8, 2, out));
    TEST_ASSERT_EQUAL_UINT32(0xAAAA, out[0]);
}

void test_static_extract_rejects_bad_arguments()
{
    uint8_t buf[kSize];
    fill(buf);
    // Null data, zero or oversized width, and zero size each read as 0.
    TEST_ASSERT_EQUAL_UINT16(0, TPS25751Register::extractBits16(nullptr, kSize, 0, 8));
    TEST_ASSERT_EQUAL_UINT16(0, TPS25751Register::extractBits16(buf, kSize, 0, 0));
    TEST_ASSERT_EQUAL_UINT16(0, TPS25751Register::extractBits16(buf, kSize, 0, 17));
    TEST_ASSERT_EQUAL_UINT16(0, TPS25751Register::extractBits16(buf, 0, 0, 8));
    TEST_ASSERT_EQUAL_UINT32(0, TPS25751Register::extractBits32(nullptr, kSize, 0, 8));
    TEST_ASSERT_EQUAL_UINT32(0, TPS25751Register::extractBits32(buf, kSize, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, TPS25751Register::extractBits32(buf, kSize, 0, 33));
    TEST_ASSERT_EQUAL_UINT32(0, TPS25751Register::extractBits32(buf, 0, 0, 8));
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(nullptr, kSize, 0, 8) == 0);
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(buf, kSize, 0, 0) == 0);
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(buf, kSize, 0, 65) == 0);
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(buf, 0, 0, 8) == 0);
}

void test_extract_bits64_narrow_field_matches_32_bit_read()
{
    uint8_t buf[kSize];
    fill(buf);
    // A field of 32 bits or fewer takes the 32-bit path: bytes 0..3 = 10 11 12 13.
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(buf, kSize, 0, 32) == 0x13121110ULL);
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(buf, kSize, 8, 12) == 0x211ULL);
}

void test_extract_bits64_high_half_past_16_bit_position_reads_zero()
{
    uint8_t buf[kSize];
    fill(buf);
    // bitPos 0xFFF0 + 32 does not fit in 16 bits, and 0xFFF0 is past the buffer anyway.
    TEST_ASSERT_TRUE(TPS25751Register::extractBits64(buf, kSize, 0xFFF0, 48) == 0);
}

void test_static_set_bits_rejects_bad_arguments()
{
    uint8_t buf[kSize];
    fill(buf);
    uint8_t copy[kSize];
    memcpy(copy, buf, kSize);

    TPS25751Register::setBits(nullptr, kSize, 0, 8, 0xFF);  // must not crash
    TPS25751Register::setBits(buf, kSize, 0, 0, 0xFF);
    TPS25751Register::setBits(buf, kSize, 0, 33, 0xFF);
    TPS25751Register::setBits(buf, 0, 0, 8, 0xFF);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(copy, buf, kSize);
}

void test_debug_dump_raw_invalid_register()
{
    EmptyRegister reg;
    const std::string out = captureDump(reg, "Raw");
    TEST_ASSERT_EQUAL_STRING("Register invalid - cannot dump\n", out.c_str());
}

void test_debug_dump_raw_header_with_and_without_prefix()
{
    uint8_t buf[kSize];
    fill(buf);
    TestRegister reg(buf);

    const std::string withPrefix = captureDump(reg, "Raw");
    TEST_ASSERT_EQUAL_size_t(0, withPrefix.find("Raw (40 bytes):\n"));

    const std::string noPrefix = captureDump(reg, nullptr);
    TEST_ASSERT_EQUAL_size_t(0, noPrefix.find("Register dump (40 bytes):\n"));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_field_ending_at_bit_255);
    RUN_TEST(test_fields_starting_past_bit_255);
    RUN_TEST(test_fields_straddling_bit_255);
    RUN_TEST(test_field_ending_at_the_last_bit_and_one_past_it);
    RUN_TEST(test_positions_far_past_the_buffer_return_zero);
    RUN_TEST(test_set_bits_past_bit_255_preserves_neighbors);
    RUN_TEST(test_set_bits_straddling_bit_255_preserves_neighbors);
    RUN_TEST(test_set_bits_out_of_bounds_is_a_no_op);
    RUN_TEST(test_static_helpers_reach_past_bit_255);
    RUN_TEST(test_extract_enum_past_bit_255);
    RUN_TEST(test_extract_array_across_bit_255);
    RUN_TEST(test_fields_below_bit_255_are_unchanged);
    RUN_TEST(test_print_hex_zero_pads_to_the_digit_count);
    RUN_TEST(test_print_hex_prints_only_the_low_digits);
    RUN_TEST(test_print_hex_digit_count_limits);
    RUN_TEST(test_default_constructed_register_is_invalid);
    RUN_TEST(test_invalid_register_decodes_to_zero);
    RUN_TEST(test_static_extract_rejects_bad_arguments);
    RUN_TEST(test_extract_bits64_narrow_field_matches_32_bit_read);
    RUN_TEST(test_extract_bits64_high_half_past_16_bit_position_reads_zero);
    RUN_TEST(test_static_set_bits_rejects_bad_arguments);
    RUN_TEST(test_debug_dump_raw_invalid_register);
    RUN_TEST(test_debug_dump_raw_header_with_and_without_prefix);

    return UNITY_END();
}
