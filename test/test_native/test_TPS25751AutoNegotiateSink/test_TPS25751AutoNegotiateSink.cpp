/**
 * @file test_TPS25751AutoNegotiateSink.cpp
 * @brief Native (host-run) unit tests for TPS25751AutoNegotiateSink.
 *
 * Run with: pio test -e native
 *
 * Test buffers are hand-packed against the AUTONEGOTIATE_SINK (0x37) bit map
 * (already converted from the TRM's high:low ranges to bitPos/bitLen form),
 * cross-checked against the tps25751-docs MCP register data. Byte/bit
 * placement for a bit index `n` is always `byte = n / 8`, `offset = n % 8`
 * (little-endian bit numbering, matching
 * TPS25751Register::extractBits()/setBits()).
 *
 * FULL_BUFFER field values (all raw, little-endian bit-packed):
 *   bit 0  rdoPriorityPrefersLowerVoltage = 1
 *   bit 1  noUSBSuspend                   = 0
 *   bit 2  autoComputeSinkMinPower        = 1
 *   bit 3  noCapabilityMismatch           = 0
 *   bit 4  autoComputeSinkMinVoltage      = 0 (host-provided)
 *   bit 5  autoComputeSinkMaxVoltage      = 0 (host-provided)
 *   bit 6  autoDisableSinkUponCapMismatch = 1
 *   bits 21:12  autoNegMaxCurrent     raw = 0    (new accessor; see reset test)
 *   bits 31:22  sinkMinRequiredPower  raw = 100  -> 25000 mW
 *   bits 41:32  autoNegMaxVoltage     raw = 240  -> 12000 mV
 *   bits 51:42  autoNegMinVoltage     raw = 100  ->  5000 mV  (min <= max)
 *   bits 61:52  capMismatchPower      raw = 50   -> 12500 mW
 *   bit 64  ppsEnableSinkMode              = 1
 *   bits 66:65  ppsRequestInterval    raw = 3    (Interval1s)
 *   bit 67  ppsOperatingModeCV             = 1
 *   bit 68  ppsRequiredFullVoltageRange    = 1
 *   bit 69  ppsDisableSinkUponNonApdo      = 1
 *   bits 102:96 ppsOperatingCurrent   raw = 60   ->  3000 mA
 *   bits 115:105 ppsOperatingVoltage  raw = 200  ->  4000 mV
 */

#include <unity.h>

#include "TPS25751AutoNegotiateSink.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t AN_SINK_SIZE = TPS25751Registers::Registers::AUTONEGOTIATE_SINK.size;  // 24

