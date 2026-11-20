/**
 * @file test_TPS25751PortControl.cpp
 * @brief Native (host-run) unit tests for TPS25751PortControl.
 *
 * Run with: pio test -e native
 *
 * Buffers are hand-packed (little-endian) from the PORT_CONTROL (0x29) field
 * table, Source: SLVUCR8B (Rev. B, 2026-08), page 31, via the tps25751-docs MCP:
 *   bits 31:30  Charger Detect Enable (2h = Detect proprietary chargers only;
 *               Reserved in Rev. A)
 *   bits 28:26  Charger Advertise Enable (0h = BC 1.2 SDP only)
 *   bit 25      DCD Enable (reset 0h)
 *   bit 24      Resistor 15k Present
 *   bit 20      Enable Current Monitor
 *   bit 19      Unconstrained Power
 *   bit 16      Automatic ID Request (reset 1h)
 *   bits 15:12  Initiate/Process Swap to DFP/UFP
 *   bits 7:4    Initiate/Process Swap to Source/Sink
 *   bits 1:0    TypeC Current
 * Rev. B reset value: 00015052h.
 */

#include <unity.h>
#include <unistd.h>
#include <string>
#include <cstdio>

#include "TPS25751PortControl.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t PORT_CONTROL_SIZE = TPS25751Registers::Registers::PORT_CONTROL.size;  // 4

// Rev. B reset 00015052h, little-endian.
constexpr uint8_t RESET_VALUE[PORT_CONTROL_SIZE] = {0x52, 0x50, 0x01, 0x00};

// Charger Detect Enable = 2h (bits 31:30 = 10b) -> byte 3 = 0x80.
constexpr uint8_t DETECT_PROPRIETARY[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x00, 0x80};

// Charger Detect Enable = 3h (bits 31:30 = 11b) -> byte 3 = 0xC0.
constexpr uint8_t DETECT_BC12_AND_PROPRIETARY[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x00, 0xC0};

// Resistor 15k Present (bit 24) + Charger Advertise Enable = 2h DCP
// (bits 28:26 = 010b -> bit 27): byte 3 = 0x09. The 15k pull-down must not be
// used with DCP modes (SLVUCR8B p.31), so this is semantically invalid.
constexpr uint8_t R15K_WITH_DCP[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x00, 0x09};

// DCD Enable (bit 25) alone: byte 3 = 0x02.
constexpr uint8_t DCD_ONLY[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x00, 0x02};

// Resistor 15k Present (bit 24) alone: byte 3 = 0x01.
constexpr uint8_t R15K_ONLY[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x00, 0x01};

// Automatic ID Request (bit 16) alone: byte 2 = 0x01.
constexpr uint8_t AUTO_ID_ONLY[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x01, 0x00};

// Every bit set except bits 25 and 16.
constexpr uint8_t ALL_BUT_DCD_AND_AUTO_ID[PORT_CONTROL_SIZE] = {0xFF, 0xFF, 0xFE, 0xFD};

// Only bits 25 and 16 set.
constexpr uint8_t BOTH_ONLY[PORT_CONTROL_SIZE] = {0x00, 0x00, 0x01, 0x02};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & size ----------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751PortControl reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(PORT_CONTROL_SIZE, reg.size());
    TEST_ASSERT_EQUAL(ChargerDetectEnable::None, reg.chargerDetectEnable());
    TEST_ASSERT_EQUAL(ChargerAdvertiseEnable::SDP, reg.chargerAdvertiseEnable());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Rev. B reset value -----------------------------------------------------

