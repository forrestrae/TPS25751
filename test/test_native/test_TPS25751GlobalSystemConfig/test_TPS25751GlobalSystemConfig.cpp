/**
 * @file test_TPS25751GlobalSystemConfig.cpp
 * @brief Native (host-run) unit tests for TPS25751GlobalSystemConfig.
 *
 * Run with: pio test -e native
 *
 * GLOBAL_SYSTEM_CONFIGURATION (0x27) is a 14-byte R/W register. The TRM gives
 * 116 bits (15 bytes), but the controller reports a 14-byte length on hardware
 * (2026-11-18). Bit n lives in byte n / 8, bit n % 8.
 *
 * Reset 0x28101 is {0x01, 0x81, 0x02, 0x00 x 11}. The bench read from a
 * TPS25751 in APP mode was 05 09 14 00 1C 03 00 x 8: the firmware sets bits in
 * every TRM-reserved range, so validation checks only the PP3 and PP1 codes.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "TPS25751GlobalSystemConfig.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t GSC_SIZE = TPS25751Registers::Registers::GLOBAL_SYSTEM_CONFIGURATION.size;  // 14

constexpr uint8_t RESET_BUFFER[GSC_SIZE] = {0x01, 0x81, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t BENCH_BUFFER[GSC_SIZE] = {0x05, 0x09, 0x14, 0x00, 0x1C, 0x03, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[GSC_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                             0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void copyBuf(uint8_t *out, const uint8_t *in) { memcpy(out, in, GSC_SIZE); }

void setBit(uint8_t *buf, unsigned bit) { buf[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8)); }

void clearBit(uint8_t *buf, unsigned bit) { buf[bit / 8] &= static_cast<uint8_t>(~(1u << (bit % 8))); }

// Reset fixture with the 3-bit field at bits 18:16 or 10:8 replaced.
TPS25751GlobalSystemConfig resetWithField(unsigned lowBit, uint8_t code)
{
    uint8_t buf[GSC_SIZE];
    copyBuf(buf, RESET_BUFFER);
    for (unsigned i = 0; i < 3; i++) clearBit(buf, lowBit + i);
    for (unsigned i = 0; i < 3; i++)
        if (code & (1u << i)) setBit(buf, lowBit + i);
    return TPS25751GlobalSystemConfig(buf);
}

// Copy of `buf` with the bits [lowBit, lowBit+len) cleared.
void clearRange(uint8_t *buf, unsigned lowBit, unsigned len)
{
    for (unsigned i = 0; i < len; i++) clearBit(buf, lowBit + i);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Identity --------------------------------------------------------------------

void test_address_identity()
{
    TEST_ASSERT_TRUE(TPS25751GlobalSystemConfig::kAddress == TPS25751Registers::Address::GLOBAL_SYSTEM_CONFIGURATION);
    TEST_ASSERT_EQUAL_UINT8(0x27, static_cast<uint8_t>(TPS25751GlobalSystemConfig::kAddress));
    TEST_ASSERT_EQUAL_UINT8(14, TPS25751Registers::getRegisterSize(TPS25751GlobalSystemConfig::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(TPS25751GlobalSystemConfig::kAddress));
}

// --- Construction ----------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751GlobalSystemConfig reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(GSC_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(GSC_SIZE));
    TEST_ASSERT_FALSE(reg.ppCable1SwitchEnabled());
}

void test_data_constructor_copies_buffer()
{
    TPS25751GlobalSystemConfig reg(RESET_BUFFER);

    TEST_ASSERT_NOT_EQUAL(RESET_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), GSC_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751GlobalSystemConfig reg(RESET_BUFFER, GSC_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(GSC_SIZE));
    TEST_ASSERT_TRUE(reg.ppCable1SwitchEnabled());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751GlobalSystemConfig reg(TPS25751Registers::Registers::GLOBAL_SYSTEM_CONFIGURATION);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(GSC_SIZE, reg.size());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[3] = {0x01, 0x81, 0x02};
    TPS25751GlobalSystemConfig reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(GSC_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Decode ----------------------------------------------------------------------

void test_reset_value_decodes()
{
    TPS25751GlobalSystemConfig reg(RESET_BUFFER);

    TEST_ASSERT_TRUE(reg.ppCable1SwitchEnabled());
    TEST_ASSERT_TRUE(reg.pp1Config() == Pp1Config::Source);
    TEST_ASSERT_TRUE(reg.ilimOvershoot() == IlimOvershoot::mA200);
    TEST_ASSERT_TRUE(reg.pp3Config() == Pp3Config::Sink);
    TEST_ASSERT_TRUE(reg.rcpThreshold() == RcpThreshold::mV6);
    TEST_ASSERT_EQUAL_UINT8(2, reg.rawPp3Config());
    TEST_ASSERT_EQUAL_UINT8(1, reg.rawPp1Config());

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_bench_value_decodes_and_is_valid()
{
    TPS25751GlobalSystemConfig reg(BENCH_BUFFER);

    TEST_ASSERT_TRUE(reg.ppCable1SwitchEnabled());
    TEST_ASSERT_TRUE(reg.pp1Config() == Pp1Config::Source);
    TEST_ASSERT_TRUE(reg.ilimOvershoot() == IlimOvershoot::None);
    TEST_ASSERT_TRUE(reg.pp3Config() == Pp3Config::Bidirectional);
    TEST_ASSERT_TRUE(reg.rcpThreshold() == RcpThreshold::mV6);

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_rcp_and_ilim_decode_all_values()
{
    for (uint8_t v = 0; v < 4; v++)
    {
        uint8_t buf[GSC_SIZE];
        copyBuf(buf, RESET_BUFFER);
        clearRange(buf, 22, 2);
        clearRange(buf, 14, 2);
        buf[2] |= static_cast<uint8_t>(v << 6);
        buf[1] |= static_cast<uint8_t>(v << 6);
        TPS25751GlobalSystemConfig reg(buf);
        TEST_ASSERT_EQUAL_UINT8(v, static_cast<uint8_t>(reg.rcpThreshold()));
        TEST_ASSERT_EQUAL_UINT8(v, static_cast<uint8_t>(reg.ilimOvershoot()));
    }
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    TPS25751GlobalSystemConfig reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_pp3_undefined_codes_invalid()
{
    TEST_ASSERT_TRUE(resetWithField(16, 5).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithField(16, 6).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithField(16, 7).isSemanticallyValid());
}

void test_pp1_undefined_codes_invalid()
{
    TEST_ASSERT_TRUE(resetWithField(8, 0).isSemanticallyValid());
    TEST_ASSERT_TRUE(resetWithField(8, 1).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithField(8, 2).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithField(8, 7).isSemanticallyValid());
}

// Reserved-bit checks were dropped because the firmware sets reserved bits.
void test_reserved_bits_are_not_checked()
{
    const unsigned bits[] = {1, 7, 11, 13, 19, 21, 24, 64, 111};
    for (unsigned b : bits)
    {
        uint8_t buf[GSC_SIZE];
        copyBuf(buf, RESET_BUFFER);
        setBit(buf, b);
        TPS25751GlobalSystemConfig reg(buf);
        TEST_ASSERT_TRUE(reg.isSemanticallyValid());
        TEST_ASSERT_TRUE(reg.pp3Config() == Pp3Config::Sink);
        TEST_ASSERT_TRUE(reg.pp1Config() == Pp1Config::Source);
    }
}

// --- Setters ---------------------------------------------------------------------

// Apply `set` for each value and check the field, and that every other bit of
// the buffer equals the bench fixture (including its undocumented set bits).
#define CHECK_ONLY_FIELD_CHANGED(reg, lowBit, len)                             \
    do {                                                                       \
        uint8_t expected[GSC_SIZE];                                            \
        copyBuf(expected, BENCH_BUFFER);                                       \
        uint8_t actual[GSC_SIZE];                                              \
        copyBuf(actual, (reg).raw());                                          \
        clearRange(expected, (lowBit), (len));                                 \
        clearRange(actual, (lowBit), (len));                                   \
        TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, GSC_SIZE);             \
    } while (0)

void test_set_rcp_threshold_round_trips()
{
    const RcpThreshold all[] = {RcpThreshold::mV6, RcpThreshold::mV8, RcpThreshold::mV10, RcpThreshold::mV12};
    for (RcpThreshold v : all)
    {
        TPS25751GlobalSystemConfig reg(BENCH_BUFFER);
        reg.setRcpThreshold(v);
        TEST_ASSERT_TRUE(reg.rcpThreshold() == v);
        CHECK_ONLY_FIELD_CHANGED(reg, 22, 2);
    }
}

void test_set_pp3_config_round_trips()
{
    const Pp3Config all[] = {Pp3Config::Disabled, Pp3Config::Source, Pp3Config::Sink,
                             Pp3Config::SinkWaitSrdy, Pp3Config::Bidirectional,
                             Pp3Config::BidirectionalWaitSrdy};
    for (Pp3Config v : all)
    {
        TPS25751GlobalSystemConfig reg(BENCH_BUFFER);
        reg.setPp3Config(v);
        TEST_ASSERT_TRUE(reg.pp3Config() == v);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(v), reg.rawPp3Config());
        CHECK_ONLY_FIELD_CHANGED(reg, 16, 3);
    }
}

void test_set_ilim_overshoot_round_trips()
{
    const IlimOvershoot all[] = {IlimOvershoot::None, IlimOvershoot::mA100, IlimOvershoot::mA200, IlimOvershoot::mA500};
    for (IlimOvershoot v : all)
    {
        TPS25751GlobalSystemConfig reg(BENCH_BUFFER);
        reg.setIlimOvershoot(v);
        TEST_ASSERT_TRUE(reg.ilimOvershoot() == v);
        CHECK_ONLY_FIELD_CHANGED(reg, 14, 2);
    }
}

void test_set_pp1_config_round_trips()
{
    const Pp1Config all[] = {Pp1Config::Disabled, Pp1Config::Source};
    for (Pp1Config v : all)
    {
        TPS25751GlobalSystemConfig reg(BENCH_BUFFER);
        reg.setPp1Config(v);
        TEST_ASSERT_TRUE(reg.pp1Config() == v);
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(v), reg.rawPp1Config());
        CHECK_ONLY_FIELD_CHANGED(reg, 8, 3);
    }
}

void test_set_cable1_round_trips()
{
    for (bool v : {false, true, false})
    {
        TPS25751GlobalSystemConfig reg(BENCH_BUFFER);
        reg.setPpCable1SwitchEnabled(v);
        TEST_ASSERT_EQUAL(v, reg.ppCable1SwitchEnabled());
        CHECK_ONLY_FIELD_CHANGED(reg, 0, 1);
    }
}

void test_setters_no_op_on_invalid_register()
{
    TPS25751GlobalSystemConfig moved(BENCH_BUFFER);
    TPS25751GlobalSystemConfig sink(static_cast<TPS25751GlobalSystemConfig &&>(moved));

    moved.setPp3Config(Pp3Config::Source);  // moved-from: invalid, must not crash
    TEST_ASSERT_FALSE(moved.isValid());
    TEST_ASSERT_TRUE(sink.pp3Config() == Pp3Config::Bidirectional);
}

// --- toString --------------------------------------------------------------------

void test_to_string_values()
{
    TEST_ASSERT_EQUAL_STRING("6 mV (nominal)", toString(RcpThreshold::mV6));
    TEST_ASSERT_EQUAL_STRING("8 mV (nominal)", toString(RcpThreshold::mV8));
    TEST_ASSERT_EQUAL_STRING("10 mV (nominal)", toString(RcpThreshold::mV10));
    TEST_ASSERT_EQUAL_STRING("12 mV (nominal)", toString(RcpThreshold::mV12));

    TEST_ASSERT_EQUAL_STRING("PP3 not used and disabled", toString(Pp3Config::Disabled));
    TEST_ASSERT_EQUAL_STRING("PP3 is a Source (output)", toString(Pp3Config::Source));
    TEST_ASSERT_EQUAL_STRING("PP3 is a Sink (input)", toString(Pp3Config::Sink));
    TEST_ASSERT_EQUAL_STRING("PP3 is sink but waits for 'SRDY'", toString(Pp3Config::SinkWaitSrdy));
    TEST_ASSERT_EQUAL_STRING("PP3 is bi-directional", toString(Pp3Config::Bidirectional));
    TEST_ASSERT_EQUAL_STRING("PP3 is bi-directional but waits for 'SRDY'", toString(Pp3Config::BidirectionalWaitSrdy));

    TEST_ASSERT_EQUAL_STRING("No additional overshoot margin", toString(IlimOvershoot::None));
    TEST_ASSERT_EQUAL_STRING("Overshoot margin of at least 100 mA", toString(IlimOvershoot::mA100));
    TEST_ASSERT_EQUAL_STRING("Overshoot margin of at least 200 mA", toString(IlimOvershoot::mA200));
    TEST_ASSERT_EQUAL_STRING("Overshoot margin of at least 500 mA", toString(IlimOvershoot::mA500));

    TEST_ASSERT_EQUAL_STRING("Not used (disabled)", toString(Pp1Config::Disabled));
    TEST_ASSERT_EQUAL_STRING("PP1 configured as source", toString(Pp1Config::Source));
}

void test_to_string_undefined_codes()
{
    TEST_ASSERT_EQUAL_STRING("Undefined", toString(static_cast<Pp3Config>(6)));
    TEST_ASSERT_EQUAL_STRING("Undefined", toString(static_cast<Pp3Config>(7)));
    TEST_ASSERT_EQUAL_STRING("Undefined", toString(static_cast<Pp1Config>(2)));
    TEST_ASSERT_EQUAL_STRING("Undefined", toString(static_cast<Pp1Config>(7)));
    TEST_ASSERT_TRUE(resetWithField(16, 6).pp3Config() == static_cast<Pp3Config>(6));
    TEST_ASSERT_EQUAL_UINT8(6, resetWithField(16, 6).rawPp3Config());
}

// --- Rule of Five ----------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751GlobalSystemConfig original(BENCH_BUFFER);
    TPS25751GlobalSystemConfig copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, copy.raw(), GSC_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    TPS25751GlobalSystemConfig original(BENCH_BUFFER);
    const uint8_t *buf = original.raw();

    TPS25751GlobalSystemConfig moved(static_cast<TPS25751GlobalSystemConfig &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, moved.raw(), GSC_SIZE);
}

void test_copy_assignment_deep_copies()
{
    TPS25751GlobalSystemConfig source(BENCH_BUFFER);
    TPS25751GlobalSystemConfig target(RESET_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, target.raw(), GSC_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    TPS25751GlobalSystemConfig source(BENCH_BUFFER);
    TPS25751GlobalSystemConfig target(RESET_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<TPS25751GlobalSystemConfig &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, target.raw(), GSC_SIZE);
}

// --- debugPrint ------------------------------------------------------------------

namespace {

}  // namespace

void test_debug_print_shows_raw_bytes_and_fields()
{
    TPS25751GlobalSystemConfig reg(BENCH_BUFFER);
    const std::string out = captureDebugPrint(reg);

    // Every byte zero-padded, so the firmware's undocumented bits are visible.
    TEST_ASSERT_TRUE(out.find("Raw (byte 0 first): 05 09 14 00 1C 03 00 00 00 00 00 00 00 00") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("PP3 Config: PP3 is bi-directional (raw 4)") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("PP1 Config: PP1 configured as source (raw 1)") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("ILIM Over Shoot (PP_5V): No additional overshoot margin (raw 0)") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("RCP Threshold: 6 mV (nominal) (raw 0)") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("PP Cable1 Switch: Enabled") != std::string::npos);
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_address_identity);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_reset_value_decodes);
    RUN_TEST(test_bench_value_decodes_and_is_valid);
    RUN_TEST(test_rcp_and_ilim_decode_all_values);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_pp3_undefined_codes_invalid);
    RUN_TEST(test_pp1_undefined_codes_invalid);
    RUN_TEST(test_reserved_bits_are_not_checked);

    RUN_TEST(test_set_rcp_threshold_round_trips);
    RUN_TEST(test_set_pp3_config_round_trips);
    RUN_TEST(test_set_ilim_overshoot_round_trips);
    RUN_TEST(test_set_pp1_config_round_trips);
    RUN_TEST(test_set_cable1_round_trips);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_to_string_values);
    RUN_TEST(test_to_string_undefined_codes);
    RUN_TEST(test_debug_print_shows_raw_bytes_and_fields);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
