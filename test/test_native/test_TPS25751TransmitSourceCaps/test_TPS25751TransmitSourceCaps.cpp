/**
 * @file test_TPS25751TransmitSourceCaps.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitSourceCaps.
 *
 * Run with: pio test -e native
 *
 * TRANSMIT_SOURCE_CAPABILITIES (0x32) is 63 bytes:
 *   Byte 0, bits 2:0   Number Valid PDOs (0-7); bits 7:3 reserved
 *   Byte 1, bits 1:0   Power Path for PDO 1 (register bits 9:8;
 *                      0h = PP5V, 2h = PP_EXT)
 *   Bits 23:10         Reserved (byte 1 bits 7:2, byte 2). Rev. A used
 *                      them for Power Path for PDO 2-7; Source: SLVUCR8B
 *                      (Rev. B, 2026-08), page 36.
 *   Bytes 3..30        PDO 1..7, 4 bytes each, little-endian
 *                      (PDO n at byte 3 + (n-1)*4)
 *
 * This is the library's first PDO ENCODE path; these tests exercise the
 * static PDO word builders directly and round-trip them through setPDO() and
 * the decode getters mirrored from TPS25751ReceivedSourceCaps.
 */

#include <unity.h>
#include <cstring>

#include "TPS25751TransmitSourceCaps.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t TX_SOURCE_CAPS_SIZE = TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES.size;  // 63

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation -----------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751TransmitSourceCaps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(TX_SOURCE_CAPS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(TX_SOURCE_CAPS_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdoCount());
    TEST_ASSERT_FALSE(reg.hasSourceCaps());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751TransmitSourceCaps reg(TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(TX_SOURCE_CAPS_SIZE, reg.size());
}

void test_explicit_length_constructor()
{
    uint8_t data[TX_SOURCE_CAPS_SIZE] = {0};
    data[0] = 0x02;  // 2 valid PDOs
    TPS25751TransmitSourceCaps reg(data, TX_SOURCE_CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT8(2, reg.pdoCount());
}

void test_empty_caps_is_semantically_valid()
{
    TPS25751TransmitSourceCaps reg;  // all zero
    TEST_ASSERT_FALSE(reg.hasSourceCaps());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t oversized[4] = {0, 0, 0, 0};
    TPS25751TransmitSourceCaps reg(oversized, sizeof(oversized));

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.isValidSize(TX_SOURCE_CAPS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Byte-offset placement ----------------------------------------------------

void test_pdo0_lands_at_byte3()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDO(0, 0x11223344);

    TEST_ASSERT_EQUAL_UINT8(0x44, reg.raw()[3]);
    TEST_ASSERT_EQUAL_UINT8(0x33, reg.raw()[4]);
    TEST_ASSERT_EQUAL_UINT8(0x22, reg.raw()[5]);
    TEST_ASSERT_EQUAL_UINT8(0x11, reg.raw()[6]);
    TEST_ASSERT_EQUAL_UINT32(0x11223344, reg.getPDO(0));
}

void test_pdo1_lands_4_bytes_after_pdo0()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDO(0, 0xAAAAAAAA);
    reg.setPDO(1, 0x55667788);

    TEST_ASSERT_EQUAL_UINT8(0x88, reg.raw()[7]);
    TEST_ASSERT_EQUAL_UINT8(0x77, reg.raw()[8]);
    TEST_ASSERT_EQUAL_UINT8(0x66, reg.raw()[9]);
    TEST_ASSERT_EQUAL_UINT8(0x55, reg.raw()[10]);
    TEST_ASSERT_EQUAL_UINT32(0x55667788, reg.getPDO(1));
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAA, reg.getPDO(0));  // untouched
}

void test_setting_pdo0_does_not_disturb_power_path_bytes()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPowerPathForPDO(0, 0x2);

    reg.setPDO(0, 0xFFFFFFFF);

    // Bytes 1-2 (power path + reserved) must be untouched by a PDO write.
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.getPowerPathForPDO(0));
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[2]);
}

void test_setting_power_path_does_not_disturb_pdo0()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDO(0, 0x11223344);

    reg.setPowerPathForPDO(0, 0x2);

    TEST_ASSERT_EQUAL_UINT32(0x11223344, reg.getPDO(0));
}

