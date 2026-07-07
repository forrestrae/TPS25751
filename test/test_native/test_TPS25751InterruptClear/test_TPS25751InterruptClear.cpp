/**
 * @file test_TPS25751InterruptClear.cpp
 * @brief Native (host-run) unit tests for TPS25751InterruptClear.
 *
 * Run with: pio test -e native
 *
 * INT_CLEAR1 (0x18) is a write-only staging buffer sharing INT_EVENT1's
 * (0x14) 88-bit (11-byte) bit layout: writing 1 to a bit clears that source
 * in INT_EVENT1. Byte/bit placement for a bit index `n` is `byte = n / 8`,
 * `offset = n % 8` (matching TPS25751Register::setBits()).
 *
 * Since this class exposes no read accessors (reads are not meaningful on
 * real hardware), these tests inspect the local staging buffer directly via
 * raw() to confirm exact bit->byte placement and neighbouring-bit
 * preservation across setBits()-based clear setters.
 *
 * clearAll()'s expected byte pattern (derived by summing the byte/offset
 * contribution of each of the 35 named bits -- see TPS25751InterruptClear.cpp
 * kNamedBits and TPS25751InterruptEvent.h for the canonical bit list):
 *   byte0  = 0x32  (bits 1,4,5)
 *   byte1  = 0xF2  (bits 9,12,13,14,15)
 *   byte2  = 0xB6  (bits 17,18,20,21,23)
 *   byte3  = 0x4D  (bits 24,26,27,30)
 *   byte4  = 0xDF  (bits 32,33,34,35,36,38,39)
 *   byte5  = 0x4C  (bits 42,43,46)
 *   byte6  = 0x00  (no named bits)
 *   byte7  = 0x13  (bits 56,57,60)
 *   byte8  = 0x06  (bits 65,66)
 *   byte9  = 0x00  (no named bits)
 *   byte10 = 0x07  (bits 80,81,82)
 */

#include <unity.h>

#include "TPS25751InterruptClear.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t INT_CLEAR1_SIZE = TPS25751Registers::Registers::INT_CLEAR1.size;  // 11

constexpr uint8_t ALL_ZEROS_DATA[INT_CLEAR1_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[INT_CLEAR1_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr uint8_t CLEAR_ALL_EXPECTED[INT_CLEAR1_SIZE] = {
    0x32, 0xF2, 0xB6, 0x4D, 0xDF, 0x4C, 0x00, 0x13, 0x06, 0x00, 0x07,
};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751InterruptClear reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_CLEAR1_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(INT_CLEAR1_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_ZEROS_DATA, reg.raw(), INT_CLEAR1_SIZE);
}

void test_data_constructor_copies_buffer()
{
    TPS25751InterruptClear reg(CLEAR_ALL_EXPECTED);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_CLEAR1_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(CLEAR_ALL_EXPECTED, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_ALL_EXPECTED, reg.raw(), INT_CLEAR1_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751InterruptClear reg(CLEAR_ALL_EXPECTED, INT_CLEAR1_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(INT_CLEAR1_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_ALL_EXPECTED, reg.raw(), INT_CLEAR1_SIZE);
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751InterruptClear reg(TPS25751Registers::Registers::INT_CLEAR1);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_CLEAR1_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_ZEROS_DATA, reg.raw(), INT_CLEAR1_SIZE);
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751InterruptClear reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_ZEROS_DATA, reg.raw(), INT_CLEAR1_SIZE);
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[3] = {0x02, 0x00, 0x00};
    TPS25751InterruptClear reg(undersized, sizeof(undersized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(INT_CLEAR1_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- hasValidData vs isSemanticallyValid: the all-zero and all-ones cases ---

void test_all_zeros_is_valid_data_and_semantically_valid()
{
    // The common case: nothing staged to clear.
    TPS25751InterruptClear reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_fails_has_valid_data_but_is_semantically_valid()
{
    // Unlike field-structured registers (e.g. SLEEP_CONTROL), isSemanticallyValid()
    // here deliberately does NOT call hasValidData() -- INT_CLEAR1 has no field
    // structure to validate and "clear everything including reserved bits" is not
    // an error state for a write-only staging buffer, just an unusual one.
    TPS25751InterruptClear reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Generic clear(bitPos) ---------------------------------------------------

void test_clear_generic_bit_position()
{
    TPS25751InterruptClear reg;  // starts all-zero
    reg.clear(9);  // overcurrent: byte 1, offset 1

    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[1]);
    for (size_t i = 2; i < INT_CLEAR1_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[i]);
    }
}

// --- Named clear<Name>() setters: bit->byte placement + preservation --------

void test_clear_pd_hardreset_sets_expected_bit()
{
    TPS25751InterruptClear reg;
    reg.clearPdHardreset();  // bit 1, byte 0

    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[0]);
    for (size_t i = 1; i < INT_CLEAR1_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[i]);
    }
}

void test_clear_i2c_controller_nacked_sets_expected_bit()
{
    TPS25751InterruptClear reg;
    reg.clearI2cControllerNacked();  // bit 82, byte 10

    TEST_ASSERT_EQUAL_UINT8(0x04, reg.raw()[10]);
    for (size_t i = 0; i < INT_CLEAR1_SIZE - 1; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[i]);
    }
}

void test_multiple_named_clears_accumulate_without_disturbing_each_other()
{
    TPS25751InterruptClear reg;
    reg.clearPdHardreset();          // byte 0, offset 1
    reg.clearOvercurrent();          // byte 1, offset 1
    reg.clearLiquidDetection();      // byte 7, offset 4
    reg.clearReadyForPatch();        // byte 10, offset 1

    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[0]);
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[1]);
    TEST_ASSERT_EQUAL_UINT8(0x10, reg.raw()[7]);
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[10]);
    // Untouched bytes remain zero.
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[4]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[6]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[8]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[9]);
}

