/**
 * @file test_TPS25751ActivePDOContract.cpp
 * @brief Native (host-run) unit tests for TPS25751ActivePDOContract.
 *
 * Run with: pio test -e native
 *
 * ACTIVE_PDO_CONTRACT (0x34) is 48 bits (6 bytes), little-endian. Source:
 * SLVUCR8B (Rev. B, 2026-08), page 38, via the tps25751-docs MCP:
 *   bits 47:42  Reserved
 *   bits 41:32  First PDO Control Bits
 *   bits 31:0   Active PDO
 * activePdo() decodes the active PDO through TPS25751Pdo at an unknown position,
 * so its Fixed PDO flags read false (Table 6.10) and the voltage, current, PPS,
 * and AVS fields decode. firstPdoControl() decodes First PDO Control Bits as bits
 * 29:20 of PDO 1 (USB PD R3.3 Table 6.8), which carry PDO 1's flags and Peak
 * Current. Validation is TPS25751Pdo::isValidForReceiver() on the active PDO.
 */

#include <unity.h>
#include <cstdio>
#include <string>

#include "TPS25751ActivePDOContract.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"
#include "TPS25751TransmitSourceCaps.h"
#include "fixtures/PdCapture_2026_11_25_tps25751_iphone.h"
#include "fixtures/PdCapture_2026_11_23_apple_140w_tps25751.h"

namespace {

constexpr size_t PDO_SIZE = TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT.size;  // 6

// Fixed 9 V / 3 A; PDO 0x2D02D12C has bits 29, 27, 26, 24 set, which are reserved
// Device Flags above 5 V. First PDO Control Bits = 0x2A5: DRP, Unconstrained,
// Dual-Role Data, and Peak Current 01b.
constexpr uint8_t FIXED_9V_3A[PDO_SIZE] = {0x2C, 0xD1, 0x02, 0x2D, 0xA5, 0x02};

// Battery 20 V max / 5 V min / 60 W; PDO 0x590190F0.
constexpr uint8_t BATTERY[PDO_SIZE] = {0xF0, 0x90, 0x01, 0x59, 0x00, 0x00};

// Variable 21 V max / 9 V min / 2 A; PDO 0x9A42D0C8.
constexpr uint8_t VARIABLE[PDO_SIZE] = {0xC8, 0xD0, 0x42, 0x9A, 0x00, 0x00};

// PPS 3.3-21 V / 3 A, Power Limited; PDO 0xC9A4213C.
constexpr uint8_t PPS[PDO_SIZE] = {0x3C, 0x21, 0xA4, 0xC9, 0x00, 0x00};

// Same PPS PDO with reserved bits 26, 25, 16, and 7 set; PDO 0xCFA521BC.
constexpr uint8_t PPS_RESERVED_SET[PDO_SIZE] = {0xBC, 0x21, 0xA5, 0xCF, 0x00, 0x00};

// Fixed 5 V with 0 mA max current: valid, the tables allow 0.
constexpr uint8_t FIXED_ZERO_CURRENT[PDO_SIZE] = {0x00, 0x90, 0x01, 0x00, 0x00, 0x00};

// Fixed 5 V with a current code of 501 (5.01 A): above the 500 limit, invalid.
constexpr uint8_t FIXED_CURRENT_ABOVE_LIMIT[PDO_SIZE] = {0xF5, 0x95, 0x01, 0x00, 0x00, 0x00};

// The 140 W charger: active PDO 0x000641D6 (Fixed 20 V / 4.7 A), First PDO Control
// Bits 0x098 (Unconstrained, Unchunked, EPR), as read from the board.
constexpr uint8_t CHARGER_140W[PDO_SIZE] = {0xD6, 0x41, 0x06, 0x00, 0x98, 0x00};

// The 67 W charger: First PDO Control Bits 0x080 (Unconstrained only).
constexpr uint8_t CHARGER_67W[PDO_SIZE] = {0xD6, 0x41, 0x06, 0x00, 0x80, 0x00};

constexpr uint8_t NO_CONTRACT[PDO_SIZE] = {0, 0, 0, 0, 0, 0};

// Augmented PDO with APDO subtype 10b (bits 29:28), not SPR PPS; PDO 0xE004B1F4,
// the word read from 0x32 PDO 5 on the bench on 2026-11-18.
constexpr uint8_t APDO_SUBTYPE_2[PDO_SIZE] = {0xF4, 0xB1, 0x04, 0xE0, 0x00, 0x00};

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

// The real capture of the 140 W charger (test/fixtures/pd-captures/).
namespace cap = PdCapture_2026_11_23_apple_140w_tps25751;

// The real source-role capture: the board as Source, an iPhone as Sink, after a
// port_reset, with registers from a bench SNAP (test/fixtures/pd-captures/).
namespace iphone = PdCapture_2026_11_25_tps25751_iphone;

}  // namespace

