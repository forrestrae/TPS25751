/**
 * @file test_TPS25751TransmitPPSStatus.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitPPSStatus.
 *
 * Run with: pio test -e native
 *
 * TRANSMITTED_PPS_STATUS_DATA_BLOCK (0x7A) is a 4-byte R/W register, reset
 * 00000000h. Bit n lives in byte n / 8, bit n % 8.
 *
 * Fields (register bit numbers):
 *   31:28 reserved, 27 OMF, 26:25 PTF, 24 reserved,
 *   23:16 Output Current (50 mA/LSB, 0xFF = not supported),
 *   15:0  Output Voltage (20 mV/LSB, 0xFFFF = not supported)
 *
 * Bench value (2026-11-18 reading shape, with PTF Normal): {0xFA, 0x00, 0x3C, 0x02}
 *   voltage raw 0x00FA = 250 -> 5000 mV
 *   current raw 0x3C = 60   -> 3000 mA
 *   byte 3 = 0x02 -> bit 25 -> PTF = 01b (Normal), OMF (bit 27) = 0 (CV)
 *
 * Busy base for setter isolation tests: every field nonzero and every reserved
 * bit set (31:28 and 24 -> 0xF1000000):
 *   0xF1000000 | OMF (1<<27 = 0x08000000) | PTF 10b (2<<25 = 0x04000000)
 *              | current 0x56 (0x00560000) | voltage 0x1234
 *   = 0xFD561234
 */

#include <unity.h>

#include <cstdio>
#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitPPSStatus.h"
#include "CaptureStdout.h"