// --- setPDOCount / pdoCount round-trip + reserved-bit preservation -----------

void test_set_pdo_count_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDOCount(5);
    TEST_ASSERT_EQUAL_UINT8(5, reg.pdoCount());
}

void test_set_pdo_count_preserves_reserved_bits()
{
    uint8_t fullOnes[TX_SOURCE_CAPS_SIZE];
    memset(fullOnes, 0xFF, sizeof(fullOnes));
    TPS25751TransmitSourceCaps reg(fullOnes, TX_SOURCE_CAPS_SIZE);

    reg.setPDOCount(3);

    TEST_ASSERT_EQUAL_UINT8(3, reg.pdoCount());
    // Reserved bits 7:3 of byte 0 must remain set (0xF8 | 0x03 == 0xFB).
    TEST_ASSERT_EQUAL_UINT8(0xFB, reg.raw()[0]);
}

// --- Power Path round-trip + reserved-bit preservation -----------------------

void test_power_path_pdo1_round_trip()
{
    TPS25751TransmitSourceCaps reg;

    reg.setPowerPathForPDO(0, 0x2);  // PP_EXT
    TEST_ASSERT_EQUAL_UINT8(0x2, reg.getPowerPathForPDO(0));
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.raw()[1]);  // bits 9:8 = byte 1 bits 1:0

    reg.setPowerPathForPDO(0, 0x0);  // PP5V
    TEST_ASSERT_EQUAL_UINT8(0x0, reg.getPowerPathForPDO(0));
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[1]);
}

void test_power_path_pdo1_preserves_reserved_bits_23_10()
{
    uint8_t fullOnes[TX_SOURCE_CAPS_SIZE];
    memset(fullOnes, 0xFF, sizeof(fullOnes));
    TPS25751TransmitSourceCaps reg(fullOnes, TX_SOURCE_CAPS_SIZE);

    reg.setPowerPathForPDO(0, 0x0);

    TEST_ASSERT_EQUAL_UINT8(0x0, reg.getPowerPathForPDO(0));
    TEST_ASSERT_EQUAL_UINT8(0xFC, reg.raw()[1]);  // bits 15:10 still set
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.raw()[2]);  // bits 23:16 still set
}

void test_power_path_index_1_to_6_set_is_no_op()
{
    // Rev. B marks bits 23:10 reserved (SLVUCR8B p.36): writes for PDO 2-7
    // must not touch the buffer, whatever the starting contents.
    uint8_t fullOnes[TX_SOURCE_CAPS_SIZE];
    memset(fullOnes, 0xFF, sizeof(fullOnes));
    TPS25751TransmitSourceCaps ones(fullOnes, TX_SOURCE_CAPS_SIZE);
    TPS25751TransmitSourceCaps zeros;

    for (uint8_t index = 1; index <= 6; index++) {
        ones.setPowerPathForPDO(index, 0x0);
        zeros.setPowerPathForPDO(index, 0x3);
    }
    ones.setPowerPathForPDO(7, 0x0);    // out of range entirely
    zeros.setPowerPathForPDO(255, 0x3);

    TEST_ASSERT_EACH_EQUAL_UINT8(0xFF, ones.raw(), TX_SOURCE_CAPS_SIZE);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x00, zeros.raw(), TX_SOURCE_CAPS_SIZE);
}

void test_power_path_index_1_to_6_get_returns_zero()
{
    uint8_t fullOnes[TX_SOURCE_CAPS_SIZE];
    memset(fullOnes, 0xFF, sizeof(fullOnes));
    TPS25751TransmitSourceCaps reg(fullOnes, TX_SOURCE_CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(0x3, reg.getPowerPathForPDO(0));
    for (uint8_t index = 1; index <= 6; index++) {
        TEST_ASSERT_EQUAL_UINT8(0, reg.getPowerPathForPDO(index));
    }
}

// --- Encode<->decode round trip for all four PDO types -----------------------

void test_fixed_supply_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000, true, true, true, true, true, true);

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.getPDOType(0));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getFixedVoltage_mV(0));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getFixedMaxCurrent_mA(0));
    TEST_ASSERT_TRUE(reg.getFixedDualRolePower(0));
    TEST_ASSERT_TRUE(reg.getFixedUSBSuspendSupported(0));
    TEST_ASSERT_TRUE(reg.getFixedUnconstrainedPower(0));
    TEST_ASSERT_TRUE(reg.getFixedUSBCommCapable(0));
    TEST_ASSERT_TRUE(reg.getFixedDualRoleData(0));
    TEST_ASSERT_TRUE(reg.getFixedUnchunkedSupported(0));
}

