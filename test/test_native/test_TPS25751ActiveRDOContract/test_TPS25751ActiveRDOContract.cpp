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

#include "TPS25751ActiveRDOContract.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

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

    // Not asserted here: operatingCurrent(), maxMinCurrent(), and
    // hasActiveContract() call TPS25751Register::extractBits() with a field
    // width above 8 bits, which that 8-bit extractor rejects (returns 0).
    // That pre-existing defect is unrelated to the Rev. B RequestedRDO field
    // and is tracked as a separate follow-up.
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
    RUN_TEST(test_short_buffer_returns_zero_requested_rdo);

    return UNITY_END();
}
