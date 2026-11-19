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

    return UNITY_END();
}
