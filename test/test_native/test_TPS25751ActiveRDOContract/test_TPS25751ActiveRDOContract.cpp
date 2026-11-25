/**
 * @file test_TPS25751ActiveRDOContract.cpp
 * @brief Native (host-run) unit tests for TPS25751ActiveRDOContract.
 *
 * Run with: pio test -e native
 *
 * ACTIVE_RDO_CONTRACT (0x35) is 128 bits (16 bytes), little-endian. Source:
 * SLVUCR8B (Rev. B, 2026-08), page 39, via the tps25751-docs MCP:
 *   bits 127:96  RequestedRDO (RDO requested by the port partner while the
 *                PD controller is the source)
 *   bits 95:32   Reserved
 *   bits 31:0    The RDO of the active contract
 *
 * Both RDOs decode through TPS25751Rdo, whose layout depends on the kind of PDO
 * requested (USB PD R3.3 Tables 6.19 to 6.22), so ownRdo() and requestedRdo() take
 * an RdoKind. With RdoKind::Unknown only the common fields (Object Position and
 * the flags) decode. TRM "Max Min Operation Current" is the specification's
 * deprecated Maximum Operating Current, a copy of the operating value.
 */

#include <unity.h>
#include <cstdio>
#include <string>

#include "TPS25751ActiveRDOContract.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"
#include "TPS25751TransmitSourceCaps.h"
#include "fixtures/PdCapture_2026_11_25_tps25751_iphone.h"
#include "fixtures/PdCapture_2026_11_23_apple_140w_tps25751.h"

namespace {

constexpr size_t RDO_SIZE = TPS25751Registers::Registers::ACTIVE_RDO_CONTRACT.size;  // 16

// Active RDO word 0x2304B12C: Object Position 2, USB Comm Capable, No USB
// Suspend, Operating Current 300 (3000 mA), Max/Min 300 (3000 mA).
//   0x2304B12C = 0010 0011 0000 0100 1011 0001 0010 1100
//   bits 31:28 = 2, bit 25 = 1, bit 24 = 1, bits 19:10 = 0x12C, bits 9:0 = 0x12C
// RequestedRDO 0x1234ABCD in bytes 12-15. Reserved bytes 4-11 are zero.
constexpr uint8_t ACTIVE_AND_REQUESTED[RDO_SIZE] = {
    0x2C, 0xB1, 0x04, 0x23,             // bits 31:0 active RDO
    0x00, 0x00, 0x00, 0x00,             // reserved
    0x00, 0x00, 0x00, 0x00,             // reserved
    0xCD, 0xAB, 0x34, 0x12,             // bits 127:96 RequestedRDO
};

// Only RequestedRDO set; active RDO word all zero.
constexpr uint8_t ONLY_REQUESTED[RDO_SIZE] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12,
};

// The board sinking on a 20 V Fixed contract: both words 0x4184B12C (Object Position
// 4, No USB Suspend, Unchunked, 3000 mA), the way two chargers read on hardware.
constexpr uint8_t BOARD_SINKING[RDO_SIZE] = {
    0x2C, 0xB1, 0x84, 0x41,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x2C, 0xB1, 0x84, 0x41,
};

// A PPS request: Object Position 5, Output Voltage 450 x 20 mV = 9 V, Operating
// Current 60 x 50 mA = 3 A. Word 0x5003843C.
constexpr uint8_t PPS_OWN[RDO_SIZE] = {0x3C, 0x84, 0x03, 0x50};

// Object Position 0 with non-zero currents (100/100): invalid.
// Word (100<<10)|100 = 0x00019064.
constexpr uint8_t OBJPOS_ZERO[RDO_SIZE] = {0x64, 0x90, 0x01, 0x00};

// Object Position 8 (above 7): invalid. Word 0x80019064.
constexpr uint8_t OBJPOS_EIGHT[RDO_SIZE] = {0x64, 0x90, 0x01, 0x80};

// A valid own RDO with Object Position 0 in the RequestedRDO word (0x00019064).
constexpr uint8_t REQUESTED_OBJPOS_ZERO[RDO_SIZE] = {
    0x2C, 0xB1, 0x04, 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0x64, 0x90, 0x01, 0x00,
};

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

