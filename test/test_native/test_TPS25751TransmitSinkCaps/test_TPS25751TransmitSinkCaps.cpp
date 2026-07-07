/**
 * @file test_TPS25751TransmitSinkCaps.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitSinkCaps.
 *
 * Run with: pio test -e native
 *
 * TRANSMIT_SINK_CAPABILITIES (0x33) is 53 bytes, identical in shape to
 * TPS25751ReceivedSinkCaps (no Power Path bytes, unlike the Source register):
 *   Byte 0, bits 2:0   Number Valid PDOs (0-7); bits 7:3 reserved
 *   Bytes 1..24        PDO 1..7, 4 bytes each, little-endian
 *                      (PDO n at byte 1 + (n-1)*4)
 *
 * This is the library's second PDO ENCODE path (alongside
 * TPS25751TransmitSourceCaps); these tests exercise the static PDO word
 * builders directly and round-trip them through setPDO() and the decode
 * getters mirrored from TPS25751ReceivedSinkCaps.
 */

#include <unity.h>
#include <cstring>

#include "TPS25751TransmitSinkCaps.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t TX_SINK_CAPS_SIZE = TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES.size;  // 53

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation -----------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751TransmitSinkCaps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(TX_SINK_CAPS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(TX_SINK_CAPS_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdoCount());
    TEST_ASSERT_FALSE(reg.hasSinkCaps());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751TransmitSinkCaps reg(TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(TX_SINK_CAPS_SIZE, reg.size());
}

void test_explicit_length_constructor()
{
    uint8_t data[TX_SINK_CAPS_SIZE] = {0};
    data[0] = 0x03;  // 3 valid PDOs
    TPS25751TransmitSinkCaps reg(data, TX_SINK_CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT8(3, reg.pdoCount());
}

void test_empty_caps_is_semantically_valid()
{
    TPS25751TransmitSinkCaps reg;  // all zero
    TEST_ASSERT_FALSE(reg.hasSinkCaps());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t oversized[4] = {0, 0, 0, 0};
    TPS25751TransmitSinkCaps reg(oversized, sizeof(oversized));

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.isValidSize(TX_SINK_CAPS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Byte-offset placement ----------------------------------------------------

void test_pdo0_lands_at_byte1()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDO(0, 0x11223344);

    TEST_ASSERT_EQUAL_UINT8(0x44, reg.raw()[1]);
    TEST_ASSERT_EQUAL_UINT8(0x33, reg.raw()[2]);
    TEST_ASSERT_EQUAL_UINT8(0x22, reg.raw()[3]);
    TEST_ASSERT_EQUAL_UINT8(0x11, reg.raw()[4]);
    TEST_ASSERT_EQUAL_UINT32(0x11223344, reg.getPDO(0));
}

void test_pdo1_lands_4_bytes_after_pdo0()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDO(0, 0xAAAAAAAA);
    reg.setPDO(1, 0x55667788);

    TEST_ASSERT_EQUAL_UINT8(0x88, reg.raw()[5]);
    TEST_ASSERT_EQUAL_UINT8(0x77, reg.raw()[6]);
    TEST_ASSERT_EQUAL_UINT8(0x66, reg.raw()[7]);
    TEST_ASSERT_EQUAL_UINT8(0x55, reg.raw()[8]);
    TEST_ASSERT_EQUAL_UINT32(0x55667788, reg.getPDO(1));
    TEST_ASSERT_EQUAL_UINT32(0xAAAAAAAA, reg.getPDO(0));  // untouched
}

// --- setPDOCount / pdoCount round-trip + reserved-bit preservation -----------

void test_set_pdo_count_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDOCount(5);
    TEST_ASSERT_EQUAL_UINT8(5, reg.pdoCount());
}

void test_set_pdo_count_preserves_reserved_bits()
{
    uint8_t fullOnes[TX_SINK_CAPS_SIZE];
    memset(fullOnes, 0xFF, sizeof(fullOnes));
    TPS25751TransmitSinkCaps reg(fullOnes, TX_SINK_CAPS_SIZE);

    reg.setPDOCount(3);

    TEST_ASSERT_EQUAL_UINT8(3, reg.pdoCount());
    // Reserved bits 7:3 of byte 0 must remain set (0xF8 | 0x03 == 0xFB).
    TEST_ASSERT_EQUAL_UINT8(0xFB, reg.raw()[0]);
}

// --- Encode<->decode round trip for all four PDO types -----------------------

void test_fixed_supply_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000, true, true, true, true, true, 0x2);

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.getPDOType(0));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getFixedVoltage_mV(0));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getFixedMaxCurrent_mA(0));
    TEST_ASSERT_TRUE(reg.getFixedDualRolePower(0));
    TEST_ASSERT_TRUE(reg.getFixedHigherCapability(0));
    TEST_ASSERT_TRUE(reg.getFixedUnconstrainedPower(0));
    TEST_ASSERT_TRUE(reg.getFixedUSBCommCapable(0));
    TEST_ASSERT_TRUE(reg.getFixedDualRoleData(0));
    TEST_ASSERT_EQUAL_UINT8(0x2, reg.getFixedFastRoleSwapCurrent(0));
}