void setUp() {}
void tearDown() {}

void test_no_contract()
{
    TPS25751ActivePDOContract reg(NO_CONTRACT);

    TEST_ASSERT_EQUAL_UINT32(0, reg.rawActivePdo());
    TEST_ASSERT_FALSE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    const std::string out = captureDebugPrint(reg);
    TEST_ASSERT_TRUE(contains(out, "=== Active PDO Contract Register (0x34) ==="));
    TEST_ASSERT_TRUE(contains(out, "No active PD contract"));
}

void test_active_pdo_and_first_pdo_control_bits()
{
    TPS25751ActivePDOContract reg(FIXED_9V_3A);

    TEST_ASSERT_EQUAL_HEX32(0x2D02D12C, reg.rawActivePdo());
    TEST_ASSERT_EQUAL_HEX16(0x2A5, reg.rawFirstPdoControlBits());
    TEST_ASSERT_TRUE(reg.hasActiveContract());
}

void test_fixed_supply_decode()
{
    TPS25751ActivePDOContract reg(FIXED_9V_3A);
    const TPS25751Pdo pdo = reg.activePdo();

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, pdo.type());
    TEST_ASSERT_EQUAL_UINT16(9000, pdo.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, pdo.fixedCurrent_mA());
    TEST_ASSERT_EQUAL_UINT8(TPS25751Pdo::kUnknownPosition, pdo.position());
    // The flag bits 29, 27, 26, 24 are set in the word, but the active PDO's position is
    // unknown, so its flags read false; PDO 1's flags come from firstPdoControl().
    TEST_ASSERT_FALSE(pdo.dualRolePower());
    TEST_ASSERT_FALSE(pdo.unconstrainedPower());
    TEST_ASSERT_FALSE(pdo.usbCommunicationsCapable());
    TEST_ASSERT_FALSE(pdo.unchunkedExtendedMessages());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_first_pdo_control_decodes_pdo1_flags()
{
    TPS25751ActivePDOContract reg(FIXED_9V_3A);  // First PDO Control Bits 0x2A5
    const TPS25751Pdo first = reg.firstPdoControl();

    TEST_ASSERT_EQUAL_UINT8(1, first.position());
    TEST_ASSERT_TRUE(first.dualRolePower());              // bit 9
    TEST_ASSERT_FALSE(first.usbSuspendSupported());       // bit 8
    TEST_ASSERT_TRUE(first.unconstrainedPower());         // bit 7
    TEST_ASSERT_FALSE(first.usbCommunicationsCapable());  // bit 6
    TEST_ASSERT_TRUE(first.dualRoleData());               // bit 5
    TEST_ASSERT_FALSE(first.unchunkedExtendedMessages()); // bit 4
    TEST_ASSERT_FALSE(first.eprCapable());                // bit 3
    TEST_ASSERT_EQUAL_UINT8(1, first.rawPeakCurrent());   // bits 1:0
    TEST_ASSERT_EQUAL(PdoPeakCurrent::Overload1, first.peakCurrent());
    // Only bits 29:20 are filled in, so the voltage and current are not PDO 1's.
    TEST_ASSERT_EQUAL_HEX32(0x2A500000u, first.raw());
}