constexpr uint8_t FULL_BUFFER[AN_SINK_SIZE] = {
    0x45, 0x00, 0x00, 0x19, 0xF0, 0x90, 0x21, 0x03,
    0x3F, 0x00, 0x00, 0x00, 0x3C, 0x90, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Both voltage bounds host-provided (bits 4,5 = 0); min (raw 100) > max (raw 50) -> invalid.
constexpr uint8_t INVALID_MIN_GT_MAX[AN_SINK_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x32, 0x90, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// autoComputeSinkMinVoltage (bit4) = 1 (PD-computed); min (raw 100) > max (raw 50), but
// the cross-field rule only applies when BOTH bounds are host-provided -> stays valid.
constexpr uint8_t VALID_PD_COMPUTED_MIN[AN_SINK_SIZE] = {
    0x10, 0x00, 0x00, 0x00, 0x32, 0x90, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr uint8_t ALL_ZEROS_DATA[AN_SINK_SIZE] = {0};
constexpr uint8_t ALL_ONES_DATA[AN_SINK_SIZE] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751AutoNegotiateSink reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(AN_SINK_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(AN_SINK_SIZE));
    TEST_ASSERT_FALSE(reg.rdoPriorityPrefersLowerVoltage());
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawAutoNegMaxVoltage());
}

void test_data_constructor_copies_buffer()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(AN_SINK_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(FULL_BUFFER, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(FULL_BUFFER, reg.raw(), AN_SINK_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER, AN_SINK_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(AN_SINK_SIZE));
    TEST_ASSERT_TRUE(reg.rdoPriorityPrefersLowerVoltage());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751AutoNegotiateSink reg(TPS25751Registers::Registers::AUTONEGOTIATE_SINK);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(AN_SINK_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.ppsEnableSinkMode());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751AutoNegotiateSink reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.rdoPriorityPrefersLowerVoltage());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[3] = {0x45, 0x00, 0x00};
    TPS25751AutoNegotiateSink reg(undersized, sizeof(undersized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(AN_SINK_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Data validation ---------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    // AUTONEGOTIATE_SINK is "Initialized by Application Customization"; an
    // all-zero buffer (both bounds host-provided, min==max==0) must remain valid.
    TPS25751AutoNegotiateSink reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    TPS25751AutoNegotiateSink reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Field decode: 1-bit fields ----------------------------------------------

void test_decode_1bit_fields()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.rdoPriorityPrefersLowerVoltage());
    TEST_ASSERT_FALSE(reg.noUSBSuspend());
    TEST_ASSERT_TRUE(reg.autoComputeSinkMinPower());
    TEST_ASSERT_FALSE(reg.noCapabilityMismatch());
    TEST_ASSERT_FALSE(reg.autoComputeSinkMinVoltage());
    TEST_ASSERT_FALSE(reg.autoComputeSinkMaxVoltage());
    TEST_ASSERT_TRUE(reg.autoDisableSinkUponCapabilityMismatch());
}

// --- Field decode: scaled multi-bit fields -----------------------------------

void test_decode_sink_min_required_power()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(100, reg.rawSinkMinRequiredPower());
    TEST_ASSERT_EQUAL_UINT32(25000, reg.sinkMinRequiredPower_mW());
}

void test_decode_auto_neg_max_voltage()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(240, reg.rawAutoNegMaxVoltage());
    TEST_ASSERT_EQUAL_UINT16(12000, reg.autoNegMaxVoltage_mV());
}

void test_decode_auto_neg_min_voltage()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(100, reg.rawAutoNegMinVoltage());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.autoNegMinVoltage_mV());
}

void test_decode_cap_mismatch_power()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(50, reg.rawCapMismatchPower());
    TEST_ASSERT_EQUAL_UINT32(12500, reg.capMismatchPower_mW());
}

// --- Field decode: PPS fields --------------------------------------------------

void test_decode_pps_fields()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);

    TEST_ASSERT_TRUE(reg.ppsEnableSinkMode());
    TEST_ASSERT_EQUAL(PpsRequestInterval::Interval1s, reg.ppsRequestInterval());
    TEST_ASSERT_TRUE(reg.ppsOperatingModeConstantVoltage());
    TEST_ASSERT_TRUE(reg.ppsRequiredFullVoltageRange());
    TEST_ASSERT_TRUE(reg.ppsDisableSinkUponNonApdoContract());
}

void test_decode_pps_operating_current()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(60, reg.rawPpsOperatingCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.ppsOperatingCurrent_mA());
}

void test_decode_pps_output_voltage()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(200, reg.rawPpsOperatingVoltage());
    TEST_ASSERT_EQUAL_UINT16(4000, reg.ppsOperatingVoltage_mV());
}

void test_pps_request_interval_enum_bounds()
{
    uint8_t buf[AN_SINK_SIZE] = {0};
    // bits 66:65 = 0 -> Interval8s
    TPS25751AutoNegotiateSink reg0(buf);
    TEST_ASSERT_EQUAL(PpsRequestInterval::Interval8s, reg0.ppsRequestInterval());

    reg0.setPpsRequestInterval(PpsRequestInterval::Interval4s);
    TEST_ASSERT_EQUAL(PpsRequestInterval::Interval4s, reg0.ppsRequestInterval());

    reg0.setPpsRequestInterval(PpsRequestInterval::Interval2s);
    TEST_ASSERT_EQUAL(PpsRequestInterval::Interval2s, reg0.ppsRequestInterval());

    reg0.setPpsRequestInterval(PpsRequestInterval::Interval1s);
    TEST_ASSERT_EQUAL(PpsRequestInterval::Interval1s, reg0.ppsRequestInterval());
}

