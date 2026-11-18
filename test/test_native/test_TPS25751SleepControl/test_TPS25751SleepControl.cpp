/**
 * @file test_TPS25751SleepControl.cpp
 * @brief Native (host-run) unit tests for TPS25751SleepControl.
 *
 * Run with: pio test -e native
 *
 * Test buffers are hand-packed from the SLEEP_CONTROL (0x70) field table,
 * Source: SLVUCR8B (Rev. B, 2026-08), page 52, via the tps25751-docs MCP:
 *   bit 0      Sleep Mode Allowed (reset 1h)
 *   bits 2:1   Sleep Time (0h=Reserved, 1h=100 ms, 2h=1000 ms, 3h=Reserved;
 *              reset 1h). Rev. A listed 2h as 1200 ms.
 *   bits 7:3   Reserved
 *
 * The Rev. B reset value is 0x03 (allowed, 100 ms), which is semantically
 * valid; see test_reset_value_0x03_is_allowed_100ms_and_valid. 0x01 (allowed
 * with a Reserved Sleep Time, the Rev. A reset) stays an invalid case. A
 * Reserved Sleep Time is only acceptable while Sleep Mode Allowed is clear.
 */

#include <unity.h>

#include "TPS25751SleepControl.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t SLEEP_CONTROL_SIZE = TPS25751Registers::Registers::SLEEP_CONTROL.size;  // 1

// bit0=1 (allowed), bits2:1 = 01 (100 ms)   -> 0b011 = 0x03 (Rev. B reset value)
constexpr uint8_t ALLOWED_100MS[SLEEP_CONTROL_SIZE] = {0x03};

// bit0=1 (allowed), bits2:1 = 10 (1000 ms)  -> 0b101 = 0x05
constexpr uint8_t ALLOWED_1000MS[SLEEP_CONTROL_SIZE] = {0x05};

// bit0=1 (allowed), bits2:1 = 00 (Reserved) -> 0b001 = 0x01 (Rev. A reset value)
constexpr uint8_t ALLOWED_RESERVED0[SLEEP_CONTROL_SIZE] = {0x01};

// bit0=0 (not allowed), bits2:1 = 10 (1000 ms) -> 0b100 = 0x04
constexpr uint8_t NOT_ALLOWED_1000MS[SLEEP_CONTROL_SIZE] = {0x04};

// bit0=1 (allowed), bits2:1 = 11 (Reserved) -> 0b111 = 0x07
constexpr uint8_t ALLOWED_RESERVED3[SLEEP_CONTROL_SIZE] = {0x07};

// bit0=0 (not allowed), bits2:1 = 11 (Reserved) -> 0b110 = 0x06
// Reserved Sleep Time is acceptable while sleep is not allowed.
constexpr uint8_t NOT_ALLOWED_RESERVED3[SLEEP_CONTROL_SIZE] = {0x06};

constexpr uint8_t ALL_ZEROS_DATA[SLEEP_CONTROL_SIZE] = {0x00};
constexpr uint8_t ALL_ONES_DATA[SLEEP_CONTROL_SIZE] = {0xFF};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751SleepControl reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(SLEEP_CONTROL_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(SLEEP_CONTROL_SIZE));
    TEST_ASSERT_FALSE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Reserved0, reg.sleepTime());
}

void test_data_constructor_copies_buffer()
{
    TPS25751SleepControl reg(ALLOWED_100MS);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(SLEEP_CONTROL_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(ALLOWED_100MS, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALLOWED_100MS, reg.raw(), SLEEP_CONTROL_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751SleepControl reg(ALLOWED_1000MS, SLEEP_CONTROL_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(SLEEP_CONTROL_SIZE));
    TEST_ASSERT_EQUAL(SleepTime::Time1000ms, reg.sleepTime());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751SleepControl reg(TPS25751Registers::Registers::SLEEP_CONTROL);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(SLEEP_CONTROL_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.sleepModeAllowed());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751SleepControl reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Reserved0, reg.sleepTime());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t oversized[2] = {0x03, 0x00};
    TPS25751SleepControl reg(oversized, sizeof(oversized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(SLEEP_CONTROL_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
    // Field decode still works normally against the (oversized) buffer.
    TEST_ASSERT_TRUE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, reg.sleepTime());
}

// --- Data validation --------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    // hasValidData() only flags all-0xFF as the bus-error signature; an
    // all-zero buffer (not allowed + Reserved0) passes it, and is also
    // semantically valid since sleep is not allowed.
    TPS25751SleepControl reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    TPS25751SleepControl reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Field decode -----------------------------------------------------------

void test_decode_allowed_100ms()
{
    TPS25751SleepControl reg(ALLOWED_100MS);
    TEST_ASSERT_TRUE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, reg.sleepTime());
}

void test_decode_allowed_1000ms()
{
    TPS25751SleepControl reg(ALLOWED_1000MS);
    TEST_ASSERT_TRUE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Time1000ms, reg.sleepTime());
}

void test_decode_not_allowed_1000ms()
{
    TPS25751SleepControl reg(NOT_ALLOWED_1000MS);
    TEST_ASSERT_FALSE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Time1000ms, reg.sleepTime());
}

// --- Semantic validation ----------------------------------------------------

void test_reset_value_0x03_is_allowed_100ms_and_valid()
{
    // Rev. B reset value 0x03 (SLVUCR8B p.52): Sleep Mode Allowed = 1,
    // Sleep Time = 1h (100 ms).
    TPS25751SleepControl reg(ALLOWED_100MS);
    TEST_ASSERT_EQUAL_UINT8(0x03, reg.raw()[0]);
    TEST_ASSERT_TRUE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, reg.sleepTime());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_allowed_reserved0_is_semantically_invalid()
{
    // 0x01: Sleep Mode Allowed set with a Reserved (0h) Sleep Time.
    TEST_ASSERT_FALSE(TPS25751SleepControl(ALLOWED_RESERVED0).isSemanticallyValid());
}

