/**
 * @file test_TPS25751ADCResults.cpp
 * @brief Native (host-run) unit tests for TPS25751ADCResults.
 *
 * Run with: pio test -e native
 *
 * Test buffer is hand-packed from the ADC_RESULTS (0x6A) byte layout,
 * cross-checked against the tps25751-docs MCP register data. Every field is
 * exactly one byte (8 bits) wide; byte 0 is the LSB per the library's
 * little-endian buffer assembly convention. Layout is NOT alphabetical --
 * transcribed exactly as documented:
 *
 *   Byte  Field         Unit / LSB
 *   0     ADCIN1        14 mV/LSB
 *   1     ADCIN2        14 mV/LSB
 *   2     LDO3V3        14 mV/LSB
 *   3     VBUS1         98 mV/LSB
 *   4     Reserved      --
 *   5     IVBUS1        16.5 mA/LSB (raw*33/2, integer truncation)
 *   6     Reserved      --
 *   7     GPIO4         14 mV/LSB
 *   8     GPIO5         14 mV/LSB
 *   9     GPIO0         14 mV/LSB
 *   10    GPIO2         14 mV/LSB
 *   11    IVBUS1_Mean   16.5 mA/LSB (raw*33/2, integer truncation)
 *   12    Reserved      --
 */

#include <unity.h>

#include "TPS25751ADCResults.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t ADC_RESULTS_SIZE = TPS25751Registers::Registers::ADC_RESULTS.size;  // 13

// Hand-packed buffer with a distinct, non-trivial value in every measurement
// byte (reserved bytes get arbitrary filler that must never surface through
// an accessor).
//                                      byte:  0     1     2     3     4     5     6     7     8     9    10    11    12
//                                     field: ADCIN1 ADCIN2 LDO3V3 VBUS1  RSVD  IVBUS1 RSVD  GPIO4 GPIO5 GPIO0 GPIO2 IVMEAN RSVD
constexpr uint8_t FULL_BUFFER[ADC_RESULTS_SIZE] = {0x64, 0x32, 0xC8, 0x0A, 0xAA, 0x03, 0x55, 0x05, 0x07, 0x09, 0x0B, 0xFF, 0x99};

constexpr uint8_t ALL_ZEROS_DATA[ADC_RESULTS_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[ADC_RESULTS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751ADCResults reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(ADC_RESULTS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(ADC_RESULTS_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawADCIN1());
    TEST_ASSERT_EQUAL_UINT16(0, reg.adcin1Millivolts());
}

void test_data_constructor_copies_buffer()
{
    TPS25751ADCResults reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(ADC_RESULTS_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(FULL_BUFFER, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(FULL_BUFFER, reg.raw(), ADC_RESULTS_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751ADCResults reg(FULL_BUFFER, ADC_RESULTS_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(ADC_RESULTS_SIZE));
    TEST_ASSERT_EQUAL_UINT8(0x64, reg.rawADCIN1());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751ADCResults reg(TPS25751Registers::Registers::ADC_RESULTS);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(ADC_RESULTS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawVBUS1());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751ADCResults reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawADCIN1());
    TEST_ASSERT_EQUAL_UINT16(0, reg.vbus1Millivolts());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t oversized[ADC_RESULTS_SIZE + 1] = {0};
    TPS25751ADCResults reg(oversized, sizeof(oversized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(ADC_RESULTS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Data validation ---------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    // hasValidData() only flags all-0xFF as the bus-error signature. An
    // all-zero ADC_RESULTS buffer is the legitimate reset / never-measured
    // state (e.g. the PD controller in Unconnected Sleep, which the TRM
    // states does not update this register) and must remain valid.
    TPS25751ADCResults reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    TPS25751ADCResults reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_semantically_valid_with_hand_packed_data()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Field decode: raw + unit-converted accessors, byte-by-byte ------------

void test_decode_adcin1()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x64, reg.rawADCIN1());          // 100
    TEST_ASSERT_EQUAL_UINT16(1400, reg.adcin1Millivolts());  // 100 * 14
}

void test_decode_adcin2()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x32, reg.rawADCIN2());         // 50
    TEST_ASSERT_EQUAL_UINT16(700, reg.adcin2Millivolts());  // 50 * 14
}

void test_decode_ldo3v3()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0xC8, reg.rawLDO3V3());          // 200
    TEST_ASSERT_EQUAL_UINT16(2800, reg.ldo3v3Millivolts());  // 200 * 14
}

void test_decode_vbus1()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x0A, reg.rawVBUS1());          // 10
    TEST_ASSERT_EQUAL_UINT16(980, reg.vbus1Millivolts());   // 10 * 98
}

void test_decode_ivbus1_truncates_half_lsb()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x03, reg.rawIVBUS1());          // 3
    // 3 * 16.5 = 49.5 mA -> truncates to 49 via integer raw*33/2
    TEST_ASSERT_EQUAL_UINT16(49, reg.ivbus1Milliamps());
}

void test_decode_gpio4()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x05, reg.rawGPIO4());
    TEST_ASSERT_EQUAL_UINT16(70, reg.gpio4Millivolts());  // 5 * 14
}

void test_decode_gpio5()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x07, reg.rawGPIO5());
    TEST_ASSERT_EQUAL_UINT16(98, reg.gpio5Millivolts());  // 7 * 14
}

void test_decode_gpio0()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x09, reg.rawGPIO0());
    TEST_ASSERT_EQUAL_UINT16(126, reg.gpio0Millivolts());  // 9 * 14
}

void test_decode_gpio2()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x0B, reg.rawGPIO2());
    TEST_ASSERT_EQUAL_UINT16(154, reg.gpio2Millivolts());  // 11 * 14
}

void test_decode_ivbus1_mean_max_value_truncates_half_lsb()
{
    TPS25751ADCResults reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.rawIVBUS1Mean());  // 255 (max)
    // 255 * 16.5 = 4207.5 mA -> truncates to 4207 via integer raw*33/2
    TEST_ASSERT_EQUAL_UINT16(4207, reg.ivbus1MeanMilliamps());
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751ADCResults original(FULL_BUFFER);
    TPS25751ADCResults copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), ADC_RESULTS_SIZE);
    TEST_ASSERT_EQUAL_UINT16(original.vbus1Millivolts(), copy.vbus1Millivolts());
}

void test_copy_assignment_deep_copies()
{
    TPS25751ADCResults original(FULL_BUFFER);
    TPS25751ADCResults copy;
    copy = original;

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), ADC_RESULTS_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    TPS25751ADCResults original(FULL_BUFFER);
    const uint8_t *buf = original.raw();

    TPS25751ADCResults moved(static_cast<TPS25751ADCResults &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0x64, moved.rawADCIN1());
}

void test_move_assignment_transfers_ownership()
{
    TPS25751ADCResults original(FULL_BUFFER);
    const uint8_t *buf = original.raw();

    TPS25751ADCResults moved;
    moved = static_cast<TPS25751ADCResults &&>(original);

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0x64, moved.rawADCIN1());
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
    RUN_TEST(test_semantically_valid_with_hand_packed_data);

    RUN_TEST(test_decode_adcin1);
    RUN_TEST(test_decode_adcin2);
    RUN_TEST(test_decode_ldo3v3);
    RUN_TEST(test_decode_vbus1);
    RUN_TEST(test_decode_ivbus1_truncates_half_lsb);
    RUN_TEST(test_decode_gpio4);
    RUN_TEST(test_decode_gpio5);
    RUN_TEST(test_decode_gpio0);
    RUN_TEST(test_decode_gpio2);
    RUN_TEST(test_decode_ivbus1_mean_max_value_truncates_half_lsb);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
