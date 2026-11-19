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
 *   bits 30:29  APDO ILIM Over Shoot (reset 0h)
 *   bits 28:27  APDO VBUS UVP Threshold (reset 0h, 3h Reserved)
 *   bits 14:13  USB3 Rate (reset 0h, 3h Reserved)
 *   bit 12      DebugAccessory Support (reset 0h)
 *   bit 11      USB Communication Capable (reset 0h)
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

// USB3 Rate bits 14:13 live in byte 1 bits 6:5; DebugAccessory Support (bit 12)
// and USB Communication Capable (bit 11) are byte 1 bits 4 and 3. Byte 0 = 0x02
// keeps the DRP state machine so the fixtures are otherwise valid.
constexpr uint8_t USB3_GEN1[PORT_CONFIG_SIZE] = {0x02, 0x20};
constexpr uint8_t USB3_GEN2[PORT_CONFIG_SIZE] = {0x02, 0x40};
constexpr uint8_t USB3_RESERVED[PORT_CONFIG_SIZE] = {0x02, 0x60};
constexpr uint8_t DEBUG_ACCESSORY_ONLY[PORT_CONFIG_SIZE] = {0x02, 0x10};
constexpr uint8_t USB_COMM_CAPABLE_ONLY[PORT_CONFIG_SIZE] = {0x02, 0x08};
// Every byte 1 bit set except USB3 Rate (14:13): bits 15, 12:8.
constexpr uint8_t ALL_BUT_USB3_RATE[PORT_CONFIG_SIZE] = {0x02, 0x9F};
// Every byte 1 bit set except bits 12 and 11: USB3 Rate reads 3h.
constexpr uint8_t ALL_BUT_FLAGS[PORT_CONFIG_SIZE] = {0x02, 0xE7};

// APDO ILIM Over Shoot bits 30:29 are byte 3 bits 6:5; APDO VBUS UVP Threshold
// bits 28:27 are byte 3 bits 4:3. Bytes 2 and 3 other bits stay 0 unless stated.
constexpr uint8_t ILIM_50MA[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x20};
constexpr uint8_t ILIM_75MA[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x40};
constexpr uint8_t ILIM_STATIC_2900MA[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x60};
constexpr uint8_t UVP_88[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x08};
constexpr uint8_t UVP_92[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x10};
constexpr uint8_t UVP_RESERVED[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x18};
// Byte 3 with every bit set except bits 30:27: bit 31 and bits 26:24 stay set.
constexpr uint8_t ALL_BUT_APDO_BYTE3[PORT_CONFIG_SIZE] = {0x02, 0x00, 0x00, 0x87};

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

void test_reset_value_new_rev_b_fields_are_zero()
{
    TPS25751PortConfig reg(RESET_VALUE);

    TEST_ASSERT_EQUAL(USB3Rate::NotSupported, reg.usb3Rate());
    TEST_ASSERT_FALSE(reg.debugAccessorySupport());
    TEST_ASSERT_FALSE(reg.usbCommunicationCapable());
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Overshoot25mA, reg.apdoIlimOvershoot());
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Percent90, reg.apdoVbusUvpThreshold());
}

// --- USB3 Rate (bits 14:13) -------------------------------------------------

void test_usb3_rate_decodes_every_value()
{
    TEST_ASSERT_EQUAL(USB3Rate::NotSupported, TPS25751PortConfig(RESET_VALUE).usb3Rate());
    TEST_ASSERT_EQUAL(USB3Rate::Gen1, TPS25751PortConfig(USB3_GEN1).usb3Rate());
    TEST_ASSERT_EQUAL(USB3Rate::Gen2, TPS25751PortConfig(USB3_GEN2).usb3Rate());
    TEST_ASSERT_EQUAL(USB3Rate::Reserved, TPS25751PortConfig(USB3_RESERVED).usb3Rate());
}

void test_usb3_rate_to_string()
{
    TEST_ASSERT_EQUAL_STRING("USB3 not supported", toString(USB3Rate::NotSupported));
    TEST_ASSERT_EQUAL_STRING("USB3 Gen1", toString(USB3Rate::Gen1));
    TEST_ASSERT_EQUAL_STRING("USB3 Gen2", toString(USB3Rate::Gen2));
    TEST_ASSERT_EQUAL_STRING("Reserved", toString(USB3Rate::Reserved));
}