namespace {

using Pps = TPS25751TransmitPPSStatus;
using Ptf = Pps::PresentTemperatureFlag;
using Omf = Pps::OperatingMode;

constexpr size_t PPS_SIZE = TPS25751Registers::Registers::TRANSMIT_PPS_STATUS.size;  // 4

constexpr uint8_t RESET_BUFFER[PPS_SIZE] = {0x00, 0x00, 0x00, 0x00};
constexpr uint8_t ALL_ONES_DATA[PPS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint8_t BENCH_BUFFER[PPS_SIZE] = {0xFA, 0x00, 0x3C, 0x02};

constexpr uint32_t RESERVED_MASK = 0xF1000000u;
constexpr uint32_t BUSY_BASE = 0xFD561234u;

void packLE(uint32_t value, uint8_t *out)
{
    for (int i = 0; i < 4; i++) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t unpackLE(const uint8_t *in)
{
    return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) | (static_cast<uint32_t>(in[3]) << 24);
}

Pps makePps(uint32_t value)
{
    uint8_t buf[PPS_SIZE];
    packLE(value, buf);
    return Pps(buf);
}

uint32_t fieldMask(uint8_t pos, uint8_t len)
{
    return ((len >= 32) ? 0xFFFFFFFFu : ((1u << len) - 1)) << pos;
}

// One row per setter that writes a raw field value.
struct SetterCase
{
    const char *name;
    uint8_t pos;
    uint8_t len;
    void (*set)(Pps &, uint32_t);
};

const SetterCase kSetterCases[] = {
    {"outputVoltage", 0, 16, [](Pps &r, uint32_t v) { r.setRawOutputVoltage(static_cast<uint16_t>(v)); }},
    {"outputCurrent", 16, 8, [](Pps &r, uint32_t v) { r.setRawOutputCurrent(static_cast<uint8_t>(v)); }},
    {"presentTemperatureFlag", 25, 2, [](Pps &r, uint32_t v) { r.setPresentTemperatureFlag(static_cast<Ptf>(v)); }},
    {"operatingMode", 27, 1, [](Pps &r, uint32_t v) { r.setOperatingMode(static_cast<Omf>(v)); }},
};
constexpr size_t kSetterCaseCount = sizeof(kSetterCases) / sizeof(kSetterCases[0]);

// Applies a setter to a copy of the base and checks the field holds
// `expectedField` while every bit outside it is unchanged.
void assertSetterIsolated(const char *name, uint32_t base, uint8_t pos, uint8_t len,
                          void (*apply)(Pps &), uint32_t expectedField)
{
    Pps reg = makePps(base);

    apply(reg);

    const uint32_t after = unpackLE(reg.raw());
    const uint32_t mask = fieldMask(pos, len);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(base & ~mask, after & ~mask, name);
    TEST_ASSERT_EQUAL_HEX32_MESSAGE(expectedField, (after & mask) >> pos, name);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Constants -------------------------------------------------------------------

void test_address_identity()
{
    TEST_ASSERT_TRUE(Pps::kAddress == TPS25751Registers::Address::TRANSMIT_PPS_STATUS);
    TEST_ASSERT_EQUAL_UINT8(0x7A, static_cast<uint8_t>(Pps::kAddress));
    TEST_ASSERT_EQUAL_UINT8(4, TPS25751Registers::getRegisterSize(Pps::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(Pps::kAddress));
}

void test_unit_and_sentinel_constants()
{
    TEST_ASSERT_EQUAL_UINT16(20, Pps::kVoltageLsb_mV);
    TEST_ASSERT_EQUAL_UINT16(50, Pps::kCurrentLsb_mA);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, Pps::kVoltageNotSupported);
    TEST_ASSERT_EQUAL_UINT8(0xFF, Pps::kCurrentNotSupported);
}

void test_reserved_mask_derivation()
{
    // Bits 31:28 = 0xF0000000, bit 24 = 0x01000000.
    TEST_ASSERT_EQUAL_HEX32(fieldMask(28, 4) | fieldMask(24, 1), Pps::kReservedMask);
    TEST_ASSERT_EQUAL_HEX32(RESERVED_MASK, Pps::kReservedMask);
}

// --- Construction ----------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    Pps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(PPS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(PPS_SIZE));
    TEST_ASSERT_EQUAL_HEX32(0u, unpackLE(reg.raw()));
}

void test_data_constructor_copies_buffer()
{
    Pps reg(BENCH_BUFFER);

    TEST_ASSERT_NOT_EQUAL(BENCH_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, reg.raw(), PPS_SIZE);
}

void test_explicit_length_constructor()
{
    Pps reg(BENCH_BUFFER, PPS_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(PPS_SIZE));
    TEST_ASSERT_EQUAL_UINT16(250, reg.rawOutputVoltage());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    Pps reg(TPS25751Registers::Registers::TRANSMIT_PPS_STATUS);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(PPS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawOutputVoltage());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[2] = {0xFA, 0x00};
    Pps reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(PPS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Decode ----------------------------------------------------------------------

// USB PD R3.3 Table 6.67 names PTF 00b "Not Supported". The old TRM-based
// enumerator NotDefined stays as an alias of the same value.
void test_ptf_not_defined_is_an_alias_of_not_supported()
{
    TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(Ptf::NotSupported));
    TEST_ASSERT_TRUE(Ptf::NotDefined == Ptf::NotSupported);
    TEST_ASSERT_EQUAL_STRING("Not supported", Pps::toString(Ptf::NotDefined));
    TEST_ASSERT_EQUAL_STRING("Not supported", Pps::toString(static_cast<Ptf>(0)));
}

void test_reset_value_decodes()
{
    Pps reg(RESET_BUFFER);

    TEST_ASSERT_TRUE(reg.presentTemperatureFlag() == Ptf::NotSupported);
    TEST_ASSERT_TRUE(reg.operatingMode() == Omf::ConstantVoltage);
    TEST_ASSERT_EQUAL_UINT32(0, reg.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.outputCurrent_mA());
    TEST_ASSERT_TRUE(reg.outputVoltageSupported());
    TEST_ASSERT_TRUE(reg.outputCurrentSupported());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_decode_5000_mv_and_3000_ma()
{
    // Raw 250 = 0x00FA (20 mV/LSB) and raw 60 = 0x3C (50 mA/LSB).
    Pps reg = makePps((0x3Cu << 16) | 0x00FAu);

    TEST_ASSERT_EQUAL_UINT16(0x00FA, reg.rawOutputVoltage());
    TEST_ASSERT_EQUAL_UINT32(5000, reg.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT8(0x3C, reg.rawOutputCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.outputCurrent_mA());
}

void test_bench_value_decodes()
{
    Pps reg(BENCH_BUFFER);

    TEST_ASSERT_EQUAL_UINT32(5000, reg.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.outputCurrent_mA());
    TEST_ASSERT_TRUE(reg.presentTemperatureFlag() == Ptf::Normal);
    TEST_ASSERT_TRUE(reg.operatingMode() == Omf::ConstantVoltage);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_voltage_in_upper_byte_decodes()
{
    // 0x1234 = 4660 raw -> 93 200 mV; checks the high byte is read.
    Pps reg = makePps(0x00001234u);

    TEST_ASSERT_EQUAL_UINT16(0x1234, reg.rawOutputVoltage());
    TEST_ASSERT_EQUAL_UINT32(93200, reg.outputVoltage_mV());
}

void test_voltage_not_supported_sentinel()
{
    Pps reg = makePps((0x3Cu << 16) | 0xFFFFu);

    TEST_ASSERT_FALSE(reg.outputVoltageSupported());
    TEST_ASSERT_EQUAL_UINT32(0, reg.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, reg.rawOutputVoltage());
    // The current still decodes.
    TEST_ASSERT_TRUE(reg.outputCurrentSupported());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.outputCurrent_mA());
}

void test_current_not_supported_sentinel()
{
    Pps reg = makePps((0xFFu << 16) | 0x00FAu);

    TEST_ASSERT_FALSE(reg.outputCurrentSupported());
    TEST_ASSERT_EQUAL_UINT16(0, reg.outputCurrent_mA());
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.rawOutputCurrent());
    // The voltage still decodes.
    TEST_ASSERT_TRUE(reg.outputVoltageSupported());
    TEST_ASSERT_EQUAL_UINT32(5000, reg.outputVoltage_mV());
}

void test_largest_supported_values_decode()
{
    Pps reg = makePps((0xFEu << 16) | 0xFFFEu);

    TEST_ASSERT_TRUE(reg.outputVoltageSupported());
    TEST_ASSERT_EQUAL_UINT32(1310680, reg.outputVoltage_mV());
    TEST_ASSERT_TRUE(reg.outputCurrentSupported());
    TEST_ASSERT_EQUAL_UINT16(12700, reg.outputCurrent_mA());
}

void test_each_ptf_value_decodes_with_text()
{
    for (uint8_t code = 0; code <= 3; code++)
    {
        Pps reg = makePps(static_cast<uint32_t>(code) << 25);
        TEST_ASSERT_EQUAL_UINT8(code, reg.rawPresentTemperatureFlag());
        TEST_ASSERT_EQUAL_UINT8(code, static_cast<uint8_t>(reg.presentTemperatureFlag()));
    }

    TEST_ASSERT_TRUE(makePps(0u << 25).presentTemperatureFlag() == Ptf::NotSupported);
    TEST_ASSERT_TRUE(makePps(1u << 25).presentTemperatureFlag() == Ptf::Normal);
    TEST_ASSERT_TRUE(makePps(2u << 25).presentTemperatureFlag() == Ptf::Warning);
    TEST_ASSERT_TRUE(makePps(3u << 25).presentTemperatureFlag() == Ptf::OverTemperature);

    TEST_ASSERT_EQUAL_STRING("Not supported", Pps::toString(Ptf::NotSupported));
    TEST_ASSERT_EQUAL_STRING("Normal", Pps::toString(Ptf::Normal));
    TEST_ASSERT_EQUAL_STRING("Warning zone", Pps::toString(Ptf::Warning));
    TEST_ASSERT_EQUAL_STRING("Over-temperature", Pps::toString(Ptf::OverTemperature));
}

void test_each_omf_value_decodes_with_text()
{
    TEST_ASSERT_TRUE(makePps(0u).operatingMode() == Omf::ConstantVoltage);
    TEST_ASSERT_TRUE(makePps(1u << 27).operatingMode() == Omf::CurrentLimit);

    TEST_ASSERT_EQUAL_STRING("Constant Voltage (CV)", Pps::toString(Omf::ConstantVoltage));
    TEST_ASSERT_EQUAL_STRING("Current Limit (CL)", Pps::toString(Omf::CurrentLimit));
}

// --- Setters ---------------------------------------------------------------------

void test_raw_setters_write_only_their_field()
{
    // Zero base: write the field's maximum. Busy base (reserved bits set): write 0.
    for (size_t i = 0; i < kSetterCaseCount; i++)
    {
        const SetterCase &c = kSetterCases[i];
        const uint32_t mask = fieldMask(c.pos, c.len);

        Pps fromZero = makePps(0u);
        c.set(fromZero, mask >> c.pos);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(mask, unpackLE(fromZero.raw()), c.name);

        Pps fromBusy = makePps(BUSY_BASE);
        c.set(fromBusy, 0);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(BUSY_BASE & ~mask, unpackLE(fromBusy.raw()), c.name);
    }
}

void test_setters_isolation_with_reserved_bits_set()
{
    // Sentinel writes must not reach the neighbouring reserved bit 24.
    assertSetterIsolated("raw current 0xFF", BUSY_BASE & ~fieldMask(16, 8), 16, 8,
                         [](Pps &r) { r.setRawOutputCurrent(0xFF); }, 0xFF);
    assertSetterIsolated("raw current 0xFF, bit 24 clear", 0xFC000000u, 16, 8,
                         [](Pps &r) { r.setRawOutputCurrent(0xFF); }, 0xFF);

    // OMF must not touch bits 31:28.
    assertSetterIsolated("omf set", 0x00000000u, 27, 1,
                         [](Pps &r) { r.setOperatingMode(Omf::CurrentLimit); }, 1);
    assertSetterIsolated("omf clear", 0xF8000000u, 27, 1,
                         [](Pps &r) { r.setOperatingMode(Omf::ConstantVoltage); }, 0);

    // PTF must not touch bit 24 or bit 27.
    assertSetterIsolated("ptf", BUSY_BASE, 25, 2,
                         [](Pps &r) { r.setPresentTemperatureFlag(Ptf::Normal); }, 1);

    assertSetterIsolated("raw voltage", BUSY_BASE, 0, 16,
                         [](Pps &r) { r.setRawOutputVoltage(0xFFFF); }, 0xFFFF);
}

void test_unit_setters_round_trip()
{
    Pps reg = makePps(BUSY_BASE);

    reg.setOutputVoltage_mV(5000);
    reg.setOutputCurrent_mA(3000);

    TEST_ASSERT_EQUAL_UINT16(250, reg.rawOutputVoltage());
    TEST_ASSERT_EQUAL_UINT32(5000, reg.outputVoltage_mV());
    TEST_ASSERT_EQUAL_UINT8(60, reg.rawOutputCurrent());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.outputCurrent_mA());
    // Flags and reserved bits are as in the base.
    TEST_ASSERT_EQUAL_HEX32((BUSY_BASE & 0xFF000000u) | (60u << 16) | 250u, unpackLE(reg.raw()));
}

void test_unit_setters_clamp_below_sentinel()
{
    Pps reg = makePps(0u);

    reg.setOutputVoltage_mV(2000000);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, reg.rawOutputVoltage());
    TEST_ASSERT_TRUE(reg.outputVoltageSupported());

    reg.setOutputVoltage_mV(0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, reg.rawOutputVoltage());
    TEST_ASSERT_TRUE(reg.outputVoltageSupported());

    // 1 310 700 mV is raw 65535: it must clamp to 0xFFFE, not hit the sentinel.
    reg.setOutputVoltage_mV(1310700);
    TEST_ASSERT_EQUAL_UINT16(0xFFFE, reg.rawOutputVoltage());

    reg.setOutputCurrent_mA(20000);
    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.rawOutputCurrent());
    TEST_ASSERT_TRUE(reg.outputCurrentSupported());

    // 12 750 mA is raw 255: it must clamp to 0xFE, not hit the sentinel.
    reg.setOutputCurrent_mA(12750);
    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.rawOutputCurrent());

