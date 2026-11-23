/**
 * @file test_TPS25751InterruptClear.cpp
 * @brief Native (host-run) unit tests for TPS25751InterruptClear.
 *
 * Run with: pio test -e native
 *
 * INT_CLEAR1 (0x18) is a clear-request staging buffer (R/W per the TRM, written only by the library) sharing INT_EVENT1's
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
 * contribution of each of the 38 named bits; bits 29 and 51 added per
 * SLVUCR8B (Rev. B, 2026-08), page 23 -- see TPS25751InterruptClear.cpp
 * kNamedBits and TPS25751InterruptEvent.h for the canonical bit list):
 *   byte0  = 0x3A  (bits 1,3,4,5)
 *   byte1  = 0xF2  (bits 9,12,13,14,15)
 *   byte2  = 0xB6  (bits 17,18,20,21,23)
 *   byte3  = 0x6D  (bits 24,26,27,29,30)
 *   byte4  = 0xDF  (bits 32,33,34,35,36,38,39)
 *   byte5  = 0x4C  (bits 42,43,46)
 *   byte6  = 0x08  (bit 51)
 *   byte7  = 0x13  (bits 56,57,60)
 *   byte8  = 0x06  (bits 65,66)
 *   byte9  = 0x00  (no named bits)
 *   byte10 = 0x07  (bits 80,81,82)
 */

#include <unity.h>
#include <cstdio>
#include <string>

#include "TPS25751InterruptClear.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t INT_CLEAR1_SIZE = TPS25751Registers::Registers::INT_CLEAR1.size;  // 11

constexpr uint8_t ALL_ZEROS_DATA[INT_CLEAR1_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[INT_CLEAR1_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr uint8_t CLEAR_ALL_EXPECTED[INT_CLEAR1_SIZE] = {
    0x3A, 0xF2, 0xB6, 0x6D, 0xDF, 0x4C, 0x08, 0x13, 0x06, 0x00, 0x07,
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
    // an error state for a clear-request staging buffer, just an unusual one.
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

void test_clear_plug_insert_or_removal_sets_expected_bit()
{
    TPS25751InterruptClear reg;
    reg.clearPlugInsertOrRemoval();  // bit 3, byte 0

    TEST_ASSERT_EQUAL_UINT8(0x08, reg.raw()[0]);
    for (size_t i = 1; i < INT_CLEAR1_SIZE; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[i]);
    }
}

void test_clear_midb_received_and_discover_mode_completed_set_expected_bits()
{
    // Rev. B bits (SLVUCR8B p.23): MIDB Received bit 29 -> byte 3 bit 5;
    // Discover Mode Completed bit 51 -> byte 6 bit 3.
    TPS25751InterruptClear reg;

    reg.clearMidbReceived();
    TEST_ASSERT_EQUAL_UINT8(0x20, reg.raw()[3]);

    reg.clearDiscoverModeCompleted();
    TEST_ASSERT_EQUAL_UINT8(0x08, reg.raw()[6]);
    TEST_ASSERT_EQUAL_UINT8(0x20, reg.raw()[3]);  // bit 29 still staged

    for (size_t i = 0; i < INT_CLEAR1_SIZE; i++) {
        if (i != 3 && i != 6) TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[i]);
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


// --- Every named clear setter sets exactly its documented bit ---------------

namespace {

struct ClearCase {
    void (TPS25751InterruptClear::*fn)();
    uint8_t bit;
};

constexpr ClearCase kClearCases[] = {
    {&TPS25751InterruptClear::clearI2cControllerNacked, 82},
    {&TPS25751InterruptClear::clearReadyForPatch, 81},
    {&TPS25751InterruptClear::clearPatchLoaded, 80},
    {&TPS25751InterruptClear::clearMbrdBufferReady, 66},
    {&TPS25751InterruptClear::clearTxMemoryBufferEmpty, 65},
    {&TPS25751InterruptClear::clearLiquidDetection, 60},
    {&TPS25751InterruptClear::clearExtDcdcSourceSafeState, 57},
    {&TPS25751InterruptClear::clearExtDcdcSinkSafeState, 56},
    {&TPS25751InterruptClear::clearUnableToSourceError, 46},
    {&TPS25751InterruptClear::clearMessageDataError, 39},
    {&TPS25751InterruptClear::clearProtocolError, 38},
    {&TPS25751InterruptClear::clearMissingGetCapabilitiesError, 36},
    {&TPS25751InterruptClear::clearPowerEventOccurredError, 35},
    {&TPS25751InterruptClear::clearCanProvideVoltageLaterError, 34},
    {&TPS25751InterruptClear::clearCannotProvideVoltageError, 33},
    {&TPS25751InterruptClear::clearDeviceIncompatibleError, 32},
    {&TPS25751InterruptClear::clearPlugEarlyNotification, 43},
    {&TPS25751InterruptClear::clearSinkTransitionCompleted, 42},
    {&TPS25751InterruptClear::clearPlugInsertOrRemoval, 3},
    {&TPS25751InterruptClear::clearCmd1Complete, 30},
    {&TPS25751InterruptClear::clearMidbReceived, 29},
    {&TPS25751InterruptClear::clearDiscoverModeCompleted, 51},
    {&TPS25751InterruptClear::clearPdStatusUpdated, 27},
    {&TPS25751InterruptClear::clearStatusUpdated, 26},
    {&TPS25751InterruptClear::clearPowerStatusUpdated, 24},
    {&TPS25751InterruptClear::clearPowerPathSwitchChanged, 23},
    {&TPS25751InterruptClear::clearUsbHostNoLongerPresent, 21},
    {&TPS25751InterruptClear::clearUsbHostPresent, 20},
    {&TPS25751InterruptClear::clearDataSwapRequested, 18},
    {&TPS25751InterruptClear::clearPowerSwapRequested, 17},
    {&TPS25751InterruptClear::clearSinkCapMessageReceived, 15},
    {&TPS25751InterruptClear::clearSourceCapMessageReceived, 14},
    {&TPS25751InterruptClear::clearNewContractAsProvider, 13},
    {&TPS25751InterruptClear::clearNewContractAsConsumer, 12},
    {&TPS25751InterruptClear::clearOvercurrent, 9},
    {&TPS25751InterruptClear::clearDataSwapComplete, 5},
    {&TPS25751InterruptClear::clearPowerSwapComplete, 4},
    {&TPS25751InterruptClear::clearPdHardreset, 1},
};

constexpr size_t kNumClearCases = sizeof(kClearCases) / sizeof(kClearCases[0]);

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void test_every_named_clear_sets_exactly_its_bit()
{
    TEST_ASSERT_EQUAL_size_t(38, kNumClearCases);
    for (size_t i = 0; i < kNumClearCases; ++i) {
        TPS25751InterruptClear reg;
        (reg.*kClearCases[i].fn)();
        for (size_t b = 0; b < INT_CLEAR1_SIZE; ++b) {
            const uint8_t expected = (b == kClearCases[i].bit / 8u)
                                         ? static_cast<uint8_t>(1u << (kClearCases[i].bit % 8u))
                                         : 0x00;
            char msg[48];
            snprintf(msg, sizeof(msg), "case %u bit %u byte %u", (unsigned)i,
                     (unsigned)kClearCases[i].bit, (unsigned)b);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, reg.raw()[b], msg);
        }
    }
}

void test_named_clears_together_match_clear_all()
{
    TPS25751InterruptClear viaEach;
    for (size_t i = 0; i < kNumClearCases; ++i) (viaEach.*kClearCases[i].fn)();
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_ALL_EXPECTED, viaEach.raw(), INT_CLEAR1_SIZE);
}

void test_generic_clear_preserves_staged_bits()
{
    TPS25751InterruptClear reg;
    reg.clear(1);
    reg.clear(82);
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[0]);
    TEST_ASSERT_EQUAL_UINT8(0x04, reg.raw()[10]);
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_empty_buffer()
{
    TPS25751InterruptClear reg;
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "=== Interrupt Clear Register (0x18) [R/W, write 1 to clear] ==="));
    TEST_ASSERT_TRUE(contains(out, "No clears staged (buffer is all-zero)"));
    TEST_ASSERT_FALSE(contains(out, "Staged clears:"));
    TEST_ASSERT_FALSE(contains(out, "[x]"));
}

