/**
 * @file test_TPS25751UsbPd.cpp
 * @brief Native (host-run) unit tests for the shared USB PD definitions in TPS25751UsbPd.h.
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751UsbPd"
 *
 * TX_SOURCE_CAPABILITIES_EXTENDED (0x77) and TRANSMIT_SINK_CAPABILITIES_EXTENDED
 * (0x7E) both decode the USB PD R3.3 load step rate and overload word through
 * these functions (Tables 6.59 and 6.70). The register suites test the decode
 * through each class; these tests pin the shared definitions and check that both
 * classes use the same LoadStep type.
 *
 * Overload word fixtures, decoded by hand:
 *   0x9ABC = 1001 1010 1011 1100b: bits 4:0 = 11100b = 28 (invalid, treated as
 *            25 = 250%), bits 10:5 = 010101b = 21 (420 ms), bits 14:11 = 0011b
 *            = 3 (15%), bit 15 = 1 (droop)
 *   0x7FF9 = bits 4:0 = 25 (250%), bits 10:5 = 63 (1260 ms), bits 14:11 = 15
 *            (75%), bit 15 = 0
 *   0x0001 = bits 4:0 = 1 (10%), everything else 0
 */

#include <unity.h>

#include <cstring>
#include <type_traits>

#include "TPS25751TransmitSinkCapsExtended.h"
#include "TPS25751TransmitSourceCapsExtended.h"
#include "TPS25751UsbPd.h"

using TPS25751UsbPd::LoadStep;

void setUp() {}
void tearDown() {}

void test_both_extended_caps_classes_share_one_load_step_type()
{
    TEST_ASSERT_TRUE((std::is_same<TPS25751TransmitSourceCapsExtended::LoadStep, LoadStep>::value));
    TEST_ASSERT_TRUE((std::is_same<TPS25751TransmitSinkCapsExtended::LoadStep, LoadStep>::value));
}

void test_load_step_from_raw_uses_bits_1_0()
{
    TEST_ASSERT_TRUE(LoadStep::Rate150mAPerUs == TPS25751UsbPd::loadStepFromRaw(0x00));
    TEST_ASSERT_TRUE(LoadStep::Rate500mAPerUs == TPS25751UsbPd::loadStepFromRaw(0x01));
    TEST_ASSERT_TRUE(LoadStep::Rate500mAPerUs == TPS25751UsbPd::loadStepFromRaw(0xFD));  // 111111 01b
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(TPS25751UsbPd::loadStepFromRaw(0x07)));
}

void test_load_step_text()
{
    TEST_ASSERT_EQUAL_STRING("150 mA/us", TPS25751UsbPd::toString(LoadStep::Rate150mAPerUs));
    TEST_ASSERT_EQUAL_STRING("500 mA/us", TPS25751UsbPd::toString(LoadStep::Rate500mAPerUs));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume 150 mA/us)", TPS25751UsbPd::toString(static_cast<LoadStep>(2)));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume 150 mA/us)", TPS25751UsbPd::toString(static_cast<LoadStep>(3)));
    // The class forwarders return the shared text.
    TEST_ASSERT_EQUAL_STRING("500 mA/us", TPS25751TransmitSourceCapsExtended::toString(LoadStep::Rate500mAPerUs));
    TEST_ASSERT_EQUAL_STRING("500 mA/us", TPS25751TransmitSinkCapsExtended::toString(LoadStep::Rate500mAPerUs));
}

void test_overload_word_with_every_field_set()
{
    TEST_ASSERT_EQUAL_UINT16(250, TPS25751UsbPd::overloadPercent(0x9ABC));
    TEST_ASSERT_EQUAL_UINT16(420, TPS25751UsbPd::overloadPeriod_ms(0x9ABC));
    TEST_ASSERT_EQUAL_UINT8(15, TPS25751UsbPd::overloadDutyCyclePercent(0x9ABC));
    TEST_ASSERT_TRUE(TPS25751UsbPd::overloadVbusDroop(0x9ABC));
}

void test_overload_word_maxima()
{
    TEST_ASSERT_EQUAL_UINT16(250, TPS25751UsbPd::overloadPercent(0x7FF9));
    TEST_ASSERT_EQUAL_UINT16(1260, TPS25751UsbPd::overloadPeriod_ms(0x7FF9));
    TEST_ASSERT_EQUAL_UINT8(75, TPS25751UsbPd::overloadDutyCyclePercent(0x7FF9));
    TEST_ASSERT_FALSE(TPS25751UsbPd::overloadVbusDroop(0x7FF9));
}

void test_overload_word_small_values()
{
    TEST_ASSERT_EQUAL_UINT16(10, TPS25751UsbPd::overloadPercent(0x0001));
    TEST_ASSERT_EQUAL_UINT16(0, TPS25751UsbPd::overloadPeriod_ms(0x0001));
    TEST_ASSERT_EQUAL_UINT8(0, TPS25751UsbPd::overloadDutyCyclePercent(0x0001));
    TEST_ASSERT_FALSE(TPS25751UsbPd::overloadVbusDroop(0x0001));
    TEST_ASSERT_EQUAL_UINT16(0, TPS25751UsbPd::overloadPercent(0x0000));
}

void test_both_classes_decode_the_same_word_alike()
{
    // 0x77 Peak Current 1 is TRM bytes 4 and 5; 0x7E Sink Load Characteristics
    // is TRM bytes 2 and 3. Put 0x9ABC in both.
    uint8_t src[15] = {};
    src[4] = 0xBC;
    src[5] = 0x9A;
    uint8_t sink[14] = {};
    sink[2] = 0xBC;
    sink[3] = 0x9A;
    const TPS25751TransmitSourceCapsExtended source(src, sizeof src);
    const TPS25751TransmitSinkCapsExtended sinkReg(sink, sizeof sink);

    TEST_ASSERT_EQUAL_UINT16(source.peakCurrentOverloadPercent(1), sinkReg.sinkLoadOverloadPercent());
    TEST_ASSERT_EQUAL_UINT16(source.peakCurrentOverloadPeriod_ms(1), sinkReg.sinkLoadOverloadPeriod_ms());
    TEST_ASSERT_EQUAL_UINT8(source.peakCurrentDutyCyclePercent(1), sinkReg.sinkLoadDutyCyclePercent());
    TEST_ASSERT_EQUAL(source.peakCurrentVbusDroop(1), sinkReg.sinkLoadVbusDroopTolerated());
    TEST_ASSERT_EQUAL_UINT16(250, sinkReg.sinkLoadOverloadPercent());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_both_extended_caps_classes_share_one_load_step_type);
    RUN_TEST(test_load_step_from_raw_uses_bits_1_0);
    RUN_TEST(test_load_step_text);
    RUN_TEST(test_overload_word_with_every_field_set);
    RUN_TEST(test_overload_word_maxima);
    RUN_TEST(test_overload_word_small_values);
    RUN_TEST(test_both_classes_decode_the_same_word_alike);
    return UNITY_END();
}