void test_usb3_rate_gen1_and_gen2_are_semantically_valid()
{
    TEST_ASSERT_TRUE(TPS25751PortConfig(USB3_GEN1).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751PortConfig(USB3_GEN2).isSemanticallyValid());
}

void test_usb3_rate_reserved_is_semantically_invalid()
{
    TEST_ASSERT_FALSE(TPS25751PortConfig(USB3_RESERVED).isSemanticallyValid());
}

void test_usb3_rate_is_isolated_from_neighbouring_bits()
{
    TPS25751PortConfig reg(ALL_BUT_USB3_RATE);

    TEST_ASSERT_EQUAL(USB3Rate::NotSupported, reg.usb3Rate());
    TEST_ASSERT_TRUE(reg.debugAccessorySupport());
    TEST_ASSERT_TRUE(reg.usbCommunicationCapable());
    TEST_ASSERT_TRUE(reg.disablePD());

    // Setting only the rate must leave the flags clear.
    TPS25751PortConfig gen2(USB3_GEN2);
    TEST_ASSERT_FALSE(gen2.debugAccessorySupport());
    TEST_ASSERT_FALSE(gen2.usbCommunicationCapable());
    TEST_ASSERT_FALSE(gen2.disablePD());
}

// --- DebugAccessory Support (bit 12) and USB Communication Capable (bit 11) --

void test_debug_accessory_support_decodes_bit_12()
{
    TPS25751PortConfig reg(DEBUG_ACCESSORY_ONLY);

    TEST_ASSERT_TRUE(reg.debugAccessorySupport());
    TEST_ASSERT_FALSE(reg.usbCommunicationCapable());
    TEST_ASSERT_EQUAL(USB3Rate::NotSupported, reg.usb3Rate());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_usb_communication_capable_decodes_bit_11()
{
    TPS25751PortConfig reg(USB_COMM_CAPABLE_ONLY);

    TEST_ASSERT_TRUE(reg.usbCommunicationCapable());
    TEST_ASSERT_FALSE(reg.debugAccessorySupport());
    TEST_ASSERT_FALSE(reg.disablePD());  // bit 10 is the next bit up
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_flags_clear_with_all_neighbouring_bits_set()
{
    TPS25751PortConfig reg(ALL_BUT_FLAGS);

    TEST_ASSERT_FALSE(reg.debugAccessorySupport());
    TEST_ASSERT_FALSE(reg.usbCommunicationCapable());
    TEST_ASSERT_EQUAL(USB3Rate::Reserved, reg.usb3Rate());
    TEST_ASSERT_TRUE(reg.disablePD());
}

// --- APDO ILIM Over Shoot (bits 30:29) --------------------------------------

void test_apdo_ilim_overshoot_decodes_every_value()
{
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Overshoot25mA, TPS25751PortConfig(RESET_VALUE).apdoIlimOvershoot());
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Above50mA, TPS25751PortConfig(ILIM_50MA).apdoIlimOvershoot());
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Above75mA, TPS25751PortConfig(ILIM_75MA).apdoIlimOvershoot());
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Static2900mA, TPS25751PortConfig(ILIM_STATIC_2900MA).apdoIlimOvershoot());
}

void test_apdo_ilim_overshoot_to_string()
{
    TEST_ASSERT_EQUAL_STRING("25 mA overshoot", toString(ApdoIlimOvershoot::Overshoot25mA));
    TEST_ASSERT_EQUAL_STRING("50 mA above PDO current", toString(ApdoIlimOvershoot::Above50mA));
    TEST_ASSERT_EQUAL_STRING("75 mA above PDO current", toString(ApdoIlimOvershoot::Above75mA));
    TEST_ASSERT_EQUAL_STRING("Static 2900 mA", toString(ApdoIlimOvershoot::Static2900mA));
}

void test_apdo_ilim_overshoot_all_values_are_semantically_valid()
{
    // All four values are defined, so none is rejected.
    TEST_ASSERT_TRUE(TPS25751PortConfig(ILIM_50MA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751PortConfig(ILIM_75MA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751PortConfig(ILIM_STATIC_2900MA).isSemanticallyValid());
}

// --- APDO VBUS UVP Threshold (bits 28:27) -----------------------------------

void test_apdo_vbus_uvp_threshold_decodes_every_value()
{
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Percent90, TPS25751PortConfig(RESET_VALUE).apdoVbusUvpThreshold());
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Percent88, TPS25751PortConfig(UVP_88).apdoVbusUvpThreshold());
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Percent92, TPS25751PortConfig(UVP_92).apdoVbusUvpThreshold());
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Reserved, TPS25751PortConfig(UVP_RESERVED).apdoVbusUvpThreshold());
}