// --- Semantic validation: cross-field min<=max rule --------------------------

void test_semantic_invalid_when_host_provided_min_gt_max()
{
    TPS25751AutoNegotiateSink reg(INVALID_MIN_GT_MAX);
    TEST_ASSERT_FALSE(reg.autoComputeSinkMinVoltage());
    TEST_ASSERT_FALSE(reg.autoComputeSinkMaxVoltage());
    TEST_ASSERT_TRUE(reg.rawAutoNegMinVoltage() > reg.rawAutoNegMaxVoltage());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_semantic_valid_when_min_pd_computed_despite_min_gt_max()
{
    TPS25751AutoNegotiateSink reg(VALID_PD_COMPUTED_MIN);
    TEST_ASSERT_TRUE(reg.autoComputeSinkMinVoltage());
    TEST_ASSERT_TRUE(reg.rawAutoNegMinVoltage() > reg.rawAutoNegMaxVoltage());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_semantic_valid_with_full_buffer()
{
    TEST_ASSERT_TRUE(TPS25751AutoNegotiateSink(FULL_BUFFER).isSemanticallyValid());
}

// --- Setters: round trip + neighbouring-bit preservation --------------------

void test_set_1bit_fields_round_trip()
{
    TPS25751AutoNegotiateSink reg;  // starts all-zero

    reg.setRdoPriorityPrefersLowerVoltage(true);
    TEST_ASSERT_TRUE(reg.rdoPriorityPrefersLowerVoltage());
    reg.setRdoPriorityPrefersLowerVoltage(false);
    TEST_ASSERT_FALSE(reg.rdoPriorityPrefersLowerVoltage());

    reg.setNoUSBSuspend(true);
    TEST_ASSERT_TRUE(reg.noUSBSuspend());

    reg.setAutoComputeSinkMinPower(true);
    TEST_ASSERT_TRUE(reg.autoComputeSinkMinPower());

    reg.setNoCapabilityMismatch(true);
    TEST_ASSERT_TRUE(reg.noCapabilityMismatch());

    reg.setAutoComputeSinkMinVoltage(true);
    TEST_ASSERT_TRUE(reg.autoComputeSinkMinVoltage());

    reg.setAutoComputeSinkMaxVoltage(true);
    TEST_ASSERT_TRUE(reg.autoComputeSinkMaxVoltage());

    reg.setAutoDisableSinkUponCapabilityMismatch(true);
    TEST_ASSERT_TRUE(reg.autoDisableSinkUponCapabilityMismatch());
}

void test_set_byte0_bit_preserves_other_bits()
{
    TPS25751AutoNegotiateSink reg(ALL_ONES_DATA);

    reg.setRdoPriorityPrefersLowerVoltage(false);  // bit 0, byte 0

    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.raw()[0]);  // 1111_1110
    TEST_ASSERT_FALSE(reg.rdoPriorityPrefersLowerVoltage());
    TEST_ASSERT_TRUE(reg.noUSBSuspend());  // bit 1, same byte, untouched
}

void test_set_raw_sink_min_required_power_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawSinkMinRequiredPower(100);
    TEST_ASSERT_EQUAL_UINT16(100, reg.rawSinkMinRequiredPower());
    TEST_ASSERT_EQUAL_UINT32(25000, reg.sinkMinRequiredPower_mW());
}

void test_set_raw_sink_min_required_power_masks_overwide_value()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawSinkMinRequiredPower(0xFFFF);  // only 10 bits (0x3FF) should stick
    TEST_ASSERT_EQUAL_UINT16(0x3FF, reg.rawSinkMinRequiredPower());
}

void test_set_raw_sink_min_required_power_preserves_neighbouring_fields()
{
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    uint16_t maxVBefore = reg.rawAutoNegMaxVoltage();
    bool bit6Before = reg.autoDisableSinkUponCapabilityMismatch();

    reg.setRawSinkMinRequiredPower(0);

    TEST_ASSERT_EQUAL_UINT16(0, reg.rawSinkMinRequiredPower());
    TEST_ASSERT_EQUAL_UINT16(maxVBefore, reg.rawAutoNegMaxVoltage());
    TEST_ASSERT_EQUAL(bit6Before, reg.autoDisableSinkUponCapabilityMismatch());
}