void test_fixed_supply_all_flags_false()
{
    TPS25751TransmitSourceCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000);  // all flags default false

    TEST_ASSERT_FALSE(reg.getFixedDualRolePower(0));
    TEST_ASSERT_FALSE(reg.getFixedUSBSuspendSupported(0));
    TEST_ASSERT_FALSE(reg.getFixedUnconstrainedPower(0));
    TEST_ASSERT_FALSE(reg.getFixedUSBCommCapable(0));
    TEST_ASSERT_FALSE(reg.getFixedDualRoleData(0));
    TEST_ASSERT_FALSE(reg.getFixedUnchunkedSupported(0));
}

void test_battery_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setBatteryPDO(1, 12000, 9000, 15000);
    reg.setPDOCount(2);

    TEST_ASSERT_EQUAL(PDOType::Battery, reg.getPDOType(1));
    TEST_ASSERT_EQUAL_UINT16(12000, reg.getBatteryMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(9000, reg.getBatteryMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT32(15000, reg.getBatteryMaxPower_mW(1));
}

void test_variable_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setVariablePDO(2, 20000, 15000, 5000);
    reg.setPDOCount(3);

    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.getPDOType(2));
    TEST_ASSERT_EQUAL_UINT16(20000, reg.getVariableMaxVoltage_mV(2));
    TEST_ASSERT_EQUAL_UINT16(15000, reg.getVariableMinVoltage_mV(2));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getVariableMaxCurrent_mA(2));
}

void test_pps_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPPSPDO(3, 11000, 5000, 3000, true);
    reg.setPDOCount(4);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.getPDOType(3));
    TEST_ASSERT_EQUAL_UINT16(11000, reg.getPPSMaxVoltage_mV(3));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getPPSMinVoltage_mV(3));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getPPSMaxCurrent_mA(3));
    TEST_ASSERT_TRUE(reg.getPPSPowerLimited(3));
}

void test_pps_subtype_bits_are_zero()
{
    uint32_t pdo = TPS25751TransmitSourceCaps::buildPPSPDO(11000, 5000, 3000, false);
    // Bits 29:28 must be 00 (SPR PPS APDO subtype).
    TEST_ASSERT_EQUAL_UINT32(0, (pdo >> 28) & 0x3);
    // Bits 31:30 must be 11 (Augmented).
    TEST_ASSERT_EQUAL_UINT32(0x3, (pdo >> 30) & 0x3);
}

void test_pps_units_are_100mv_and_50ma_not_50mv()
{
    // 11000 mV / 100 = 110 -> bits 24:17; 5000 mV / 100 = 50 -> bits 15:8;
    // 3000 mA / 50 = 60 -> bits 6:0. If the (wrong) 50 mV/LSB unit were used
    // instead, 11000/50 = 220 would overflow the 8-bit field -- so an exact
    // round trip here proves the 100 mV / 50 mA units are in effect.
    uint32_t pdo = TPS25751TransmitSourceCaps::buildPPSPDO(11000, 5000, 3000, false);
    TEST_ASSERT_EQUAL_UINT32(110, (pdo >> 17) & 0xFF);
    TEST_ASSERT_EQUAL_UINT32(50, (pdo >> 8) & 0xFF);
    TEST_ASSERT_EQUAL_UINT32(60, pdo & 0x7F);
}

// --- Field masking: over-range values must not bleed into type bits ---------

void test_fixed_voltage_overflow_does_not_corrupt_type_bits()
{
    // 10-bit voltage field maxes out at 0x3FF * 50 = 51150 mV. Request far
    // more (65000 mV -> raw 1300, which needs 11 bits) and confirm the type
    // bits (31:30) are still 00 (Fixed Supply) afterward.
    uint32_t pdo = TPS25751TransmitSourceCaps::buildFixedSupplyPDO(65000, 3000);
    TEST_ASSERT_EQUAL_UINT32(0, (pdo >> 30) & 0x3);
}

