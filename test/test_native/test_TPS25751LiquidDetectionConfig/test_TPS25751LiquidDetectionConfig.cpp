/**
 * @file test_TPS25751LiquidDetectionConfig.cpp
 * @brief Native (host-run) unit tests for TPS25751LiquidDetectionConfig.
 *
 * Run with: pio test -e native
 *
 * Test buffers are hand-packed against the LIQUID_DETECTION_CONFIG (0x98) bit
 * map (already converted from the TRM's high:low ranges to bitPos/bitLen
 * form), cross-checked against the tps25751-docs MCP register data. Byte/bit
 * placement for a bit index `n` is always `byte = n / 8`, `offset = n % 8`
 * (little-endian bit numbering, matching
 * TPS25751Register::extractBits()/setBits()).
 *
 * FULL_BUFFER field values (all raw, little-endian bit-packed):
 *   bits 15:0   waitTimeNoLiquid       raw = 300  -> 300000 ms  (byte0=0x2C, byte1=0x01)
 *   bits 31:16  waitTimeLiquid         raw = 60   ->  60000 ms  (byte2=0x3C, byte3=0x00)
 *   bits 39:32  numberOfSamples         raw = 8               (byte4=0x08)
 *   bits 47:40  lowThresholdNoLiquid    raw = 50   ->   700 mV  (byte5=0x32)
 *   bits 55:48  highThresholdNoLiquid   raw = 100  ->  1400 mV  (byte6=0x64)
 *   bits 63:56  lowThresholdLiquid      raw = 150  ->  2100 mV  (byte7=0x96)
 *   bits 71:64  highThresholdLiquid     raw = 200  ->  2800 mV  (byte8=0xC8)
 *   bits 75:72  sampleTimeNoLiquid      raw = 5    ->    50 ms  (byte9 low nibble)
 *   bits 79:76  sampleTimeLiquid        raw = 10   ->   100 ms  (byte9 high nibble = 0xA5)
 *   bit 80      liquidDetectionState         = 1
 *   bit 81      enableCorrosionMitigation    = 1
 *   bit 82      enableLiquidDetection        = 0   (byte10 = 0b011 = 0x03)
 */

#include <unity.h>

#include "TPS25751LiquidDetectionConfig.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t LDC_SIZE = TPS25751Registers::Registers::LIQUID_DETECTION_CONFIG.size;  // 12, the hardware length byte

constexpr uint8_t FULL_BUFFER[LDC_SIZE] = {
    0x2C, 0x01, 0x3C, 0x00, 0x08, 0x32, 0x64, 0x96, 0xC8, 0xA5, 0x03,
};

// lowThresholdNoLiquid (raw 200) > highThresholdNoLiquid (raw 100) -> invalid.
constexpr uint8_t INVALID_NO_LIQUID_LOW_GT_HIGH[LDC_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x64, 0x00, 0x00, 0x00, 0x00,
};

// lowThresholdLiquid (raw 200) > highThresholdLiquid (raw 100) -> invalid.
constexpr uint8_t INVALID_LIQUID_LOW_GT_HIGH[LDC_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x64, 0x00, 0x00,
};

constexpr uint8_t ALL_ZEROS_DATA[LDC_SIZE] = {0};
constexpr uint8_t ALL_ONES_DATA[LDC_SIZE] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751LiquidDetectionConfig reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(LDC_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(LDC_SIZE));
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawWaitTimeNoLiquid());
    TEST_ASSERT_FALSE(reg.enableLiquidDetection());
}

