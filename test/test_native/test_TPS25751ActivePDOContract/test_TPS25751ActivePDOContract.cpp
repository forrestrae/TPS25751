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
 * The fields inside the PDO word follow the USB PD PDO layouts documented in
 * docs/engineering/CONSTRAINTS.md (PPS: 8-bit voltages at 24:17 and 15:8,
 * 7-bit current at 6:0, matching the hardware-verified PPS decode).
 *
 * Every accessor here uses fields wider than 8 bits; before the fix they
 * called the 8-bit extractBits() and always returned 0.
 */

#include <unity.h>
#include <cstdio>
#include <string>

#include "TPS25751ActivePDOContract.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t PDO_SIZE = TPS25751Registers::Registers::ACTIVE_PDO_CONTRACT.size;  // 6

// Fixed 9 V / 3 A, DRP + Unconstrained + USB Comm + Unchunked; PDO 0x2D02D12C.
// First PDO Control Bits = 0x2A5.
constexpr uint8_t FIXED_9V_3A[PDO_SIZE] = {0x2C, 0xD1, 0x02, 0x2D, 0xA5, 0x02};

// Battery 20 V max / 5 V min / 60 W; PDO 0x590190F0.
constexpr uint8_t BATTERY[PDO_SIZE] = {0xF0, 0x90, 0x01, 0x59, 0x00, 0x00};

// Variable 21 V max / 9 V min / 2 A; PDO 0x9A42D0C8.
constexpr uint8_t VARIABLE[PDO_SIZE] = {0xC8, 0xD0, 0x42, 0x9A, 0x00, 0x00};

// PPS 3.3-21 V / 3 A, Power Limited; PDO 0xC9A4213C.
constexpr uint8_t PPS[PDO_SIZE] = {0x3C, 0x21, 0xA4, 0xC9, 0x00, 0x00};

// Same PPS PDO with reserved bits 26, 25, 16, and 7 set; PDO 0xCFA521BC.
constexpr uint8_t PPS_RESERVED_SET[PDO_SIZE] = {0xBC, 0x21, 0xA5, 0xCF, 0x00, 0x00};

// Fixed 5 V with 0 mA max current: an active contract that fails validation.
constexpr uint8_t FIXED_ZERO_CURRENT[PDO_SIZE] = {0x00, 0x90, 0x01, 0x00, 0x00, 0x00};

constexpr uint8_t NO_CONTRACT[PDO_SIZE] = {0, 0, 0, 0, 0, 0};

// Augmented PDO with APDO subtype 10b (bits 29:28), not SPR PPS; PDO 0xE004B1F4,
// the word read from 0x32 PDO 5 on the bench on 2026-11-18.
constexpr uint8_t APDO_SUBTYPE_2[PDO_SIZE] = {0xF4, 0xB1, 0x04, 0xE0, 0x00, 0x00};

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_no_contract()
{
    TPS25751ActivePDOContract reg(NO_CONTRACT);

    TEST_ASSERT_EQUAL_UINT32(0, reg.activePDO());
    TEST_ASSERT_FALSE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    TEST_ASSERT_TRUE(contains(captureDebugPrint(reg), "No active PD contract"));
}

void test_active_pdo_and_first_pdo_control_bits()
{
    TPS25751ActivePDOContract reg(FIXED_9V_3A);

    TEST_ASSERT_EQUAL_HEX32(0x2D02D12C, reg.activePDO());
    TEST_ASSERT_EQUAL_HEX16(0x2A5, reg.firstPDOControlBits());
    TEST_ASSERT_TRUE(reg.hasActiveContract());
}