void test_reset_value_decodes_rev_b_defaults()
{
    TPS25751PortControl reg(RESET_VALUE);

    TEST_ASSERT_EQUAL(ChargerDetectEnable::None, reg.chargerDetectEnable());
    TEST_ASSERT_EQUAL(ChargerAdvertiseEnable::SDP, reg.chargerAdvertiseEnable());
    TEST_ASSERT_FALSE(reg.resistor15kPresent());
    TEST_ASSERT_FALSE(reg.enableCurrentMonitor());
    TEST_ASSERT_FALSE(reg.unconstrainedPower());
    TEST_ASSERT_FALSE(reg.dcdEnable());          // bit 25, reset 0h
    TEST_ASSERT_TRUE(reg.automaticIdRequest());  // bit 16, reset 1h

    TEST_ASSERT_FALSE(reg.initiateSwapToDFP());
    TEST_ASSERT_TRUE(reg.processSwapToDFP());    // bit 14, reset 1h
    TEST_ASSERT_FALSE(reg.initiateSwapToUFP());
    TEST_ASSERT_TRUE(reg.processSwapToUFP());    // bit 12, reset 1h

    TEST_ASSERT_FALSE(reg.initiateSwapToSource());
    TEST_ASSERT_TRUE(reg.processSwapToSource()); // bit 6, reset 1h
    TEST_ASSERT_FALSE(reg.initiateSwapToSink());
    TEST_ASSERT_TRUE(reg.processSwapToSink());   // bit 4, reset 1h

    TEST_ASSERT_EQUAL(TypeCCurrent::Current_3_0A, reg.typeCCurrent());  // reset 2h

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- DCD Enable (bit 25) and Automatic ID Request (bit 16) ------------------

void test_default_buffer_has_dcd_and_auto_id_clear()
{
    TPS25751PortControl reg;

    TEST_ASSERT_FALSE(reg.dcdEnable());
    TEST_ASSERT_FALSE(reg.automaticIdRequest());
}

void test_dcd_enable_decodes_bit_25()
{
    TPS25751PortControl reg(DCD_ONLY);

    TEST_ASSERT_TRUE(reg.dcdEnable());
    TEST_ASSERT_FALSE(reg.resistor15kPresent());  // bit 24, the next bit down
    TEST_ASSERT_FALSE(reg.automaticIdRequest());
    TEST_ASSERT_EQUAL(ChargerAdvertiseEnable::SDP, reg.chargerAdvertiseEnable());  // bits 28:26
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_resistor_15k_does_not_leak_into_dcd_enable()
{
    TPS25751PortControl reg(R15K_ONLY);

    TEST_ASSERT_TRUE(reg.resistor15kPresent());
    TEST_ASSERT_FALSE(reg.dcdEnable());
}

void test_automatic_id_request_decodes_bit_16()
{
    TPS25751PortControl reg(AUTO_ID_ONLY);

    TEST_ASSERT_TRUE(reg.automaticIdRequest());
    TEST_ASSERT_FALSE(reg.dcdEnable());
    TEST_ASSERT_FALSE(reg.unconstrainedPower());      // bit 19
    TEST_ASSERT_FALSE(reg.enableCurrentMonitor());    // bit 20
    TEST_ASSERT_FALSE(reg.processSwapToDFP());        // bit 14
    TEST_ASSERT_FALSE(reg.initiateSwapToDFP());       // bit 15
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_both_fields_set_together()
{
    TPS25751PortControl reg(BOTH_ONLY);

    TEST_ASSERT_TRUE(reg.dcdEnable());
    TEST_ASSERT_TRUE(reg.automaticIdRequest());
    TEST_ASSERT_FALSE(reg.resistor15kPresent());
    TEST_ASSERT_FALSE(reg.unconstrainedPower());
}

void test_dcd_and_auto_id_clear_with_every_other_bit_set()
{
    TPS25751PortControl reg(ALL_BUT_DCD_AND_AUTO_ID);

    TEST_ASSERT_FALSE(reg.dcdEnable());
    TEST_ASSERT_FALSE(reg.automaticIdRequest());
    TEST_ASSERT_TRUE(reg.resistor15kPresent());
    TEST_ASSERT_TRUE(reg.enableCurrentMonitor());
    TEST_ASSERT_TRUE(reg.unconstrainedPower());
    TEST_ASSERT_TRUE(reg.initiateSwapToDFP());
}

// --- Charger Detect Enable 2h (new in Rev. B) -------------------------------

void test_charger_detect_2h_decodes_as_proprietary_only()
{
    TPS25751PortControl reg(DETECT_PROPRIETARY);

    TEST_ASSERT_EQUAL(ChargerDetectEnable::ProprietaryOnly, reg.chargerDetectEnable());
    TEST_ASSERT_EQUAL_STRING("Proprietary only", toString(reg.chargerDetectEnable()));
}

void test_charger_detect_2h_is_semantically_valid()
{
    // Rev. A reserved 2h and the class rejected it; Rev. B defines it.
    TEST_ASSERT_TRUE(TPS25751PortControl(DETECT_PROPRIETARY).isSemanticallyValid());
}

void test_charger_detect_3h_decodes()
{
    TPS25751PortControl reg(DETECT_BC12_AND_PROPRIETARY);

    TEST_ASSERT_EQUAL(ChargerDetectEnable::BC12AndProprietary, reg.chargerDetectEnable());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Charger Advertise Enable 0h text ----------------------------------------

void test_charger_advertise_0h_string_is_sdp()
{
    TEST_ASSERT_EQUAL_STRING("BC 1.2 SDP only", toString(ChargerAdvertiseEnable::SDP));
}

// --- Deprecated aliases -------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void test_deprecated_aliases_match_rev_b_names()
{
    // Rev. A names keep their old values so existing sketches decode the same.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChargerDetectEnable::ProprietaryOnly),
                            static_cast<uint8_t>(ChargerDetectEnable::Reserved));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ChargerAdvertiseEnable::SDP),
                            static_cast<uint8_t>(ChargerAdvertiseEnable::None));

    TEST_ASSERT_EQUAL(ChargerDetectEnable::Reserved,
                      TPS25751PortControl(DETECT_PROPRIETARY).chargerDetectEnable());
    TEST_ASSERT_EQUAL(ChargerAdvertiseEnable::None,
                      TPS25751PortControl(RESET_VALUE).chargerAdvertiseEnable());
}
#pragma GCC diagnostic pop

// --- Existing semantic rule ---------------------------------------------------

void test_15k_resistor_with_dcp_is_semantically_invalid()
{
    TPS25751PortControl reg(R15K_WITH_DCP);

    TEST_ASSERT_TRUE(reg.resistor15kPresent());
    TEST_ASSERT_EQUAL(ChargerAdvertiseEnable::DCP, reg.chargerAdvertiseEnable());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_wrong_size_fails_semantic_validation()
{
    const uint8_t oversized[5] = {0x52, 0x50, 0x01, 0x00, 0x00};
    TEST_ASSERT_FALSE(TPS25751PortControl(oversized, sizeof(oversized)).isSemanticallyValid());
}


// --- Every flag decodes from its own bit (TRM SLVUCR8B p.31) ----------------

namespace {

struct FlagCase {
    uint8_t bit;
    bool (TPS25751PortControl::*get)() const;
    const char *name;
};

constexpr FlagCase kFlags[] = {
    {24, &TPS25751PortControl::resistor15kPresent, "resistor15kPresent"},
    {25, &TPS25751PortControl::dcdEnable, "dcdEnable"},
    {16, &TPS25751PortControl::automaticIdRequest, "automaticIdRequest"},
    {20, &TPS25751PortControl::enableCurrentMonitor, "enableCurrentMonitor"},
    {19, &TPS25751PortControl::unconstrainedPower, "unconstrainedPower"},
    {15, &TPS25751PortControl::initiateSwapToDFP, "initiateSwapToDFP"},
    {14, &TPS25751PortControl::processSwapToDFP, "processSwapToDFP"},
    {13, &TPS25751PortControl::initiateSwapToUFP, "initiateSwapToUFP"},
    {12, &TPS25751PortControl::processSwapToUFP, "processSwapToUFP"},
    {7, &TPS25751PortControl::initiateSwapToSource, "initiateSwapToSource"},
    {6, &TPS25751PortControl::processSwapToSource, "processSwapToSource"},
    {5, &TPS25751PortControl::initiateSwapToSink, "initiateSwapToSink"},
    {4, &TPS25751PortControl::processSwapToSink, "processSwapToSink"},
};

constexpr size_t kNumFlags = sizeof(kFlags) / sizeof(kFlags[0]);

void packBit(uint8_t (&buf)[PORT_CONTROL_SIZE], uint8_t bit)
{
    buf[bit / 8] = static_cast<uint8_t>(buf[bit / 8] | (1u << (bit % 8)));
}

// Runs reg.debugPrint() against the stdout-backed native Stream stub and
// returns what it printed.
std::string captureDebugPrint(const TPS25751PortControl &reg)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    const int saved = dup(fileno(stdout));
    dup2(fileno(tmp), fileno(stdout));

    reg.debugPrint();

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

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void test_each_flag_decodes_from_exactly_its_bit()
{
    for (size_t i = 0; i < kNumFlags; ++i) {
        uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, 0};
        packBit(buf, kFlags[i].bit);
        TPS25751PortControl reg(buf);
        TEST_ASSERT_TRUE_MESSAGE((reg.*kFlags[i].get)(), kFlags[i].name);
        for (size_t j = 0; j < kNumFlags; ++j) {
            if (j != i) TEST_ASSERT_FALSE_MESSAGE((reg.*kFlags[j].get)(), kFlags[j].name);
        }
    }
}

void test_charger_detect_enable_all_codes()
{
    // Bits 31:30 -> byte 3 bits 7:6.
    const ChargerDetectEnable expected[4] = {ChargerDetectEnable::None, ChargerDetectEnable::BC12Only,
                                             ChargerDetectEnable::ProprietaryOnly,
                                             ChargerDetectEnable::BC12AndProprietary};
    for (uint8_t code = 0; code < 4; ++code) {
        const uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, static_cast<uint8_t>(code << 6)};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected[code]),
                                static_cast<uint8_t>(TPS25751PortControl(buf).chargerDetectEnable()));
    }
}

