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
 *   bits 31:28   Object Position
 *   bit 27       Give Back Flag
 *   bit 26       Capability Mismatch (TRM: "Capabality Missmatch")
 *   bit 25       USB Communication Capable
 *   bit 24       No USB Suspend
 *   bit 23       Unchunked Supported
 *   bits 19:10   Operating Current (10 mA/LSB)
 *   bits 9:0     Max Min Operation Current (10 mA/LSB)
 */

#include <unity.h>
#include <cstdio>
#include <string>

#include "TPS25751ActiveRDOContract.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

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

// GiveBack set with Max/Min (200) above Operating Current (100): invalid.
// Word (2<<28)|(1<<27)|(100<<10)|200 = 0x280190C8.
constexpr uint8_t GIVEBACK_MIN_ABOVE_OP[RDO_SIZE] = {0xC8, 0x90, 0x01, 0x28};

// Object Position 0 with non-zero currents (100/100): invalid.
// Word (100<<10)|100 = 0x00019064.
constexpr uint8_t OBJPOS_ZERO[RDO_SIZE] = {0x64, 0x90, 0x01, 0x00};

// Object Position 1 with both currents zero: invalid. Word 0x10000000.
constexpr uint8_t ZERO_CURRENTS[RDO_SIZE] = {0x00, 0x00, 0x00, 0x10};

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_size_is_16_bytes()
{
    TPS25751ActiveRDOContract reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(16, RDO_SIZE);
    TEST_ASSERT_EQUAL_size_t(RDO_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT32(0, reg.requestedRDO());
    TEST_ASSERT_FALSE(reg.hasActiveContract());
}

void test_requested_rdo_decodes_bytes_12_to_15()
{
    TPS25751ActiveRDOContract reg(ACTIVE_AND_REQUESTED);

    TEST_ASSERT_EQUAL_HEX32(0x1234ABCD, reg.requestedRDO());
}

void test_active_rdo_fields_decode_independently_of_requested_rdo()
{
    TPS25751ActiveRDOContract reg(ACTIVE_AND_REQUESTED);

    TEST_ASSERT_EQUAL_UINT8(2, reg.objectPosition());
    TEST_ASSERT_FALSE(reg.giveBackFlag());
    TEST_ASSERT_FALSE(reg.capabilityMismatch());
    TEST_ASSERT_TRUE(reg.usbCommCapable());
    TEST_ASSERT_TRUE(reg.noUSBSuspend());
    TEST_ASSERT_FALSE(reg.unchunkedSupported());

    // 10-bit and 32-bit fields: these returned 0 before they moved from
    // extractBits() to extractBits16()/extractBits32().
    TEST_ASSERT_EQUAL_UINT16(300, reg.operatingCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.operatingCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(300, reg.maxMinCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.maxMinCurrent_mA());
    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_no_contract_when_active_word_is_zero()
{
    TPS25751ActiveRDOContract reg;

    TEST_ASSERT_FALSE(reg.hasActiveContract());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    TEST_ASSERT_TRUE(contains(captureDebugPrint(reg), "No active RDO contract"));
}

void test_giveback_with_min_above_operating_is_semantically_invalid()
{
    TPS25751ActiveRDOContract reg(GIVEBACK_MIN_ABOVE_OP);

    TEST_ASSERT_TRUE(reg.giveBackFlag());
    TEST_ASSERT_EQUAL_UINT16(100, reg.operatingCurrent());
    TEST_ASSERT_EQUAL_UINT16(200, reg.maxMinCurrent());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_object_position_zero_is_semantically_invalid()
{
    TPS25751ActiveRDOContract reg(OBJPOS_ZERO);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT8(0, reg.objectPosition());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_zero_currents_with_contract_is_semantically_invalid()
{
    TPS25751ActiveRDOContract reg(ZERO_CURRENTS);

    TEST_ASSERT_TRUE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT8(1, reg.objectPosition());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_debug_print_active_contract_path()
{
    const std::string out = captureDebugPrint(TPS25751ActiveRDOContract(ACTIVE_AND_REQUESTED));

    TEST_ASSERT_FALSE(contains(out, "No active RDO contract"));
    TEST_ASSERT_TRUE(contains(out, "Requested RDO (as source): 0x1234ABCD"));
    TEST_ASSERT_TRUE(contains(out, "Object Position: 2"));
    TEST_ASSERT_TRUE(contains(out, "Operating Current: 3000 mA"));
    TEST_ASSERT_TRUE(contains(out, "Max/Min Current: 3000 mA"));
}

void test_requested_rdo_alone_does_not_create_active_contract()
{
    // hasActiveContract() looks only at the active RDO word (bits 31:0).
    TPS25751ActiveRDOContract reg(ONLY_REQUESTED);

    TEST_ASSERT_EQUAL_HEX32(0x12345678, reg.requestedRDO());
    TEST_ASSERT_FALSE(reg.hasActiveContract());
    TEST_ASSERT_EQUAL_UINT8(0, reg.objectPosition());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_short_buffer_returns_zero_requested_rdo()
{
    // A 4-byte buffer (the pre-Rev. B assumption) has no bits 127:96.
    const uint8_t shortBuf[4] = {0x2C, 0xB1, 0x04, 0x23};
    TPS25751ActiveRDOContract reg(shortBuf, sizeof(shortBuf));

    TEST_ASSERT_EQUAL_UINT32(0, reg.requestedRDO());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());  // wrong size
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_size_is_16_bytes);
    RUN_TEST(test_requested_rdo_decodes_bytes_12_to_15);
    RUN_TEST(test_active_rdo_fields_decode_independently_of_requested_rdo);
    RUN_TEST(test_requested_rdo_alone_does_not_create_active_contract);
    RUN_TEST(test_no_contract_when_active_word_is_zero);
    RUN_TEST(test_giveback_with_min_above_operating_is_semantically_invalid);
    RUN_TEST(test_object_position_zero_is_semantically_invalid);
    RUN_TEST(test_zero_currents_with_contract_is_semantically_invalid);
    RUN_TEST(test_debug_print_active_contract_path);
    RUN_TEST(test_short_buffer_returns_zero_requested_rdo);

    return UNITY_END();
}
