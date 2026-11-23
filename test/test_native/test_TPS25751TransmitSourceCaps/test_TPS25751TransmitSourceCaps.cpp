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
 * pdo(i), the TPS25751Pdo decode shared with TPS25751ReceivedSourceCaps.
 * Validation is validatePdoList() with PdoListRules::TransmitSource (USB PD R3.3).
 */

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "TPS25751TransmitSourceCaps.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t TX_SOURCE_CAPS_SIZE = TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES.size;  // 63

// Fixed 5 V / 3 A from the field layout: (100 << 10) | 300 = 0x0001912C.
constexpr uint32_t FIXED_5V_3A = (100u << 10) | 300u;

// Builds a register holding @p pdos (PDO 1 first) and the matching count.
TPS25751TransmitSourceCaps make(std::initializer_list<uint32_t> pdos)
{
    TPS25751TransmitSourceCaps reg;
    uint8_t i = 0;
    for (uint32_t w : pdos) reg.setPDO(i++, w);
    reg.setPDOCount(i);
    return reg;
}

bool contains(const std::string &hay, const char *needle)
{
    return hay.find(needle) != std::string::npos;
}

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

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.pdo(0).type());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(0).fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(0).fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbSuspendSupported());
    TEST_ASSERT_TRUE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRoleData());
    TEST_ASSERT_TRUE(reg.pdo(0).unchunkedExtendedMessages());
}

void test_fixed_supply_all_flags_false()
{
    TPS25751TransmitSourceCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000);  // all flags default false

    TEST_ASSERT_FALSE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_FALSE(reg.pdo(0).usbSuspendSupported());
    TEST_ASSERT_FALSE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_FALSE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_FALSE(reg.pdo(0).dualRoleData());
    TEST_ASSERT_FALSE(reg.pdo(0).unchunkedExtendedMessages());
}

void test_battery_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setBatteryPDO(1, 12000, 9000, 15000);
    reg.setPDOCount(2);

    TEST_ASSERT_EQUAL(PDOType::Battery, reg.pdo(1).type());
    TEST_ASSERT_EQUAL_UINT16(12000, reg.pdo(1).batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(9000, reg.pdo(1).batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(15000, reg.pdo(1).batteryPower_mW());
}

void test_variable_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setVariablePDO(2, 20000, 15000, 5000);
    reg.setPDOCount(3);

    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.pdo(2).type());
    TEST_ASSERT_EQUAL_UINT16(20000, reg.pdo(2).variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(15000, reg.pdo(2).variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(2).variableCurrent_mA());
}

void test_pps_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPPSPDO(3, 11000, 5000, 3000, true);
    reg.setPDOCount(4);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(3).type());
    TEST_ASSERT_EQUAL_UINT16(11000, reg.pdo(3).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(3).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(3).ppsCurrent_mA());
    TEST_ASSERT_TRUE(reg.pdo(3).ppsPowerLimited());
}

void test_spr_avs_round_trip()
{
    TPS25751TransmitSourceCaps reg;
    reg.setSprAvsPDO(4, 3000, 5000);
    reg.setPDOCount(5);

    TEST_ASSERT_EQUAL(PdoKind::SprAvs, reg.pdo(4).kind());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(4).avsCurrent15V_mA());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(4).avsCurrent20V_mA());
    TEST_ASSERT_EQUAL(PdoPeakCurrent::IoC, reg.pdo(4).avsPeakCurrent());
    TEST_ASSERT_EQUAL_UINT32(0xE004B1F4u, reg.getPDO(4));
}

