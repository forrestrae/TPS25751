/**
 * @file test_TPS25751Command.cpp
 * @brief Native (host-run) unit tests for TPS25751Command.
 *
 * Run with: pio test -e native
 *
 * COMMAND (0x08) holds a 4-character ASCII task code, first character in the
 * lowest-addressed byte (TPS25751FourCC::of("I2Cr") is {'I','2','C','r'}).
 * The controller clears it to all-zero on success and writes "!CMD" when the
 * code is not recognized (TRM Sec 4.4.2, see AGENTS.md "4CC Command-Task
 * Interface").
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "TPS25751Command.h"
#include "TPS25751Task.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t COMMAND_SIZE = TPS25751Registers::Registers::COMMAND.size;  // 4

constexpr uint8_t CLEAR_DATA[COMMAND_SIZE] = {0, 0, 0, 0};
constexpr uint8_t REJECTED_DATA[COMMAND_SIZE] = {'!', 'C', 'M', 'D'};
constexpr uint8_t I2CR_DATA[COMMAND_SIZE] = {'I', '2', 'C', 'r'};
constexpr uint8_t ALL_ONES[COMMAND_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};

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
    TPS25751Command cmd;

    TEST_ASSERT_TRUE(cmd.isValid());
    TEST_ASSERT_EQUAL_size_t(COMMAND_SIZE, cmd.size());
    TEST_ASSERT_TRUE(cmd.isValidSize(COMMAND_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CLEAR_DATA, cmd.raw(), COMMAND_SIZE);
    TEST_ASSERT_TRUE(cmd.isClear());
}

void test_data_constructor_copies_buffer()
{
    TPS25751Command cmd(I2CR_DATA);

    TEST_ASSERT_NOT_EQUAL(I2CR_DATA, cmd.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(I2CR_DATA, cmd.raw(), COMMAND_SIZE);
}

void test_explicit_length_constructor()
{
    const uint8_t shortData[2] = {'I', '2'};
    TPS25751Command cmd(shortData, sizeof(shortData));

    TEST_ASSERT_EQUAL_size_t(2, cmd.size());
    TEST_ASSERT_FALSE(cmd.isValidSize(COMMAND_SIZE));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751Command cmd(TPS25751Registers::Registers::COMMAND);

    TEST_ASSERT_TRUE(cmd.isValid());
    TEST_ASSERT_EQUAL_size_t(COMMAND_SIZE, cmd.size());
    TEST_ASSERT_TRUE(cmd.isClear());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751Command cmd(nullptr);

    TEST_ASSERT_TRUE(cmd.isValid());
    TEST_ASSERT_TRUE(cmd.isClear());
}

// --- 4CC decode --------------------------------------------------------------

void test_four_cc_returns_ascii_code_in_byte_order()
{
    TPS25751Command cmd(I2CR_DATA);

    TEST_ASSERT_EQUAL_STRING("I2Cr", cmd.fourCC());
}

void test_four_cc_matches_task_header_encoding()
{
    constexpr TPS25751FourCC code = TPS25751FourCC::of("GAID");
    TPS25751Command cmd(reinterpret_cast<const uint8_t *>(code.code));

    TEST_ASSERT_EQUAL_STRING("GAID", cmd.fourCC());
}

void test_four_cc_of_rejected_matches_constant()
{
    TPS25751Command cmd(reinterpret_cast<const uint8_t *>(TPS25751_CMD_REJECTED.code));

    TEST_ASSERT_EQUAL_STRING("!CMD", cmd.fourCC());
    TEST_ASSERT_TRUE(cmd.isRejected());
}

void test_clear_constant_matches_clear_state()
{
    TPS25751Command cmd(reinterpret_cast<const uint8_t *>(TPS25751_CMD_CLEAR.code));

    TEST_ASSERT_TRUE(cmd.isClear());
    TEST_ASSERT_EQUAL_STRING("", cmd.fourCC());  // leading NUL ends the string
}

void test_four_cc_empty_when_invalid()
{
    TPS25751Command original(I2CR_DATA);
    TPS25751Command moved(static_cast<TPS25751Command &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_STRING("", original.fourCC());
}

// --- State predicates --------------------------------------------------------

void test_is_clear_only_for_all_zero()
{
    TEST_ASSERT_TRUE(TPS25751Command(CLEAR_DATA).isClear());
    TEST_ASSERT_FALSE(TPS25751Command(I2CR_DATA).isClear());
    TEST_ASSERT_FALSE(TPS25751Command(REJECTED_DATA).isClear());

    // A single non-zero byte anywhere makes it not clear.
    const uint8_t lastByte[COMMAND_SIZE] = {0, 0, 0, 1};
    TEST_ASSERT_FALSE(TPS25751Command(lastByte).isClear());
}

void test_is_rejected_only_for_exact_bang_cmd()
{
    TEST_ASSERT_TRUE(TPS25751Command(REJECTED_DATA).isRejected());
    TEST_ASSERT_FALSE(TPS25751Command(CLEAR_DATA).isRejected());
    TEST_ASSERT_FALSE(TPS25751Command(I2CR_DATA).isRejected());

    const uint8_t nearMiss[COMMAND_SIZE] = {'!', 'c', 'm', 'd'};
    TEST_ASSERT_FALSE(TPS25751Command(nearMiss).isRejected());
}

void test_is_rejected_false_for_wrong_length()
{
    TPS25751Command shortCmd(REJECTED_DATA, 3);

    TEST_ASSERT_FALSE(shortCmd.isRejected());
}

void test_predicates_false_when_invalid()
{
    TPS25751Command original(REJECTED_DATA);
    TPS25751Command moved(static_cast<TPS25751Command &&>(original));

    TEST_ASSERT_FALSE(original.isClear());
    TEST_ASSERT_FALSE(original.isRejected());
}

// --- Validation --------------------------------------------------------------

void test_all_ones_is_invalid_data()
{
    TPS25751Command cmd(ALL_ONES);

    TEST_ASSERT_FALSE(cmd.hasValidData());
    TEST_ASSERT_FALSE(cmd.isSemanticallyValid());  // 0xFF is not printable ASCII
}

void test_semantic_valid_states()
{
    TEST_ASSERT_TRUE(TPS25751Command(CLEAR_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Command(REJECTED_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Command(I2CR_DATA).isSemanticallyValid());
}

void test_semantic_valid_at_printable_boundaries()
{
    const uint8_t edges[COMMAND_SIZE] = {0x20, 0x7E, 0x20, 0x7E};
    TEST_ASSERT_TRUE(TPS25751Command(edges).isSemanticallyValid());
}

void test_semantic_invalid_for_non_printable_byte()
{
    const uint8_t control[COMMAND_SIZE] = {'I', '2', 0x07, 'r'};
    const uint8_t del[COMMAND_SIZE] = {'I', '2', 'C', 0x7F};
    const uint8_t partialZero[COMMAND_SIZE] = {'I', 0, 'C', 'r'};

    TEST_ASSERT_FALSE(TPS25751Command(control).isSemanticallyValid());
    TEST_ASSERT_FALSE(TPS25751Command(del).isSemanticallyValid());
    TEST_ASSERT_FALSE(TPS25751Command(partialZero).isSemanticallyValid());
}

void test_semantic_invalid_for_wrong_size()
{
    const uint8_t shortData[2] = {'I', '2'};
    TEST_ASSERT_FALSE(TPS25751Command(shortData, sizeof(shortData)).isSemanticallyValid());
}

void test_semantic_invalid_when_register_invalid()
{
    TPS25751Command original(I2CR_DATA);
    TPS25751Command moved(static_cast<TPS25751Command &&>(original));

    TEST_ASSERT_FALSE(original.isSemanticallyValid());
}

// --- debugPrint --------------------------------------------------------------

void test_debug_print_clear()
{
    const std::string out = captureDebugPrint(TPS25751Command(CLEAR_DATA));

    TEST_ASSERT_TRUE(contains(out, "=== Command Register (0x08) ==="));
    TEST_ASSERT_TRUE(contains(out, "State: Clear (no command pending / last command succeeded)"));
}

void test_debug_print_rejected()
{
    const std::string out = captureDebugPrint(TPS25751Command(REJECTED_DATA));

    TEST_ASSERT_TRUE(contains(out, "Raw 4CC: !CMD"));
    TEST_ASSERT_TRUE(contains(out, "State: Rejected (\"!CMD\" - unrecognized task code)"));
}

void test_debug_print_pending()
{
    const std::string out = captureDebugPrint(TPS25751Command(I2CR_DATA));

    TEST_ASSERT_TRUE(contains(out, "Raw 4CC: I2Cr"));
    TEST_ASSERT_TRUE(contains(out, "State: Pending / unrecognized echo"));
}

void test_debug_print_invalid()
{
    TPS25751Command original(I2CR_DATA);
    TPS25751Command moved(static_cast<TPS25751Command &&>(original));

    const std::string out = captureDebugPrint(original);

    TEST_ASSERT_TRUE(contains(out, "=== Command Register (0x08) ==="));
    TEST_ASSERT_TRUE(contains(out, "<invalid>"));
    TEST_ASSERT_FALSE(contains(out, "State:"));
}

// --- Rule of Five ------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751Command original(I2CR_DATA);
    TPS25751Command copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(I2CR_DATA, copy.raw(), COMMAND_SIZE);
    TEST_ASSERT_EQUAL_STRING("I2Cr", copy.fourCC());
}

void test_copy_assignment_deep_copies()
{
    TPS25751Command original(REJECTED_DATA);
    TPS25751Command target;
    target = original;

    TEST_ASSERT_NOT_EQUAL(original.raw(), target.raw());
    TEST_ASSERT_TRUE(target.isRejected());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751Command original(I2CR_DATA);
    const uint8_t *buf = original.raw();

    TPS25751Command moved(static_cast<TPS25751Command &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
}

void test_move_assignment_transfers_ownership()
{
    TPS25751Command original(REJECTED_DATA);
    const uint8_t *buf = original.raw();
    TPS25751Command target;

    target = static_cast<TPS25751Command &&>(original);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_TRUE(target.isRejected());
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

    RUN_TEST(test_four_cc_returns_ascii_code_in_byte_order);
    RUN_TEST(test_four_cc_matches_task_header_encoding);
    RUN_TEST(test_four_cc_of_rejected_matches_constant);
    RUN_TEST(test_clear_constant_matches_clear_state);
    RUN_TEST(test_four_cc_empty_when_invalid);

    RUN_TEST(test_is_clear_only_for_all_zero);
    RUN_TEST(test_is_rejected_only_for_exact_bang_cmd);
    RUN_TEST(test_is_rejected_false_for_wrong_length);
    RUN_TEST(test_predicates_false_when_invalid);

    RUN_TEST(test_all_ones_is_invalid_data);
    RUN_TEST(test_semantic_valid_states);
    RUN_TEST(test_semantic_valid_at_printable_boundaries);
    RUN_TEST(test_semantic_invalid_for_non_printable_byte);
    RUN_TEST(test_semantic_invalid_for_wrong_size);
    RUN_TEST(test_semantic_invalid_when_register_invalid);

    RUN_TEST(test_debug_print_clear);
    RUN_TEST(test_debug_print_rejected);
    RUN_TEST(test_debug_print_pending);
    RUN_TEST(test_debug_print_invalid);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
