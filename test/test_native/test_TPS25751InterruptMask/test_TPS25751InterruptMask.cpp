/**
 * @file test_TPS25751InterruptMask.cpp
 * @brief Native (host-run) unit tests for TPS25751InterruptMask.
 *
 * Run with: pio test -e native
 *
 * INT_MASK1 (0x16) shares its 88-bit (11-byte) layout with the read-only
 * INT_EVENT1 (0x14) register (see TPS25751InterruptEvent) -- a mask bit of 1
 * means the corresponding INT_EVENT1 source is enabled (allowed to assert);
 * 0 disables/blocks it. Byte/bit placement for a bit index `n` is always
 * `byte = n / 8`, `offset = n % 8` (little-endian bit numbering, matching
 * TPS25751Register::extractBits()/setBits()).
 *
 * Representative bits exercised below, with their byte/offset math:
 *   bit  1 (pdHardreset)          -> byte 0, offset 1  -> 0x02
 *   bit  9 (overcurrent)          -> byte 1, offset 1  -> 0x02
 *   bit 30 (cmd1Complete)         -> byte 3, offset 6  -> 0x40
 *   bit 60 (liquidDetection)      -> byte 7, offset 4  -> 0x10
 *   bit 82 (i2cControllerNacked)  -> byte 10, offset 2 -> 0x04
 */

#include <unity.h>

#include "TPS25751InterruptMask.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t INT_MASK1_SIZE = TPS25751Registers::Registers::INT_MASK1.size;  // 11

// Five representative bits set, spread across distinct bytes (see file header
// for the byte/offset derivation of each).
constexpr uint8_t MULTI_BIT_DATA[INT_MASK1_SIZE] = {
    0x02,  // byte0: bit1  (pdHardreset)
    0x02,  // byte1: bit9  (overcurrent)
    0x00,  // byte2
    0x40,  // byte3: bit30 (cmd1Complete)
    0x00,  // byte4
    0x00,  // byte5
    0x00,  // byte6
    0x10,  // byte7: bit60 (liquidDetection)
    0x00,  // byte8
    0x00,  // byte9
    0x04,  // byte10: bit82 (i2cControllerNacked)
};

constexpr uint8_t ALL_ZEROS_DATA[INT_MASK1_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[INT_MASK1_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751InterruptMask reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_MASK1_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(INT_MASK1_SIZE));
    TEST_ASSERT_FALSE(reg.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(0, reg.getEnabledCount());
}

void test_data_constructor_copies_buffer()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_MASK1_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(MULTI_BIT_DATA, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MULTI_BIT_DATA, reg.raw(), INT_MASK1_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA, INT_MASK1_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(INT_MASK1_SIZE));
    TEST_ASSERT_TRUE(reg.pdHardreset());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751InterruptMask reg(TPS25751Registers::Registers::INT_MASK1);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_MASK1_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.hasAnyEnabled());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751InterruptMask reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.hasAnyEnabled());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[3] = {0x02, 0x00, 0x00};
    TPS25751InterruptMask reg(undersized, sizeof(undersized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(INT_MASK1_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Data validation ---------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    TPS25751InterruptMask reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Field decode: bit->byte placement at several distinct offsets ----------

void test_decode_multi_bit_pattern()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);

    TEST_ASSERT_TRUE(reg.pdHardreset());          // bit 1
    TEST_ASSERT_TRUE(reg.overcurrent());          // bit 9
    TEST_ASSERT_TRUE(reg.cmd1Complete());         // bit 30
    TEST_ASSERT_TRUE(reg.liquidDetection());      // bit 60
    TEST_ASSERT_TRUE(reg.i2cControllerNacked());  // bit 82

    // Spot-check a handful of bits that must NOT be set by this pattern.
    TEST_ASSERT_FALSE(reg.powerSwapComplete());          // bit 4
    TEST_ASSERT_FALSE(reg.dataSwapComplete());           // bit 5
    TEST_ASSERT_FALSE(reg.newContractAsConsumer());      // bit 12
    TEST_ASSERT_FALSE(reg.usbHostPresent());             // bit 20
    TEST_ASSERT_FALSE(reg.deviceIncompatibleError());    // bit 32
    TEST_ASSERT_FALSE(reg.extDcdcSinkSafeState());        // bit 56
    TEST_ASSERT_FALSE(reg.patchLoaded());                // bit 80
}

void test_enabled_count_and_has_any_enabled()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);
    TEST_ASSERT_TRUE(reg.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(5, reg.getEnabledCount());

    TPS25751InterruptMask none;
    TEST_ASSERT_FALSE(none.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(0, none.getEnabledCount());
}

// --- Semantic validation: any combination is legal for a mask ----------------

void test_semantic_valid_for_arbitrary_combination()
{
    TEST_ASSERT_TRUE(TPS25751InterruptMask(MULTI_BIT_DATA).isSemanticallyValid());
}

// --- Setters: round trip + neighbouring-bit preservation --------------------

void test_set_pd_hardreset_round_trip()
{
    TPS25751InterruptMask reg;  // starts all-zero
    reg.setPdHardreset(true);
    TEST_ASSERT_TRUE(reg.pdHardreset());

    reg.setPdHardreset(false);
    TEST_ASSERT_FALSE(reg.pdHardreset());
}

void test_set_pd_hardreset_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setPdHardreset(false);  // bit 1, byte 0

    TEST_ASSERT_EQUAL_UINT8(0xFD, reg.raw()[0]);  // 1111_1101
    TEST_ASSERT_FALSE(reg.pdHardreset());
    // Neighbouring bits/bytes untouched.
    TEST_ASSERT_TRUE(reg.overcurrent());          // byte 1 untouched
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.raw()[1]);
}

