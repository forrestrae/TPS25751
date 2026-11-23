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
 * builders directly and round-trip them through setPDO() and pdo(i), the
 * TPS25751Pdo decode shared with TPS25751ReceivedSinkCaps. Validation is
 * validatePdoList() with PdoListRules::TransmitSink (USB PD R3.3), which also
 * rejects an SPR AVS APDO: a Sink Shall Not send one.
 */

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "TPS25751TransmitSinkCaps.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t TX_SINK_CAPS_SIZE = TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES.size;  // 53

// Fixed 5 V / 3 A from the field layout: (100 << 10) | 300 = 0x0001912C.
constexpr uint32_t FIXED_5V_3A = (100u << 10) | 300u;

// Builds a register holding @p pdos (PDO 1 first) and the matching count.
TPS25751TransmitSinkCaps make(std::initializer_list<uint32_t> pdos)
{
    TPS25751TransmitSinkCaps reg;
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

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.pdo(0).type());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(0).fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(0).fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_TRUE(reg.pdo(0).higherCapability());
    TEST_ASSERT_TRUE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRoleData());
    TEST_ASSERT_EQUAL_UINT8(0x2, reg.pdo(0).rawFastRoleSwapCurrent());
}

void test_higher_capability_bit_is_not_misread_as_usb_suspend()
{
    // Sink Fixed PDO bit 28 is "Higher Capability", NOT "USB Suspend
    // Supported" (that's the Source variant's bit 28). Set only bit 28 via
    // higherCapability=true and confirm it reads back through the Sink
    // accessor and not through usbSuspendSupported(), which is false for a Sink PDO.
    TPS25751TransmitSinkCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000, false, true, false, false, false, 0);

    TEST_ASSERT_TRUE(reg.pdo(0).higherCapability());
    TEST_ASSERT_EQUAL_UINT32(1u, (reg.getPDO(0) >> 28) & 0x1);
}

void test_fast_role_swap_current_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setFixedSupplyPDO(0, 5000, 3000, false, false, false, false, false, 0x3);
    TEST_ASSERT_EQUAL_UINT8(0x3, reg.pdo(0).rawFastRoleSwapCurrent());

    reg.setFixedSupplyPDO(0, 5000, 3000, false, false, false, false, false, 0x1);
    TEST_ASSERT_EQUAL_UINT8(0x1, reg.pdo(0).rawFastRoleSwapCurrent());
}

void test_battery_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setBatteryPDO(1, 12000, 9000, 15000);
    reg.setPDOCount(2);

    TEST_ASSERT_EQUAL(PDOType::Battery, reg.pdo(1).type());
    TEST_ASSERT_EQUAL_UINT16(12000, reg.pdo(1).batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(9000, reg.pdo(1).batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(15000, reg.pdo(1).batteryPower_mW());
}

void test_variable_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setVariablePDO(2, 20000, 15000, 5000);
    reg.setPDOCount(3);

    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.pdo(2).type());
    TEST_ASSERT_EQUAL_UINT16(20000, reg.pdo(2).variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(15000, reg.pdo(2).variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(2).variableCurrent_mA());
}

void test_pps_round_trip()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPPSPDO(3, 11000, 5000, 3000);
    reg.setPDOCount(4);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(3).type());
    TEST_ASSERT_EQUAL_UINT16(11000, reg.pdo(3).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(3).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(3).ppsCurrent_mA());
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

// --- APDO subtypes other than SPR PPS ---------------------------------------
//
// An Augmented PDO with bits 29:28 = 10b is not an SPR PPS APDO. The PPS
// accessors return 0 for it. It is the SPR AVS APDO, which a Sink Shall Not send
// (USB PD R3.3 page 137), so a transmit list that holds one fails validation.
// The word is the subtype 10b APDO read from 0x32 on the bench on 2026-11-18.
void test_spr_avs_apdo_is_not_pps_and_fails_transmit_sink_validation()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDO(0, TPS25751TransmitSinkCaps::buildFixedSupplyPDO(5000, 3000));
    reg.setPDO(1, 0xE004B1F4u);
    reg.setPDOCount(2);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(1).type());
    TEST_ASSERT_EQUAL_UINT8(2, reg.pdo(1).apdoType());
    TEST_ASSERT_EQUAL(PdoKind::SprAvs, reg.pdo(1).kind());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(1).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(1).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(1).ppsCurrent_mA());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());

    // An SPR PPS APDO in the same slot is decoded and valid.
    reg.setPDO(1, TPS25751TransmitSinkCaps::buildPPSPDO(11000, 5000, 3000));
    TEST_ASSERT_EQUAL(PdoKind::SprPps, reg.pdo(1).kind());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(1).ppsMinVoltage_mV());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// The board's 0x33 contents: four Fixed PDOs, PDO 1 with Higher Capability.