void test_140w_charger_first_pdo_control_and_active_pdo()
{
    TPS25751ActivePDOContract reg(CHARGER_140W);
    const TPS25751Pdo first = reg.firstPdoControl();

    TEST_ASSERT_TRUE(first.unconstrainedPower());
    TEST_ASSERT_TRUE(first.unchunkedExtendedMessages());
    TEST_ASSERT_TRUE(first.eprCapable());
    TEST_ASSERT_FALSE(first.dualRolePower());
    TEST_ASSERT_FALSE(first.usbSuspendSupported());
    TEST_ASSERT_FALSE(first.usbCommunicationsCapable());
    TEST_ASSERT_FALSE(first.dualRoleData());

    TEST_ASSERT_EQUAL_UINT16(20000, reg.activePdo().fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(4700, reg.activePdo().fixedCurrent_mA());
    TEST_ASSERT_EQUAL(PdoPeakCurrent::IoC, reg.activePdo().peakCurrent());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_67w_charger_first_pdo_control()
{
    TPS25751ActivePDOContract reg(CHARGER_67W);
    const TPS25751Pdo first = reg.firstPdoControl();

    TEST_ASSERT_TRUE(first.unconstrainedPower());
    TEST_ASSERT_FALSE(first.eprCapable());
    TEST_ASSERT_FALSE(first.unchunkedExtendedMessages());
}

void test_battery_decode()
{
    TPS25751ActivePDOContract reg(BATTERY);
    const TPS25751Pdo pdo = reg.activePdo();

    TEST_ASSERT_EQUAL(PDOType::Battery, pdo.type());
    TEST_ASSERT_EQUAL_UINT16(20000, pdo.batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, pdo.batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(60000, pdo.batteryPower_mW());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_variable_decode()
{
    TPS25751ActivePDOContract reg(VARIABLE);
    const TPS25751Pdo pdo = reg.activePdo();

    TEST_ASSERT_EQUAL(PDOType::VariableSupply, pdo.type());
    TEST_ASSERT_EQUAL_UINT16(21000, pdo.variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(9000, pdo.variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2000, pdo.variableCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_pps_decode()
{
    TPS25751ActivePDOContract reg(PPS);
    const TPS25751Pdo pdo = reg.activePdo();

    TEST_ASSERT_EQUAL(PDOType::Augmented, pdo.type());
    TEST_ASSERT_EQUAL(PdoKind::SprPps, pdo.kind());
    TEST_ASSERT_TRUE(pdo.ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, pdo.ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, pdo.ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, pdo.ppsCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_pps_reserved_bits_do_not_leak_into_fields()
{
    // Reserved bits 26:25, 16, and 7 must not change the decoded values.
    TPS25751ActivePDOContract reg(PPS_RESERVED_SET);
    const TPS25751Pdo pdo = reg.activePdo();

    TEST_ASSERT_TRUE(pdo.ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, pdo.ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, pdo.ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, pdo.ppsCurrent_mA());
}

void test_fixed_zero_current_is_now_valid()
{
    // The old rule rejected a zero current; Tables 6.8 to 6.10 allow 0.
    TPS25751ActivePDOContract reg(FIXED_ZERO_CURRENT);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.activePdo().fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.activePdo().fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_fixed_current_above_limit_is_invalid()
{
    TPS25751ActivePDOContract reg(FIXED_CURRENT_ABOVE_LIMIT);

    TEST_ASSERT_EQUAL_UINT16(5010, reg.activePdo().fixedCurrent_mA());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_semantic_rejects_wrong_size_and_all_ones()
{
    const uint8_t ones[PDO_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_FALSE(TPS25751ActivePDOContract(ones).isSemanticallyValid());

    const uint8_t small[2] = {0x2C, 0x91};
    TEST_ASSERT_FALSE(TPS25751ActivePDOContract(small, sizeof(small)).isSemanticallyValid());
}

void test_debug_print_active_contract_path()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(FIXED_9V_3A));

    TEST_ASSERT_FALSE(contains(out, "No active PD contract"));
    TEST_ASSERT_TRUE(contains(out, "Active PDO: 0x2D02D12C"));
    TEST_ASSERT_TRUE(contains(out, "Type: Fixed Supply"));
    TEST_ASSERT_TRUE(contains(out, "Voltage: 9000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
}

void test_debug_print_pdo1_flag_lines()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(CHARGER_140W));

    TEST_ASSERT_TRUE(contains(out, "PDO 1 flags (First PDO Control Bits 0x098):"));
    TEST_ASSERT_TRUE(contains(out, "    Dual-Role Power: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    USB Suspend Supported: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Unconstrained Power: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    USB Communications Capable: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Dual-Role Data: No\n"));
    TEST_ASSERT_TRUE(contains(out, "    Unchunked Extended Messages: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    EPR Capable: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "    PDO 1 Peak Current: IoC (default) (0)\n"));
}

void test_debug_print_pps_path()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(PPS));

    TEST_ASSERT_TRUE(contains(out, "Type: SPR PPS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 21000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Min Voltage: 3300 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
}

void test_spr_avs_active_pdo_decodes_and_is_valid()
{
    TPS25751ActivePDOContract reg(APDO_SUBTYPE_2);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.activePdo().type());
    TEST_ASSERT_EQUAL_UINT8(2, reg.activePdo().apdoType());
    TEST_ASSERT_EQUAL(PdoKind::SprAvs, reg.activePdo().kind());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.activePdo().avsCurrent15V_mA());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.activePdo().avsCurrent20V_mA());
    TEST_ASSERT_EQUAL_UINT16(0, reg.activePdo().ppsMaxVoltage_mV());
    TEST_ASSERT_FALSE(reg.activePdo().ppsPowerLimited());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_debug_print_spr_avs_path()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(APDO_SUBTYPE_2));

    TEST_ASSERT_TRUE(contains(out, "Type: SPR AVS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Max Current 15 to 20 V: 5000 mA"));
    TEST_ASSERT_FALSE(contains(out, "Min Voltage"));
}

// --- Capture bundle 2026-11-23-apple-140w-tps25751 ----------------------------------
//
// 0x34 built from the bundle's register bytes against the wire:
// - Bits 31:0 equal the wire PDO that the Request's Object Position names. RDO
//   0x4184B12C bits 31:28 = 4 (USB PD R3.3 Table 6.19), so PDO 4, 0x000641D6: Fixed
//   (bits 31:30 00b), 20 V (bits 19:10 = 400), 4.7 A (bits 9:0 = 470), Table 6.10.
// - Bits 41:32 equal bits 29:20 of wire PDO 1, 0x0981912C: 0x098, which is bit 27
//   Unconstrained Power, bit 24 Unchunked, bit 23 EPR Capable, Peak Current 00b
//   (Table 6.8). The TRM does not state this mapping (TPS25751ActivePDOContract.h).

void test_capture_active_pdo_is_the_requested_wire_pdo()
{
    TEST_ASSERT_EQUAL_size_t(PDO_SIZE, cap::kRegister0x34Length);
    TPS25751ActivePDOContract reg(cap::kRegister0x34, cap::kRegister0x34Length);

    TEST_ASSERT_EQUAL_UINT32(4, cap::kRequestRdo >> 28);  // Object Position, by hand
    TEST_ASSERT_EQUAL_HEX32(cap::kSourceCapabilitiesPdos[4 - 1], reg.rawActivePdo());
    TEST_ASSERT_TRUE(reg.hasActiveContract());

    const TPS25751Pdo active = reg.activePdo();
    TEST_ASSERT_EQUAL(PDOType::FixedSupply, active.type());
    TEST_ASSERT_EQUAL_UINT16(20000, active.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(4700, active.fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_capture_first_pdo_control_bits_are_wire_pdo1_bits_29_20()
{
    TPS25751ActivePDOContract reg(cap::kRegister0x34, cap::kRegister0x34Length);

    TEST_ASSERT_EQUAL_HEX16((cap::kSourceCapabilitiesPdos[0] >> 20) & 0x3FF, reg.rawFirstPdoControlBits());
    TEST_ASSERT_EQUAL_HEX16(0x098, reg.rawFirstPdoControlBits());
    const TPS25751Pdo first = reg.firstPdoControl();
    TEST_ASSERT_FALSE(first.dualRolePower());             // PDO 1 bit 29
    TEST_ASSERT_FALSE(first.usbSuspendSupported());       // bit 28
    TEST_ASSERT_TRUE(first.unconstrainedPower());         // bit 27
    TEST_ASSERT_FALSE(first.usbCommunicationsCapable());  // bit 26
    TEST_ASSERT_FALSE(first.dualRoleData());              // bit 25
    TEST_ASSERT_TRUE(first.unchunkedExtendedMessages());  // bit 24
    TEST_ASSERT_TRUE(first.eprCapable());                 // bit 23
    TEST_ASSERT_EQUAL(PdoPeakCurrent::IoC, first.peakCurrent());  // bits 21:20
}

// ---------------------------------------------------------------------------
// Source-role capture: the board as Source, an iPhone as Sink
// ---------------------------------------------------------------------------
//
// The iPhone's final Request, 0x1384B12C, has Object Position 1 (bits 31:28, Table
// 6.19), so the active PDO is PDO 1 as the board sent it, 0x0301912C: Fixed 5 V 3.0 A.

void test_capture_sourcing_active_pdo_is_the_pdo1_the_board_sent()
{
    TPS25751ActivePDOContract reg(iphone::kRegister0x34, iphone::kRegister0x34Length);

    TEST_ASSERT_EQUAL_UINT32(1, iphone::kRequestRdo >> 28);
    TEST_ASSERT_EQUAL_HEX32(iphone::kSourceCapabilitiesPdos[1 - 1], reg.rawActivePdo());
    const TPS25751Pdo active = reg.activePdo();
    TEST_ASSERT_EQUAL(PDOType::FixedSupply, active.type());
    TEST_ASSERT_EQUAL_UINT16(5000, active.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, active.fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// Present behavior, from one hardware observation (2026-11-25): with the board as
// Source, bits 41:32 read 0x200, which equals bits 29:20 of PDO 1 in 0x32
// (0x2001912C), not of the PDO 1 the board sent on the wire (0x0301912C, giving 0x030).
// With the board sinking, two chargers showed bits 41:32 equal to the received PDO 1
// (test_capture_first_pdo_control_bits_are_wire_pdo1_bits_29_20). The TRM states
// neither; if a later observation differs, this test records what changed.
void test_capture_sourcing_first_pdo_control_bits_follow_0x32()
{
    TPS25751ActivePDOContract reg(iphone::kRegister0x34, iphone::kRegister0x34Length);
    TPS25751TransmitSourceCaps caps(iphone::kRegister0x32, iphone::kRegister0x32Length);

    TEST_ASSERT_EQUAL_HEX16(0x200, reg.rawFirstPdoControlBits());
    TEST_ASSERT_EQUAL_HEX16((caps.getPDO(0) >> 20) & 0x3FF, reg.rawFirstPdoControlBits());
    TEST_ASSERT_EQUAL_HEX16(0x030, (iphone::kSourceCapabilitiesPdos[0] >> 20) & 0x3FF);
    TEST_ASSERT_TRUE(reg.firstPdoControl().dualRolePower());  // 0x32's bit 29
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_no_contract);
    RUN_TEST(test_active_pdo_and_first_pdo_control_bits);
    RUN_TEST(test_fixed_supply_decode);
    RUN_TEST(test_first_pdo_control_decodes_pdo1_flags);
    RUN_TEST(test_140w_charger_first_pdo_control_and_active_pdo);
    RUN_TEST(test_67w_charger_first_pdo_control);
    RUN_TEST(test_battery_decode);
    RUN_TEST(test_variable_decode);
    RUN_TEST(test_pps_decode);
    RUN_TEST(test_pps_reserved_bits_do_not_leak_into_fields);
    RUN_TEST(test_fixed_zero_current_is_now_valid);
    RUN_TEST(test_fixed_current_above_limit_is_invalid);
    RUN_TEST(test_semantic_rejects_wrong_size_and_all_ones);
    RUN_TEST(test_debug_print_active_contract_path);
    RUN_TEST(test_debug_print_pdo1_flag_lines);
    RUN_TEST(test_debug_print_pps_path);
    RUN_TEST(test_spr_avs_active_pdo_decodes_and_is_valid);
    RUN_TEST(test_debug_print_spr_avs_path);
    RUN_TEST(test_capture_active_pdo_is_the_requested_wire_pdo);
    RUN_TEST(test_capture_first_pdo_control_bits_are_wire_pdo1_bits_29_20);

    RUN_TEST(test_capture_sourcing_active_pdo_is_the_pdo1_the_board_sent);
    RUN_TEST(test_capture_sourcing_first_pdo_control_bits_follow_0x32);
    return UNITY_END();
}
