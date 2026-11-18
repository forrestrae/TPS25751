/**
 * @file test_TPS25751GPIOStatus.cpp
 * @brief Native (host-run) unit tests for TPS25751GPIOStatus.
 *
 * Run with: pio test -e native
 *
 * Buffers are hand-packed (little-endian) from the GPIO_STATUS (0x72) field
 * table, Source: SLVUCR8B (Rev. B, 2026-08), page 53, via the tps25751-docs MCP:
 *   bits 7:0    GPIO 0-7 Data
 *   bits 9:8    Reserved
 *   bits 12:10  GPIO 10-12 Data (GPIO 10 and 11 are new in Rev. B)
 *   bits 39:32  GPIO 0-7 Direction (1 = output)
 */

#include <unity.h>

#include "TPS25751GPIOStatus.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t GPIO_STATUS_SIZE = TPS25751Registers::Registers::GPIO_STATUS.size;  // 8

// Every data bit 12:0 set, including reserved bits 9:8: byte 0 = 0xFF, byte 1 = 0x1F.
constexpr uint8_t ALL_DATA_BITS[GPIO_STATUS_SIZE] = {0xFF, 0x1F, 0, 0, 0, 0, 0, 0};

// Only GPIO 10 (bit 10) and GPIO 11 (bit 11): byte 1 = 0x0C.
constexpr uint8_t GPIO10_11_HIGH[GPIO_STATUS_SIZE] = {0x00, 0x0C, 0, 0, 0, 0, 0, 0};

// Only reserved bits 9:8: byte 1 = 0x03.
constexpr uint8_t RESERVED_9_8_SET[GPIO_STATUS_SIZE] = {0x00, 0x03, 0, 0, 0, 0, 0, 0};

// GPIO 0 and GPIO 7 configured as outputs: byte 4 = 0x81.
constexpr uint8_t DIR_0_7_OUTPUT[GPIO_STATUS_SIZE] = {0, 0, 0, 0, 0x81, 0, 0, 0};

}  // namespace

void setUp() {}
void tearDown() {}

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751GPIOStatus reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(GPIO_STATUS_SIZE, reg.size());
    for (uint8_t pin = 0; pin <= 12; pin++) {
        TEST_ASSERT_FALSE(reg.gpioData(pin));
    }
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_gpio_10_and_11_data_decode()
{
    TPS25751GPIOStatus reg(GPIO10_11_HIGH);

    TEST_ASSERT_TRUE(reg.gpioData(10));
    TEST_ASSERT_TRUE(reg.gpioData(11));
    TEST_ASSERT_FALSE(reg.gpioData(12));
    for (uint8_t pin = 0; pin <= 7; pin++) {
        TEST_ASSERT_FALSE(reg.gpioData(pin));
    }
}

void test_all_data_bits_decode_for_supported_pins()
{
    TPS25751GPIOStatus reg(ALL_DATA_BITS);

    for (uint8_t pin = 0; pin <= 7; pin++) {
        TEST_ASSERT_TRUE(reg.gpioData(pin));
    }
    TEST_ASSERT_TRUE(reg.gpioData(10));
    TEST_ASSERT_TRUE(reg.gpioData(11));
    TEST_ASSERT_TRUE(reg.gpioData(12));
    TEST_ASSERT_TRUE(reg.gpio12Data());
}

void test_reserved_pins_8_and_9_read_false()
{
    // Bits 9:8 are reserved; gpioData(8) and gpioData(9) must not expose them.
    TPS25751GPIOStatus reg(RESERVED_9_8_SET);

    TEST_ASSERT_FALSE(reg.gpioData(8));
    TEST_ASSERT_FALSE(reg.gpioData(9));
}

void test_out_of_range_pin_reads_false()
{
    TPS25751GPIOStatus reg(ALL_DATA_BITS);

    TEST_ASSERT_FALSE(reg.gpioData(13));
    TEST_ASSERT_FALSE(reg.gpioData(255));
}

void test_direction_decode()
{
    TPS25751GPIOStatus reg(DIR_0_7_OUTPUT);

    TEST_ASSERT_TRUE(reg.gpioDirection(0));
    TEST_ASSERT_TRUE(reg.gpioDirection(7));
    for (uint8_t pin = 1; pin <= 6; pin++) {
        TEST_ASSERT_FALSE(reg.gpioDirection(pin));
    }
    TEST_ASSERT_FALSE(reg.gpioDirection(8));  // no direction bit above GPIO 7
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_gpio_10_and_11_data_decode);
    RUN_TEST(test_all_data_bits_decode_for_supported_pins);
    RUN_TEST(test_reserved_pins_8_and_9_read_false);
    RUN_TEST(test_out_of_range_pin_reads_false);
    RUN_TEST(test_direction_decode);

    return UNITY_END();
}