void test_data_constructor_copies_buffer()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(LDC_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(FULL_BUFFER, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(FULL_BUFFER, reg.raw(), LDC_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER, LDC_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(LDC_SIZE));
    TEST_ASSERT_EQUAL_UINT16(300, reg.rawWaitTimeNoLiquid());
}

// The TRM gives 83 bits (11 bytes), but the controller's length byte is 12
// (bench read, 2026-11-18). TPS25751::readRegister() rejects any mismatch, so an
// 11-byte RegisterInfo made every 0x98 read fail.
void test_register_size_matches_hardware_length_byte()
{
    TEST_ASSERT_EQUAL_size_t(12, LDC_SIZE);
    TEST_ASSERT_EQUAL_UINT8(12, TPS25751Registers::getRegisterSize(TPS25751Registers::Address::LIQUID_DETECTION_CONFIG));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751LiquidDetectionConfig reg(TPS25751Registers::Registers::LIQUID_DETECTION_CONFIG);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(LDC_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.liquidDetectionState());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751LiquidDetectionConfig reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.numberOfSamples());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[3] = {0x2C, 0x01, 0x3C};
    TPS25751LiquidDetectionConfig reg(undersized, sizeof(undersized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(LDC_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Data validation ---------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    // Reset is all-zero; both threshold pairs trivially satisfy low <= high.
    TPS25751LiquidDetectionConfig reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    TPS25751LiquidDetectionConfig reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Field decode: wait-interval fields --------------------------------------

void test_decode_wait_time_no_liquid()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(300, reg.rawWaitTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT32(300000, reg.waitTimeNoLiquid_ms());
}

void test_decode_wait_time_liquid()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(60, reg.rawWaitTimeLiquid());
    TEST_ASSERT_EQUAL_UINT32(60000, reg.waitTimeLiquid_ms());
}

// --- Field decode: sampling configuration -------------------------------------

void test_decode_number_of_samples()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(8, reg.numberOfSamples());
}

// --- Field decode: ADC threshold fields ---------------------------------------

void test_decode_no_liquid_thresholds()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(50, reg.rawLowThresholdNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(700, reg.lowThresholdNoLiquid_mV());
    TEST_ASSERT_EQUAL_UINT8(100, reg.rawHighThresholdNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(1400, reg.highThresholdNoLiquid_mV());
}

void test_decode_liquid_thresholds()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(150, reg.rawLowThresholdLiquid());
    TEST_ASSERT_EQUAL_UINT16(2100, reg.lowThresholdLiquid_mV());
    TEST_ASSERT_EQUAL_UINT8(200, reg.rawHighThresholdLiquid());
    TEST_ASSERT_EQUAL_UINT16(2800, reg.highThresholdLiquid_mV());
}

// --- Field decode: ADC sample-time fields -------------------------------------

void test_decode_sample_times()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(5, reg.rawSampleTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(50, reg.sampleTimeNoLiquid_ms());
    TEST_ASSERT_EQUAL_UINT8(10, reg.rawSampleTimeLiquid());
    TEST_ASSERT_EQUAL_UINT16(100, reg.sampleTimeLiquid_ms());
}

// --- Field decode: state / enable bits ----------------------------------------

void test_decode_state_and_enable_bits()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TEST_ASSERT_TRUE(reg.liquidDetectionState());
    TEST_ASSERT_TRUE(reg.enableCorrosionMitigation());
    TEST_ASSERT_FALSE(reg.enableLiquidDetection());
}

// --- Semantic validation: cross-field low<=high rules -------------------------

void test_semantic_invalid_when_no_liquid_low_gt_high()
{
    TPS25751LiquidDetectionConfig reg(INVALID_NO_LIQUID_LOW_GT_HIGH);
    TEST_ASSERT_TRUE(reg.rawLowThresholdNoLiquid() > reg.rawHighThresholdNoLiquid());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_semantic_invalid_when_liquid_low_gt_high()
{
    TPS25751LiquidDetectionConfig reg(INVALID_LIQUID_LOW_GT_HIGH);
    TEST_ASSERT_TRUE(reg.rawLowThresholdLiquid() > reg.rawHighThresholdLiquid());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_semantic_valid_with_full_buffer()
{
    TEST_ASSERT_TRUE(TPS25751LiquidDetectionConfig(FULL_BUFFER).isSemanticallyValid());
}

// --- Setters: round trip + neighbouring-field preservation --------------------

void test_set_raw_wait_time_no_liquid_round_trip()
{
    TPS25751LiquidDetectionConfig reg;
    reg.setRawWaitTimeNoLiquid(300);
    TEST_ASSERT_EQUAL_UINT16(300, reg.rawWaitTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT32(300000, reg.waitTimeNoLiquid_ms());
}

void test_set_raw_wait_time_no_liquid_preserves_neighbouring_field()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    uint16_t waitLiquidBefore = reg.rawWaitTimeLiquid();

    reg.setRawWaitTimeNoLiquid(0);

    TEST_ASSERT_EQUAL_UINT16(0, reg.rawWaitTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(waitLiquidBefore, reg.rawWaitTimeLiquid());
}

void test_set_raw_wait_time_liquid_round_trip()
{
    TPS25751LiquidDetectionConfig reg;
    reg.setRawWaitTimeLiquid(60);
    TEST_ASSERT_EQUAL_UINT16(60, reg.rawWaitTimeLiquid());
    TEST_ASSERT_EQUAL_UINT32(60000, reg.waitTimeLiquid_ms());
}

void test_set_number_of_samples_round_trip()
{
    TPS25751LiquidDetectionConfig reg;
    reg.setNumberOfSamples(8);
    TEST_ASSERT_EQUAL_UINT8(8, reg.numberOfSamples());
}

void test_set_no_liquid_thresholds_round_trip()
{
    TPS25751LiquidDetectionConfig reg;
    reg.setRawLowThresholdNoLiquid(50);
    reg.setRawHighThresholdNoLiquid(100);
    TEST_ASSERT_EQUAL_UINT8(50, reg.rawLowThresholdNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(700, reg.lowThresholdNoLiquid_mV());
    TEST_ASSERT_EQUAL_UINT8(100, reg.rawHighThresholdNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(1400, reg.highThresholdNoLiquid_mV());
}

void test_set_liquid_thresholds_round_trip()
{
    TPS25751LiquidDetectionConfig reg;
    reg.setRawLowThresholdLiquid(150);
    reg.setRawHighThresholdLiquid(200);
    TEST_ASSERT_EQUAL_UINT8(150, reg.rawLowThresholdLiquid());
    TEST_ASSERT_EQUAL_UINT16(2100, reg.lowThresholdLiquid_mV());
    TEST_ASSERT_EQUAL_UINT8(200, reg.rawHighThresholdLiquid());
    TEST_ASSERT_EQUAL_UINT16(2800, reg.highThresholdLiquid_mV());
}

void test_set_threshold_preserves_neighbouring_field()
{
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    uint8_t highNoLiquidBefore = reg.rawHighThresholdNoLiquid();

    reg.setRawLowThresholdNoLiquid(0);

    TEST_ASSERT_EQUAL_UINT8(0, reg.rawLowThresholdNoLiquid());
    TEST_ASSERT_EQUAL_UINT8(highNoLiquidBefore, reg.rawHighThresholdNoLiquid());
}

void test_set_sample_times_round_trip_preserves_neighbour()
{
    TPS25751LiquidDetectionConfig reg;  // starts all-zero

    reg.setRawSampleTimeNoLiquid(5);
    TEST_ASSERT_EQUAL_UINT8(5, reg.rawSampleTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(50, reg.sampleTimeNoLiquid_ms());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawSampleTimeLiquid());  // untouched, same byte

    reg.setRawSampleTimeLiquid(10);
    TEST_ASSERT_EQUAL_UINT8(10, reg.rawSampleTimeLiquid());
    TEST_ASSERT_EQUAL_UINT16(100, reg.sampleTimeLiquid_ms());
    TEST_ASSERT_EQUAL_UINT8(5, reg.rawSampleTimeNoLiquid());  // still untouched
}

void test_set_sample_time_masks_overwide_value()
{
    TPS25751LiquidDetectionConfig reg;
    reg.setRawSampleTimeNoLiquid(0xFF);  // only 4 bits (0xF) should stick
    TEST_ASSERT_EQUAL_UINT8(0x0F, reg.rawSampleTimeNoLiquid());
}

void test_set_state_and_enable_bits_round_trip()
{
    TPS25751LiquidDetectionConfig reg;  // starts all-zero

    reg.setLiquidDetectionState(true);
    TEST_ASSERT_TRUE(reg.liquidDetectionState());
    reg.setLiquidDetectionState(false);
    TEST_ASSERT_FALSE(reg.liquidDetectionState());

    reg.setEnableCorrosionMitigation(true);
    TEST_ASSERT_TRUE(reg.enableCorrosionMitigation());

    reg.setEnableLiquidDetection(true);
    TEST_ASSERT_TRUE(reg.enableLiquidDetection());
}

void test_set_state_bit_preserves_other_bits()
{
    TPS25751LiquidDetectionConfig reg(ALL_ONES_DATA);

    reg.setLiquidDetectionState(false);  // bit 80, byte 10

    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.raw()[10]);  // 1111_1110
    TEST_ASSERT_FALSE(reg.liquidDetectionState());
    TEST_ASSERT_TRUE(reg.enableCorrosionMitigation());  // bit 81, same byte, untouched
    TEST_ASSERT_TRUE(reg.enableLiquidDetection());       // bit 82, same byte, untouched
}

void test_setters_no_op_on_invalid_register()
{
    // A moved-from register is !isValid(); setters must guard and no-op.
    TPS25751LiquidDetectionConfig reg(FULL_BUFFER);
    TPS25751LiquidDetectionConfig moved(static_cast<TPS25751LiquidDetectionConfig &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setLiquidDetectionState(false);
    reg.setRawWaitTimeNoLiquid(1);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
    TEST_ASSERT_EQUAL_UINT16(300, moved.rawWaitTimeNoLiquid());  // untouched
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751LiquidDetectionConfig original(FULL_BUFFER);
    TPS25751LiquidDetectionConfig copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), LDC_SIZE);
    TEST_ASSERT_EQUAL_UINT16(original.rawWaitTimeNoLiquid(), copy.rawWaitTimeNoLiquid());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751LiquidDetectionConfig original(FULL_BUFFER);
    const uint8_t *buf = original.raw();

    TPS25751LiquidDetectionConfig moved(static_cast<TPS25751LiquidDetectionConfig &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT16(300, moved.rawWaitTimeNoLiquid());
}

// --- Deprecated aliases (pre-Rev. B "Sleep Time" names) ------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void test_deprecated_sleep_time_aliases_match_wait_time()
{
    // The Rev. A names must read and write the same bits as the Rev. B
    // "Wait Time" names (SLVUCR8B p.60: bits 15:0 and 31:16, 1000 ms/LSB).
    TPS25751LiquidDetectionConfig reg;

    reg.setRawSleepTimeNoLiquid(300);
    reg.setRawSleepTimeLiquid(60);
    TEST_ASSERT_EQUAL_UINT16(300, reg.rawWaitTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT16(60, reg.rawWaitTimeLiquid());
    TEST_ASSERT_EQUAL_UINT8(0x2C, reg.raw()[0]);
    TEST_ASSERT_EQUAL_UINT8(0x01, reg.raw()[1]);
    TEST_ASSERT_EQUAL_UINT8(0x3C, reg.raw()[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[3]);

    TEST_ASSERT_EQUAL_UINT16(reg.rawWaitTimeNoLiquid(), reg.rawSleepTimeNoLiquid());
    TEST_ASSERT_EQUAL_UINT32(reg.waitTimeNoLiquid_ms(), reg.sleepTimeNoLiquid_ms());
    TEST_ASSERT_EQUAL_UINT16(reg.rawWaitTimeLiquid(), reg.rawSleepTimeLiquid());
    TEST_ASSERT_EQUAL_UINT32(reg.waitTimeLiquid_ms(), reg.sleepTimeLiquid_ms());
}
#pragma GCC diagnostic pop

// --- Runner -------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_register_size_matches_hardware_length_byte);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);

    RUN_TEST(test_decode_wait_time_no_liquid);
    RUN_TEST(test_decode_wait_time_liquid);
    RUN_TEST(test_decode_number_of_samples);
    RUN_TEST(test_decode_no_liquid_thresholds);
    RUN_TEST(test_decode_liquid_thresholds);
    RUN_TEST(test_decode_sample_times);
    RUN_TEST(test_decode_state_and_enable_bits);

    RUN_TEST(test_semantic_invalid_when_no_liquid_low_gt_high);
    RUN_TEST(test_semantic_invalid_when_liquid_low_gt_high);
    RUN_TEST(test_semantic_valid_with_full_buffer);

    RUN_TEST(test_set_raw_wait_time_no_liquid_round_trip);
    RUN_TEST(test_set_raw_wait_time_no_liquid_preserves_neighbouring_field);
    RUN_TEST(test_set_raw_wait_time_liquid_round_trip);
    RUN_TEST(test_set_number_of_samples_round_trip);
    RUN_TEST(test_set_no_liquid_thresholds_round_trip);
    RUN_TEST(test_set_liquid_thresholds_round_trip);
    RUN_TEST(test_set_threshold_preserves_neighbouring_field);
    RUN_TEST(test_set_sample_times_round_trip_preserves_neighbour);
    RUN_TEST(test_set_sample_time_masks_overwide_value);
    RUN_TEST(test_set_state_and_enable_bits_round_trip);
    RUN_TEST(test_set_state_bit_preserves_other_bits);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    RUN_TEST(test_deprecated_sleep_time_aliases_match_wait_time);

    return UNITY_END();
}
