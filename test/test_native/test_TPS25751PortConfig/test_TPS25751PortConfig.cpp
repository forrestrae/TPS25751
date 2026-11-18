/**
 * @file test_TPS25751PortConfig.cpp
 * @brief Native (host-run) unit tests for TPS25751PortConfig.
 *
 * Run with: pio test -e native
 *
 * Buffers are hand-packed (little-endian) from the PORT_CONFIGURATION (0x28)
 * field table, Source: SLVUCR8B (Rev. B, 2026-08), page 29, via the
 * tps25751-docs MCP:
 *   bits 26:24  VBUS Sink UVP Trip HV (reset 1h)
 *   bits 21:20  OVP for PP5V (reset 2h)
 *   bits 17:16  VBUS OVP Usage (reset 2h)
 *   bit 10      Disable PD
 *   bits 9:8    TypeC Support Options (1h Try.SRC, 2h Try.SNK new in Rev. B)
 *   bits 1:0    TypeC State machine (reset 2h, DRP)
 * Rev. B reset value: 0001220002h (17-byte register).
 */

#include <unity.h>

#include "TPS25751PortConfig.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

// Defined in TPS25751PortConfig.cpp but not declared in its header.
const char* toString(TypeCSupportOptions opt);

namespace {

constexpr size_t PORT_CONFIG_SIZE = TPS25751Registers::Registers::PORT_CONFIGURATION.size;  // 17

// Rev. B reset 0001220002h, little-endian.
constexpr uint8_t RESET_VALUE[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x22, 0x01};

// DRP state machine (2h) + TypeC Support Options 2h (Try.SNK): byte 1 = 0x02.
constexpr uint8_t DRP_TRY_SNK[PORT_CONFIG_SIZE] = {0x02, 0x02};

// DRP state machine (2h) + TypeC Support Options 1h (Try.SRC): byte 1 = 0x01.
constexpr uint8_t DRP_TRY_SRC[PORT_CONFIG_SIZE] = {0x02, 0x01};

// Sink-only state machine (0h) + Try.SRC (1h): inconsistent.
constexpr uint8_t SINK_ONLY_TRY_SRC[PORT_CONFIG_SIZE] = {0x00, 0x01};

// DRP + TypeC Support Options 3h (Reserved).
constexpr uint8_t DRP_SUPPORT_RESERVED[PORT_CONFIG_SIZE] = {0x02, 0x03};

}  // namespace

void setUp() {}
void tearDown() {}

void test_size_is_17_bytes()
{
    TPS25751PortConfig reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(17, PORT_CONFIG_SIZE);
    TEST_ASSERT_EQUAL_size_t(PORT_CONFIG_SIZE, reg.size());
}

void test_reset_value_decodes_rev_b_defaults()
{
    TPS25751PortConfig reg(RESET_VALUE);

    TEST_ASSERT_EQUAL(TypeCStateMachine::DRP, reg.typeCStateMachine());
    TEST_ASSERT_EQUAL(TypeCSupportOptions::None, reg.typeCSupportOptions());
    TEST_ASSERT_FALSE(reg.disablePD());
    TEST_ASSERT_EQUAL(VbusOVPUsage::OVP111, reg.vbusOVPUsage());
    TEST_ASSERT_EQUAL(OVP_PP5V::V5_8, reg.ovpPP5V());
    TEST_ASSERT_EQUAL(VbusUVPTripHV::Margin10, reg.vbusUVPTripHV());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_type_c_support_2h_decodes_as_try_snk()
{
    TPS25751PortConfig reg(DRP_TRY_SNK);

    TEST_ASSERT_EQUAL(TypeCSupportOptions::TrySNK, reg.typeCSupportOptions());
    TEST_ASSERT_EQUAL_STRING("Try.SNK", toString(reg.typeCSupportOptions()));
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_type_c_support_1h_decodes_as_try_src()
{
    TPS25751PortConfig reg(DRP_TRY_SRC);

    TEST_ASSERT_EQUAL(TypeCSupportOptions::TrySRC, reg.typeCSupportOptions());
    TEST_ASSERT_EQUAL_STRING("Try.SRC", toString(reg.typeCSupportOptions()));
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_try_src_with_sink_only_is_semantically_invalid()
{
    TEST_ASSERT_FALSE(TPS25751PortConfig(SINK_ONLY_TRY_SRC).isSemanticallyValid());
}

void test_reserved_support_option_is_semantically_invalid()
{
    TPS25751PortConfig reg(DRP_SUPPORT_RESERVED);

    TEST_ASSERT_EQUAL(TypeCSupportOptions::Reserved, reg.typeCSupportOptions());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_support_options_to_string()
{
    TEST_ASSERT_EQUAL_STRING("None", toString(TypeCSupportOptions::None));
    TEST_ASSERT_EQUAL_STRING("Reserved", toString(TypeCSupportOptions::Reserved));
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void test_deprecated_drp_src_alias_matches_try_src()
{
    // The old name keeps its value (1h) so existing sketches decode the same.
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TypeCSupportOptions::TrySRC),
                            static_cast<uint8_t>(TypeCSupportOptions::DRP_Src));
    TEST_ASSERT_EQUAL(TypeCSupportOptions::DRP_Src,
                      TPS25751PortConfig(DRP_TRY_SRC).typeCSupportOptions());
}
#pragma GCC diagnostic pop

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_size_is_17_bytes);
    RUN_TEST(test_reset_value_decodes_rev_b_defaults);
    RUN_TEST(test_type_c_support_2h_decodes_as_try_snk);
    RUN_TEST(test_type_c_support_1h_decodes_as_try_src);
    RUN_TEST(test_try_src_with_sink_only_is_semantically_invalid);
    RUN_TEST(test_reserved_support_option_is_semantically_invalid);
    RUN_TEST(test_support_options_to_string);
    RUN_TEST(test_deprecated_drp_src_alias_matches_try_src);

    return UNITY_END();
}