void test_set_overcurrent_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setOvercurrent(false);  // bit 9, byte 1

    TEST_ASSERT_EQUAL_UINT8(0xFD, reg.raw()[1]);  // 1111_1101
    TEST_ASSERT_FALSE(reg.overcurrent());
    TEST_ASSERT_TRUE(reg.pdHardreset());          // byte 0 untouched
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.raw()[0]);
}

void test_set_liquid_detection_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setLiquidDetection(false);  // bit 60, byte 7

    TEST_ASSERT_EQUAL_UINT8(0xEF, reg.raw()[7]);  // 1110_1111
    TEST_ASSERT_FALSE(reg.liquidDetection());
    TEST_ASSERT_TRUE(reg.extDcdcSourceSafeState());  // bit 57, same byte, untouched
}

void test_set_i2c_controller_nacked_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setI2cControllerNacked(false);  // bit 82, byte 10

    TEST_ASSERT_EQUAL_UINT8(0xFB, reg.raw()[10]);  // 1111_1011
    TEST_ASSERT_FALSE(reg.i2cControllerNacked());
    TEST_ASSERT_TRUE(reg.readyForPatch());  // bit 81, same byte, untouched
}

void test_setters_no_op_on_invalid_register()
{
    // A moved-from register is !isValid(); setters must guard and no-op.
    TPS25751InterruptMask reg(MULTI_BIT_DATA);
    TPS25751InterruptMask moved(static_cast<TPS25751InterruptMask &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setPdHardreset(true);
    reg.setOvercurrent(false);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
    TEST_ASSERT_TRUE(moved.pdHardreset());  // untouched
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751InterruptMask original(MULTI_BIT_DATA);
    TPS25751InterruptMask copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), INT_MASK1_SIZE);
    TEST_ASSERT_EQUAL_UINT8(original.getEnabledCount(), copy.getEnabledCount());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751InterruptMask original(MULTI_BIT_DATA);
    const uint8_t *buf = original.raw();

    TPS25751InterruptMask moved(static_cast<TPS25751InterruptMask &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_TRUE(moved.pdHardreset());
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

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);

    RUN_TEST(test_decode_multi_bit_pattern);
    RUN_TEST(test_enabled_count_and_has_any_enabled);

    RUN_TEST(test_semantic_valid_for_arbitrary_combination);

    RUN_TEST(test_set_pd_hardreset_round_trip);
    RUN_TEST(test_set_pd_hardreset_preserves_other_bits);
    RUN_TEST(test_set_overcurrent_preserves_other_bits);
    RUN_TEST(test_set_liquid_detection_preserves_other_bits);
    RUN_TEST(test_set_i2c_controller_nacked_preserves_other_bits);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    return UNITY_END();
}
