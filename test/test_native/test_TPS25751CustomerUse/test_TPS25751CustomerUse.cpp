/**
 * @file test_TPS25751CustomerUse.cpp
 * @brief Native (host-run) unit tests for TPS25751CustomerUse.
 *
 * Run with: pio test -e native
 *
 * CUSTOMER_USE (0x06) is an opaque 8-byte, application-defined blob with no
 * field structure of its own (TRM Table 3-4). Its reset value is all zeros.
 * Unlike TPS25751SleepControl, isSemanticallyValid() here does NOT call the
 * inherited hasValidData() heuristic (mirroring TPS25751Data's minimal
 * approach), so an all-0xFF buffer is semantically valid for this register --
 * see test_all_ones_is_semantically_valid below, which is the opposite of
 * how all-0xFF is treated for field-structured registers like STATUS or
 * SLEEP_CONTROL.
 */

#include <unity.h>

#include "TPS25751CustomerUse.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t CUSTOMER_USE_SIZE = TPS25751Registers::Registers::CUSTOMER_USE.size;  // 8

constexpr uint8_t ALL_ZEROS_DATA[CUSTOMER_USE_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[CUSTOMER_USE_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Little-endian bytes 1..8 -> value 0x0807060504030201 (byte 0 = LSB).
constexpr uint8_t SEQUENTIAL_DATA[CUSTOMER_USE_SIZE] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
constexpr uint64_t SEQUENTIAL_VALUE = 0x0807060504030201ULL;

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751CustomerUse reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CUSTOMER_USE_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(CUSTOMER_USE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(0, reg.value());
}

void test_data_constructor_copies_buffer()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CUSTOMER_USE_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(SEQUENTIAL_DATA, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SEQUENTIAL_DATA, reg.raw(), CUSTOMER_USE_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA, CUSTOMER_USE_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(CUSTOMER_USE_SIZE));
    TEST_ASSERT_EQUAL_UINT64(SEQUENTIAL_VALUE, reg.value());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751CustomerUse reg(TPS25751Registers::Registers::CUSTOMER_USE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CUSTOMER_USE_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT64(0, reg.value());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751CustomerUse reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT64(0, reg.value());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t shortData[4] = {0x01, 0x02, 0x03, 0x04};
    TPS25751CustomerUse reg(shortData, sizeof(shortData));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(CUSTOMER_USE_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
    // extractBits64() assembles each 32-bit half independently with its own
    // bounds check: the low 32 bits fit entirely within this 4-byte buffer
    // and decode normally, while the high 32 bits fall out of bounds and
    // decode to 0 -- not the whole 64-bit field collapsing to 0.
    TEST_ASSERT_EQUAL_UINT64(0x04030201ULL, reg.value());
    TEST_ASSERT_EQUAL_UINT8(0x01, reg.byteAt(0));
    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(4));  // out of range for this 4-byte buffer
}

// --- Semantic validation (any byte pattern is valid) ------------------------

void test_all_zeros_is_semantically_valid()
{
    // The register's reset state.
    TPS25751CustomerUse reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_semantically_valid()
{
    // Unlike field-structured registers, CUSTOMER_USE has no hasValidData()
    // gate in isSemanticallyValid() -- any byte pattern, including all-0xFF,
    // is valid customer data.
    TPS25751CustomerUse reg(ALL_ONES_DATA);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Field decode: little-endian assembly -----------------------------------

void test_value_little_endian_assembly()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);
    TEST_ASSERT_EQUAL_UINT64(SEQUENTIAL_VALUE, reg.value());
}

void test_byte_at_matches_raw_bytes()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);
    for (size_t i = 0; i < CUSTOMER_USE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(SEQUENTIAL_DATA[i], reg.byteAt(i));
    }
}

void test_byte_at_out_of_range_returns_zero()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);
    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(CUSTOMER_USE_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0, reg.byteAt(1000));
}

// --- Setters -----------------------------------------------------------------

void test_set_value_round_trip()
{
    TPS25751CustomerUse reg;
    reg.setValue(SEQUENTIAL_VALUE);

    TEST_ASSERT_EQUAL_UINT64(SEQUENTIAL_VALUE, reg.value());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SEQUENTIAL_DATA, reg.raw(), CUSTOMER_USE_SIZE);
}

void test_set_value_zero()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);
    reg.setValue(0);
    TEST_ASSERT_EQUAL_UINT64(0, reg.value());
}

void test_set_bytes_short_copy_preserves_remaining_bytes()
{
    const uint8_t initial[CUSTOMER_USE_SIZE] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    TPS25751CustomerUse reg(initial);

    const uint8_t partial[3] = {0x11, 0x22, 0x33};
    reg.setBytes(partial, sizeof(partial));

    TEST_ASSERT_EQUAL_UINT8(0x11, reg.byteAt(0));
    TEST_ASSERT_EQUAL_UINT8(0x22, reg.byteAt(1));
    TEST_ASSERT_EQUAL_UINT8(0x33, reg.byteAt(2));
    // Untouched tail bytes remain from before the short copy.
    for (size_t i = 3; i < CUSTOMER_USE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xAA, reg.byteAt(i));
    }
}

void test_set_bytes_caps_at_eight_bytes()
{
    TPS25751CustomerUse reg;
    const uint8_t tooLong[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    reg.setBytes(tooLong, sizeof(tooLong));

    for (size_t i = 0; i < CUSTOMER_USE_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(tooLong[i], reg.byteAt(i));
    }
}

void test_set_bytes_null_source_is_noop()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);
    reg.setBytes(nullptr, 4);
    TEST_ASSERT_EQUAL_UINT64(SEQUENTIAL_VALUE, reg.value());  // unchanged
}

void test_setters_no_op_on_invalid_register()
{
    TPS25751CustomerUse reg(SEQUENTIAL_DATA);
    TPS25751CustomerUse moved(static_cast<TPS25751CustomerUse &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setValue(1234);
    const uint8_t bytes[2] = {0x01, 0x02};
    reg.setBytes(bytes, sizeof(bytes));
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
    TEST_ASSERT_EQUAL_UINT64(SEQUENTIAL_VALUE, moved.value());  // untouched
}

// --- Rule of Five -----------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751CustomerUse original(SEQUENTIAL_DATA);
    TPS25751CustomerUse copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), CUSTOMER_USE_SIZE);
    TEST_ASSERT_EQUAL_UINT64(original.value(), copy.value());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751CustomerUse original(SEQUENTIAL_DATA);
    const uint8_t *buf = original.raw();

    TPS25751CustomerUse moved(static_cast<TPS25751CustomerUse &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT64(SEQUENTIAL_VALUE, moved.value());
}

// --- Runner -----------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_zeros_is_semantically_valid);
    RUN_TEST(test_all_ones_is_semantically_valid);

    RUN_TEST(test_value_little_endian_assembly);
    RUN_TEST(test_byte_at_matches_raw_bytes);
    RUN_TEST(test_byte_at_out_of_range_returns_zero);

    RUN_TEST(test_set_value_round_trip);
    RUN_TEST(test_set_value_zero);
    RUN_TEST(test_set_bytes_short_copy_preserves_remaining_bytes);
    RUN_TEST(test_set_bytes_caps_at_eight_bytes);
    RUN_TEST(test_set_bytes_null_source_is_noop);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    return UNITY_END();
}