void test_charger_advertise_enable_all_codes()
{
    // Bits 28:26 -> byte 3 bits 4:2.
    for (uint8_t code = 0; code < 8; ++code) {
        const uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, static_cast<uint8_t>(code << 2)};
        TEST_ASSERT_EQUAL_UINT8(code, static_cast<uint8_t>(TPS25751PortControl(buf).chargerAdvertiseEnable()));
    }
}

void test_type_c_current_all_codes()
{
    // Bits 1:0 -> byte 0.
    const TypeCCurrent expected[4] = {TypeCCurrent::USBDefault, TypeCCurrent::Current_1_5A,
                                      TypeCCurrent::Current_3_0A, TypeCCurrent::Reserved};
    for (uint8_t code = 0; code < 4; ++code) {
        const uint8_t buf[PORT_CONTROL_SIZE] = {code, 0, 0, 0};
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected[code]),
                                static_cast<uint8_t>(TPS25751PortControl(buf).typeCCurrent()));
    }
}

// --- toString -----------------------------------------------------------------

void test_to_string_charger_detect_enable()
{
    TEST_ASSERT_EQUAL_STRING("None", toString(ChargerDetectEnable::None));
    TEST_ASSERT_EQUAL_STRING("BC 1.2 only", toString(ChargerDetectEnable::BC12Only));
    TEST_ASSERT_EQUAL_STRING("Proprietary only", toString(ChargerDetectEnable::ProprietaryOnly));
    TEST_ASSERT_EQUAL_STRING("BC 1.2 + Proprietary", toString(ChargerDetectEnable::BC12AndProprietary));
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<ChargerDetectEnable>(4)));
}

