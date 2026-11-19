/**
 * @file test_TPS25751ReceivedSinkCaps.cpp
 * @brief Native (host-run) unit tests for TPS25751ReceivedSinkCaps.
 *
 * Run with: pio test -e native
 *
 * RECEIVED_SINK_CAPABILITIES is 53 bytes: byte 0 bits 2:0 hold the
 * number of valid PDOs, and PDO n sits at byte 1 + (n-1)*4, little-endian.
 *
 * This suite covers the APDO subtype handling added after a bench read on
 * 2026-11-18 found an Augmented PDO with bits 29:28 = 10b (not the SPR PPS
 * subtype 00b) in TRANSMIT_SOURCE_CAPABILITIES. A partner that advertises such
 * an APDO lands in this register the same way.
 */

#include <unity.h>

#include "TPS25751ReceivedSinkCaps.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t CAPS_SIZE = TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES.size;  // 53

// Two PDOs: Fixed 5 V / 3 A (0x0001912C), then 0xE004B1F4, an Augmented PDO
// with APDO subtype 10b.
constexpr uint8_t FIXED_AND_APDO_SUBTYPE_2[CAPS_SIZE] = {
    0x02,
    0x2C, 0x91, 0x01, 0x00,
    0xF4, 0xB1, 0x04, 0xE0,
};

// Two PDOs: Fixed 5 V / 3 A, then an SPR PPS APDO 3.3-21 V / 3 A (0xC1A4213C).
constexpr uint8_t FIXED_AND_SPR_PPS[CAPS_SIZE] = {
    0x02,
    0x2C, 0x91, 0x01, 0x00,
    0x3C, 0x21, 0xA4, 0xC1,
};

// Same as FIXED_AND_SPR_PPS with min (bits 15:8) and max (bits 24:17) swapped
// so that min > max: min 21 V, max 3.3 V.
constexpr uint8_t FIXED_AND_BAD_SPR_PPS[CAPS_SIZE] = {
    0x02,
    0x2C, 0x91, 0x01, 0x00,
    0x3C, 0xD2, 0x42, 0xC0,
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751ReceivedSinkCaps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdoCount());
}

void test_non_pps_apdo_reports_subtype_and_no_pps_fields()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_APDO_SUBTYPE_2, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.getPDOType(1));
    TEST_ASSERT_EQUAL_UINT8(2, reg.getAPDOSubtype(1));
    TEST_ASSERT_FALSE(reg.isSprPpsPDO(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.getPPSMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.getPPSMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.getPPSMaxCurrent_mA(1));
}

void test_non_pps_apdo_is_semantically_valid()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_APDO_SUBTYPE_2, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_apdo_is_decoded()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_SPR_PPS, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSprPpsPDO(1));
    TEST_ASSERT_EQUAL_UINT8(0, reg.getAPDOSubtype(1));
    TEST_ASSERT_EQUAL_UINT16(21000, reg.getPPSMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(3300, reg.getPPSMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getPPSMaxCurrent_mA(1));
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_min_above_max_is_invalid()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_BAD_SPR_PPS, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSprPpsPDO(1));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_non_pps_apdo_reports_subtype_and_no_pps_fields);
    RUN_TEST(test_non_pps_apdo_is_semantically_valid);
    RUN_TEST(test_spr_pps_apdo_is_decoded);
    RUN_TEST(test_spr_pps_min_above_max_is_invalid);
    return UNITY_END();
}
