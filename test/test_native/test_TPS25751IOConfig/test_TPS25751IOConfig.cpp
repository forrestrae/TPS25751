/**
 * @file test_TPS25751IOConfig.cpp
 * @brief Native (host-run) unit tests for TPS25751IOConfig.
 *
 * Run with: pio test -e native
 *
 * IO_CONFIG (0x5C, 49 bytes / 392 bits) is READ-ONLY -- the TRM states GPIO
 * configuration is fixed at boot. All fields are byte-aligned; bit offsets
 * run well past 255 (e.g. GPIO12 Mapped Event starts at bit 384), which the
 * base-class extractBits*() reaches through its 16-bit bit position (ADR-012).
 * These tests hand-pack a full 49-byte buffer and check every field group by
 * its documented byte offset.
 *
 * FULL_BUFFER layout (byte offsets; all masks are the raw little-endian
 * 13-bit value at that offset pair):
 *   bytes 0-1   outputEnableMask     = 0x1FFF  (all GPIO0-12 bits set)
 *   bytes 4-5   interruptEnableMask  = 0x0055  (bits 0,2,4,6 set)
 *   bytes 8-9   initialValueMask     = 0x1000  (bit 12 only)
 *   bytes 12-13 openDrainEnableMask  = 0x0003  (bits 0,1 set)
 *   bytes 20-21 pullDownEnableMask   = 0x0A0A  (bits 1,3,9,11 set)
 *   bytes 24-25 pullUpEnableMask     = 0x1555  (bits 0,2,4,6,8,10,12 set)
 *   bytes 32-33 eventPolarityMask    = 0x1FFF  (all GPIO0-12 bits set)
 *   byte 28     analog input bits: bit0=1 (GPIO0), bit2=1 (GPIO2),
 *               bit4=0 (GPIO4), bit5=1 (GPIO5), bit6=1 (reserved, must be ignored)
 *               -> byte28 = 0x65
 *   bytes 36-43 GPIO0-7 Mapped Event = 0x01..0x08
 *   bytes 44-45 GPIO8/9 (reserved)   = 0xAA, 0xBB (must never surface)
 *   bytes 46-48 GPIO10-12 Mapped Event = 0x0A, 0x0B, 0x0C
 */

#include <unity.h>

#include "TPS25751IOConfig.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t IO_CONFIG_SIZE = TPS25751Registers::Registers::IO_CONFIG.size;  // 49

// byte:                              0     1     2     3     4     5     6     7
constexpr uint8_t FULL_BUFFER[IO_CONFIG_SIZE] = {
    0xFF, 0x1F, 0x00, 0x00, 0x55, 0x00, 0x00, 0x00,  //  0- 7
    0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,  //  8-15
    0x00, 0x00, 0x00, 0x00, 0x0A, 0x0A, 0x00, 0x00,  // 16-23
    0x55, 0x15, 0x00, 0x00, 0x65, 0x00, 0x00, 0x00,  // 24-31
    0xFF, 0x1F, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,  // 32-39
    0x05, 0x06, 0x07, 0x08, 0xAA, 0xBB, 0x0A, 0x0B,  // 40-47
    0x0C,                                            // 48
};

constexpr uint8_t ALL_ZEROS_DATA[IO_CONFIG_SIZE] = {0};

uint8_t makeAllOnes(size_t i) { (void)i; return 0xFF; }

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751IOConfig reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(IO_CONFIG_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(IO_CONFIG_SIZE));
    TEST_ASSERT_EQUAL_UINT16(0, reg.gpioOutputEnableMask());
}

