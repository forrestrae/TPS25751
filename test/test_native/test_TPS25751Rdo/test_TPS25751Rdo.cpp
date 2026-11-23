/**
 * @file test_TPS25751Rdo.cpp
 * @brief Native (host-run) unit tests for TPS25751Rdo and rdoKindFor().
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751Rdo"
 *
 * One fixture per RDO kind, built from USB PD R3.3 V1.0 Tables 6.19 to 6.22:
 *   Fixed/Variable 0x4184B12C (bench, 0x35 bits 31:0 on 2026-11-22): Object
 *     Position 4, bit 24 No USB Suspend, bit 23 Unchunked, bits 19:10 = 300
 *     (3000 mA), bits 9:0 = 300 (the deprecated copy)
 *   Battery 0x2003C0F0: Object Position 2, bits 19:10 = 240 and bits 9:0 = 240
 *     (60 W in 250 mW units)
 *   PPS 0x5003843C: Object Position 5, bits 20:9 = 450 (9000 mV in 20 mV units),
 *     bits 6:0 = 60 (3000 mA in 50 mA units). Read as a Fixed/Variable RDO, bits
 *     19:10 are 225, which the old 0x35 accessors reported as 2250 mA.
 *   AVS 0x5002D028: Object Position 5, bits 20:9 = 360 (9000 mV in 25 mV units,
 *     bits 10:9 = 00b), bits 6:0 = 40 (2000 mA)
 */

#include <unity.h>

#include <cstring>
#include <string>

#include "CaptureStdout.h"
#include "TPS25751Rdo.h"
#include "fixtures/PdCapture_2026_11_23_apple_140w_tps25751.h"

namespace cap = PdCapture_2026_11_23_apple_140w_tps25751;

void setUp() {}
void tearDown() {}

void test_rdo_kind_for_each_pdo_kind()
{
    TEST_ASSERT_TRUE(RdoKind::FixedVariable == rdoKindFor(0x000641D6u));  // Fixed
    TEST_ASSERT_TRUE(RdoKind::FixedVariable == rdoKindFor(0x80000000u));  // Variable
    TEST_ASSERT_TRUE(RdoKind::Battery == rdoKindFor(0x40000000u));
    TEST_ASSERT_TRUE(RdoKind::Pps == rdoKindFor(0xC0000000u));
    TEST_ASSERT_TRUE(RdoKind::Avs == rdoKindFor(0xE004B1F4u));            // SPR AVS
    TEST_ASSERT_TRUE(RdoKind::Avs == rdoKindFor(0xD0000000u));            // EPR AVS
    TEST_ASSERT_TRUE(RdoKind::Unknown == rdoKindFor(0xF0000000u));        // invalid APDO
    TEST_ASSERT_TRUE(RdoKind::Pps == rdoKindFor(TPS25751Pdo(0xC0000000u, 5, PdoRole::Sink)));
}