    reg.setOutputCurrent_mA(0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT8(0xFE, reg.rawOutputCurrent());
    // Nothing above the current field was touched.
    TEST_ASSERT_EQUAL_HEX32(0x00FEFFFEu, unpackLE(reg.raw()));
}

void test_unit_setters_floor()
{
    Pps reg = makePps(0u);

    reg.setOutputVoltage_mV(5019);
    TEST_ASSERT_EQUAL_UINT16(250, reg.rawOutputVoltage());

    reg.setOutputVoltage_mV(19);
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawOutputVoltage());

    reg.setOutputCurrent_mA(3049);
    TEST_ASSERT_EQUAL_UINT8(60, reg.rawOutputCurrent());

    reg.setOutputCurrent_mA(49);
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawOutputCurrent());
}

void test_not_supported_setters_write_sentinels()
{
    Pps reg = makePps(BUSY_BASE);

    reg.setOutputVoltageNotSupported();
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, reg.rawOutputVoltage());
    TEST_ASSERT_FALSE(reg.outputVoltageSupported());

    reg.setOutputCurrentNotSupported();
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.rawOutputCurrent());
    TEST_ASSERT_FALSE(reg.outputCurrentSupported());

    // Everything above bit 23 is as in the base.
    TEST_ASSERT_EQUAL_HEX32((BUSY_BASE & 0xFF000000u) | 0x00FFFFFFu, unpackLE(reg.raw()));
}