void test_size_is_16_bytes()
{
    TPS25751ActiveRDOContract reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(16, RDO_SIZE);
    TEST_ASSERT_EQUAL_size_t(RDO_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT32(0, reg.rawRequestedRdo());
    TEST_ASSERT_EQUAL_UINT32(0, reg.rawOwnRdo());
    TEST_ASSERT_FALSE(reg.hasActiveContract());
}

void test_requested_rdo_decodes_bytes_12_to_15()
{
    TPS25751ActiveRDOContract reg(ACTIVE_AND_REQUESTED);

    TEST_ASSERT_EQUAL_HEX32(0x1234ABCD, reg.rawRequestedRdo());
    TEST_ASSERT_EQUAL_HEX32(0x1234ABCD, reg.requestedRdo().raw());
    TEST_ASSERT_EQUAL_UINT8(1, reg.requestedRdo().objectPosition());
}

void test_own_rdo_fields_decode_independently_of_requested_rdo()
{
    TPS25751ActiveRDOContract reg(ACTIVE_AND_REQUESTED);
    const TPS25751Rdo rdo = reg.ownRdo(RdoKind::FixedVariable);

    TEST_ASSERT_EQUAL_HEX32(0x2304B12C, reg.rawOwnRdo());
    TEST_ASSERT_EQUAL_UINT8(2, rdo.objectPosition());
    TEST_ASSERT_FALSE(rdo.giveback());
    TEST_ASSERT_FALSE(rdo.capabilityMismatch());
    TEST_ASSERT_TRUE(rdo.usbCommunicationsCapable());
    TEST_ASSERT_TRUE(rdo.noUsbSuspend());
    TEST_ASSERT_FALSE(rdo.unchunkedExtendedMessages());
    TEST_ASSERT_EQUAL_UINT16(3000, rdo.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(3000, rdo.maximumOperatingCurrent_mA());
    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_board_sinking_both_words_decode_as_fixed()
{
    TPS25751ActiveRDOContract reg(BOARD_SINKING);

    TEST_ASSERT_EQUAL_HEX32(0x4184B12C, reg.rawOwnRdo());
    TEST_ASSERT_EQUAL_HEX32(0x4184B12C, reg.rawRequestedRdo());
    const TPS25751Rdo own = reg.ownRdo(RdoKind::FixedVariable);
    TEST_ASSERT_EQUAL_UINT16(3000, own.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT8(4, own.objectPosition());
    TEST_ASSERT_TRUE(own.noUsbSuspend());
    TEST_ASSERT_TRUE(own.unchunkedExtendedMessages());
    TEST_ASSERT_TRUE(own.kind() == RdoKind::FixedVariable);
    TEST_ASSERT_EQUAL_UINT16(3000, reg.requestedRdo(RdoKind::FixedVariable).operatingCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_unknown_kind_decodes_only_common_fields()
{
    TPS25751ActiveRDOContract reg(BOARD_SINKING);
    const TPS25751Rdo rdo = reg.ownRdo();

    TEST_ASSERT_TRUE(rdo.kind() == RdoKind::Unknown);
    TEST_ASSERT_EQUAL_UINT8(4, rdo.objectPosition());
    TEST_ASSERT_EQUAL_UINT16(0, rdo.operatingCurrent_mA());
}

void test_pps_own_rdo_decodes_voltage_and_current()
{
    // The old register decoded this word as a Fixed RDO, giving 2250 mA of "operating
    // current" from bits 19:10; as a PPS RDO the fields are 9 V and 3 A.
    TPS25751ActiveRDOContract reg(PPS_OWN);
    const TPS25751Rdo rdo = reg.ownRdo(RdoKind::Pps);

    TEST_ASSERT_EQUAL_HEX32(0x5003843C, reg.rawOwnRdo());
    TEST_ASSERT_EQUAL_UINT8(5, rdo.objectPosition());
    TEST_ASSERT_EQUAL_UINT32(9000, rdo.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, rdo.operatingCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_no_contract_when_active_word_is_zero()
{
    TPS25751ActiveRDOContract reg;

    TEST_ASSERT_FALSE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    TEST_ASSERT_TRUE(contains(captureDebugPrint(reg), "No active RDO contract"));
}

void test_object_position_zero_is_semantically_invalid()
{
    TPS25751ActiveRDOContract reg(OBJPOS_ZERO);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT8(0, reg.ownRdo().objectPosition());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_object_position_above_seven_is_semantically_invalid()
{
    TPS25751ActiveRDOContract reg(OBJPOS_EIGHT);

    TEST_ASSERT_EQUAL_UINT8(8, reg.ownRdo().objectPosition());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_requested_rdo_object_position_zero_is_semantically_invalid()
{
    TPS25751ActiveRDOContract reg(REQUESTED_OBJPOS_ZERO);

    TEST_ASSERT_EQUAL_UINT8(0, reg.requestedRdo().objectPosition());
    TEST_ASSERT_NOT_EQUAL(0, reg.rawRequestedRdo());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_zero_currents_with_contract_are_now_valid()
{
    // Object Position 1 with both currents zero (word 0x10000000) was invalid; the
    // operating-current and GiveBack rules are gone, and Object Position 1 passes.
    const uint8_t zeroCurrents[RDO_SIZE] = {0x00, 0x00, 0x00, 0x10};
    TPS25751ActiveRDOContract reg(zeroCurrents);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_giveback_with_min_above_operating_is_now_valid()
{
    // (2<<28)|(1<<27)|(100<<10)|200 = 0x280190C8: the old GiveBack rule rejected it.
    const uint8_t giveback[RDO_SIZE] = {0xC8, 0x90, 0x01, 0x28};
    TPS25751ActiveRDOContract reg(giveback);

    TEST_ASSERT_TRUE(reg.ownRdo().giveback());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_debug_print_unknown_kinds()
{
    const std::string out = captureDebugPrint(TPS25751ActiveRDOContract(ACTIVE_AND_REQUESTED));

    TEST_ASSERT_TRUE(contains(out, "=== Active RDO Contract Register (0x35) ==="));
    TEST_ASSERT_FALSE(contains(out, "No active RDO contract"));
    TEST_ASSERT_TRUE(contains(out, "  Own RDO (bits 31:0, sent as Sink): 0x2304B12C"));
    TEST_ASSERT_TRUE(contains(out, "  Requested RDO (bits 127:96, a partner's request only while the port is Source): 0x1234ABCD"));
    TEST_ASSERT_TRUE(contains(out, "    RDO 0x2304B12C, decoded as Unknown"));
    TEST_ASSERT_TRUE(contains(out, "    Object Position: 2"));
    TEST_ASSERT_TRUE(contains(out, "Kind unknown: the typed fields need the requested PDO's type"));
    TEST_ASSERT_FALSE(contains(out, "Operating Current"));
}

void test_debug_print_with_kinds_decodes_each_rdo()
{
    const TPS25751ActiveRDOContract reg(BOARD_SINKING);
    const std::string out = captureStdout([&] {
        reg.debugPrint(Serial, RdoKind::FixedVariable, RdoKind::Pps);
    });

    TEST_ASSERT_TRUE(contains(out, "    RDO 0x4184B12C, decoded as Fixed/Variable"));
    TEST_ASSERT_TRUE(contains(out, "    Operating Current: 3000 mA"));
    TEST_ASSERT_TRUE(contains(out, "    RDO 0x4184B12C, decoded as PPS"));
    TEST_ASSERT_TRUE(contains(out, "    Output Voltage: "));
}

void test_debug_print_pps_own_rdo_with_kind()
{
    const TPS25751ActiveRDOContract reg(PPS_OWN);
    const std::string out = captureStdout([&] {
        reg.debugPrint(Serial, RdoKind::Pps, RdoKind::Unknown);
    });

    TEST_ASSERT_TRUE(contains(out, "    Output Voltage: 9000 mV"));
    TEST_ASSERT_TRUE(contains(out, "    Operating Current: 3000 mA"));
    // The RequestedRDO word is zero, so no decoded block follows its line.
    TEST_ASSERT_TRUE(contains(out, "port is Source): 0x00000000"));
    TEST_ASSERT_FALSE(contains(out, "decoded as Unknown"));
}

void test_requested_rdo_alone_does_not_create_active_contract()
{
    // hasActiveContract() looks only at the active RDO word (bits 31:0).
    TPS25751ActiveRDOContract reg(ONLY_REQUESTED);

    TEST_ASSERT_EQUAL_HEX32(0x12345678, reg.rawRequestedRdo());
    TEST_ASSERT_FALSE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT8(0, reg.ownRdo().objectPosition());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());

    // The register is not "No active RDO contract": the requested RDO still prints.
    const std::string out = captureDebugPrint(reg);
    TEST_ASSERT_FALSE(contains(out, "No active RDO contract"));
    TEST_ASSERT_TRUE(contains(out, "Own RDO (bits 31:0, sent as Sink): 0x00000000"));
    TEST_ASSERT_TRUE(contains(out, "Requested RDO (bits 127:96, a partner's request only while the port is Source): 0x12345678"));
}

void test_short_buffer_returns_zero_requested_rdo()
{
    // A 4-byte buffer (the pre-Rev. B assumption) has no bits 127:96.
    const uint8_t shortBuf[4] = {0x2C, 0xB1, 0x04, 0x23};
    TPS25751ActiveRDOContract reg(shortBuf, sizeof(shortBuf));

    TEST_ASSERT_EQUAL_UINT32(0, reg.rawRequestedRdo());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());  // wrong size
}

// --- Capture bundle 2026-11-23-apple-140w-tps25751 ----------------------------------
//
// 0x35 built from the bundle's register bytes: bits 31:0 equal the Request RDO on the
// wire, 0x4184B12C. By hand from USB PD R3.3 Table 6.19 (the requested PDO 4 is Fixed,
// so the Fixed and Variable RDO applies): Object Position 4 (bits 31:28), No USB
// Suspend (bit 24), Unchunked (bit 23), Operating Current 300 (bits 19:10, 3000 mA in
// 10 mA units), and the deprecated Maximum Operating Current copy 300 (bits 9:0). The
// board is sinking, so bits 127:96 read the same word (see BOARD_SINKING).
//
// Because both words are equal here, these tests cannot catch a decoder that swaps the
// own RDO (bits 31:0) and the requested RDO (bits 127:96). The source-role bundle of
// 2026-11-25, with the board as source and an iPhone as sink, does not either: the
// controller reported the iPhone's Request in both words there too.

void test_capture_own_rdo_is_the_wire_request()
{
    TEST_ASSERT_EQUAL_size_t(RDO_SIZE, cap::kRegister0x35Length);
    TPS25751ActiveRDOContract reg(cap::kRegister0x35, cap::kRegister0x35Length);

    TEST_ASSERT_EQUAL_HEX32(cap::kRequestRdo, reg.rawOwnRdo());
    TEST_ASSERT_EQUAL_HEX32(cap::kRequestRdo, reg.rawRequestedRdo());
    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_capture_own_rdo_fields_per_table_6_19()
{
    TPS25751ActiveRDOContract reg(cap::kRegister0x35, cap::kRegister0x35Length);
    TEST_ASSERT_EQUAL_UINT8(4, reg.ownRdo().objectPosition());
    // Object Position 4 names wire PDO 4, Fixed 20 V, so the RDO is Fixed/Variable.
    const RdoKind kind = rdoKindFor(cap::kSourceCapabilitiesPdos[4 - 1]);
    TEST_ASSERT_TRUE(RdoKind::FixedVariable == kind);

    const TPS25751Rdo own = reg.ownRdo(kind);
    TEST_ASSERT_EQUAL_UINT8(4, own.objectPosition());
    TEST_ASSERT_FALSE(own.giveback());
    TEST_ASSERT_FALSE(own.capabilityMismatch());
    TEST_ASSERT_FALSE(own.usbCommunicationsCapable());
    TEST_ASSERT_TRUE(own.noUsbSuspend());
    TEST_ASSERT_TRUE(own.unchunkedExtendedMessages());
    TEST_ASSERT_FALSE(own.eprCapable());
    TEST_ASSERT_EQUAL_UINT16(3000, own.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(3000, own.maximumOperatingCurrent_mA());
}

// ---------------------------------------------------------------------------
// Source-role capture: the board as Source, an iPhone as Sink
// ---------------------------------------------------------------------------
//
// STATUS reported the port role as Source, so bits 127:96 hold the partner's request:
// the iPhone's final Request on the wire, 0x1384B12C (header 0x1A82, bit 8 = 0, sent
// as Sink). By hand from USB PD R3.3 Table 6.19 (Object Position 1 names the board's
// Fixed 5 V PDO): Object Position 1, USB Communications Capable (bit 25), No USB
// Suspend (bit 24), Unchunked (bit 23), Operating Current 300 (3000 mA), and the
// deprecated Maximum Operating Current copy 300.
//
// On this hardware bits 31:0 read the same word, so this bundle, like the sink one,
// cannot catch a decoder that swaps the two words; the assertion below records that.

void test_capture_sourcing_requested_rdo_is_the_partners_final_request()
{
    TPS25751ActiveRDOContract reg(iphone::kRegister0x35, iphone::kRegister0x35Length);
    TPS25751TransmitSourceCaps caps(iphone::kRegister0x32, iphone::kRegister0x32Length);

    TEST_ASSERT_EQUAL_HEX16(0, iphone::kRequestHeader & 0x0100);  // sent as Sink
    TEST_ASSERT_EQUAL_HEX32(iphone::kRequestRdo, reg.rawRequestedRdo());
    TEST_ASSERT_EQUAL_HEX32(0x1384B12C, reg.rawRequestedRdo());
    TEST_ASSERT_EQUAL_HEX32(iphone::kRequestRdo, reg.rawOwnRdo());  // observed: the same word

    const RdoKind kind = rdoKindFor(caps.getPDO(1 - 1));
    TEST_ASSERT_TRUE(RdoKind::FixedVariable == kind);
    const TPS25751Rdo requested = reg.requestedRdo(kind);
    TEST_ASSERT_EQUAL_UINT8(1, requested.objectPosition());
    TEST_ASSERT_FALSE(requested.capabilityMismatch());
    TEST_ASSERT_TRUE(requested.usbCommunicationsCapable());
    TEST_ASSERT_TRUE(requested.noUsbSuspend());
    TEST_ASSERT_TRUE(requested.unchunkedExtendedMessages());
    TEST_ASSERT_FALSE(requested.eprCapable());
    TEST_ASSERT_EQUAL_UINT16(3000, requested.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(3000, requested.maximumOperatingCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_size_is_16_bytes);
    RUN_TEST(test_requested_rdo_decodes_bytes_12_to_15);
    RUN_TEST(test_own_rdo_fields_decode_independently_of_requested_rdo);
    RUN_TEST(test_board_sinking_both_words_decode_as_fixed);
    RUN_TEST(test_unknown_kind_decodes_only_common_fields);
    RUN_TEST(test_pps_own_rdo_decodes_voltage_and_current);
    RUN_TEST(test_requested_rdo_alone_does_not_create_active_contract);
    RUN_TEST(test_no_contract_when_active_word_is_zero);
    RUN_TEST(test_object_position_zero_is_semantically_invalid);
    RUN_TEST(test_object_position_above_seven_is_semantically_invalid);
    RUN_TEST(test_requested_rdo_object_position_zero_is_semantically_invalid);
    RUN_TEST(test_zero_currents_with_contract_are_now_valid);
    RUN_TEST(test_giveback_with_min_above_operating_is_now_valid);
    RUN_TEST(test_debug_print_unknown_kinds);
    RUN_TEST(test_debug_print_with_kinds_decodes_each_rdo);
    RUN_TEST(test_debug_print_pps_own_rdo_with_kind);
    RUN_TEST(test_short_buffer_returns_zero_requested_rdo);
    RUN_TEST(test_capture_own_rdo_is_the_wire_request);
    RUN_TEST(test_capture_own_rdo_fields_per_table_6_19);

    RUN_TEST(test_capture_sourcing_requested_rdo_is_the_partners_final_request);
    return UNITY_END();
}