void test_to_string_charger_advertise_enable()
{
    TEST_ASSERT_EQUAL_STRING("BC 1.2 SDP only", toString(ChargerAdvertiseEnable::SDP));
    TEST_ASSERT_EQUAL_STRING("CDP", toString(ChargerAdvertiseEnable::CDP));
    TEST_ASSERT_EQUAL_STRING("DCP", toString(ChargerAdvertiseEnable::DCP));
    TEST_ASSERT_EQUAL_STRING("Reserved (3)", toString(ChargerAdvertiseEnable::Reserved3));
    TEST_ASSERT_EQUAL_STRING("Reserved (4)", toString(ChargerAdvertiseEnable::Reserved4));
    TEST_ASSERT_EQUAL_STRING("DCP Auto 1", toString(ChargerAdvertiseEnable::DCP_Auto1));
    TEST_ASSERT_EQUAL_STRING("DCP Auto 2", toString(ChargerAdvertiseEnable::DCP_Auto2));
    TEST_ASSERT_EQUAL_STRING("Reserved (7)", toString(ChargerAdvertiseEnable::Reserved7));
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<ChargerAdvertiseEnable>(8)));
}

void test_to_string_type_c_current()
{
    TEST_ASSERT_EQUAL_STRING("USB Default", toString(TypeCCurrent::USBDefault));
    TEST_ASSERT_EQUAL_STRING("1.5A", toString(TypeCCurrent::Current_1_5A));
    TEST_ASSERT_EQUAL_STRING("3.0A", toString(TypeCCurrent::Current_3_0A));
    TEST_ASSERT_EQUAL_STRING("Reserved", toString(TypeCCurrent::Reserved));
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<TypeCCurrent>(4)));
}