void test_set_raw_auto_neg_max_voltage_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawAutoNegMaxVoltage(240);
    TEST_ASSERT_EQUAL_UINT16(240, reg.rawAutoNegMaxVoltage());
    TEST_ASSERT_EQUAL_UINT16(12000, reg.autoNegMaxVoltage_mV());
}

void test_set_raw_auto_neg_min_voltage_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawAutoNegMinVoltage(100);
    TEST_ASSERT_EQUAL_UINT16(100, reg.rawAutoNegMinVoltage());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.autoNegMinVoltage_mV());
}

void test_set_raw_cap_mismatch_power_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawCapMismatchPower(50);
    TEST_ASSERT_EQUAL_UINT16(50, reg.rawCapMismatchPower());
    TEST_ASSERT_EQUAL_UINT32(12500, reg.capMismatchPower_mW());
}

void test_set_pps_enable_sink_mode_round_trip_preserves_neighbours()
{
    TPS25751AutoNegotiateSink reg(ALL_ONES_DATA);

    reg.setPpsEnableSinkMode(false);  // bit 64, byte 8

    TEST_ASSERT_FALSE(reg.ppsEnableSinkMode());
    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.raw()[8]);  // 1111_1110
    // Bits 66:65 (ppsRequestInterval), same byte, untouched.
    TEST_ASSERT_EQUAL(PpsRequestInterval::Interval1s, reg.ppsRequestInterval());
}

void test_set_pps_source_operating_mode_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setPpsOperatingModeConstantVoltage(true);
    TEST_ASSERT_TRUE(reg.ppsOperatingModeConstantVoltage());
    reg.setPpsOperatingModeConstantVoltage(false);
    TEST_ASSERT_FALSE(reg.ppsOperatingModeConstantVoltage());
}

void test_set_pps_required_full_voltage_range_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setPpsRequiredFullVoltageRange(true);
    TEST_ASSERT_TRUE(reg.ppsRequiredFullVoltageRange());
}

void test_set_pps_disable_sink_upon_non_apdo_contract_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setPpsDisableSinkUponNonApdoContract(true);
    TEST_ASSERT_TRUE(reg.ppsDisableSinkUponNonApdoContract());
}

void test_set_raw_pps_operating_current_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawPpsOperatingCurrent(60);
    TEST_ASSERT_EQUAL_UINT8(60, reg.rawPpsOperatingCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.ppsOperatingCurrent_mA());
}

void test_set_raw_pps_operating_current_masks_overwide_value()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawPpsOperatingCurrent(0xFF);  // only 7 bits (0x7F) should stick
    TEST_ASSERT_EQUAL_UINT8(0x7F, reg.rawPpsOperatingCurrent());
}

void test_set_raw_pps_operating_voltage_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawPpsOperatingVoltage(200);
    TEST_ASSERT_EQUAL_UINT16(200, reg.rawPpsOperatingVoltage());
    TEST_ASSERT_EQUAL_UINT16(4000, reg.ppsOperatingVoltage_mV());
}

void test_set_raw_pps_operating_voltage_masks_overwide_value()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawPpsOperatingVoltage(0xFFFF);  // only 11 bits (0x7FF) should stick
    TEST_ASSERT_EQUAL_UINT16(0x7FF, reg.rawPpsOperatingVoltage());
}

// --- Rev. B: Auto Neg Max Current (bits 21:12) and reset value ---------------
// Source: SLVUCR8B (Rev. B, 2026-08), page 40.

// Rev. B reset 21919041145076h, little-endian.
constexpr uint8_t REV_B_RESET[AN_SINK_SIZE] = {
    0x76, 0x50, 0x14, 0x41, 0x90, 0x91, 0x21, 0x00,
};