void test_allowed_reserved3_is_semantically_invalid()
{
    TEST_ASSERT_FALSE(TPS25751SleepControl(ALLOWED_RESERVED3).isSemanticallyValid());
}

void test_not_allowed_reserved_time_is_semantically_valid()
{
    // Reserved Sleep Time is acceptable when sleep is not allowed.
    TEST_ASSERT_TRUE(TPS25751SleepControl(NOT_ALLOWED_RESERVED3).isSemanticallyValid());
}

void test_semantic_valid_states()
{
    TEST_ASSERT_TRUE(TPS25751SleepControl(ALLOWED_100MS).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751SleepControl(ALLOWED_1000MS).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751SleepControl(NOT_ALLOWED_1000MS).isSemanticallyValid());
}

// --- Setters: round trip + neighbouring-bit preservation --------------------

void test_set_sleep_mode_allowed_round_trip()
{
    TPS25751SleepControl reg;  // starts all-zero
    reg.setSleepModeAllowed(true);
    TEST_ASSERT_TRUE(reg.sleepModeAllowed());

    reg.setSleepModeAllowed(false);
    TEST_ASSERT_FALSE(reg.sleepModeAllowed());
}

void test_set_sleep_mode_allowed_preserves_other_bits()
{
    const uint8_t fullOnes[SLEEP_CONTROL_SIZE] = {0xFF};
    TPS25751SleepControl reg(fullOnes);

    reg.setSleepModeAllowed(false);

    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.raw()[0]);
    TEST_ASSERT_FALSE(reg.sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Reserved3, reg.sleepTime());  // untouched
}

void test_set_sleep_time_round_trip()
{
    TPS25751SleepControl reg;  // starts all-zero
    reg.setSleepTime(SleepTime::Time1000ms);
    TEST_ASSERT_EQUAL(SleepTime::Time1000ms, reg.sleepTime());

    reg.setSleepTime(SleepTime::Time100ms);
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, reg.sleepTime());
}

void test_set_sleep_time_preserves_other_bits()
{
    const uint8_t fullOnes[SLEEP_CONTROL_SIZE] = {0xFF};
    TPS25751SleepControl reg(fullOnes);

    reg.setSleepTime(SleepTime::Time100ms);

    TEST_ASSERT_EQUAL_UINT8(0xFB, reg.raw()[0]);  // 1111_1011: bit0 + reserved bits untouched
    TEST_ASSERT_TRUE(reg.sleepModeAllowed());      // untouched
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, reg.sleepTime());
}

void test_setters_no_op_on_invalid_register()
{
    // A moved-from register is !isValid(); setters must guard and no-op.
    TPS25751SleepControl reg(ALLOWED_100MS);
    TPS25751SleepControl moved(static_cast<TPS25751SleepControl &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setSleepModeAllowed(true);
    reg.setSleepTime(SleepTime::Time1000ms);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, moved.sleepTime());  // untouched
}

// --- toString() coverage ----------------------------------------------------

void test_to_string_covers_all_enum_values()
{
    TEST_ASSERT_EQUAL_STRING("Reserved (0h)", toString(SleepTime::Reserved0));
    TEST_ASSERT_EQUAL_STRING("100 ms", toString(SleepTime::Time100ms));
    TEST_ASSERT_EQUAL_STRING("1000 ms", toString(SleepTime::Time1000ms));
    TEST_ASSERT_EQUAL_STRING("Reserved (3h)", toString(SleepTime::Reserved3));
}

// --- Deprecated alias ---------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void test_deprecated_time1200ms_alias_matches_time1000ms()
{
    // The Rev. A name must keep its old value (2h) so existing sketches read
    // and write the same bits.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SleepTime::Time1000ms),
                            static_cast<uint8_t>(SleepTime::Time1200ms));

    TPS25751SleepControl reg;
    reg.setSleepTime(SleepTime::Time1200ms);
    TEST_ASSERT_EQUAL_UINT8(0x04, reg.raw()[0]);
    TEST_ASSERT_EQUAL(SleepTime::Time1000ms, reg.sleepTime());
}
#pragma GCC diagnostic pop

// --- Rule of Five -----------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751SleepControl original(ALLOWED_1000MS);
    TPS25751SleepControl copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), SLEEP_CONTROL_SIZE);
    TEST_ASSERT_EQUAL(original.sleepTime(), copy.sleepTime());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751SleepControl original(ALLOWED_100MS);
    const uint8_t *buf = original.raw();

    TPS25751SleepControl moved(static_cast<TPS25751SleepControl &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, moved.sleepTime());
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

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);

    RUN_TEST(test_decode_allowed_100ms);
    RUN_TEST(test_decode_allowed_1000ms);
    RUN_TEST(test_decode_not_allowed_1000ms);

    RUN_TEST(test_reset_value_0x03_is_allowed_100ms_and_valid);
    RUN_TEST(test_allowed_reserved0_is_semantically_invalid);
    RUN_TEST(test_allowed_reserved3_is_semantically_invalid);
    RUN_TEST(test_not_allowed_reserved_time_is_semantically_valid);
    RUN_TEST(test_semantic_valid_states);

    RUN_TEST(test_set_sleep_mode_allowed_round_trip);
    RUN_TEST(test_set_sleep_mode_allowed_preserves_other_bits);
    RUN_TEST(test_set_sleep_time_round_trip);
    RUN_TEST(test_set_sleep_time_preserves_other_bits);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_to_string_covers_all_enum_values);
    RUN_TEST(test_deprecated_time1200ms_alias_matches_time1000ms);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    return UNITY_END();
}