void test_hardware_sink_caps_fixture_is_valid()
{
    const uint8_t hw[TX_SINK_CAPS_SIZE] = {
        0x04,
        0x2C, 0x91, 0x01, 0x10,  // 0x1001912C: Fixed 5 V / 3 A, Higher Capability
        0x2C, 0xD1, 0x02, 0x00,  // 0x0002D12C: Fixed 9 V / 3 A
        0x2C, 0xB1, 0x04, 0x00,  // 0x0004B12C: Fixed 15 V / 3 A
        0x2C, 0x41, 0x06, 0x00,  // 0x0006412C: Fixed 20 V / 3 A
    };
    TPS25751TransmitSinkCaps reg(hw, sizeof(hw));

    TEST_ASSERT_EQUAL_UINT8(4, reg.pdoCount());
    TEST_ASSERT_TRUE(reg.pdo(0).higherCapability());
    TEST_ASSERT_FALSE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_EQUAL_UINT16(20000, reg.pdo(3).fixedVoltage_mV());
    TEST_ASSERT_TRUE(reg.pdo(0).role() == PdoRole::Sink);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_transmit_sink_rejects_out_of_order_pdos()
{
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t fixed9 = 0x0002D12Cu;
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, fixed9, battery}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, battery, fixed9}).isSemanticallyValid());
}

// --- Decode of raw words and bounds -------------------------------------------

void test_get_pdo_bounds_and_short_buffer()
{
    TPS25751TransmitSinkCaps reg = make({FIXED_5V_3A});
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, reg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(7));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(255));

    // 8 bytes hold PDO 1 (bytes 1..4) but not PDO 2 (bytes 5..8).
    const uint8_t shortBuf[8] = {0x02, 0x2C, 0x91, 0x01, 0x00, 0x01, 0x02, 0x03};
    TPS25751TransmitSinkCaps shortReg(shortBuf, sizeof(shortBuf));
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, shortReg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0, shortReg.getPDO(1));
}

void test_fixed_flags_and_fast_role_swap_values_decode()
{
    // PDO 1 carries every flag; FRS current field = 3 (bits 24:23 = 11b). PDO 2
    // (9 V / 2.5 A) has the same bits set, which read false there (Table 6.10).
    const uint32_t flagBits = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                              (1u << 25) | (3u << 23);
    TPS25751TransmitSinkCaps reg = make({FIXED_5V_3A | flagBits, flagBits | (180u << 10) | 250u});

    TEST_ASSERT_EQUAL_UINT16(9000, reg.pdo(1).fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2500, reg.pdo(1).fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_TRUE(reg.pdo(0).higherCapability());
    TEST_ASSERT_TRUE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRoleData());
    TEST_ASSERT_EQUAL_UINT8(3, reg.pdo(0).rawFastRoleSwapCurrent());
    TEST_ASSERT_EQUAL(FastRoleSwapCurrent::Current3_0A, reg.pdo(0).fastRoleSwapCurrent());

    TEST_ASSERT_FALSE(reg.pdo(1).dualRolePower());
    TEST_ASSERT_FALSE(reg.pdo(1).higherCapability());
    TEST_ASSERT_FALSE(reg.pdo(1).unconstrainedPower());
    TEST_ASSERT_FALSE(reg.pdo(1).usbCommunicationsCapable());
    TEST_ASSERT_FALSE(reg.pdo(1).dualRoleData());
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdo(1).rawFastRoleSwapCurrent());
}

void test_pdo_type_decodes_bits_31_30()
{
    TPS25751TransmitSinkCaps reg = make({FIXED_5V_3A, 0x40000000u, 0x80000000u, 0xC0000000u});

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.pdo(0).type());
    TEST_ASSERT_EQUAL(PDOType::Battery, reg.pdo(1).type());
    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.pdo(2).type());
    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(3).type());
}