void test_pps_current_overflow_does_not_corrupt_neighbouring_bits()
{
    // 7-bit current field maxes at 0x7F * 50 = 6350 mA. Request 10000 mA
    // (raw 200, needs 8 bits) and confirm bit 7 (part of the min-voltage
    // field) is not corrupted by the overflowed current field.
    uint32_t pdoOverflow = TPS25751TransmitSourceCaps::buildPPSPDO(11000, 100, 10000, false);
    uint32_t pdoBaseline = TPS25751TransmitSourceCaps::buildPPSPDO(11000, 100, 0, false);
    TEST_ASSERT_EQUAL_UINT32((pdoBaseline >> 8) & 0xFF, (pdoOverflow >> 8) & 0xFF);
}

// --- Type bit assertions for all four builders -------------------------------

void test_type_bits_for_all_builders()
{
    TEST_ASSERT_EQUAL_UINT32(0x0, (TPS25751TransmitSourceCaps::buildFixedSupplyPDO(5000, 3000) >> 30) & 0x3);
    TEST_ASSERT_EQUAL_UINT32(0x1, (TPS25751TransmitSourceCaps::buildBatteryPDO(12000, 9000, 15000) >> 30) & 0x3);
    TEST_ASSERT_EQUAL_UINT32(0x2, (TPS25751TransmitSourceCaps::buildVariablePDO(20000, 15000, 5000) >> 30) & 0x3);
    TEST_ASSERT_EQUAL_UINT32(0x3, (TPS25751TransmitSourceCaps::buildPPSPDO(11000, 5000, 3000) >> 30) & 0x3);
}

// --- setPDO bounds / no-op behavior ------------------------------------------

void test_set_pdo_out_of_range_index_is_noop()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDO(7, 0xDEADBEEF);  // index 7 is out of range (0-6 valid)
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(7));  // getPDO also treats 7 as out of range -> 0
}

void test_setters_no_op_on_invalid_register()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDO(0, 0x11223344);
    TPS25751TransmitSourceCaps moved(static_cast<TPS25751TransmitSourceCaps &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setPDO(1, 0xAABBCCDD);
    reg.setPDOCount(5);
    reg.setPowerPathForPDO(0, 0x2);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash

    TEST_ASSERT_EQUAL_UINT32(0x11223344, moved.getPDO(0));  // untouched
}

// --- Rule of Five (compiler-generated via base class) ------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751TransmitSourceCaps original;
    original.setPDO(0, 0x11223344);

    TPS25751TransmitSourceCaps copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), TX_SOURCE_CAPS_SIZE);
    TEST_ASSERT_EQUAL_UINT32(original.getPDO(0), copy.getPDO(0));
}

// --- Runner -------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_empty_caps_is_semantically_valid);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_pdo0_lands_at_byte3);
    RUN_TEST(test_pdo1_lands_4_bytes_after_pdo0);
    RUN_TEST(test_setting_pdo0_does_not_disturb_power_path_bytes);
    RUN_TEST(test_setting_power_path_does_not_disturb_pdo0);

    RUN_TEST(test_set_pdo_count_round_trip);
    RUN_TEST(test_set_pdo_count_preserves_reserved_bits);

    RUN_TEST(test_power_path_pdo1_round_trip);
    RUN_TEST(test_power_path_pdo1_preserves_reserved_bits_23_10);
    RUN_TEST(test_power_path_index_1_to_6_set_is_no_op);
    RUN_TEST(test_power_path_index_1_to_6_get_returns_zero);

    RUN_TEST(test_fixed_supply_round_trip);
    RUN_TEST(test_fixed_supply_all_flags_false);
    RUN_TEST(test_battery_round_trip);
    RUN_TEST(test_variable_round_trip);
    RUN_TEST(test_pps_round_trip);
    RUN_TEST(test_pps_subtype_bits_are_zero);
    RUN_TEST(test_pps_units_are_100mv_and_50ma_not_50mv);

    RUN_TEST(test_fixed_voltage_overflow_does_not_corrupt_type_bits);
    RUN_TEST(test_pps_current_overflow_does_not_corrupt_neighbouring_bits);

    RUN_TEST(test_type_bits_for_all_builders);

    RUN_TEST(test_set_pdo_out_of_range_index_is_noop);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);

    return UNITY_END();
}