void test_higher_capability_bit_is_not_misread_as_usb_suspend()
{
    // Sink Fixed PDO bit 28 is "Higher Capability", NOT "USB Suspend
    // Supported" (that's the Source variant's bit 28). Set only bit 28 via
    // higherCapability=true and confirm it reads back through the Sink
    // accessor -- the Sink class has no getFixedUSBSuspendSupported() at
    // all, so this is really asserting the bit position is correct.
    TPS25751TransmitSinkCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000, false, true, false, false, false, 0);

    TEST_ASSERT_TRUE(reg.getFixedHigherCapability(0));
    TEST_ASSERT_EQUAL_UINT32(1u, (reg.getPDO(0) >> 28) & 0x1);
}

void test_fast_role_swap_current_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000, false, false, false, false, false, 0x3);
    TEST_ASSERT_EQUAL_UINT8(0x3, reg.getFixedFastRoleSwapCurrent(0));

    reg.setFixedSupplyPDO(0, 5000, 3000, false, false, false, false, false, 0x1);
    TEST_ASSERT_EQUAL_UINT8(0x1, reg.getFixedFastRoleSwapCurrent(0));
}

void test_battery_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setBatteryPDO(1, 12000, 9000, 15000);
    reg.setPDOCount(2);

    TEST_ASSERT_EQUAL(PDOType::Battery, reg.getPDOType(1));
    TEST_ASSERT_EQUAL_UINT16(12000, reg.getBatteryMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(9000, reg.getBatteryMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT32(15000, reg.getBatteryMaxPower_mW(1));
}

void test_variable_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setVariablePDO(2, 20000, 15000, 5000);
    reg.setPDOCount(3);

    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.getPDOType(2));
    TEST_ASSERT_EQUAL_UINT16(20000, reg.getVariableMaxVoltage_mV(2));
    TEST_ASSERT_EQUAL_UINT16(15000, reg.getVariableMinVoltage_mV(2));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getVariableMaxCurrent_mA(2));
}

void test_pps_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPPSPDO(3, 11000, 5000, 3000);
    reg.setPDOCount(4);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.getPDOType(3));
    TEST_ASSERT_EQUAL_UINT16(11000, reg.getPPSMaxVoltage_mV(3));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getPPSMinVoltage_mV(3));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getPPSMaxCurrent_mA(3));
}

void test_pps_subtype_bits_are_zero()
{
    uint32_t pdo = TPS25751TransmitSinkCaps::buildPPSPDO(11000, 5000, 3000);
    // Bits 29:28 must be 00 (SPR PPS APDO subtype).
    TEST_ASSERT_EQUAL_UINT32(0, (pdo >> 28) & 0x3);
    // Bits 31:30 must be 11 (Augmented).
    TEST_ASSERT_EQUAL_UINT32(0x3, (pdo >> 30) & 0x3);
}

void test_pps_units_are_100mv_and_50ma_not_50mv()
{
    // See the analogous TransmitSourceCaps test for the overflow rationale:
    // 11000/100=110, 5000/100=50, 3000/50=60 -- these only fit if the PPS
    // fields are 100 mV / 50 mA units, not 50 mV / 10 mA.
    uint32_t pdo = TPS25751TransmitSinkCaps::buildPPSPDO(11000, 5000, 3000);
    TEST_ASSERT_EQUAL_UINT32(110, (pdo >> 17) & 0xFF);
    TEST_ASSERT_EQUAL_UINT32(50, (pdo >> 8) & 0xFF);
    TEST_ASSERT_EQUAL_UINT32(60, pdo & 0x7F);
}

