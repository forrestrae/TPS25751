/**
 * @file test_TPS25751InterruptEvent.cpp
 * @brief Native (host-run) unit tests for TPS25751InterruptEvent.
 *
 * Run with: pio test -e native
 *
 * INT_EVENT1 (0x14) is an 88-bit (11-byte) read-only register. Bit index `n`
 * lives at byte n / 8, offset n % 8. Source: SLVUCR8B (Rev. B, 2026-08),
 * page 19, via the tps25751-docs MCP. Rev. B adds two event bits these tests
 * focus on:
 *   bit 29  MIDB Received (Manufacturer Info message)  -> byte 3, 0x20
 *   bit 51  Discover Mode Completed                    -> byte 6, 0x08
 * Rev. B reset value: 0000000000000000000008h (Plug Insert or Removal, bit 3).
 */

#include <unity.h>

#include "TPS25751InterruptEvent.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t INT_EVENT1_SIZE = TPS25751Registers::Registers::INT_EVENT1.size;  // 11

constexpr uint8_t ONLY_BIT_29[INT_EVENT1_SIZE] = {0, 0, 0, 0x20, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ONLY_BIT_51[INT_EVENT1_SIZE] = {0, 0, 0, 0, 0, 0, 0x08, 0, 0, 0, 0};
constexpr uint8_t RESET_VALUE[INT_EVENT1_SIZE] = {0x08, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

// Bits 29, 30 (CMD1 Complete), 51, 56 (Ext DCDC Sink Safe State).
constexpr uint8_t BITS_29_30_51_56[INT_EVENT1_SIZE] = {0, 0, 0, 0x60, 0, 0, 0x08, 0x01, 0, 0, 0};

}  // namespace

void setUp() {}
void tearDown() {}

void test_default_constructor_has_no_interrupts()
{
    TPS25751InterruptEvent reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_EVENT1_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.hasAnyInterrupt());
    TEST_ASSERT_EQUAL_UINT8(0, reg.getActiveInterruptCount());
    TEST_ASSERT_FALSE(reg.midbReceived());
    TEST_ASSERT_FALSE(reg.discoverModeCompleted());
}

void test_reset_value_reports_plug_insert_or_removal()
{
    TPS25751InterruptEvent reg(RESET_VALUE);

    TEST_ASSERT_TRUE(reg.plugInsertOrRemoval());
    TEST_ASSERT_EQUAL_UINT8(1, reg.getActiveInterruptCount());
}

void test_midb_received_bit_29_decodes()
{
    TPS25751InterruptEvent reg(ONLY_BIT_29);

    TEST_ASSERT_TRUE(reg.midbReceived());
    TEST_ASSERT_FALSE(reg.cmd1Complete());        // neighbour bit 30
    TEST_ASSERT_FALSE(reg.discoverModeCompleted());
}

void test_has_any_interrupt_with_only_bit_29()
{
    TPS25751InterruptEvent reg(ONLY_BIT_29);

    TEST_ASSERT_TRUE(reg.hasAnyInterrupt());
    TEST_ASSERT_EQUAL_UINT8(1, reg.getActiveInterruptCount());
}

void test_discover_mode_completed_bit_51_decodes()
{
    TPS25751InterruptEvent reg(ONLY_BIT_51);

    TEST_ASSERT_TRUE(reg.discoverModeCompleted());
    TEST_ASSERT_FALSE(reg.extDcdcSinkSafeState());  // bit 56
    TEST_ASSERT_FALSE(reg.unableToSourceError());   // bit 46
    TEST_ASSERT_FALSE(reg.midbReceived());
    TEST_ASSERT_TRUE(reg.hasAnyInterrupt());
    TEST_ASSERT_EQUAL_UINT8(1, reg.getActiveInterruptCount());
}

void test_count_includes_new_bits_alongside_neighbours()
{
    TPS25751InterruptEvent reg(BITS_29_30_51_56);

    TEST_ASSERT_TRUE(reg.midbReceived());
    TEST_ASSERT_TRUE(reg.cmd1Complete());
    TEST_ASSERT_TRUE(reg.discoverModeCompleted());
    TEST_ASSERT_TRUE(reg.extDcdcSinkSafeState());
    TEST_ASSERT_EQUAL_UINT8(4, reg.getActiveInterruptCount());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_has_no_interrupts);
    RUN_TEST(test_reset_value_reports_plug_insert_or_removal);
    RUN_TEST(test_midb_received_bit_29_decodes);
    RUN_TEST(test_has_any_interrupt_with_only_bit_29);
    RUN_TEST(test_discover_mode_completed_bit_51_decodes);
    RUN_TEST(test_count_includes_new_bits_alongside_neighbours);

    return UNITY_END();
}