void test_build_spr_avs_matches_hardware_word_and_peak_current()
{
    TEST_ASSERT_EQUAL_UINT32(0xE004B1F4u, TPS25751TransmitSourceCaps::buildSprAvsPDO(3000, 5000));
    // Peak Current 10b goes to bits 27:26.
    const uint32_t word = TPS25751TransmitSourceCaps::buildSprAvsPDO(3000, 5000, PdoPeakCurrent::Overload2);
    TEST_ASSERT_EQUAL_UINT32(0xE804B1F4u, word);
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

// --- APDO subtypes other than SPR PPS ---------------------------------------
//
// Bench read of 0x32 on 2026-11-18 (EEPROM configuration): five PDOs, PDO 5 =
// 0xE004B1F4. Bits 31:30 = 11b (Augmented) but bits 29:28 = 10b, not the SPR
// PPS subtype 00b. Decoding it as PPS gave a 17700 mV minimum and a 200 mV
// maximum, so the whole register failed semantic validation.
namespace {
constexpr uint8_t HW_0X32[TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES.size] = {
    0x05, 0xAA, 0x2A,        // 5 PDOs; Power Path bytes as the firmware wrote them
    0x2C, 0x91, 0x01, 0x20,  // PDO 1 0x2001912C: Fixed 5 V, 3 A
    0x2C, 0xD1, 0x02, 0x00,  // PDO 2 0x0002D12C: Fixed 9 V, 3 A
    0x2C, 0xB1, 0x04, 0x00,  // PDO 3 0x0004B12C: Fixed 15 V, 3 A
    0xF4, 0x41, 0x06, 0x00,  // PDO 4 0x000641F4: Fixed 20 V, 5 A
    0xF4, 0xB1, 0x04, 0xE0,  // PDO 5 0xE004B1F4: Augmented, APDO subtype 2
};
}  // namespace

void test_hardware_caps_with_non_pps_apdo_are_semantically_valid()
{
    TPS25751TransmitSourceCaps reg(HW_0X32, sizeof(HW_0X32));

    TEST_ASSERT_EQUAL_UINT8(5, reg.pdoCount());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_non_pps_apdo_reports_subtype_and_no_pps_fields()
{
    TPS25751TransmitSourceCaps reg(HW_0X32, sizeof(HW_0X32));

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(4).type());
    TEST_ASSERT_EQUAL_UINT8(2, reg.pdo(4).apdoType());
    TEST_ASSERT_EQUAL(PdoKind::SprAvs, reg.pdo(4).kind());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(4).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(4).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(4).ppsCurrent_mA());
    TEST_ASSERT_FALSE(reg.pdo(4).ppsPowerLimited());
    // A Fixed PDO is not an SPR PPS APDO either.
    TEST_ASSERT_FALSE(reg.pdo(0).kind() == PdoKind::SprPps);
}

void test_hardware_caps_decode_spr_avs_apdo()
{
    TPS25751TransmitSourceCaps reg(HW_0X32, sizeof(HW_0X32));

    TEST_ASSERT_EQUAL(PdoKind::SprAvs, reg.pdo(4).kind());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(4).avsCurrent15V_mA());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(4).avsCurrent20V_mA());
    TEST_ASSERT_EQUAL_UINT8(0x2, reg.getPowerPathForPDO(0));  // 0xAA: bits 9:8 = 10b
    TEST_ASSERT_TRUE(reg.pdo(0).dualRolePower());              // 0x2001912C: bit 29
    TEST_ASSERT_FALSE(reg.pdo(0).unconstrainedPower());
}

// --- TransmitSource rules (validatePdoList) ------------------------------------

void test_transmit_rejects_out_of_order_pdos()
{
    // Fixed, Variable, Battery: the order must be Fixed, Battery, Variable, AVS, PPS.
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (200u << 10) | 150u;
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, battery, variable}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, variable, battery}).isSemanticallyValid());
    // PPS before a Fixed PDO is out of order too.
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 0xC1A4213Cu, 0x0002D12Cu}).isSemanticallyValid());
}

void test_transmit_rejects_variable_pdo_below_80_percent()
{
    // 12 V max, 5 V min is 42% of the maximum.
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, variable}).isSemanticallyValid());
    // 12 V max, 9.6 V min is exactly 80%.
    const uint32_t atLimit = (2u << 30) | (240u << 20) | (192u << 10) | 150u;
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, atLimit}).isSemanticallyValid());
}