void test_reset_value_decodes_rev_b_defaults()
{
    TPS25751AutoNegotiateSink reg(REV_B_RESET);

    TEST_ASSERT_FALSE(reg.rdoPriorityPrefersLowerVoltage());      // bit 0 = 0
    TEST_ASSERT_TRUE(reg.noUSBSuspend());                         // bit 1 = 1
    TEST_ASSERT_TRUE(reg.autoComputeSinkMinPower());              // bit 2 = 1
    TEST_ASSERT_FALSE(reg.noCapabilityMismatch());                // bit 3 = 0
    TEST_ASSERT_TRUE(reg.autoComputeSinkMinVoltage());            // bit 4 = 1
    TEST_ASSERT_TRUE(reg.autoComputeSinkMaxVoltage());            // bit 5 = 1
    TEST_ASSERT_TRUE(reg.autoDisableSinkUponCapabilityMismatch()); // bit 6 = 1

    TEST_ASSERT_EQUAL_UINT16(0x145, reg.rawAutoNegMaxCurrent());  // bits 21:12
    TEST_ASSERT_EQUAL_UINT16(3250, reg.autoNegMaxCurrent_mA());
    TEST_ASSERT_EQUAL_UINT16(0x104, reg.rawSinkMinRequiredPower());
    TEST_ASSERT_EQUAL_UINT16(0x190, reg.rawAutoNegMaxVoltage());
    TEST_ASSERT_EQUAL_UINT16(0x64, reg.rawAutoNegMinVoltage());
    TEST_ASSERT_EQUAL_UINT16(0x2, reg.rawCapMismatchPower());

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_set_raw_auto_neg_max_current_round_trip()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawAutoNegMaxCurrent(0x145);

    TEST_ASSERT_EQUAL_UINT16(0x145, reg.rawAutoNegMaxCurrent());
    TEST_ASSERT_EQUAL_UINT16(3250, reg.autoNegMaxCurrent_mA());
    // bits 21:12 = 0x145 -> byte1 bits 7:4 = 0x5, byte2 bits 5:0 = 0x14.
    TEST_ASSERT_EQUAL_UINT8(0x50, reg.raw()[1]);
    TEST_ASSERT_EQUAL_UINT8(0x14, reg.raw()[2]);
}

void test_set_raw_auto_neg_max_current_preserves_neighbouring_fields()
{
    TPS25751AutoNegotiateSink reg(REV_B_RESET);
    const uint16_t sinkMinPowerBefore = reg.rawSinkMinRequiredPower();

    reg.setRawAutoNegMaxCurrent(0);

    TEST_ASSERT_EQUAL_UINT16(0, reg.rawAutoNegMaxCurrent());
    TEST_ASSERT_EQUAL_UINT16(sinkMinPowerBefore, reg.rawSinkMinRequiredPower());  // bits 31:22
    TEST_ASSERT_EQUAL_UINT8(0x76, reg.raw()[0]);   // bits 7:0 untouched
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[1]);   // bits 11:8 reserved were 0
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[2]);   // bits 23:22 (Sink Min Power low bits) were 0
}

void test_set_raw_auto_neg_max_current_masks_overwide_value()
{
    TPS25751AutoNegotiateSink reg;
    reg.setRawAutoNegMaxCurrent(0xFFFF);  // only 10 bits (0x3FF) should stick

    TEST_ASSERT_EQUAL_UINT16(0x3FF, reg.rawAutoNegMaxCurrent());
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawSinkMinRequiredPower());
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[0]);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void test_deprecated_pps_aliases_match_rev_b_names()
{
    // Pre-Rev. B names must read and write the same bits (67 and 115:105).
    TPS25751AutoNegotiateSink reg;

    reg.setPpsSourceOperatingModeConstantVoltage(true);
    TEST_ASSERT_TRUE(reg.ppsOperatingModeConstantVoltage());
    TEST_ASSERT_EQUAL(reg.ppsOperatingModeConstantVoltage(), reg.ppsSourceOperatingModeConstantVoltage());
    TEST_ASSERT_EQUAL_UINT8(0x08, reg.raw()[8]);  // bit 67

    reg.setRawPpsOutputVoltage(200);
    TEST_ASSERT_EQUAL_UINT16(200, reg.rawPpsOperatingVoltage());
    TEST_ASSERT_EQUAL_UINT16(reg.rawPpsOperatingVoltage(), reg.rawPpsOutputVoltage());
    TEST_ASSERT_EQUAL_UINT16(reg.ppsOperatingVoltage_mV(), reg.ppsOutputVoltage_mV());
}
#pragma GCC diagnostic pop