// --- clearAll(): every named bit set, reserved bits left alone, idempotent --

void test_clear_all_sets_every_named_bit()
{
    TPS25751InterruptClear reg;
    reg.clearAll();

    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_ALL_EXPECTED, reg.raw(), INT_CLEAR1_SIZE);
}

void test_clear_all_is_idempotent()
{
    TPS25751InterruptClear reg;
    reg.clearAll();
    reg.clearAll();

    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_ALL_EXPECTED, reg.raw(), INT_CLEAR1_SIZE);
}

void test_clear_all_preserves_prior_manual_clears()
{
    // clearAll() only ORs in the named bits; it must not clobber bits a
    // caller had already staged (there are none outside the named set here,
    // but this also confirms clearAll() doesn't zero the buffer first).
    TPS25751InterruptClear reg;
    reg.clearPdHardreset();
    reg.clearAll();

    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_ALL_EXPECTED, reg.raw(), INT_CLEAR1_SIZE);
}

void test_setters_no_op_on_invalid_register()
{
    // A moved-from register is !isValid(); setters must guard and no-op.
    TPS25751InterruptClear reg;
    TPS25751InterruptClear moved(static_cast<TPS25751InterruptClear &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.clearPdHardreset();
    reg.clearAll();
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751InterruptClear original;
    original.clearAll();
    TPS25751InterruptClear copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), INT_CLEAR1_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    TPS25751InterruptClear original;
    original.clearPdHardreset();
    const uint8_t *buf = original.raw();

    TPS25751InterruptClear moved(static_cast<TPS25751InterruptClear &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0x02, moved.raw()[0]);
}

// --- Runner -------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_zeros_is_valid_data_and_semantically_valid);
    RUN_TEST(test_all_ones_fails_has_valid_data_but_is_semantically_valid);

    RUN_TEST(test_clear_generic_bit_position);
    RUN_TEST(test_clear_pd_hardreset_sets_expected_bit);
    RUN_TEST(test_clear_i2c_controller_nacked_sets_expected_bit);
    RUN_TEST(test_multiple_named_clears_accumulate_without_disturbing_each_other);

    RUN_TEST(test_clear_all_sets_every_named_bit);
    RUN_TEST(test_clear_all_is_idempotent);
    RUN_TEST(test_clear_all_preserves_prior_manual_clears);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    return UNITY_END();
}