void test_data_constructor_copies_buffer()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(IO_CONFIG_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(FULL_BUFFER, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(FULL_BUFFER, reg.raw(), IO_CONFIG_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751IOConfig reg(FULL_BUFFER, IO_CONFIG_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(IO_CONFIG_SIZE));
    TEST_ASSERT_EQUAL_UINT16(0x1FFF, reg.gpioOutputEnableMask());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751IOConfig reg(TPS25751Registers::Registers::IO_CONFIG);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(IO_CONFIG_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8(0, reg.gpioMappedEvent(0));
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751IOConfig reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT16(0, reg.gpioOutputEnableMask());
    TEST_ASSERT_FALSE(reg.gpio0AnalogInputEnabled());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[5] = {0xFF, 0x1F, 0x00, 0x00, 0x55};
    TPS25751IOConfig reg(undersized, sizeof(undersized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(IO_CONFIG_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Data validation ---------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    // All-zero is a legitimate configuration (every GPIO left at Hi-Z/disabled).
    TPS25751IOConfig reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    uint8_t allOnes[IO_CONFIG_SIZE];
    for (size_t i = 0; i < IO_CONFIG_SIZE; i++) allOnes[i] = makeAllOnes(i);

    TPS25751IOConfig reg(allOnes, IO_CONFIG_SIZE);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_semantically_valid_with_full_buffer()
{
    TEST_ASSERT_TRUE(TPS25751IOConfig(FULL_BUFFER).isSemanticallyValid());
}

// --- Field decode: 13-bit GPIO bitmaps + per-GPIO helpers -------------------

void test_decode_output_enable_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x1FFF, reg.gpioOutputEnableMask());
    for (uint8_t gpio = 0; gpio <= 12; gpio++) {
        TEST_ASSERT_TRUE_MESSAGE(reg.outputEnabled(gpio), "every GPIO0-12 output-enabled");
    }
    TEST_ASSERT_FALSE(reg.outputEnabled(13));  // out of range
}

void test_decode_interrupt_enable_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x0055, reg.gpioInterruptEnableMask());
    TEST_ASSERT_TRUE(reg.interruptEnabled(0));
    TEST_ASSERT_FALSE(reg.interruptEnabled(1));
    TEST_ASSERT_TRUE(reg.interruptEnabled(2));
    TEST_ASSERT_FALSE(reg.interruptEnabled(3));
    TEST_ASSERT_TRUE(reg.interruptEnabled(4));
    TEST_ASSERT_FALSE(reg.interruptEnabled(5));
    TEST_ASSERT_TRUE(reg.interruptEnabled(6));
    TEST_ASSERT_FALSE(reg.interruptEnabled(7));
    TEST_ASSERT_FALSE(reg.interruptEnabled(12));
}

void test_decode_initial_value_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x1000, reg.gpioInitialValueMask());
    TEST_ASSERT_TRUE(reg.initialValueHigh(12));
    TEST_ASSERT_FALSE(reg.initialValueHigh(11));
    TEST_ASSERT_FALSE(reg.initialValueHigh(0));
}

void test_decode_open_drain_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x0003, reg.gpioOpenDrainEnableMask());
    TEST_ASSERT_TRUE(reg.openDrainEnabled(0));
    TEST_ASSERT_TRUE(reg.openDrainEnabled(1));
    TEST_ASSERT_FALSE(reg.openDrainEnabled(2));
}

void test_decode_pull_down_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x0A0A, reg.gpioPullDownEnableMask());
    TEST_ASSERT_TRUE(reg.pullDownEnabled(1));
    TEST_ASSERT_TRUE(reg.pullDownEnabled(3));
    TEST_ASSERT_TRUE(reg.pullDownEnabled(9));
    TEST_ASSERT_TRUE(reg.pullDownEnabled(11));
    TEST_ASSERT_FALSE(reg.pullDownEnabled(0));
    TEST_ASSERT_FALSE(reg.pullDownEnabled(2));
    TEST_ASSERT_FALSE(reg.pullDownEnabled(12));
}

void test_decode_pull_up_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x1555, reg.gpioPullUpEnableMask());
    TEST_ASSERT_TRUE(reg.pullUpEnabled(0));
    TEST_ASSERT_TRUE(reg.pullUpEnabled(2));
    TEST_ASSERT_TRUE(reg.pullUpEnabled(4));
    TEST_ASSERT_TRUE(reg.pullUpEnabled(6));
    TEST_ASSERT_TRUE(reg.pullUpEnabled(8));
    TEST_ASSERT_TRUE(reg.pullUpEnabled(10));
    TEST_ASSERT_TRUE(reg.pullUpEnabled(12));
    TEST_ASSERT_FALSE(reg.pullUpEnabled(1));
    TEST_ASSERT_FALSE(reg.pullUpEnabled(3));
}