void test_setters_no_op_on_invalid_register()
{
    // A moved-from register is !isValid(); setters must guard and no-op.
    TPS25751AutoNegotiateSink reg(FULL_BUFFER);
    TPS25751AutoNegotiateSink moved(static_cast<TPS25751AutoNegotiateSink &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setRdoPriorityPrefersLowerVoltage(false);
    reg.setRawAutoNegMaxVoltage(1);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
    TEST_ASSERT_TRUE(moved.rdoPriorityPrefersLowerVoltage());  // untouched
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751AutoNegotiateSink original(FULL_BUFFER);
    TPS25751AutoNegotiateSink copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), AN_SINK_SIZE);
    TEST_ASSERT_EQUAL_UINT16(original.rawAutoNegMaxVoltage(), copy.rawAutoNegMaxVoltage());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751AutoNegotiateSink original(FULL_BUFFER);
    const uint8_t *buf = original.raw();

    TPS25751AutoNegotiateSink moved(static_cast<TPS25751AutoNegotiateSink &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_TRUE(moved.rdoPriorityPrefersLowerVoltage());
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

    RUN_TEST(test_decode_1bit_fields);
    RUN_TEST(test_decode_sink_min_required_power);
    RUN_TEST(test_decode_auto_neg_max_voltage);
    RUN_TEST(test_decode_auto_neg_min_voltage);
    RUN_TEST(test_decode_cap_mismatch_power);
    RUN_TEST(test_decode_pps_fields);
    RUN_TEST(test_decode_pps_operating_current);
    RUN_TEST(test_decode_pps_output_voltage);
    RUN_TEST(test_pps_request_interval_enum_bounds);

    RUN_TEST(test_semantic_invalid_when_host_provided_min_gt_max);
    RUN_TEST(test_semantic_valid_when_min_pd_computed_despite_min_gt_max);
    RUN_TEST(test_semantic_valid_with_full_buffer);

    RUN_TEST(test_set_1bit_fields_round_trip);
    RUN_TEST(test_set_byte0_bit_preserves_other_bits);
    RUN_TEST(test_set_raw_sink_min_required_power_round_trip);
    RUN_TEST(test_set_raw_sink_min_required_power_masks_overwide_value);
    RUN_TEST(test_set_raw_sink_min_required_power_preserves_neighbouring_fields);
    RUN_TEST(test_set_raw_auto_neg_max_voltage_round_trip);
    RUN_TEST(test_set_raw_auto_neg_min_voltage_round_trip);
    RUN_TEST(test_set_raw_cap_mismatch_power_round_trip);
    RUN_TEST(test_set_pps_enable_sink_mode_round_trip_preserves_neighbours);
    RUN_TEST(test_set_pps_source_operating_mode_round_trip);
    RUN_TEST(test_set_pps_required_full_voltage_range_round_trip);
    RUN_TEST(test_set_pps_disable_sink_upon_non_apdo_contract_round_trip);
    RUN_TEST(test_set_raw_pps_operating_current_round_trip);
    RUN_TEST(test_set_raw_pps_operating_current_masks_overwide_value);
    RUN_TEST(test_set_raw_pps_operating_voltage_round_trip);
    RUN_TEST(test_set_raw_pps_operating_voltage_masks_overwide_value);
    RUN_TEST(test_reset_value_decodes_rev_b_defaults);
    RUN_TEST(test_set_raw_auto_neg_max_current_round_trip);
    RUN_TEST(test_set_raw_auto_neg_max_current_preserves_neighbouring_fields);
    RUN_TEST(test_set_raw_auto_neg_max_current_masks_overwide_value);
    RUN_TEST(test_deprecated_pps_aliases_match_rev_b_names);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    return UNITY_END();
}
