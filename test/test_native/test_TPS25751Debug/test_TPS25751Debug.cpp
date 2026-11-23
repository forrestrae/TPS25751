/**
 * @file test_TPS25751Debug.cpp
 * @brief Native (host-run) unit tests for the TPS25751Debug logging utility.
 *
 * Run with: pio test -e native
 *
 * The native Arduino stub (test/support/native/Arduino.h) writes Stream output
 * to stdout and returns 0 from millis(), so every log line starts with "[0] ".
 * Tests redirect stdout to a temp file to capture it. Line format:
 *   "[<millis>] <LEVEL> <CAT>: <message>"
 * with LEVEL one of ERROR/WARN /INFO /DEBUG/TRACE and CAT one of
 * I2C/REG/VAL/PAR (anything else prints "???").
 *
 * Not covered: nothing in this file is timing- or hardware-dependent on the
 * host; millis() is a constant stub, so timestamp formatting is only checked
 * for the stubbed value 0.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "CaptureStdout.h"
#include "TPS25751Debug.h"

// Defined in TPS25751Debug.cpp but not declared in the public header.
namespace TPS25751Debug {
const char *getLevelString(uint8_t level);
const char *getCategoryString(uint8_t category);
}  // namespace TPS25751Debug

namespace {

bool contains(const std::string &s, const char *needle)
{
    return s.find(needle) != std::string::npos;
}

Stream testStream;

}  // namespace

void setUp()
{
    TPS25751Debug::setDebugStream(&testStream);
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_TRACE);
    TPS25751Debug::setDebugCategories(DEBUG_CAT_ALL);
}

void tearDown()
{
    TPS25751Debug::setDebugStream(&testStream);
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_NONE);
    TPS25751Debug::setDebugCategories(DEBUG_CAT_ALL);
}

// --- Configuration -----------------------------------------------------------

void test_level_and_category_constants()
{
    TEST_ASSERT_EQUAL(0, DEBUG_LEVEL_NONE);
    TEST_ASSERT_EQUAL(1, DEBUG_LEVEL_ERROR);
    TEST_ASSERT_EQUAL(2, DEBUG_LEVEL_WARN);
    TEST_ASSERT_EQUAL(3, DEBUG_LEVEL_INFO);
    TEST_ASSERT_EQUAL(4, DEBUG_LEVEL_DEBUG);
    TEST_ASSERT_EQUAL(5, DEBUG_LEVEL_TRACE);
    TEST_ASSERT_EQUAL_HEX8(0x01, DEBUG_CAT_I2C);
    TEST_ASSERT_EQUAL_HEX8(0x02, DEBUG_CAT_REGISTER);
    TEST_ASSERT_EQUAL_HEX8(0x04, DEBUG_CAT_VALIDATION);
    TEST_ASSERT_EQUAL_HEX8(0x08, DEBUG_CAT_PARSING);
    TEST_ASSERT_EQUAL_HEX8(0x10, DEBUG_CAT_TASK);
    TEST_ASSERT_EQUAL_HEX8(0xFF, DEBUG_CAT_ALL);
}

void test_set_debug_level_updates_current_level()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_WARN);
    TEST_ASSERT_EQUAL_UINT8(DEBUG_LEVEL_WARN, TPS25751Debug::currentDebugLevel);

    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_NONE);
    TEST_ASSERT_EQUAL_UINT8(DEBUG_LEVEL_NONE, TPS25751Debug::currentDebugLevel);
}

void test_set_debug_categories_replaces_mask()
{
    TPS25751Debug::setDebugCategories(DEBUG_CAT_I2C | DEBUG_CAT_PARSING);
    TEST_ASSERT_EQUAL_HEX8(0x09, TPS25751Debug::enabledCategories);
}

void test_enable_category_sets_only_that_bit()
{
    TPS25751Debug::setDebugCategories(DEBUG_CAT_NONE);

    TPS25751Debug::enableCategory(DEBUG_CAT_VALIDATION);
    TEST_ASSERT_EQUAL_HEX8(0x04, TPS25751Debug::enabledCategories);

    TPS25751Debug::enableCategory(DEBUG_CAT_I2C);
    TEST_ASSERT_EQUAL_HEX8(0x05, TPS25751Debug::enabledCategories);
}

void test_disable_category_clears_only_that_bit()
{
    TPS25751Debug::setDebugCategories(DEBUG_CAT_ALL);

    TPS25751Debug::disableCategory(DEBUG_CAT_REGISTER);
    TEST_ASSERT_EQUAL_HEX8(0xFD, TPS25751Debug::enabledCategories);

    TPS25751Debug::disableCategory(DEBUG_CAT_REGISTER);  // idempotent
    TEST_ASSERT_EQUAL_HEX8(0xFD, TPS25751Debug::enabledCategories);
}

void test_set_debug_stream_ignores_null()
{
    TPS25751Debug::setDebugStream(nullptr);

    TEST_ASSERT_EQUAL_PTR(&testStream, TPS25751Debug::debugStream);
}

void test_set_debug_stream_replaces_stream()
{
    Stream other;
    TPS25751Debug::setDebugStream(&other);

    TEST_ASSERT_EQUAL_PTR(&other, TPS25751Debug::debugStream);
}

// --- Level and category name tables -----------------------------------------

void test_level_strings()
{
    TEST_ASSERT_EQUAL_STRING("ERROR", TPS25751Debug::getLevelString(DEBUG_LEVEL_ERROR));
    TEST_ASSERT_EQUAL_STRING("WARN ", TPS25751Debug::getLevelString(DEBUG_LEVEL_WARN));
    TEST_ASSERT_EQUAL_STRING("INFO ", TPS25751Debug::getLevelString(DEBUG_LEVEL_INFO));
    TEST_ASSERT_EQUAL_STRING("DEBUG", TPS25751Debug::getLevelString(DEBUG_LEVEL_DEBUG));
    TEST_ASSERT_EQUAL_STRING("TRACE", TPS25751Debug::getLevelString(DEBUG_LEVEL_TRACE));
    TEST_ASSERT_EQUAL_STRING("?????", TPS25751Debug::getLevelString(DEBUG_LEVEL_NONE));
    TEST_ASSERT_EQUAL_STRING("?????", TPS25751Debug::getLevelString(99));
}

void test_category_strings()
{
    TEST_ASSERT_EQUAL_STRING("I2C", TPS25751Debug::getCategoryString(DEBUG_CAT_I2C));
    TEST_ASSERT_EQUAL_STRING("REG", TPS25751Debug::getCategoryString(DEBUG_CAT_REGISTER));
    TEST_ASSERT_EQUAL_STRING("VAL", TPS25751Debug::getCategoryString(DEBUG_CAT_VALIDATION));
    TEST_ASSERT_EQUAL_STRING("PAR", TPS25751Debug::getCategoryString(DEBUG_CAT_PARSING));
    // DEBUG_CAT_TASK has no name in the table, and a combined mask is not a single category.
    TEST_ASSERT_EQUAL_STRING("???", TPS25751Debug::getCategoryString(DEBUG_CAT_TASK));
    TEST_ASSERT_EQUAL_STRING("???", TPS25751Debug::getCategoryString(DEBUG_CAT_ALL));
}

// --- debugPrint --------------------------------------------------------------

void test_debug_print_formats_prefix_and_message()
{
    const std::string out = captureStdout([] {
        TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, "reg 0x%02X len %d", 0x1A, 5);
    });

    TEST_ASSERT_EQUAL_STRING("[0] ERROR I2C: reg 0x1A len 5\n", out.c_str());
}

void test_debug_print_prefix_for_each_level_and_category()
{
    const std::string out = captureStdout([] {
        TPS25751Debug::debugPrint(DEBUG_LEVEL_WARN, DEBUG_CAT_REGISTER, "a");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_INFO, DEBUG_CAT_VALIDATION, "b");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_DEBUG, DEBUG_CAT_PARSING, "c");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_TRACE, DEBUG_CAT_TASK, "d");
    });

    TEST_ASSERT_EQUAL_STRING("[0] WARN  REG: a\n"
                             "[0] INFO  VAL: b\n"
                             "[0] DEBUG PAR: c\n"
                             "[0] TRACE ???: d\n",
                             out.c_str());
}

void test_debug_print_suppressed_above_current_level()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_WARN);

    const std::string out = captureStdout([] {
        TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, "error shown");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_WARN, DEBUG_CAT_I2C, "warn shown");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_INFO, DEBUG_CAT_I2C, "info hidden");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_TRACE, DEBUG_CAT_I2C, "trace hidden");
    });

    TEST_ASSERT_TRUE(contains(out, "error shown"));
    TEST_ASSERT_TRUE(contains(out, "warn shown"));
    TEST_ASSERT_FALSE(contains(out, "hidden"));
}

void test_debug_print_silent_at_level_none()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_NONE);

    const std::string out = captureStdout(
        [] { TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, "nope"); });

    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_debug_print_suppressed_for_disabled_category()
{
    TPS25751Debug::disableCategory(DEBUG_CAT_PARSING);

    const std::string out = captureStdout([] {
        TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_PARSING, "parsing hidden");
        TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, "i2c shown");
    });

    TEST_ASSERT_FALSE(contains(out, "parsing hidden"));
    TEST_ASSERT_TRUE(contains(out, "i2c shown"));
}

void test_debug_print_category_none_message_is_never_shown()
{
    // category 0 has no bit in the mask, so it can never pass the filter.
    const std::string out = captureStdout(
        [] { TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_NONE, "never"); });

    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_debug_print_silent_when_stream_is_null()
{
    TPS25751Debug::debugStream = nullptr;  // setDebugStream(nullptr) is ignored, so set directly

    const std::string out = captureStdout(
        [] { TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, "no stream"); });

    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_debug_print_truncates_long_messages_to_255_characters()
{
    const std::string longText(400, 'x');

    const std::string out = captureStdout([&] {
        TPS25751Debug::debugPrint(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C, "%s", longText.c_str());
    });

    const std::string prefix = "[0] ERROR I2C: ";
    TEST_ASSERT_EQUAL_size_t(prefix.size() + 255 + 1, out.size());  // + trailing newline
    TEST_ASSERT_EQUAL_STRING(std::string(255, 'x').c_str(), out.substr(prefix.size(), 255).c_str());
}

// --- debugPrintF -------------------------------------------------------------

void test_debug_print_f_formats_flash_string()
{
    const std::string out = captureStdout([] {
        TPS25751Debug::debugPrintF(DEBUG_LEVEL_INFO, DEBUG_CAT_REGISTER, F("value=%u name=%s"), 42u, "abc");
    });

    TEST_ASSERT_EQUAL_STRING("[0] INFO  REG: value=42 name=abc\n", out.c_str());
}

void test_debug_print_f_respects_level_category_and_stream_filters()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_ERROR);
    std::string out = captureStdout([] {
        TPS25751Debug::debugPrintF(DEBUG_LEVEL_INFO, DEBUG_CAT_REGISTER, F("level hidden"));
    });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_TRACE);
    TPS25751Debug::disableCategory(DEBUG_CAT_REGISTER);
    out = captureStdout([] {
        TPS25751Debug::debugPrintF(DEBUG_LEVEL_ERROR, DEBUG_CAT_REGISTER, F("category hidden"));
    });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::enableCategory(DEBUG_CAT_REGISTER);
    TPS25751Debug::debugStream = nullptr;
    out = captureStdout([] {
        TPS25751Debug::debugPrintF(DEBUG_LEVEL_ERROR, DEBUG_CAT_REGISTER, F("stream hidden"));
    });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_debug_print_f_truncates_format_to_127_characters()
{
    const std::string longFormat(200, 'y');

    const std::string out = captureStdout([&] {
        TPS25751Debug::debugPrintF(DEBUG_LEVEL_ERROR, DEBUG_CAT_I2C,
                                   reinterpret_cast<const __FlashStringHelper *>(longFormat.c_str()));
    });

    const std::string prefix = "[0] ERROR I2C: ";
    TEST_ASSERT_EQUAL_size_t(prefix.size() + 127 + 1, out.size());
}

// --- Convenience macros ------------------------------------------------------

void test_debug_macros_route_to_matching_level()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_TRACE);

    const std::string out = captureStdout([] {
        TPS_DEBUG_ERROR(DEBUG_CAT_I2C, "e%d", 1);
        TPS_DEBUG_WARN(DEBUG_CAT_I2C, "w%d", 2);
        TPS_DEBUG_INFO(DEBUG_CAT_I2C, "i%d", 3);
        TPS_DEBUG(DEBUG_CAT_I2C, "d%d", 4);
        TPS_DEBUG_TRACE(DEBUG_CAT_I2C, "t%d", 5);
        TPS_DEBUG_ERROR_F(DEBUG_CAT_I2C, F("fe"));
        TPS_DEBUG_WARN_F(DEBUG_CAT_I2C, F("fw"));
        TPS_DEBUG_INFO_F(DEBUG_CAT_I2C, F("fi"));
        TPS_DEBUG_F(DEBUG_CAT_I2C, F("fd"));
        TPS_DEBUG_TRACE_F(DEBUG_CAT_I2C, F("ft"));
    });

    TEST_ASSERT_EQUAL_STRING("[0] ERROR I2C: e1\n"
                             "[0] WARN  I2C: w2\n"
                             "[0] INFO  I2C: i3\n"
                             "[0] DEBUG I2C: d4\n"
                             "[0] TRACE I2C: t5\n"
                             "[0] ERROR I2C: fe\n"
                             "[0] WARN  I2C: fw\n"
                             "[0] INFO  I2C: fi\n"
                             "[0] DEBUG I2C: fd\n"
                             "[0] TRACE I2C: ft\n",
                             out.c_str());
}

void test_debug_macros_filtered_by_runtime_level()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_INFO);

    const std::string out = captureStdout([] {
        TPS_DEBUG_INFO(DEBUG_CAT_I2C, "kept");
        TPS_DEBUG(DEBUG_CAT_I2C, "dropped");
    });

    TEST_ASSERT_EQUAL_STRING("[0] INFO  I2C: kept\n", out.c_str());
}

void test_hexdump_macro_requires_debug_level_and_category()
{
    const uint8_t data[2] = {0xAB, 0xCD};

    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_INFO);
    std::string out = captureStdout([&] { TPS_DEBUG_HEXDUMP(DEBUG_CAT_I2C, data, 2, "p"); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_DEBUG);
    TPS25751Debug::disableCategory(DEBUG_CAT_I2C);
    out = captureStdout([&] { TPS_DEBUG_HEXDUMP(DEBUG_CAT_I2C, data, 2, "p"); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::enableCategory(DEBUG_CAT_I2C);
    out = captureStdout([&] { TPS_DEBUG_HEXDUMP(DEBUG_CAT_I2C, data, 2, "p"); });
    TEST_ASSERT_TRUE(contains(out, "p: 0x00: AB CD "));
}

// --- hexDump -----------------------------------------------------------------

void test_hex_dump_single_line_with_prefix_and_ascii()
{
    const uint8_t data[4] = {'T', 'P', 'S', 0x00};

    const std::string out = captureStdout([&] { TPS25751Debug::hexDump(testStream, data, 4, "Regs"); });

    // 4 bytes, 12 missing bytes padded with 3 spaces each, then " |ASCII|".
    const std::string expected =
        "Regs: 0x00: 54 50 53 00 " + std::string(12 * 3, ' ') + " |TPS.|\n";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), out.c_str());
}

void test_hex_dump_without_prefix_omits_label()
{
    const uint8_t data[1] = {0x0A};

    const std::string out = captureStdout([&] { TPS25751Debug::hexDump(testStream, data, 1); });

    TEST_ASSERT_TRUE(out.rfind("0x00: 0A ", 0) == 0);
    TEST_ASSERT_TRUE(contains(out, " |.|\n"));
}

void test_hex_dump_wraps_at_16_bytes_with_offsets()
{
    uint8_t data[20];
    for (int i = 0; i < 20; i++) data[i] = static_cast<uint8_t>(0x40 + i);  // '@'..'S'

    const std::string out = captureStdout([&] { TPS25751Debug::hexDump(testStream, data, 20); });

    const std::string line1 = "0x00: 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F  |@ABCDEFGHIJKLMNO|\n";
    const std::string line2 = "0x10: 50 51 52 53 " + std::string(12 * 3, ' ') + " |PQRS|\n";
    TEST_ASSERT_EQUAL_STRING((line1 + line2).c_str(), out.c_str());
}

void test_hex_dump_exact_multiple_of_16_has_no_padding()
{
    uint8_t data[16];
    for (int i = 0; i < 16; i++) data[i] = static_cast<uint8_t>(i);

    const std::string out = captureStdout([&] { TPS25751Debug::hexDump(testStream, data, 16); });

    TEST_ASSERT_TRUE(contains(out, "0x00: 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F  |"));
    // Exactly one line.
    TEST_ASSERT_EQUAL_size_t(out.find('\n') + 1, out.size());
}

void test_hex_dump_non_printable_bytes_show_as_dots()
{
    const uint8_t data[4] = {0x1F, 0x20, 0x7E, 0x7F};

    const std::string out = captureStdout([&] { TPS25751Debug::hexDump(testStream, data, 4); });

    TEST_ASSERT_TRUE(contains(out, "|. ~.|"));
}

void test_hex_dump_ignores_null_data_and_zero_length()
{
    const uint8_t data[1] = {0x01};

    std::string out = captureStdout([&] { TPS25751Debug::hexDump(testStream, nullptr, 4, "x"); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    out = captureStdout([&] { TPS25751Debug::hexDump(testStream, data, 0, "x"); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

// --- hexDumpLine -------------------------------------------------------------

void test_hex_dump_line_space_separated_uppercase_hex()
{
    const uint8_t data[4] = {0x00, 0x0A, 0xFF, 0x10};

    const std::string out = captureStdout([&] { TPS25751Debug::hexDumpLine(testStream, data, 4); });

    TEST_ASSERT_EQUAL_STRING("00 0A FF 10", out.c_str());  // no trailing space or newline
}

void test_hex_dump_line_ignores_null_data_and_zero_length()
{
    const uint8_t data[1] = {0x01};

    std::string out = captureStdout([&] { TPS25751Debug::hexDumpLine(testStream, nullptr, 3); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    out = captureStdout([&] { TPS25751Debug::hexDumpLine(testStream, data, 0); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

// --- reportI2CError ----------------------------------------------------------

void test_report_i2c_error_formats_all_fields()
{
    const std::string out = captureStdout(
        [] { TPS25751Debug::reportI2CError(0x20, 0x1A, "read", 4); });

    TEST_ASSERT_EQUAL_STRING("[0] ERROR I2C: I2C read failed: addr=0x20, reg=0x1A, error=4\n", out.c_str());
}

void test_report_i2c_error_macro_matches_function()
{
    const std::string out = captureStdout([] { TPS_REPORT_I2C_ERROR(0x21, 0x03, "write", -1); });

    TEST_ASSERT_TRUE(contains(out, "I2C write failed: addr=0x21, reg=0x03, error=-1"));
}

void test_report_i2c_error_suppressed_when_silent_or_category_disabled()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_NONE);
    std::string out = captureStdout([] { TPS25751Debug::reportI2CError(1, 2, "read", 3); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_ERROR);
    TPS25751Debug::disableCategory(DEBUG_CAT_I2C);
    out = captureStdout([] { TPS25751Debug::reportI2CError(1, 2, "read", 3); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

// --- reportValidationError ---------------------------------------------------

void test_report_validation_error_with_details()
{
    const std::string out = captureStdout(
        [] { TPS25751Debug::reportValidationError("Mode", "size validation", "register size mismatch"); });

    TEST_ASSERT_EQUAL_STRING(
        "[0] WARN  VAL: Mode validation failed: size validation (register size mismatch)\n", out.c_str());
}

void test_report_validation_error_without_details()
{
    const std::string out = captureStdout(
        [] { TPS25751Debug::reportValidationError("Status", "reserved bits"); });

    TEST_ASSERT_EQUAL_STRING("[0] WARN  VAL: Status validation failed: reserved bits\n", out.c_str());
}

void test_report_validation_error_macro_matches_function()
{
    const std::string out = captureStdout([] { TPS_REPORT_VALIDATION_ERROR("Cmd", "ascii", "bad byte"); });

    TEST_ASSERT_TRUE(contains(out, "Cmd validation failed: ascii (bad byte)"));
}

void test_report_validation_error_needs_warn_level_and_validation_category()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_ERROR);  // below WARN
    std::string out = captureStdout([] { TPS25751Debug::reportValidationError("A", "b", "c"); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_WARN);
    TPS25751Debug::disableCategory(DEBUG_CAT_VALIDATION);
    out = captureStdout([] { TPS25751Debug::reportValidationError("A", "b", "c"); });
    TEST_ASSERT_EQUAL_STRING("", out.c_str());

    TPS25751Debug::enableCategory(DEBUG_CAT_VALIDATION);
    out = captureStdout([] { TPS25751Debug::reportValidationError("A", "b"); });
    TEST_ASSERT_EQUAL_STRING("[0] WARN  VAL: A validation failed: b\n", out.c_str());
}

// --- Runner ------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_level_and_category_constants);
    RUN_TEST(test_set_debug_level_updates_current_level);
    RUN_TEST(test_set_debug_categories_replaces_mask);
    RUN_TEST(test_enable_category_sets_only_that_bit);
    RUN_TEST(test_disable_category_clears_only_that_bit);
    RUN_TEST(test_set_debug_stream_ignores_null);
    RUN_TEST(test_set_debug_stream_replaces_stream);

    RUN_TEST(test_level_strings);
    RUN_TEST(test_category_strings);

    RUN_TEST(test_debug_print_formats_prefix_and_message);
    RUN_TEST(test_debug_print_prefix_for_each_level_and_category);
    RUN_TEST(test_debug_print_suppressed_above_current_level);
    RUN_TEST(test_debug_print_silent_at_level_none);
    RUN_TEST(test_debug_print_suppressed_for_disabled_category);
    RUN_TEST(test_debug_print_category_none_message_is_never_shown);
    RUN_TEST(test_debug_print_silent_when_stream_is_null);
    RUN_TEST(test_debug_print_truncates_long_messages_to_255_characters);

    RUN_TEST(test_debug_print_f_formats_flash_string);
    RUN_TEST(test_debug_print_f_respects_level_category_and_stream_filters);
    RUN_TEST(test_debug_print_f_truncates_format_to_127_characters);

    RUN_TEST(test_debug_macros_route_to_matching_level);
    RUN_TEST(test_debug_macros_filtered_by_runtime_level);
    RUN_TEST(test_hexdump_macro_requires_debug_level_and_category);

    RUN_TEST(test_hex_dump_single_line_with_prefix_and_ascii);
    RUN_TEST(test_hex_dump_without_prefix_omits_label);
    RUN_TEST(test_hex_dump_wraps_at_16_bytes_with_offsets);
    RUN_TEST(test_hex_dump_exact_multiple_of_16_has_no_padding);
    RUN_TEST(test_hex_dump_non_printable_bytes_show_as_dots);
    RUN_TEST(test_hex_dump_ignores_null_data_and_zero_length);

    RUN_TEST(test_hex_dump_line_space_separated_uppercase_hex);
    RUN_TEST(test_hex_dump_line_ignores_null_data_and_zero_length);

    RUN_TEST(test_report_i2c_error_formats_all_fields);
    RUN_TEST(test_report_i2c_error_macro_matches_function);
    RUN_TEST(test_report_i2c_error_suppressed_when_silent_or_category_disabled);

    RUN_TEST(test_report_validation_error_with_details);
    RUN_TEST(test_report_validation_error_without_details);
    RUN_TEST(test_report_validation_error_macro_matches_function);
    RUN_TEST(test_report_validation_error_needs_warn_level_and_validation_category);

    return UNITY_END();
}