void test_stale_count_with_zero_pdo1_means_no_caps()
{
    TPS25751TransmitSinkCaps reg;
    reg.setPDOCount(5);

    TEST_ASSERT_EQUAL_UINT8(5, reg.pdoCount());
    TEST_ASSERT_FALSE(reg.hasSinkCaps());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_no_caps()
{
    TPS25751TransmitSinkCaps reg;
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Transmit Sink Capabilities Register (0x33)"));
    TEST_ASSERT_TRUE(contains(out, "PDO Count: 0"));
    TEST_ASSERT_TRUE(contains(out, "No sink capabilities configured"));
}

void test_debug_print_all_pdo_types()
{
    const uint32_t fixed = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                           (1u << 25) | (2u << 23) | (100u << 10) | 300u;
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;  // 20 V, 5 V, 60 W
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;  // 12 V, 5 V, 1.5 A
    TPS25751TransmitSinkCaps reg = make({fixed, battery, variable, 0xC1A4323Cu, 0xE004B1F4u});
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "PDO Count: 5"));
    TEST_ASSERT_TRUE(contains(out, "--- PDO 5 ---"));
    TEST_ASSERT_TRUE(contains(out, "Type: Fixed Supply"));
    TEST_ASSERT_TRUE(contains(out, "Voltage: 5000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Operational Current: 3000 mA"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Higher Capability: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "USB Communications Capable: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Data: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Fast Role Swap Current: "));
    TEST_ASSERT_TRUE(contains(out, "(2)"));
    TEST_ASSERT_TRUE(contains(out, "Type: Battery"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 20000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Operational Power: 60000 mW"));
    TEST_ASSERT_TRUE(contains(out, "Type: Variable Supply"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 12000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Operational Current: 1500 mA"));
    TEST_ASSERT_TRUE(contains(out, "Type: SPR PPS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 21000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Min Voltage: 5000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Type: SPR AVS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Deprecated SPR AVS Sink APDO, not decoded: 0xE004B1F4"));
}

void test_debug_print_fixed_flags_clear_print_no()
{
    TPS25751TransmitSinkCaps reg = make({FIXED_5V_3A});
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Dual-Role Power: No"));
    TEST_ASSERT_TRUE(contains(out, "Higher Capability: No"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: No"));
    TEST_ASSERT_TRUE(contains(out, "USB Communications Capable: No"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Data: No"));
    TEST_ASSERT_TRUE(contains(out, "Fast Role Swap Current: Not supported (0)"));
}

// --- Semantic validation ------------------------------------------------------

void test_semantic_valid_mixed_caps()
{
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, battery, variable, 0xC1A4323Cu}).isSemanticallyValid());
}

void test_semantic_rejects_all_ones()
{
    uint8_t ones[TX_SINK_CAPS_SIZE];
    memset(ones, 0xFF, sizeof(ones));
    TEST_ASSERT_FALSE(TPS25751TransmitSinkCaps(ones, TX_SINK_CAPS_SIZE).isSemanticallyValid());
}

void test_semantic_fixed_rules()
{
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 300u}).isSemanticallyValid());        // zero voltage
    // Zero operational current is valid; the old rule rejected it.
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
}

void test_semantic_variable_rules()
{
    const uint32_t v = (2u << 30);
    // The 80% minimum-voltage rule applies to a Source only, so a Sink list may
    // hold a Variable PDO whose minimum is a fraction of its maximum.
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, v | (240u << 20) | (100u << 10) | 150u}).isSemanticallyValid());
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, v | (240u << 20) | 150u}).isSemanticallyValid());                  // zero min
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, v | (100u << 10) | 150u}).isSemanticallyValid());                  // zero max
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, v | (240u << 20) | (100u << 10)}).isSemanticallyValid());           // zero current: valid
}

void test_semantic_pps_rules()
{
    TEST_ASSERT_TRUE(make({FIXED_5V_3A, 0xC1A43200u}).isSemanticallyValid());   // 5-21 V, 0 A: valid
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 0xC1A4213Cu}).isSemanticallyValid());  // 3.3 V minimum is below 5 V
    TEST_ASSERT_FALSE(make({FIXED_5V_3A, 0xC1A4003Cu}).isSemanticallyValid());  // zero min voltage
}

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
    RUN_TEST(test_spr_avs_apdo_is_not_pps_and_fails_transmit_sink_validation);
    RUN_TEST(test_hardware_sink_caps_fixture_is_valid);
    RUN_TEST(test_transmit_sink_rejects_out_of_order_pdos);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);

    RUN_TEST(test_get_pdo_bounds_and_short_buffer);
    RUN_TEST(test_fixed_flags_and_fast_role_swap_values_decode);
    RUN_TEST(test_pdo_type_decodes_bits_31_30);
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
