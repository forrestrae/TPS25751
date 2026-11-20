/**
 * @file test_TPS25751Mode.cpp
 * @brief Native (host-run) unit tests for TPS25751Mode.
 *
 * Run with: pio test -e native
 *
 * MODE (0x03) is a 4-byte ASCII string, first character in the lowest byte:
 * "APP " (application), "BOOT" (boot), "PTCH" (patch); any other value means
 * the controller runs with limited functionality (TRM SLVUCR8B, MODE).
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751Mode.h"
#include "TPS25751Debug.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t MODE_SIZE = TPS25751Registers::Registers::MODE.size;  // 4

constexpr uint8_t APP_DATA[MODE_SIZE] = {'A', 'P', 'P', ' '};
constexpr uint8_t BOOT_DATA[MODE_SIZE] = {'B', 'O', 'O', 'T'};
constexpr uint8_t PTCH_DATA[MODE_SIZE] = {'P', 'T', 'C', 'H'};
constexpr uint8_t OTHER_DATA[MODE_SIZE] = {'F', 'W', 'U', 'P'};
constexpr uint8_t ALL_ZEROS[MODE_SIZE] = {0, 0, 0, 0};
constexpr uint8_t ALL_ONES[MODE_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint8_t NON_PRINTABLE[MODE_SIZE] = {'A', 'P', 0x07, ' '};
constexpr uint8_t HIGH_BYTE[MODE_SIZE] = {'A', 'P', 'P', 0x7F};

// Runs fn with stdout redirected to a temp file and returns what it wrote.
template <typename Fn>
std::string captureStdout(Fn fn)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    const int saved = dup(fileno(stdout));
    dup2(fileno(tmp), fileno(stdout));

    fn();

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

std::string captureDebugPrint(const TPS25751Mode &reg)
{
    return captureStdout([&] { reg.debugPrint(); });
}

bool contains(const std::string &s, const char *needle)
{
    return s.find(needle) != std::string::npos;
}

}  // namespace

void setUp() { TPS25751Debug::setDebugLevel(DEBUG_LEVEL_NONE); }
void tearDown() { TPS25751Debug::setDebugLevel(DEBUG_LEVEL_NONE); }

// --- Construction ------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751Mode mode;

    TEST_ASSERT_TRUE(mode.isValid());
    TEST_ASSERT_EQUAL_size_t(MODE_SIZE, mode.size());
    TEST_ASSERT_TRUE(mode.isValidSize(MODE_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_ZEROS, mode.raw(), MODE_SIZE);
}

void test_data_constructor_copies_buffer()
{
    TPS25751Mode mode(APP_DATA);

    TEST_ASSERT_NOT_EQUAL(APP_DATA, mode.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(APP_DATA, mode.raw(), MODE_SIZE);
}

void test_explicit_length_constructor()
{
    const uint8_t shortData[2] = {'A', 'P'};
    TPS25751Mode mode(shortData, sizeof(shortData));

    TEST_ASSERT_EQUAL_size_t(2, mode.size());
    TEST_ASSERT_FALSE(mode.isValidSize(MODE_SIZE));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751Mode mode(TPS25751Registers::Registers::MODE);

    TEST_ASSERT_TRUE(mode.isValid());
    TEST_ASSERT_EQUAL_size_t(MODE_SIZE, mode.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_ZEROS, mode.raw(), MODE_SIZE);
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751Mode mode(nullptr);

    TEST_ASSERT_TRUE(mode.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ALL_ZEROS, mode.raw(), MODE_SIZE);
    TEST_ASSERT_EQUAL(ModeType::Unknown, mode.getModeType());
}

// --- Mode decode -------------------------------------------------------------

void test_mode_string_returns_four_characters()
{
    TPS25751Mode app(APP_DATA);
    TEST_ASSERT_EQUAL_STRING("APP ", app.modeString());
    TEST_ASSERT_EQUAL_size_t(4, strlen(app.modeString()));

    TPS25751Mode boot(BOOT_DATA);
    TEST_ASSERT_EQUAL_STRING("BOOT", boot.modeString());
}

void test_mode_string_empty_when_invalid()
{
    TPS25751Mode original(APP_DATA);
    TPS25751Mode moved(static_cast<TPS25751Mode &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_STRING("", original.modeString());
}

void test_application_mode()
{
    TPS25751Mode mode(APP_DATA);

    TEST_ASSERT_TRUE(mode.isApplicationMode());
    TEST_ASSERT_FALSE(mode.isBootMode());
    TEST_ASSERT_FALSE(mode.isPatchMode());
    TEST_ASSERT_EQUAL(ModeType::Application, mode.getModeType());
}

void test_boot_mode()
{
    TPS25751Mode mode(BOOT_DATA);

    TEST_ASSERT_FALSE(mode.isApplicationMode());
    TEST_ASSERT_TRUE(mode.isBootMode());
    TEST_ASSERT_FALSE(mode.isPatchMode());
    TEST_ASSERT_EQUAL(ModeType::Boot, mode.getModeType());
}

void test_patch_mode()
{
    TPS25751Mode mode(PTCH_DATA);

    TEST_ASSERT_FALSE(mode.isApplicationMode());
    TEST_ASSERT_FALSE(mode.isBootMode());
    TEST_ASSERT_TRUE(mode.isPatchMode());
    TEST_ASSERT_EQUAL(ModeType::Patch, mode.getModeType());
}

void test_unrecognized_mode_is_unknown()
{
    TPS25751Mode mode(OTHER_DATA);

    TEST_ASSERT_FALSE(mode.isApplicationMode());
    TEST_ASSERT_FALSE(mode.isBootMode());
    TEST_ASSERT_FALSE(mode.isPatchMode());
    TEST_ASSERT_EQUAL(ModeType::Unknown, mode.getModeType());
}

void test_mode_predicates_false_when_invalid()
{
    TPS25751Mode original(APP_DATA);
    TPS25751Mode moved(static_cast<TPS25751Mode &&>(original));

    TEST_ASSERT_FALSE(original.isApplicationMode());
    TEST_ASSERT_FALSE(original.isBootMode());
    TEST_ASSERT_FALSE(original.isPatchMode());
    TEST_ASSERT_EQUAL(ModeType::Unknown, original.getModeType());
}

void test_mode_matching_is_case_sensitive_and_exact()
{
    const uint8_t lower[MODE_SIZE] = {'a', 'p', 'p', ' '};
    const uint8_t noSpace[MODE_SIZE] = {'A', 'P', 'P', 'X'};

    TEST_ASSERT_EQUAL(ModeType::Unknown, TPS25751Mode(lower).getModeType());
    TEST_ASSERT_EQUAL(ModeType::Unknown, TPS25751Mode(noSpace).getModeType());
}

// --- toString ----------------------------------------------------------------

void test_to_string_covers_all_enum_values()
{
    TEST_ASSERT_EQUAL_STRING("Application", toString(ModeType::Application));
    TEST_ASSERT_EQUAL_STRING("Boot", toString(ModeType::Boot));
    TEST_ASSERT_EQUAL_STRING("Patch", toString(ModeType::Patch));
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(ModeType::Unknown));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<ModeType>(9)));
}

// --- Validation --------------------------------------------------------------

void test_basic_and_data_validation()
{
    TPS25751Mode mode(APP_DATA);
    TEST_ASSERT_TRUE(mode.isValid());
    TEST_ASSERT_TRUE(mode.hasValidData());

    TPS25751Mode ones(ALL_ONES);
    TEST_ASSERT_TRUE(ones.isValid());
    TEST_ASSERT_FALSE(ones.hasValidData());
}

void test_semantic_valid_for_ascii_modes()
{
    TEST_ASSERT_TRUE(TPS25751Mode(APP_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Mode(BOOT_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Mode(PTCH_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Mode(OTHER_DATA).isSemanticallyValid());
}

void test_semantic_invalid_for_all_ones()
{
    TEST_ASSERT_FALSE(TPS25751Mode(ALL_ONES).isSemanticallyValid());
}

void test_semantic_invalid_for_all_zeros_non_printable()
{
    // NUL is not printable ASCII (0x20..0x7E).
    TEST_ASSERT_FALSE(TPS25751Mode(ALL_ZEROS).isSemanticallyValid());
}

void test_semantic_invalid_for_wrong_size()
{
    const uint8_t shortData[2] = {'A', 'P'};
    TEST_ASSERT_FALSE(TPS25751Mode(shortData, sizeof(shortData)).isSemanticallyValid());
}

void test_semantic_invalid_for_non_printable_character()
{
    TEST_ASSERT_FALSE(TPS25751Mode(NON_PRINTABLE).isSemanticallyValid());
    TEST_ASSERT_FALSE(TPS25751Mode(HIGH_BYTE).isSemanticallyValid());  // 0x7F is DEL
}

void test_semantic_valid_at_ascii_boundaries()
{
    const uint8_t edges[MODE_SIZE] = {0x20, 0x7E, 0x20, 0x7E};
    TEST_ASSERT_TRUE(TPS25751Mode(edges).isSemanticallyValid());
}

void test_semantic_invalid_when_register_invalid()
{
    TPS25751Mode original(APP_DATA);
    TPS25751Mode moved(static_cast<TPS25751Mode &&>(original));

    TEST_ASSERT_FALSE(original.isSemanticallyValid());
}

void test_semantic_validation_logs_failures_when_debug_enabled()
{
    TPS25751Debug::setDebugLevel(DEBUG_LEVEL_TRACE);

    bool okApp = false, badOnes = true, badAscii = true, badSize = true;
    const std::string out = captureStdout([&] {
        okApp = TPS25751Mode(APP_DATA).isSemanticallyValid();
        badOnes = TPS25751Mode(ALL_ONES).isSemanticallyValid();
        badAscii = TPS25751Mode(NON_PRINTABLE).isSemanticallyValid();
        badSize = TPS25751Mode(APP_DATA, 2).isSemanticallyValid();
    });

    TEST_ASSERT_TRUE(okApp);
    TEST_ASSERT_FALSE(badOnes);
    TEST_ASSERT_FALSE(badAscii);
    TEST_ASSERT_FALSE(badSize);
    TEST_ASSERT_TRUE(contains(out, "Mode validation: non-ASCII character at byte 2: 0x07"));
    TEST_ASSERT_TRUE(contains(out, "Mode validation failed: ASCII validation"));
    TEST_ASSERT_TRUE(contains(out, "Mode validation failed: size validation"));
    TEST_ASSERT_TRUE(contains(out, "Mode register validation passed: 'APP '"));
}

// --- debugPrint --------------------------------------------------------------

void test_debug_print_application()
{
    const std::string out = captureDebugPrint(TPS25751Mode(APP_DATA));

    TEST_ASSERT_TRUE(contains(out, "=== Mode Register (0x03) ==="));
    TEST_ASSERT_TRUE(contains(out, "Raw Mode String: APP "));
    TEST_ASSERT_TRUE(contains(out, "Application - PD controller is fully functioning"));
}

void test_debug_print_boot()
{
    const std::string out = captureDebugPrint(TPS25751Mode(BOOT_DATA));

    TEST_ASSERT_TRUE(contains(out, "Raw Mode String: BOOT"));
    TEST_ASSERT_TRUE(contains(out, "Boot - Only the Mode, Command, Data"));
}

void test_debug_print_patch()
{
    const std::string out = captureDebugPrint(TPS25751Mode(PTCH_DATA));

    TEST_ASSERT_TRUE(contains(out, "Raw Mode String: PTCH"));
    TEST_ASSERT_TRUE(contains(out, "Patch - Only the Mode, Command, Data"));
}

void test_debug_print_unknown()
{
    const std::string out = captureDebugPrint(TPS25751Mode(OTHER_DATA));

    TEST_ASSERT_TRUE(contains(out, "Raw Mode String: FWUP"));
    TEST_ASSERT_TRUE(contains(out, "Unknown / Limited Functionality"));
}

// --- Rule of Five ------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751Mode original(BOOT_DATA);
    TPS25751Mode copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BOOT_DATA, copy.raw(), MODE_SIZE);
    TEST_ASSERT_EQUAL(ModeType::Boot, copy.getModeType());
}

void test_copy_assignment_deep_copies()
{
    TPS25751Mode original(PTCH_DATA);
    TPS25751Mode target(APP_DATA);
    target = original;

    TEST_ASSERT_NOT_EQUAL(original.raw(), target.raw());
    TEST_ASSERT_EQUAL(ModeType::Patch, target.getModeType());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751Mode original(APP_DATA);
    const uint8_t *buf = original.raw();

    TPS25751Mode moved(static_cast<TPS25751Mode &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_TRUE(moved.isApplicationMode());
}

void test_move_assignment_transfers_ownership()
{
    TPS25751Mode original(BOOT_DATA);
    const uint8_t *buf = original.raw();
    TPS25751Mode target(APP_DATA);

    target = static_cast<TPS25751Mode &&>(original);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_TRUE(target.isBootMode());
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

    RUN_TEST(test_mode_string_returns_four_characters);
    RUN_TEST(test_mode_string_empty_when_invalid);
    RUN_TEST(test_application_mode);
    RUN_TEST(test_boot_mode);
    RUN_TEST(test_patch_mode);
    RUN_TEST(test_unrecognized_mode_is_unknown);
    RUN_TEST(test_mode_predicates_false_when_invalid);
    RUN_TEST(test_mode_matching_is_case_sensitive_and_exact);

    RUN_TEST(test_to_string_covers_all_enum_values);

    RUN_TEST(test_basic_and_data_validation);
    RUN_TEST(test_semantic_valid_for_ascii_modes);
    RUN_TEST(test_semantic_invalid_for_all_ones);
    RUN_TEST(test_semantic_invalid_for_all_zeros_non_printable);
    RUN_TEST(test_semantic_invalid_for_wrong_size);
    RUN_TEST(test_semantic_invalid_for_non_printable_character);
    RUN_TEST(test_semantic_valid_at_ascii_boundaries);
    RUN_TEST(test_semantic_invalid_when_register_invalid);
    RUN_TEST(test_semantic_validation_logs_failures_when_debug_enabled);

    RUN_TEST(test_debug_print_application);
    RUN_TEST(test_debug_print_boot);
    RUN_TEST(test_debug_print_patch);
    RUN_TEST(test_debug_print_unknown);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