void test_fixed_supply_decode()
{
    TPS25751ActivePDOContract reg(FIXED_9V_3A);

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.pdoType());
    TEST_ASSERT_EQUAL_UINT16(9000, reg.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.fixedMaxCurrent_mA());
    TEST_ASSERT_TRUE(reg.fixedDualRolePower());
    TEST_ASSERT_FALSE(reg.fixedUSBSuspendSupported());
    TEST_ASSERT_TRUE(reg.fixedUnconstrainedPower());
    TEST_ASSERT_TRUE(reg.fixedUSBCommCapable());
    TEST_ASSERT_FALSE(reg.fixedDualRoleData());
    TEST_ASSERT_TRUE(reg.fixedUnchunkedSupported());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_battery_decode()
{
    TPS25751ActivePDOContract reg(BATTERY);

    TEST_ASSERT_EQUAL(PDOType::Battery, reg.pdoType());
    TEST_ASSERT_EQUAL_UINT16(20000, reg.batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(60000, reg.batteryMaxPower_mW());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_variable_decode()
{
    TPS25751ActivePDOContract reg(VARIABLE);

    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.pdoType());
    TEST_ASSERT_EQUAL_UINT16(21000, reg.variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(9000, reg.variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2000, reg.variableMaxCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_pps_decode()
{
    TPS25751ActivePDOContract reg(PPS);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdoType());
    TEST_ASSERT_TRUE(reg.ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, reg.ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, reg.ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.ppsMaxCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_pps_reserved_bits_do_not_leak_into_fields()
{
    // Reserved bits 26:25, 16, and 7 must not change the decoded values.
    TPS25751ActivePDOContract reg(PPS_RESERVED_SET);

    TEST_ASSERT_TRUE(reg.ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, reg.ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, reg.ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.ppsMaxCurrent_mA());
}

void test_fixed_zero_current_is_semantically_invalid()
{
    TPS25751ActivePDOContract reg(FIXED_ZERO_CURRENT);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.fixedMaxCurrent_mA());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_debug_print_active_contract_path()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(FIXED_9V_3A));

    TEST_ASSERT_FALSE(contains(out, "No active PD contract"));
    TEST_ASSERT_TRUE(contains(out, "PDO Type: Fixed Supply"));
    TEST_ASSERT_TRUE(contains(out, "First PDO Control Bits: 0x2A5"));
    TEST_ASSERT_TRUE(contains(out, "Voltage: 9000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
}

void test_debug_print_pps_path()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(PPS));

    TEST_ASSERT_TRUE(contains(out, "Augmented/PPS PDO:"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 21000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Min Voltage: 3300 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
}

void test_non_pps_apdo_is_not_decoded_as_pps()
{
    TPS25751ActivePDOContract reg(APDO_SUBTYPE_2);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdoType());
    TEST_ASSERT_EQUAL_UINT8(2, reg.apdoSubtype());
    TEST_ASSERT_FALSE(reg.isSprPps());
    TEST_ASSERT_EQUAL_UINT16(0, reg.ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.ppsMaxCurrent_mA());
    TEST_ASSERT_FALSE(reg.ppsPowerLimited());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_contract_reports_subtype_zero()
{
    TPS25751ActivePDOContract reg(PPS);

    TEST_ASSERT_EQUAL_UINT8(0, reg.apdoSubtype());
    TEST_ASSERT_TRUE(reg.isSprPps());
}

void test_debug_print_non_pps_apdo_path()
{
    const std::string out = captureDebugPrint(TPS25751ActivePDOContract(APDO_SUBTYPE_2));

    TEST_ASSERT_TRUE(contains(out, "APDO subtype 2 (not SPR PPS; not decoded)"));
    TEST_ASSERT_FALSE(contains(out, "Max Voltage"));
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_no_contract);
    RUN_TEST(test_active_pdo_and_first_pdo_control_bits);
    RUN_TEST(test_fixed_supply_decode);
    RUN_TEST(test_battery_decode);
    RUN_TEST(test_variable_decode);
    RUN_TEST(test_pps_decode);
    RUN_TEST(test_pps_reserved_bits_do_not_leak_into_fields);
    RUN_TEST(test_fixed_zero_current_is_semantically_invalid);
    RUN_TEST(test_debug_print_active_contract_path);
    RUN_TEST(test_debug_print_pps_path);
    RUN_TEST(test_non_pps_apdo_is_not_decoded_as_pps);
    RUN_TEST(test_spr_pps_contract_reports_subtype_zero);
    RUN_TEST(test_debug_print_non_pps_apdo_path);

    return UNITY_END();
}