void test_fixed_variable_rdo_from_the_bench()
{
    const TPS25751Rdo r(0x4184B12C, RdoKind::FixedVariable);
    TEST_ASSERT_EQUAL_UINT8(4, r.objectPosition());
    TEST_ASSERT_FALSE(r.giveback());
    TEST_ASSERT_FALSE(r.capabilityMismatch());
    TEST_ASSERT_FALSE(r.usbCommunicationsCapable());
    TEST_ASSERT_TRUE(r.noUsbSuspend());
    TEST_ASSERT_TRUE(r.unchunkedExtendedMessages());
    TEST_ASSERT_FALSE(r.eprCapable());
    TEST_ASSERT_EQUAL_UINT16(3000, r.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(3000, r.maximumOperatingCurrent_mA());
    TEST_ASSERT_TRUE(r.deprecatedCopyMatches());
    TEST_ASSERT_EQUAL_UINT32(0, r.operatingPower_mW());
    TEST_ASSERT_EQUAL_UINT32(0, r.outputVoltage_mV());
}

void test_battery_rdo()
{
    const TPS25751Rdo r(0x2003C0F0, RdoKind::Battery);
    TEST_ASSERT_EQUAL_UINT8(2, r.objectPosition());
    TEST_ASSERT_EQUAL_UINT32(60000, r.operatingPower_mW());
    TEST_ASSERT_EQUAL_UINT32(60000, r.maximumOperatingPower_mW());
    TEST_ASSERT_EQUAL_UINT16(0, r.operatingCurrent_mA());  // power, not current
    TEST_ASSERT_TRUE(r.deprecatedCopyMatches());
}

void test_pps_rdo_9v_3a()
{
    const TPS25751Rdo r(0x5003843C, RdoKind::Pps);
    TEST_ASSERT_EQUAL_UINT8(5, r.objectPosition());
    TEST_ASSERT_EQUAL_UINT16(450, r.rawOutputVoltage());
    TEST_ASSERT_EQUAL_UINT32(9000, r.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT8(60, r.rawPpsAvsOperatingCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, r.operatingCurrent_mA());
    TEST_ASSERT_TRUE(r.deprecatedCopyMatches());
    // The same word read as a Fixed/Variable RDO gives the old, wrong 2250 mA.
    TEST_ASSERT_EQUAL_UINT16(2250, TPS25751Rdo(0x5003843C, RdoKind::FixedVariable).operatingCurrent_mA());
}

void test_avs_rdo()
{
    const TPS25751Rdo r(0x5002D028, RdoKind::Avs);
    TEST_ASSERT_EQUAL_UINT16(360, r.rawOutputVoltage());
    TEST_ASSERT_EQUAL_UINT32(9000, r.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2000, r.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(0, r.maximumOperatingCurrent_mA());
}

void test_epr_capable_bit_22()
{
    const TPS25751Rdo r(0x4184B12Cu | (1u << 22), RdoKind::FixedVariable);
    TEST_ASSERT_TRUE(r.eprCapable());
    TEST_ASSERT_EQUAL_UINT16(3000, r.operatingCurrent_mA());
}

void test_unknown_kind_decodes_only_common_fields()
{
    const TPS25751Rdo r(0x4184B12C, RdoKind::Unknown);
    TEST_ASSERT_EQUAL_UINT8(4, r.objectPosition());
    TEST_ASSERT_TRUE(r.noUsbSuspend());
    TEST_ASSERT_EQUAL_UINT16(0, r.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT32(0, r.operatingPower_mW());
    TEST_ASSERT_EQUAL_UINT32(0, r.outputVoltage_mV());
    TEST_ASSERT_TRUE(r.deprecatedCopyMatches());
    TEST_ASSERT_TRUE(RdoKind::Unknown == TPS25751Rdo().kind());
}

void test_deprecated_copy_mismatch()
{
    // Operating 300, deprecated copy 200.
    const TPS25751Rdo r(0x4004B0C8, RdoKind::FixedVariable);
    TEST_ASSERT_EQUAL_UINT16(3000, r.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(2000, r.maximumOperatingCurrent_mA());
    TEST_ASSERT_FALSE(r.deprecatedCopyMatches());
}

void test_debug_print_per_kind()
{
    const std::string fv = captureStdout([] { TPS25751Rdo(0x4184B12C, RdoKind::FixedVariable).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(fv.c_str(), "RDO 0x4184B12C, decoded as Fixed/Variable\n"));
    TEST_ASSERT_NOT_NULL(strstr(fv.c_str(), "Object Position: 4\n"));
    TEST_ASSERT_NOT_NULL(strstr(fv.c_str(), "EPR Capable: No\n"));
    TEST_ASSERT_NOT_NULL(strstr(fv.c_str(), "Operating Current: 3000 mA\n"));
    TEST_ASSERT_NULL(strstr(fv.c_str(), "differs"));

    const std::string pps = captureStdout([] { TPS25751Rdo(0x5003843C, RdoKind::Pps).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(pps.c_str(), "Output Voltage: 9000 mV\n"));
    TEST_ASSERT_NOT_NULL(strstr(pps.c_str(), "Operating Current: 3000 mA\n"));

    const std::string bat = captureStdout([] { TPS25751Rdo(0x2003C0F0, RdoKind::Battery).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(bat.c_str(), "Operating Power: 60000 mW\n"));

    const std::string bad = captureStdout([] { TPS25751Rdo(0x4004B0C8, RdoKind::FixedVariable).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(bad.c_str(), "Maximum Operating Current (deprecated) differs: 2000 mA\n"));

    // Battery: Operating Power 240 (60 W), deprecated copy 200 (50 W).
    const std::string batBad = captureStdout([] { TPS25751Rdo(0x2003C0C8, RdoKind::Battery).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(batBad.c_str(), "Operating Power: 60000 mW\n"));
    TEST_ASSERT_NOT_NULL(strstr(batBad.c_str(), "Maximum Operating Power (deprecated) differs: 50000 mW\n"));

    const std::string unk = captureStdout([] { TPS25751Rdo(0x4184B12C, RdoKind::Unknown).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(unk.c_str(), "Kind unknown"));
    TEST_ASSERT_EQUAL_STRING("AVS", toString(RdoKind::Avs));
}

// --- Wire: the Request from the 2026-11-23 capture ---------------------------------
//
// Message header 0x1082 (USB PD R3.3 Table 6.1, not served by usb-pd-docs): bits 4:0 =
// 2 Request, bit 5 = 0 UFP, bits 7:6 = 10b Revision 3.x, bit 8 = 0 Sink, bits 14:12 = 1
// data object. RDO 0x4184B12C by hand from Table 6.19 (Fixed and Variable RDO; the
// requested PDO 4 is Fixed): bits 31:28 = 4 Object Position; 27 Giveback 0; 26
// Capability Mismatch 0; 25 USB Communications Capable 0; 24 No USB Suspend 1;
// 23 Unchunked 1; 22 EPR Capable 0; 21:20 reserved 00b; 19:10 = 300 Operating Current
// (3000 mA, 10 mA units); 9:0 = 300 Maximum Operating Current (deprecated copy).

void test_wire_request_header()
{
    TEST_ASSERT_TRUE(cap::kHasContract);  // Accept, then PS_RDY, after this Request
    TEST_ASSERT_FALSE(cap::kRequestEpr);
    TEST_ASSERT_EQUAL_HEX16(0x1082, cap::kRequestHeader);
    TEST_ASSERT_EQUAL_UINT8(2, cap::kRequestHeader & 0x1F);          // Request
    TEST_ASSERT_EQUAL_UINT8(0, (cap::kRequestHeader >> 5) & 0x1);    // UFP
    TEST_ASSERT_EQUAL_UINT8(0, (cap::kRequestHeader >> 8) & 0x1);    // Sink
    TEST_ASSERT_EQUAL_UINT8(1, (cap::kRequestHeader >> 12) & 0x7);   // one RDO
}

void test_wire_request_rdo_decodes_per_table_6_19()
{
    TEST_ASSERT_EQUAL_HEX32(0x4184B12C, cap::kRequestRdo);
    // Object Position 4 names the fourth wire PDO, Fixed 20 V, so the RDO is Fixed/Variable.
    const RdoKind kind = rdoKindFor(cap::kSourceCapabilitiesPdos[4 - 1]);
    TEST_ASSERT_TRUE(RdoKind::FixedVariable == kind);

    const TPS25751Rdo r(cap::kRequestRdo, kind);
    TEST_ASSERT_EQUAL_UINT8(4, r.objectPosition());
    TEST_ASSERT_FALSE(r.giveback());
    TEST_ASSERT_FALSE(r.capabilityMismatch());
    TEST_ASSERT_FALSE(r.usbCommunicationsCapable());
    TEST_ASSERT_TRUE(r.noUsbSuspend());
    TEST_ASSERT_TRUE(r.unchunkedExtendedMessages());
    TEST_ASSERT_FALSE(r.eprCapable());
    TEST_ASSERT_EQUAL_UINT16(3000, r.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(3000, r.maximumOperatingCurrent_mA());
    TEST_ASSERT_TRUE(r.deprecatedCopyMatches());
    // The request stays within PDO 4's 4700 mA Maximum Current (Table 6.19, Operating Current).
    TEST_ASSERT_TRUE(r.operatingCurrent_mA() <= 4700);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_rdo_kind_for_each_pdo_kind);
    RUN_TEST(test_fixed_variable_rdo_from_the_bench);
    RUN_TEST(test_battery_rdo);
    RUN_TEST(test_pps_rdo_9v_3a);
    RUN_TEST(test_avs_rdo);
    RUN_TEST(test_epr_capable_bit_22);
    RUN_TEST(test_unknown_kind_decodes_only_common_fields);
    RUN_TEST(test_deprecated_copy_mismatch);
    RUN_TEST(test_debug_print_per_kind);
    RUN_TEST(test_wire_request_header);
    RUN_TEST(test_wire_request_rdo_decodes_per_table_6_19);
    return UNITY_END();
}