void test_decode_event_polarity_mask_and_per_gpio()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0x1FFF, reg.gpioEventPolarityMask());
    for (uint8_t gpio = 0; gpio <= 12; gpio++) {
        TEST_ASSERT_TRUE(reg.eventPolarityInverted(gpio));
    }
}

// --- Field decode: byte-28 analog-input bits --------------------------------

void test_decode_analog_input_bits()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.gpio0AnalogInputEnabled());   // bit 0
    TEST_ASSERT_TRUE(reg.gpio2AnalogInputEnabled());   // bit 2
    TEST_ASSERT_FALSE(reg.gpio4AnalogInputControl());  // bit 4 (deliberately 0)
    TEST_ASSERT_TRUE(reg.gpio5AnalogInputControl());   // bit 5
}

// --- Field decode: mapped-event bytes (high-offset fields) -----------------

void test_decode_mapped_event_gpio0_through_7()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0x01, reg.gpioMappedEvent(0));
    TEST_ASSERT_EQUAL_UINT8(0x02, reg.gpioMappedEvent(1));
    TEST_ASSERT_EQUAL_UINT8(0x03, reg.gpioMappedEvent(2));
    TEST_ASSERT_EQUAL_UINT8(0x04, reg.gpioMappedEvent(3));
    TEST_ASSERT_EQUAL_UINT8(0x05, reg.gpioMappedEvent(4));
    TEST_ASSERT_EQUAL_UINT8(0x06, reg.gpioMappedEvent(5));
    TEST_ASSERT_EQUAL_UINT8(0x07, reg.gpioMappedEvent(6));
    TEST_ASSERT_EQUAL_UINT8(0x08, reg.gpioMappedEvent(7));
}

void test_decode_mapped_event_gpio10_through_12()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0x0A, reg.gpioMappedEvent(10));
    TEST_ASSERT_EQUAL_UINT8(0x0B, reg.gpioMappedEvent(11));
    TEST_ASSERT_EQUAL_UINT8(0x0C, reg.gpioMappedEvent(12));
}

void test_decode_mapped_event_reserved_gpio8_and_9_return_zero()
{
    // Bytes 44/45 hold 0xAA/0xBB filler, but GPIO8/GPIO9 are reserved (no
    // mapped-event field exists for them) -- the accessor must return 0, not
    // leak the filler bytes.
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0, reg.gpioMappedEvent(8));
    TEST_ASSERT_EQUAL_UINT8(0, reg.gpioMappedEvent(9));
}

void test_decode_mapped_event_out_of_range_returns_zero()
{
    TPS25751IOConfig reg(FULL_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0, reg.gpioMappedEvent(13));
    TEST_ASSERT_EQUAL_UINT8(0, reg.gpioMappedEvent(255));
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751IOConfig original(FULL_BUFFER);
    TPS25751IOConfig copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), IO_CONFIG_SIZE);
    TEST_ASSERT_EQUAL_UINT16(original.gpioOutputEnableMask(), copy.gpioOutputEnableMask());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751IOConfig original(FULL_BUFFER);
    const uint8_t *buf = original.raw();

    TPS25751IOConfig moved(static_cast<TPS25751IOConfig &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0x01, moved.gpioMappedEvent(0));
}

// --- Runner -------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);
    RUN_TEST(test_semantically_valid_with_full_buffer);

    RUN_TEST(test_decode_output_enable_mask_and_per_gpio);
    RUN_TEST(test_decode_interrupt_enable_mask_and_per_gpio);
    RUN_TEST(test_decode_initial_value_mask_and_per_gpio);
    RUN_TEST(test_decode_open_drain_mask_and_per_gpio);
    RUN_TEST(test_decode_pull_down_mask_and_per_gpio);
    RUN_TEST(test_decode_pull_up_mask_and_per_gpio);
    RUN_TEST(test_decode_event_polarity_mask_and_per_gpio);

    RUN_TEST(test_decode_analog_input_bits);

    RUN_TEST(test_decode_mapped_event_gpio0_through_7);
    RUN_TEST(test_decode_mapped_event_gpio10_through_12);
    RUN_TEST(test_decode_mapped_event_reserved_gpio8_and_9_return_zero);
    RUN_TEST(test_decode_mapped_event_out_of_range_returns_zero);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    return UNITY_END();
}