void test_debug_print_names_each_staged_bit()
{
    TPS25751InterruptClear reg;
    reg.clearPdHardreset();
    reg.clearLiquidDetection();
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Staged clears:"));
    TEST_ASSERT_TRUE(contains(out, "  [x] Liquid Detection\n"));
    TEST_ASSERT_TRUE(contains(out, "  [x] PD Hard Reset\n"));
    TEST_ASSERT_FALSE(contains(out, "Overcurrent"));
    TEST_ASSERT_FALSE(contains(out, "No clears staged"));
}

void test_debug_print_clear_all_lists_every_named_source()
{
    TPS25751InterruptClear reg;
    reg.clearAll();
    const std::string out = captureDebugPrint(reg);

    size_t count = 0;
    for (size_t pos = out.find("  [x] "); pos != std::string::npos; pos = out.find("  [x] ", pos + 1)) ++count;
    TEST_ASSERT_EQUAL_size_t(38, count);
    TEST_ASSERT_TRUE(contains(out, "[x] I2C Controller NACKed"));
    TEST_ASSERT_TRUE(contains(out, "[x] Discover Mode Completed"));
    TEST_ASSERT_TRUE(contains(out, "[x] MIDB Received"));
    TEST_ASSERT_TRUE(contains(out, "[x] Plug Insert or Removal"));
}

void test_debug_print_invalid_register()
{
    TPS25751InterruptClear reg;
    TPS25751InterruptClear moved(static_cast<TPS25751InterruptClear &&>(reg));
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "[R/W, write 1 to clear] ==="));
    TEST_ASSERT_TRUE(contains(out, "  <invalid>"));
    TEST_ASSERT_FALSE(contains(out, "No clears staged"));
}

void test_semantic_validity_false_when_invalid()
{
    TPS25751InterruptClear reg;
    TPS25751InterruptClear moved(static_cast<TPS25751InterruptClear &&>(reg));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
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
    RUN_TEST(test_clear_plug_insert_or_removal_sets_expected_bit);
    RUN_TEST(test_clear_midb_received_and_discover_mode_completed_set_expected_bits);
    RUN_TEST(test_multiple_named_clears_accumulate_without_disturbing_each_other);

    RUN_TEST(test_clear_all_sets_every_named_bit);
    RUN_TEST(test_clear_all_is_idempotent);
    RUN_TEST(test_clear_all_preserves_prior_manual_clears);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    RUN_TEST(test_every_named_clear_sets_exactly_its_bit);
    RUN_TEST(test_named_clears_together_match_clear_all);
    RUN_TEST(test_generic_clear_preserves_staged_bits);
    RUN_TEST(test_debug_print_empty_buffer);
    RUN_TEST(test_debug_print_names_each_staged_bit);
    RUN_TEST(test_debug_print_clear_all_lists_every_named_source);
    RUN_TEST(test_debug_print_invalid_register);
    RUN_TEST(test_semantic_validity_false_when_invalid);

    return UNITY_END();
}