// --- Semantic validation: remaining branches ---------------------------------

void test_15k_resistor_with_dcp_auto_modes_is_semantically_invalid()
{
    // Advertise 5h (DCP Auto 1) and 6h (DCP Auto 2) with the 15k resistor present.
    for (uint8_t code = 5; code <= 6; ++code) {
        const uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, static_cast<uint8_t>(0x01 | (code << 2))};
        TEST_ASSERT_FALSE(TPS25751PortControl(buf).isSemanticallyValid());
    }
}

void test_15k_resistor_with_non_dcp_modes_is_semantically_valid()
{
    // SDP (0h), CDP (1h), and the reserved codes carry no DCP rule.
    const uint8_t codes[] = {0, 1, 3, 4, 7};
    for (uint8_t code : codes) {
        const uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, static_cast<uint8_t>(0x01 | (code << 2))};
        TEST_ASSERT_TRUE(TPS25751PortControl(buf).isSemanticallyValid());
    }
}

void test_dcp_without_15k_resistor_is_semantically_valid()
{
    const uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, static_cast<uint8_t>(2 << 2)};
    TEST_ASSERT_TRUE(TPS25751PortControl(buf).isSemanticallyValid());
}

void test_all_ones_data_is_semantically_invalid()
{
    const uint8_t buf[PORT_CONTROL_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(TPS25751PortControl(buf).isSemanticallyValid());
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_reset_value()
{
    // Reset 00015052h: Auto ID Request set, Process-swap bits 14, 12, 6, 4 set, TypeC Current 2h.
    const std::string out = captureDebugPrint(TPS25751PortControl(RESET_VALUE));

    TEST_ASSERT_TRUE(contains(out, "=== Port Control Register (0x29) ==="));
    TEST_ASSERT_TRUE(contains(out, "    Detect Enable: None\n"));
    TEST_ASSERT_TRUE(contains(out, "    Advertise Enable: BC 1.2 SDP only\n"));
    TEST_ASSERT_TRUE(contains(out, "    15k Resistor Present: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    DCD Enable: Disabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Automatic ID Request: Enabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Current Monitor: Disabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Unconstrained Power: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to DFP: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to DFP: Accept\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to UFP: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to UFP: Accept\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to Source: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to Source: Accept\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to Sink: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to Sink: Accept\n"));
    TEST_ASSERT_TRUE(contains(out, "Type-C Current: 3.0A\n"));
}

void test_debug_print_all_zero_rejects_swaps()
{
    const uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, 0};
    const std::string out = captureDebugPrint(TPS25751PortControl(buf));

    TEST_ASSERT_TRUE(contains(out, "    Automatic ID Request: Disabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to DFP: Reject\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to UFP: Reject\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to Source: Reject\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to Sink: Reject\n"));
    TEST_ASSERT_TRUE(contains(out, "Type-C Current: USB Default\n"));
}

void test_debug_print_every_flag_set()
{
    uint8_t buf[PORT_CONTROL_SIZE] = {0, 0, 0, 0};
    for (size_t i = 0; i < kNumFlags; ++i) packBit(buf, kFlags[i].bit);
    buf[3] = static_cast<uint8_t>(buf[3] | (3u << 6) | (6u << 2));  // detect 3h, advertise 6h
    buf[0] = static_cast<uint8_t>(buf[0] | 0x01);                   // Type-C current 1.5 A
    const std::string out = captureDebugPrint(TPS25751PortControl(buf));

    TEST_ASSERT_TRUE(contains(out, "    Detect Enable: BC 1.2 + Proprietary\n"));
    TEST_ASSERT_TRUE(contains(out, "    Advertise Enable: DCP Auto 2\n"));
    TEST_ASSERT_TRUE(contains(out, "    15k Resistor Present: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    DCD Enable: Enabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Automatic ID Request: Enabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Current Monitor: Enabled\n"));
    TEST_ASSERT_TRUE(contains(out, "    Unconstrained Power: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to DFP: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    Process to DFP: Accept\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to UFP: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to Source: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    Initiate to Sink: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "Type-C Current: 1.5A\n"));
}

// --- Runner -----------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_reset_value_decodes_rev_b_defaults);
    RUN_TEST(test_default_buffer_has_dcd_and_auto_id_clear);
    RUN_TEST(test_dcd_enable_decodes_bit_25);
    RUN_TEST(test_resistor_15k_does_not_leak_into_dcd_enable);
    RUN_TEST(test_automatic_id_request_decodes_bit_16);
    RUN_TEST(test_both_fields_set_together);
    RUN_TEST(test_dcd_and_auto_id_clear_with_every_other_bit_set);
    RUN_TEST(test_charger_detect_2h_decodes_as_proprietary_only);
    RUN_TEST(test_charger_detect_2h_is_semantically_valid);
    RUN_TEST(test_charger_detect_3h_decodes);
    RUN_TEST(test_charger_advertise_0h_string_is_sdp);
    RUN_TEST(test_deprecated_aliases_match_rev_b_names);
    RUN_TEST(test_15k_resistor_with_dcp_is_semantically_invalid);
    RUN_TEST(test_wrong_size_fails_semantic_validation);

    RUN_TEST(test_each_flag_decodes_from_exactly_its_bit);
    RUN_TEST(test_charger_detect_enable_all_codes);
    RUN_TEST(test_charger_advertise_enable_all_codes);
    RUN_TEST(test_type_c_current_all_codes);
    RUN_TEST(test_to_string_charger_detect_enable);
    RUN_TEST(test_to_string_charger_advertise_enable);
    RUN_TEST(test_to_string_type_c_current);
    RUN_TEST(test_15k_resistor_with_dcp_auto_modes_is_semantically_invalid);
    RUN_TEST(test_15k_resistor_with_non_dcp_modes_is_semantically_valid);
    RUN_TEST(test_dcp_without_15k_resistor_is_semantically_valid);
    RUN_TEST(test_all_ones_data_is_semantically_invalid);
    RUN_TEST(test_debug_print_reset_value);
    RUN_TEST(test_debug_print_all_zero_rejects_swaps);
    RUN_TEST(test_debug_print_every_flag_set);

    return UNITY_END();
}