void test_transmit_rejects_avs_current_not_matching_fixed_pdos()
{
    // 15 V / 3 A and 20 V / 5 A Fixed PDOs, so the AVS currents must be 3 A and 5 A.
    const uint32_t f15 = 0x0004B12Cu;
    const uint32_t f20 = 0x000641F4u;
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, f15, f20,
                           TPS25751TransmitSourceCaps::buildSprAvsPDO(3000, 5000)}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, f15, f20,
                            TPS25751TransmitSourceCaps::buildSprAvsPDO(2000, 5000)}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, f15, f20,
                            TPS25751TransmitSourceCaps::buildSprAvsPDO(3000, 4000)}).isSemanticallyValid());
    // Without a 20 V Fixed PDO the 20 V current must be 0.
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, f15,
                            TPS25751TransmitSourceCaps::buildSprAvsPDO(3000, 5000)}).isSemanticallyValid());
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, f15,
                           TPS25751TransmitSourceCaps::buildSprAvsPDO(3000, 0)}).isSemanticallyValid());
}

void test_spr_pps_apdo_is_still_decoded_and_checked()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDO(0, TPS25751TransmitSourceCaps::buildFixedSupplyPDO(5000, 3000));
    reg.setPDO(1, TPS25751TransmitSourceCaps::buildPPSPDO(11000, 5000, 3000));
    reg.setPDOCount(2);

    TEST_ASSERT_EQUAL(PdoKind::SprPps, reg.pdo(1).kind());
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdo(1).apdoType());
    TEST_ASSERT_EQUAL_UINT16(11000, reg.pdo(1).ppsMaxVoltage_mV());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());

    // The PPS min <= max rule still applies to subtype 00b.
    reg.setPDO(1, TPS25751TransmitSourceCaps::buildPPSPDO(5000, 11000, 3000));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Decode of raw words and bounds -------------------------------------------

void test_get_pdo_bounds_and_short_buffer()
{
    TPS25751TransmitSourceCaps reg = make({FIXED_5V_3A});
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, reg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(7));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(255));

    // PDO 1 sits at bytes 3..6; a 10-byte buffer holds it but not PDO 2 (7..10).
    const uint8_t shortBuf[10] = {0x02, 0x00, 0x00, 0x2C, 0x91, 0x01, 0x00, 0x01, 0x02, 0x03};
    TPS25751TransmitSourceCaps shortReg(shortBuf, sizeof(shortBuf));
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, shortReg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0, shortReg.getPDO(1));
}

void test_fixed_flags_decode()
{
    // PDO 1 carries every flag bit (29:24); PDO 2 (9 V / 2.5 A) has the same bits set.
    const uint32_t flagBits = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                              (1u << 25) | (1u << 24);
    TPS25751TransmitSourceCaps reg = make({FIXED_5V_3A | flagBits, flagBits | (180u << 10) | 250u});

    TEST_ASSERT_EQUAL_UINT16(9000, reg.pdo(1).fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2500, reg.pdo(1).fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbSuspendSupported());
    TEST_ASSERT_TRUE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRoleData());
    TEST_ASSERT_TRUE(reg.pdo(0).unchunkedExtendedMessages());

    // Only PDO 1 carries the flags (Table 6.10): PDO 2 reads false.
    TEST_ASSERT_FALSE(reg.pdo(1).dualRolePower());
    TEST_ASSERT_FALSE(reg.pdo(1).usbSuspendSupported());
    TEST_ASSERT_FALSE(reg.pdo(1).unconstrainedPower());
    TEST_ASSERT_FALSE(reg.pdo(1).usbCommunicationsCapable());
    TEST_ASSERT_FALSE(reg.pdo(1).dualRoleData());
    TEST_ASSERT_FALSE(reg.pdo(1).unchunkedExtendedMessages());
}