void test_apdo_vbus_uvp_threshold_to_string()
{
    TEST_ASSERT_EQUAL_STRING("90%", toString(ApdoVbusUvpThreshold::Percent90));
    TEST_ASSERT_EQUAL_STRING("88%", toString(ApdoVbusUvpThreshold::Percent88));
    TEST_ASSERT_EQUAL_STRING("92%", toString(ApdoVbusUvpThreshold::Percent92));
    TEST_ASSERT_EQUAL_STRING("Reserved", toString(ApdoVbusUvpThreshold::Reserved));
}

void test_apdo_vbus_uvp_threshold_reserved_is_semantically_invalid()
{
    TEST_ASSERT_TRUE(TPS25751PortConfig(UVP_88).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751PortConfig(UVP_92).isSemanticallyValid());
    TEST_ASSERT_FALSE(TPS25751PortConfig(UVP_RESERVED).isSemanticallyValid());
}

void test_apdo_fields_are_isolated_from_each_other_and_neighbours()
{
    // Overshoot 3h alone: UVP threshold and the sink UVP trip (bits 26:24) stay 0.
    TPS25751PortConfig ilim(ILIM_STATIC_2900MA);
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Percent90, ilim.apdoVbusUvpThreshold());
    TEST_ASSERT_EQUAL(VbusUVPTripHV::Margin5, ilim.vbusUVPTripHV());

    // UVP threshold 3h alone: overshoot stays 0.
    TPS25751PortConfig uvp(UVP_RESERVED);
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Overshoot25mA, uvp.apdoIlimOvershoot());
    TEST_ASSERT_EQUAL(VbusUVPTripHV::Margin5, uvp.vbusUVPTripHV());

    // Bit 31 and bits 26:24 set: both APDO fields read 0 and the trip reads 7h.
    TPS25751PortConfig around(ALL_BUT_APDO_BYTE3);
    TEST_ASSERT_EQUAL(ApdoIlimOvershoot::Overshoot25mA, around.apdoIlimOvershoot());
    TEST_ASSERT_EQUAL(ApdoVbusUvpThreshold::Percent90, around.apdoVbusUvpThreshold());
    TEST_ASSERT_EQUAL(VbusUVPTripHV::Margin50, around.vbusUVPTripHV());
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
    RUN_TEST(test_reset_value_new_rev_b_fields_are_zero);
    RUN_TEST(test_usb3_rate_decodes_every_value);
    RUN_TEST(test_usb3_rate_to_string);
    RUN_TEST(test_usb3_rate_gen1_and_gen2_are_semantically_valid);
    RUN_TEST(test_usb3_rate_reserved_is_semantically_invalid);
    RUN_TEST(test_usb3_rate_is_isolated_from_neighbouring_bits);
    RUN_TEST(test_debug_accessory_support_decodes_bit_12);
    RUN_TEST(test_usb_communication_capable_decodes_bit_11);
    RUN_TEST(test_flags_clear_with_all_neighbouring_bits_set);
    RUN_TEST(test_apdo_ilim_overshoot_decodes_every_value);
    RUN_TEST(test_apdo_ilim_overshoot_to_string);
    RUN_TEST(test_apdo_ilim_overshoot_all_values_are_semantically_valid);
    RUN_TEST(test_apdo_vbus_uvp_threshold_decodes_every_value);
    RUN_TEST(test_apdo_vbus_uvp_threshold_to_string);
    RUN_TEST(test_apdo_vbus_uvp_threshold_reserved_is_semantically_invalid);
    RUN_TEST(test_apdo_fields_are_isolated_from_each_other_and_neighbours);
    RUN_TEST(test_type_c_support_2h_decodes_as_try_snk);
    RUN_TEST(test_type_c_support_1h_decodes_as_try_src);
    RUN_TEST(test_try_src_with_sink_only_is_semantically_invalid);
    RUN_TEST(test_reserved_support_option_is_semantically_invalid);
    RUN_TEST(test_support_options_to_string);
    RUN_TEST(test_deprecated_drp_src_alias_matches_try_src);

    return UNITY_END();
}