// --- Field masking: over-range values must not bleed into type bits ---------

void test_fixed_voltage_overflow_does_not_corrupt_type_bits()
{
    uint32_t pdo = TPS25751TransmitSinkCaps::buildFixedSupplyPDO(65000, 3000);
    TEST_ASSERT_EQUAL_UINT32(0, (pdo >> 30) & 0x3);
}

void test_fast_role_swap_current_masked_to_2_bits()
{
    // Passing a value with bits beyond 1:0 set must not bleed into the
    // adjacent Dual-Role Data bit (25).
    uint32_t pdo = TPS25751TransmitSinkCaps::buildFixedSupplyPDO(5000, 3000, false, false, false, false, false, 0xFF);
    TEST_ASSERT_EQUAL_UINT32(0x3, (pdo >> 23) & 0x3);  // masked to 2 bits
    TEST_ASSERT_EQUAL_UINT32(0, (pdo >> 25) & 0x1);    // Dual-Role Data untouched
}

// --- Type bit assertions for all four builders -------------------------------

void test_type_bits_for_all_builders()
{
    TEST_ASSERT_EQUAL_UINT32(0x0, (TPS25751TransmitSinkCaps::buildFixedSupplyPDO(5000, 3000) >> 30) & 0x3);
    TEST_ASSERT_EQUAL_UINT32(0x1, (TPS25751TransmitSinkCaps::buildBatteryPDO(12000, 9000, 15000) >> 30) & 0x3);
    TEST_ASSERT_EQUAL_UINT32(0x2, (TPS25751TransmitSinkCaps::buildVariablePDO(20000, 15000, 5000) >> 30) & 0x3);
    TEST_ASSERT_EQUAL_UINT32(0x3, (TPS25751TransmitSinkCaps::buildPPSPDO(11000, 5000, 3000) >> 30) & 0x3);
}

// --- setPDO bounds / no-op behavior ------------------------------------------

void test_set_pdo_out_of_range_index_is_noop()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDO(7, 0xDEADBEEF);  // index 7 is out of range (0-6 valid)
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(7));
}

void test_setters_no_op_on_invalid_register()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDO(0, 0x11223344);
    TPS25751TransmitSinkCaps moved(static_cast<TPS25751TransmitSinkCaps &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setPDO(1, 0xAABBCCDD);
    reg.setPDOCount(5);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash

    TEST_ASSERT_EQUAL_UINT32(0x11223344, moved.getPDO(0));  // untouched
}

// --- Rule of Five (compiler-generated via base class) ------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751TransmitSinkCaps original;
    original.setPDO(0, 0x11223344);

    TPS25751TransmitSinkCaps copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), TX_SINK_CAPS_SIZE);
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

    RUN_TEST(test_pdo0_lands_at_byte1);
    RUN_TEST(test_pdo1_lands_4_bytes_after_pdo0);

    RUN_TEST(test_set_pdo_count_round_trip);
    RUN_TEST(test_set_pdo_count_preserves_reserved_bits);

    RUN_TEST(test_fixed_supply_round_trip);
    RUN_TEST(test_higher_capability_bit_is_not_misread_as_usb_suspend);
    RUN_TEST(test_fast_role_swap_current_round_trip);
    RUN_TEST(test_battery_round_trip);
    RUN_TEST(test_variable_round_trip);
    RUN_TEST(test_pps_round_trip);
    RUN_TEST(test_pps_subtype_bits_are_zero);
    RUN_TEST(test_pps_units_are_100mv_and_50ma_not_50mv);

    RUN_TEST(test_fixed_voltage_overflow_does_not_corrupt_type_bits);
    RUN_TEST(test_fast_role_swap_current_masked_to_2_bits);

    RUN_TEST(test_type_bits_for_all_builders);

    RUN_TEST(test_set_pdo_out_of_range_index_is_noop);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);

    return UNITY_END();
}