void test_battery_and_variable_fields_decode()
{
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;   // 20 V, 5 V, 60 W
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;  // 12 V, 5 V, 1.5 A
    TPS25751TransmitSourceCaps reg = make({FIXED_5V_3A, battery, variable});  // decode only; not validated

    TEST_ASSERT_EQUAL(PDOType::Battery, reg.pdo(1).type());
    TEST_ASSERT_EQUAL_UINT16(20000, reg.pdo(1).batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(1).batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(60000, reg.pdo(1).batteryPower_mW());
    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.pdo(2).type());
    TEST_ASSERT_EQUAL_UINT16(12000, reg.pdo(2).variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(2).variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(1500, reg.pdo(2).variableCurrent_mA());
}

void test_pps_power_limited_flag_decodes()
{
    // 0xC9A4213C = SPR PPS 3.3-21 V / 3 A with Power Limited (bit 27) set.
    TPS25751TransmitSourceCaps reg = make({FIXED_5V_3A, 0xC9A4213Cu, 0xC1A4213Cu});

    TEST_ASSERT_TRUE(reg.pdo(1).ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, reg.pdo(1).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, reg.pdo(1).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(1).ppsCurrent_mA());
    TEST_ASSERT_FALSE(reg.pdo(2).ppsPowerLimited());
}

void test_stale_count_with_zero_pdo1_means_no_caps()
{
    TPS25751TransmitSourceCaps reg;
    reg.setPDOCount(5);

    TEST_ASSERT_EQUAL_UINT8(5, reg.pdoCount());
    TEST_ASSERT_FALSE(reg.hasSourceCaps());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_no_caps()
{
    TPS25751TransmitSourceCaps reg;
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Transmit Source Capabilities Register (0x32)"));
    TEST_ASSERT_TRUE(contains(out, "PDO Count: 0"));
    TEST_ASSERT_TRUE(contains(out, "No source capabilities configured"));
}

void test_debug_print_all_pdo_types()
{
    const uint32_t fixed = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                           (1u << 25) | (1u << 24) | (100u << 10) | 300u;
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    TPS25751TransmitSourceCaps reg = make({fixed, battery, variable, 0xC9A4213Cu, 0xE004B1F4u});
    reg.setPowerPathForPDO(0, 2);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "PDO Count: 5"));
    TEST_ASSERT_TRUE(contains(out, "--- PDO 5 ---"));
    TEST_ASSERT_TRUE(contains(out, "Power Path: 0x2"));
    TEST_ASSERT_TRUE(contains(out, "Type: Fixed Supply"));
    TEST_ASSERT_TRUE(contains(out, "Voltage: 5000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "USB Suspend Supported: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "USB Communications Capable: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Data: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Unchunked Extended Messages: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Type: Battery"));
    TEST_ASSERT_TRUE(contains(out, "Max Power: 60000 mW"));
    TEST_ASSERT_TRUE(contains(out, "Type: Variable Supply"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 12000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 1500 mA"));
    TEST_ASSERT_TRUE(contains(out, "Type: SPR PPS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Power Limited: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 21000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Min Voltage: 3300 mV"));
    TEST_ASSERT_TRUE(contains(out, "Type: SPR AVS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Max Current 9 to 15 V: 3000 mA"));
}

void test_debug_print_fixed_flags_clear_print_no()
{
    TPS25751TransmitSourceCaps reg = make({FIXED_5V_3A});
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Power Path: 0x0"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Power: No"));
    TEST_ASSERT_TRUE(contains(out, "USB Suspend Supported: No"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: No"));
    TEST_ASSERT_TRUE(contains(out, "USB Communications Capable: No"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Data: No"));
    TEST_ASSERT_TRUE(contains(out, "Unchunked Extended Messages: No"));
}

// --- Semantic validation ------------------------------------------------------

void test_semantic_valid_mixed_caps()
{
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (200u << 10) | 150u;  // 12 V max, 10 V min
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, battery, variable, 0xC1A4213Cu}).isSemanticallyValid());
}

void test_semantic_rejects_all_ones()
{
    uint8_t ones[TX_SOURCE_CAPS_SIZE];
    memset(ones, 0xFF, sizeof(ones));
    TEST_ASSERT_FALSE(TPS25751TransmitSourceCaps(ones, TX_SOURCE_CAPS_SIZE).isSemanticallyValid());
}

void test_semantic_fixed_rules()
{
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 300u}).isSemanticallyValid());        // zero voltage
    // Zero current is valid (Tables 6.8 to 6.10 allow 0); the old rule rejected it.
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, 180u << 10}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, (180u << 10) | 501u}).isSemanticallyValid());  // above 5 A

    // PDO 1 must be exactly 5 V; the old 4.75 to 5.25 V band is gone.
    TEST_ASSERT_TRUE(make({(100u << 10) | 300u}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({(95u << 10) | 300u}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({(105u << 10) | 300u}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({(180u << 10) | 300u}).isSemanticallyValid());
}

void test_semantic_battery_rules()
{
    const uint32_t b = (1u << 30);
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, b | (100u << 20) | (400u << 10) | 240u}).isSemanticallyValid());   // min > max: valid
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, b | (400u << 20) | 240u}).isSemanticallyValid());                  // zero min
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, b | (100u << 10) | 240u}).isSemanticallyValid());                  // zero max
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, b | (400u << 20) | (100u << 10)}).isSemanticallyValid());           // zero power: valid
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, b | (400u << 20) | (100u << 10) | 401u}).isSemanticallyValid());   // above 100 W
}