void test_ptf_and_omf_setters_round_trip()
{
    Pps reg = makePps(0u);

    for (uint8_t code = 0; code <= 3; code++)
    {
        reg.setPresentTemperatureFlag(static_cast<Ptf>(code));
        TEST_ASSERT_EQUAL_UINT8(code, reg.rawPresentTemperatureFlag());
    }

    reg.setOperatingMode(Omf::CurrentLimit);
    TEST_ASSERT_TRUE(reg.operatingMode() == Omf::CurrentLimit);
    reg.setOperatingMode(Omf::ConstantVoltage);
    TEST_ASSERT_TRUE(reg.operatingMode() == Omf::ConstantVoltage);
}

void test_setters_no_op_on_invalid_register()
{
    Pps original(BENCH_BUFFER);
    Pps moved(static_cast<Pps &&>(original));

    // original has been moved from and holds no buffer.
    TEST_ASSERT_FALSE(original.isValid());
    original.setOutputVoltage_mV(5000);
    original.setOperatingMode(Omf::CurrentLimit);
    original.setOutputCurrentNotSupported();
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT16(0, original.rawOutputVoltage());
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    Pps reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_zero_reset_is_valid()
{
    Pps reg(RESET_BUFFER);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_defined_bits_set_is_valid()
{
    // Every non-reserved bit set: 0x0EFFFFFF.
    Pps reg = makePps(~RESERVED_MASK);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_reserved_bits_invalid()
{
    TEST_ASSERT_FALSE(makePps(1u << 24).isSemanticallyValid());
    TEST_ASSERT_FALSE(makePps(1u << 28).isSemanticallyValid());
    TEST_ASSERT_FALSE(makePps(1u << 31).isSemanticallyValid());
}

void test_every_reserved_bit_individually_invalid()
{
    for (uint8_t bit = 0; bit < 32; bit++)
    {
        const uint32_t value = 1u << bit;
        const bool reserved = (value & RESERVED_MASK) != 0;
        TEST_ASSERT_EQUAL_MESSAGE(!reserved, makePps(value).isSemanticallyValid(), "per-bit reserved check");
    }
}

// --- Debug print -----------------------------------------------------------------

void test_debug_print_bench_value()
{
    Pps reg(BENCH_BUFFER);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("TRANSMITTED_PPS_STATUS (0x7A)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw: 0x023C00FA"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Output Voltage: 250 (5000 mV)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Output Current: 60 (3000 mA)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Present Temperature Flag: Normal (raw 1)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Operating Mode: Constant Voltage (CV)"));
    TEST_ASSERT_EQUAL(std::string::npos, out.find("Reserved bits set"));
}

void test_debug_print_reset_and_sentinels()
{
    Pps reset(RESET_BUFFER);
    const std::string resetOut = captureDebugPrint(reset);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Raw: 0x00000000"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Present Temperature Flag: Not supported (raw 0)"));

    Pps unsupported = makePps(0x00FFFFFFu);
    const std::string out = captureDebugPrint(unsupported);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Output Voltage: 65535 (not supported)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Output Current: 255 (not supported)"));
}

void test_debug_print_reports_reserved_bits()
{
    Pps reg = makePps(1u << 28);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits set: 0x10000000"));
}

void test_debug_print_zero_pads_reserved_bits()
{
    // Bit 24 alone: Stream's HEX would print 1000000 (7 digits).
    Pps reg = makePps(1u << 24);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits set: 0x01000000"));
}

// --- Rule of Five ----------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    Pps original(BENCH_BUFFER);
    Pps copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, copy.raw(), PPS_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    Pps original(BENCH_BUFFER);
    const uint8_t *buf = original.raw();

    Pps moved(static_cast<Pps &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, moved.raw(), PPS_SIZE);
}

void test_copy_assignment_deep_copies()
{
    Pps source(BENCH_BUFFER);
    Pps target(RESET_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, target.raw(), PPS_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    Pps source(BENCH_BUFFER);
    Pps target(RESET_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<Pps &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(BENCH_BUFFER, target.raw(), PPS_SIZE);
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_address_identity);
    RUN_TEST(test_unit_and_sentinel_constants);
    RUN_TEST(test_reserved_mask_derivation);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_reset_value_decodes);
    RUN_TEST(test_ptf_not_defined_is_an_alias_of_not_supported);
    RUN_TEST(test_decode_5000_mv_and_3000_ma);
    RUN_TEST(test_bench_value_decodes);
    RUN_TEST(test_voltage_in_upper_byte_decodes);
    RUN_TEST(test_voltage_not_supported_sentinel);
    RUN_TEST(test_current_not_supported_sentinel);
    RUN_TEST(test_largest_supported_values_decode);
    RUN_TEST(test_each_ptf_value_decodes_with_text);
    RUN_TEST(test_each_omf_value_decodes_with_text);

    RUN_TEST(test_raw_setters_write_only_their_field);
    RUN_TEST(test_setters_isolation_with_reserved_bits_set);
    RUN_TEST(test_unit_setters_round_trip);
    RUN_TEST(test_unit_setters_clamp_below_sentinel);
    RUN_TEST(test_unit_setters_floor);
    RUN_TEST(test_not_supported_setters_write_sentinels);
    RUN_TEST(test_ptf_and_omf_setters_round_trip);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_all_zero_reset_is_valid);
    RUN_TEST(test_all_defined_bits_set_is_valid);
    RUN_TEST(test_reserved_bits_invalid);
    RUN_TEST(test_every_reserved_bit_individually_invalid);

    RUN_TEST(test_debug_print_bench_value);
    RUN_TEST(test_debug_print_reset_and_sentinels);
    RUN_TEST(test_debug_print_reports_reserved_bits);
    RUN_TEST(test_debug_print_zero_pads_reserved_bits);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