void test_semantic_variable_rules()
{
    const uint32_t v = (2u << 30);
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, v | (100u << 20) | (240u << 10) | 150u}).isSemanticallyValid());   // min > max: passes the 80% rule
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, v | (240u << 20) | 150u}).isSemanticallyValid());                  // zero min
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, v | (100u << 10) | 150u}).isSemanticallyValid());                  // zero max
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, v | (240u << 20) | (200u << 10)}).isSemanticallyValid());           // zero current: valid
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, v | (240u << 20) | (200u << 10) | 501u}).isSemanticallyValid());   // above 5 A
}

void test_semantic_pps_rules()
{
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, 0xC1A42100u}).isSemanticallyValid());   // 3.3-21 V, 0 A: valid
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 0xC1A4003Cu}).isSemanticallyValid());  // zero min voltage
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 0xC042D23Cu}).isSemanticallyValid());  // max 3.3 V is not 11, 16, or 21 V
}

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
    RUN_TEST(test_spr_avs_round_trip);
    RUN_TEST(test_build_spr_avs_matches_hardware_word_and_peak_current);
    RUN_TEST(test_pps_subtype_bits_are_zero);
    RUN_TEST(test_pps_units_are_100mv_and_50ma_not_50mv);
    RUN_TEST(test_hardware_caps_with_non_pps_apdo_are_semantically_valid);
    RUN_TEST(test_non_pps_apdo_reports_subtype_and_no_pps_fields);
    RUN_TEST(test_hardware_caps_decode_spr_avs_apdo);
    RUN_TEST(test_transmit_rejects_out_of_order_pdos);
    RUN_TEST(test_transmit_rejects_variable_pdo_below_80_percent);
    RUN_TEST(test_transmit_rejects_avs_current_not_matching_fixed_pdos);
    RUN_TEST(test_spr_pps_apdo_is_still_decoded_and_checked);

    RUN_TEST(test_fixed_voltage_overflow_does_not_corrupt_type_bits);
    RUN_TEST(test_pps_current_overflow_does_not_corrupt_neighbouring_bits);

    RUN_TEST(test_type_bits_for_all_builders);

    RUN_TEST(test_set_pdo_out_of_range_index_is_noop);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);

    RUN_TEST(test_get_pdo_bounds_and_short_buffer);
    RUN_TEST(test_fixed_flags_decode);
    RUN_TEST(test_battery_and_variable_fields_decode);
    RUN_TEST(test_pps_power_limited_flag_decodes);
    RUN_TEST(test_stale_count_with_zero_pdo1_means_no_caps);
    RUN_TEST(test_debug_print_no_caps);
    RUN_TEST(test_debug_print_all_pdo_types);
    RUN_TEST(test_debug_print_fixed_flags_clear_print_no);
    RUN_TEST(test_semantic_valid_mixed_caps);
    RUN_TEST(test_semantic_rejects_all_ones);
    RUN_TEST(test_semantic_fixed_rules);
    RUN_TEST(test_semantic_battery_rules);
    RUN_TEST(test_semantic_variable_rules);
    RUN_TEST(test_semantic_pps_rules);
    return UNITY_END();
}
