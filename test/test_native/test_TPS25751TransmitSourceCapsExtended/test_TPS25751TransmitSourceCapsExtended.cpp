/**
 * @file test_TPS25751TransmitSourceCapsExtended.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitSourceCapsExtended.
 *
 * Run with: pio test -e native
 *
 * TX_SOURCE_CAPABILITIES_EXTENDED_DATA_BLOCK (0x77) is a 15-byte R/W register,
 * reset all zero. Bit n lives in byte n / 8, bit n % 8; multi-byte fields are
 * little-endian. The raw accessors return field bits unchanged; the typed accessors
 * decode them per the USB PD specification, and the Typed decode section below
 * tests those.
 *
 * Distinct fixture (every field different, byte order visible):
 *   byte 0  = 0x11  Voltage Regulation
 *   byte 1  = 0x22  Hold Up Time
 *   byte 2  = 0x33  Compliance
 *   byte 3  = 0x44  Touch Current
 *   bytes 4,5   = 0x34,0x12 -> Peak Current 1 = 0x1234
 *   bytes 6,7   = 0x78,0x56 -> Peak Current 2 = 0x5678
 *   bytes 8,9   = 0xBC,0x9A -> Peak Current 3 = 0x9ABC
 *   byte 10 = 0x66  Touch Temperature
 *   byte 11 = 0x77  Source Inputs
 *   byte 12 = 0x31  Number Fixed Batteries 1 (low nibble), Hot Swappable 3 (high nibble)
 *   byte 13 = 0x55  Source PDP 0x55, bit 111 clear
 *   byte 14 = 0x00  reserved
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitSourceCapsExtended.h"

namespace {

using Caps = TPS25751TransmitSourceCapsExtended;

constexpr size_t CAPS_SIZE = TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED.size;  // 15

constexpr uint8_t RESET_BUFFER[CAPS_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[CAPS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// Bench reading 2026-11-18: Source PDP = 0x7F, everything else zero.
constexpr uint8_t BENCH_BUFFER[CAPS_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x7F, 0};
constexpr uint8_t DISTINCT_BUFFER[CAPS_SIZE] = {
    0x11, 0x22, 0x33, 0x44,
    0x34, 0x12,  // Peak Current 1
    0x78, 0x56,  // Peak Current 2
    0xBC, 0x9A,  // Peak Current 3
    0x66, 0x77,
    0x31,        // fixed 1, hot swappable 3
    0x55,        // Source PDP 0x55
    0x00};

void setBit(uint8_t *buf, unsigned bit, bool on)
{
    if (on) buf[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
    else buf[bit / 8] &= static_cast<uint8_t>(~(1u << (bit % 8)));
}

bool getBit(const uint8_t *buf, unsigned bit)
{
    return (buf[bit / 8] >> (bit % 8)) & 1u;
}

// Replaces the field [pos, pos+len) of buf with value (little-endian bit order).
void replaceField(uint8_t *buf, unsigned pos, unsigned len, uint32_t value)
{
    for (unsigned i = 0; i < len; i++) setBit(buf, pos + i, (value >> i) & 1u);
}

std::string captureDebugPrint(const Caps &reg)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    const int saved = dup(fileno(stdout));
    dup2(fileno(tmp), fileno(stdout));

    reg.debugPrint();

    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    std::string out;
    rewind(tmp);
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), tmp)) > 0) out.append(buf, n);
    fclose(tmp);
    return out;
}

// One row per single-field setter, with the field's position and width.
struct SetterCase
{
    const char *name;
    uint8_t pos;
    uint8_t len;
    uint32_t value;  // value to write (fits the field)
    void (*set)(Caps &, uint32_t);
    uint32_t (*get)(const Caps &);
};

const SetterCase kSetterCases[] = {
    {"voltageRegulation", 0, 8, 0xA1, [](Caps &r, uint32_t v) { r.setVoltageRegulation(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.voltageRegulation(); }},
    {"holdUpTime", 8, 8, 0xB2, [](Caps &r, uint32_t v) { r.setHoldUpTime(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.holdUpTime(); }},
    {"compliance", 16, 8, 0xC3, [](Caps &r, uint32_t v) { r.setCompliance(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.compliance(); }},
    {"touchCurrent", 24, 8, 0xD4, [](Caps &r, uint32_t v) { r.setTouchCurrent(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.touchCurrent(); }},
    {"peakCurrent1", 32, 16, 0xA1B2, [](Caps &r, uint32_t v) { r.setPeakCurrent(1, static_cast<uint16_t>(v)); }, [](const Caps &r) -> uint32_t { return r.peakCurrent(1); }},
    {"peakCurrent2", 48, 16, 0xC3D4, [](Caps &r, uint32_t v) { r.setPeakCurrent(2, static_cast<uint16_t>(v)); }, [](const Caps &r) -> uint32_t { return r.peakCurrent(2); }},
    {"peakCurrent3", 64, 16, 0xE5F6, [](Caps &r, uint32_t v) { r.setPeakCurrent(3, static_cast<uint16_t>(v)); }, [](const Caps &r) -> uint32_t { return r.peakCurrent(3); }},
    {"touchTemperature", 80, 8, 0x5A, [](Caps &r, uint32_t v) { r.setTouchTemperature(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.touchTemperature(); }},
    {"sourceInputs", 88, 8, 0xA5, [](Caps &r, uint32_t v) { r.setSourceInputs(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.sourceInputs(); }},
    {"numberFixedBatteries", 96, 4, 0x4, [](Caps &r, uint32_t v) { r.setNumberFixedBatteries(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.numberFixedBatteries(); }},
    {"numberHotSwappableBatteries", 100, 4, 0x2, [](Caps &r, uint32_t v) { r.setNumberHotSwappableBatteries(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.numberHotSwappableBatteries(); }},
    {"sourcePdp", 104, 7, 0x2A, [](Caps &r, uint32_t v) { r.setSourcePdp(static_cast<uint8_t>(v)); }, [](const Caps &r) -> uint32_t { return r.sourcePdp(); }},
};
constexpr size_t kSetterCaseCount = sizeof(kSetterCases) / sizeof(kSetterCases[0]);

}  // namespace

void setUp() {}
void tearDown() {}

// --- Constants -------------------------------------------------------------------

void test_address_identity()
{
    TEST_ASSERT_TRUE(Caps::kAddress == TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED);
    TEST_ASSERT_EQUAL_UINT8(0x77, static_cast<uint8_t>(Caps::kAddress));
    TEST_ASSERT_EQUAL_UINT8(15, TPS25751Registers::getRegisterSize(Caps::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(Caps::kAddress));
}

void test_count_constants()
{
    TEST_ASSERT_EQUAL_UINT8(4, Caps::kMaxBatteryCount);
    TEST_ASSERT_EQUAL_UINT8(3, Caps::kPeakCurrentCount);
}

// --- Construction ----------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    Caps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), CAPS_SIZE);
}

void test_data_constructor_copies_buffer()
{
    Caps reg(DISTINCT_BUFFER);

    TEST_ASSERT_NOT_EQUAL(DISTINCT_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, reg.raw(), CAPS_SIZE);
}

void test_explicit_length_constructor()
{
    Caps reg(DISTINCT_BUFFER, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_EQUAL_UINT16(0x1234, reg.peakCurrent(1));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    Caps reg(TPS25751Registers::Registers::TRANSMIT_SOURCE_CAPABILITIES_EXTENDED);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), CAPS_SIZE);
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[4] = {0, 0, 0, 0};
    Caps reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Decode ----------------------------------------------------------------------

void test_reset_value_decodes_to_zero()
{
    Caps reg(RESET_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0, reg.voltageRegulation());
    TEST_ASSERT_EQUAL_UINT8(0, reg.holdUpTime());
    TEST_ASSERT_EQUAL_UINT8(0, reg.compliance());
    TEST_ASSERT_EQUAL_UINT8(0, reg.touchCurrent());
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(2));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(3));
    TEST_ASSERT_EQUAL_UINT8(0, reg.touchTemperature());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sourceInputs());
    TEST_ASSERT_EQUAL_UINT8(0, reg.numberFixedBatteries());
    TEST_ASSERT_EQUAL_UINT8(0, reg.numberHotSwappableBatteries());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sourcePdp());
}

void test_bench_value_decodes()
{
    Caps reg(BENCH_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0x7F, reg.sourcePdp());
    TEST_ASSERT_EQUAL_UINT8(127, reg.sourcePdp());
    TEST_ASSERT_EQUAL_UINT8(0, reg.voltageRegulation());
    TEST_ASSERT_EQUAL_UINT8(0, reg.holdUpTime());
    TEST_ASSERT_EQUAL_UINT8(0, reg.compliance());
    TEST_ASSERT_EQUAL_UINT8(0, reg.touchCurrent());
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(2));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(3));
    TEST_ASSERT_EQUAL_UINT8(0, reg.touchTemperature());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sourceInputs());
    TEST_ASSERT_EQUAL_UINT8(0, reg.numberFixedBatteries());
    TEST_ASSERT_EQUAL_UINT8(0, reg.numberHotSwappableBatteries());
}

void test_distinct_fixture_decodes_every_field()
{
    Caps reg(DISTINCT_BUFFER);

    TEST_ASSERT_EQUAL_HEX8(0x11, reg.voltageRegulation());
    TEST_ASSERT_EQUAL_HEX8(0x22, reg.holdUpTime());
    TEST_ASSERT_EQUAL_HEX8(0x33, reg.compliance());
    TEST_ASSERT_EQUAL_HEX8(0x44, reg.touchCurrent());
    TEST_ASSERT_EQUAL_HEX16(0x1234, reg.peakCurrent(1));
    TEST_ASSERT_EQUAL_HEX16(0x5678, reg.peakCurrent(2));
    TEST_ASSERT_EQUAL_HEX16(0x9ABC, reg.peakCurrent(3));
    TEST_ASSERT_EQUAL_HEX8(0x66, reg.touchTemperature());
    TEST_ASSERT_EQUAL_HEX8(0x77, reg.sourceInputs());
    TEST_ASSERT_EQUAL_UINT8(1, reg.numberFixedBatteries());
    TEST_ASSERT_EQUAL_UINT8(3, reg.numberHotSwappableBatteries());
    TEST_ASSERT_EQUAL_HEX8(0x55, reg.sourcePdp());
}

void test_peak_current_out_of_range_index_returns_zero()
{
    Caps reg(DISTINCT_BUFFER);

    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(0));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(4));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrent(255));
}

// --- Setters ---------------------------------------------------------------------

void test_every_setter_round_trips_and_leaves_other_bits_unchanged()
{
    for (size_t i = 0; i < kSetterCaseCount; i++)
    {
        const SetterCase &c = kSetterCases[i];
        Caps reg(DISTINCT_BUFFER);

        c.set(reg, c.value);

        uint8_t expected[CAPS_SIZE];
        memcpy(expected, DISTINCT_BUFFER, CAPS_SIZE);
        replaceField(expected, c.pos, c.len, c.value);

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.value, c.get(reg), c.name);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, reg.raw(), CAPS_SIZE, c.name);
    }
}

void test_peak_current_setters_are_little_endian()
{
    Caps reg;

    reg.setPeakCurrent(1, 0x1234);
    reg.setPeakCurrent(2, 0x5678);
    reg.setPeakCurrent(3, 0x9ABC);

    TEST_ASSERT_EQUAL_HEX8(0x34, reg.raw()[4]);
    TEST_ASSERT_EQUAL_HEX8(0x12, reg.raw()[5]);
    TEST_ASSERT_EQUAL_HEX8(0x78, reg.raw()[6]);
    TEST_ASSERT_EQUAL_HEX8(0x56, reg.raw()[7]);
    TEST_ASSERT_EQUAL_HEX8(0xBC, reg.raw()[8]);
    TEST_ASSERT_EQUAL_HEX8(0x9A, reg.raw()[9]);
}

void test_peak_current_setter_out_of_range_index_is_no_op()
{
    Caps reg(DISTINCT_BUFFER);

    reg.setPeakCurrent(0, 0xFFFF);
    reg.setPeakCurrent(4, 0xFFFF);
    reg.setPeakCurrent(255, 0xFFFF);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, reg.raw(), CAPS_SIZE);
}

void test_battery_nibbles_do_not_leak_into_each_other()
{
    Caps reg(DISTINCT_BUFFER);  // byte 12 = 0x31

    reg.setNumberFixedBatteries(0x1F);
    TEST_ASSERT_EQUAL_UINT8(0x0F, reg.numberFixedBatteries());
    TEST_ASSERT_EQUAL_UINT8(3, reg.numberHotSwappableBatteries());  // bits 103:100 unchanged
    TEST_ASSERT_EQUAL_HEX8(0x3F, reg.raw()[12]);

    Caps reg2(DISTINCT_BUFFER);
    reg2.setNumberHotSwappableBatteries(0x1F);
    TEST_ASSERT_EQUAL_UINT8(0x0F, reg2.numberHotSwappableBatteries());
    TEST_ASSERT_EQUAL_UINT8(1, reg2.numberFixedBatteries());  // bits 99:96 unchanged
    TEST_ASSERT_EQUAL_HEX8(0xF1, reg2.raw()[12]);
}

void test_source_pdp_setter_masks_to_seven_bits()
{
    Caps reg(DISTINCT_BUFFER);

    reg.setSourcePdp(0xFF);

    TEST_ASSERT_EQUAL_HEX8(0x7F, reg.sourcePdp());
    TEST_ASSERT_FALSE(getBit(reg.raw(), 111));
    TEST_ASSERT_EQUAL_HEX8(0x7F, reg.raw()[13]);
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_setters_no_op_on_invalid_register()
{
    Caps original(DISTINCT_BUFFER);
    Caps moved(static_cast<Caps &&>(original));

    // original has been moved from and holds no buffer.
    TEST_ASSERT_FALSE(original.isValid());
    original.setVoltageRegulation(0x55);
    original.setPeakCurrent(1, 0x1234);
    original.setSourcePdp(0x7F);
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, original.voltageRegulation());
    TEST_ASSERT_EQUAL_UINT16(0, original.peakCurrent(1));
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    Caps reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_reset_and_bench_and_distinct_are_valid()
{
    TEST_ASSERT_TRUE(Caps(RESET_BUFFER).isSemanticallyValid());
    TEST_ASSERT_TRUE(Caps(BENCH_BUFFER).isSemanticallyValid());
    TEST_ASSERT_TRUE(Caps(DISTINCT_BUFFER).isSemanticallyValid());
}

void test_battery_count_four_valid_five_invalid()
{
    Caps fixed(DISTINCT_BUFFER);
    fixed.setNumberFixedBatteries(4);
    TEST_ASSERT_TRUE(fixed.isSemanticallyValid());
    fixed.setNumberFixedBatteries(5);
    TEST_ASSERT_FALSE(fixed.isSemanticallyValid());

    Caps hot(DISTINCT_BUFFER);
    hot.setNumberHotSwappableBatteries(4);
    TEST_ASSERT_TRUE(hot.isSemanticallyValid());
    hot.setNumberHotSwappableBatteries(5);
    TEST_ASSERT_FALSE(hot.isSemanticallyValid());
}

void test_reserved_bits_invalid()
{
    const unsigned bits[] = {111, 112, 119};
    for (unsigned bit : bits)
    {
        uint8_t buf[CAPS_SIZE];
        memcpy(buf, DISTINCT_BUFFER, CAPS_SIZE);
        setBit(buf, bit, true);

        TEST_ASSERT_FALSE_MESSAGE(Caps(buf).isSemanticallyValid(), "reserved bit set");
    }
}

void test_every_reserved_bit_individually_invalid()
{
    for (unsigned bit = 111; bit <= 119; bit++)
    {
        uint8_t buf[CAPS_SIZE] = {};
        setBit(buf, bit, true);

        TEST_ASSERT_FALSE_MESSAGE(Caps(buf).isSemanticallyValid(), "reserved bit set");
    }
}

// --- Debug print -----------------------------------------------------------------

void test_debug_print_distinct_fixture()
{
    const std::string out = captureDebugPrint(Caps(DISTINCT_BUFFER));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("TX_SOURCE_CAPABILITIES_EXTENDED (0x77)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw bytes (byte 0 first): 11 22 33 44 34 12 78 56 BC 9A 66 77 31 55 00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Peak Current 1: 0x1234"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Peak Current 3: 0x9ABC"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Number Fixed Batteries: 1"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Number Hot Swappable Batteries: 3"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Source PDP: 0x55 (85)"));
    TEST_ASSERT_EQUAL(std::string::npos, out.find("Reserved bits set"));
}

void test_debug_print_reset_zero_pads_and_reports_reserved()
{
    const std::string resetOut = captureDebugPrint(Caps(RESET_BUFFER));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Raw bytes (byte 0 first): 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Peak Current 2: 0x0000"));

    uint8_t buf[CAPS_SIZE] = {};
    setBit(buf, 111, true);
    const std::string out = captureDebugPrint(Caps(buf));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits set (119:111): 0x0001"));
}

// --- Rule of Five ----------------------------------------------------------------

// --- Typed decode (USB PD R3.3 Table 6.59) ---------------------------------------------
//
// TRM byte n is SCEDB byte n + 10. The typed accessors read sub-fields of the raw
// bytes: Voltage Regulation bits 1:0 load step and bit 2 IoC; Compliance bits 0 to
// 2; Touch Current bits 0 to 2; Peak Current word bits 4:0 overload, 10:5 period,
// 14:11 duty, 15 droop; Source Inputs bits 0 to 2.

void test_typed_load_step_and_ioc()
{
    Caps reg(RESET_BUFFER);
    // Reset: load step code 0, IoC bit clear.
    TEST_ASSERT_TRUE(reg.loadStep() == Caps::LoadStep::Rate150mAPerUs);
    TEST_ASSERT_EQUAL_UINT8(25, reg.iocPercent());

    reg.setVoltageRegulation(0x01);  // load step 01b
    TEST_ASSERT_TRUE(reg.loadStep() == Caps::LoadStep::Rate500mAPerUs);
    TEST_ASSERT_EQUAL_UINT8(25, reg.iocPercent());

    reg.setVoltageRegulation(0x02);  // 10b invalid
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(reg.loadStep()));
    reg.setVoltageRegulation(0x03);  // 11b invalid
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(reg.loadStep()));

    reg.setVoltageRegulation(0x04);  // IoC bit 2 set, load step 0
    TEST_ASSERT_TRUE(reg.loadStep() == Caps::LoadStep::Rate150mAPerUs);
    TEST_ASSERT_EQUAL_UINT8(90, reg.iocPercent());

    reg.setVoltageRegulation(0xF8);  // reserved bits 7:3 only: no effect on either field
    TEST_ASSERT_TRUE(reg.loadStep() == Caps::LoadStep::Rate150mAPerUs);
    TEST_ASSERT_EQUAL_UINT8(25, reg.iocPercent());

    TEST_ASSERT_EQUAL_STRING("150 mA/us", Caps::toString(Caps::LoadStep::Rate150mAPerUs));
    TEST_ASSERT_EQUAL_STRING("500 mA/us", Caps::toString(Caps::LoadStep::Rate500mAPerUs));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume 150 mA/us)", Caps::toString(static_cast<Caps::LoadStep>(2)));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume 150 mA/us)", Caps::toString(static_cast<Caps::LoadStep>(3)));
}

void test_typed_hold_up_time()
{
    Caps reg(RESET_BUFFER);
    TEST_ASSERT_FALSE(reg.holdUpTimeSupported());
    TEST_ASSERT_EQUAL_UINT8(0, reg.holdUpTime_ms());

    reg.setHoldUpTime(3);
    TEST_ASSERT_TRUE(reg.holdUpTimeSupported());
    TEST_ASSERT_EQUAL_UINT8(3, reg.holdUpTime_ms());

    reg.setHoldUpTime(255);
    TEST_ASSERT_EQUAL_UINT8(255, reg.holdUpTime_ms());
}

void test_typed_compliance_bits()
{
    Caps reg(RESET_BUFFER);
    TEST_ASSERT_FALSE(reg.lpsCompliant());
    TEST_ASSERT_FALSE(reg.ps1Compliant());
    TEST_ASSERT_FALSE(reg.ps2Compliant());

    reg.setCompliance(0x01);
    TEST_ASSERT_TRUE(reg.lpsCompliant());
    TEST_ASSERT_FALSE(reg.ps1Compliant());
    TEST_ASSERT_FALSE(reg.ps2Compliant());

    reg.setCompliance(0x02);
    TEST_ASSERT_FALSE(reg.lpsCompliant());
    TEST_ASSERT_TRUE(reg.ps1Compliant());
    TEST_ASSERT_FALSE(reg.ps2Compliant());

    reg.setCompliance(0x04);
    TEST_ASSERT_FALSE(reg.lpsCompliant());
    TEST_ASSERT_FALSE(reg.ps1Compliant());
    TEST_ASSERT_TRUE(reg.ps2Compliant());

    reg.setCompliance(0xF8);  // reserved bits 7:3 do not map to a flag
    TEST_ASSERT_FALSE(reg.lpsCompliant());
    TEST_ASSERT_FALSE(reg.ps1Compliant());
    TEST_ASSERT_FALSE(reg.ps2Compliant());
}

void test_typed_touch_current_bits()
{
    Caps reg(RESET_BUFFER);
    TEST_ASSERT_FALSE(reg.lowTouchCurrentEps());
    TEST_ASSERT_FALSE(reg.groundPinSupported());
    TEST_ASSERT_FALSE(reg.groundPinProtectiveEarth());

    reg.setTouchCurrent(0x01);
    TEST_ASSERT_TRUE(reg.lowTouchCurrentEps());
    TEST_ASSERT_FALSE(reg.groundPinSupported());
    TEST_ASSERT_FALSE(reg.groundPinProtectiveEarth());

    reg.setTouchCurrent(0x02);
    TEST_ASSERT_FALSE(reg.lowTouchCurrentEps());
    TEST_ASSERT_TRUE(reg.groundPinSupported());
    TEST_ASSERT_FALSE(reg.groundPinProtectiveEarth());

    reg.setTouchCurrent(0x04);
    TEST_ASSERT_FALSE(reg.lowTouchCurrentEps());
    TEST_ASSERT_FALSE(reg.groundPinSupported());
    TEST_ASSERT_TRUE(reg.groundPinProtectiveEarth());
}

// Peak Current word: overload 25 = 0x19 in bits 4:0, period 63 << 5 = 0x7E0,
// duty 15 << 11 = 0x7800, droop 1 << 15 = 0x8000. Sum 0x19 + 0x7E0 + 0x7800 + 0x8000 = 0xFFF9.
void test_typed_peak_current_maximum_word()
{
    Caps reg(RESET_BUFFER);
    reg.setPeakCurrent(1, 0xFFF9);

    TEST_ASSERT_EQUAL_UINT16(250, reg.peakCurrentOverloadPercent(1));     // 25 * 10
    TEST_ASSERT_EQUAL_UINT16(1260, reg.peakCurrentOverloadPeriod_ms(1));  // 63 * 20
    TEST_ASSERT_EQUAL_UINT8(75, reg.peakCurrentDutyCyclePercent(1));      // 15 * 5
    TEST_ASSERT_TRUE(reg.peakCurrentVbusDroop(1));
}

// Overload 30 (0x1E) is invalid and is treated as 25, so 250 percent. Period and
// duty stay 0 and droop clear.
void test_typed_peak_current_overload_above_25_clamps()
{
    Caps reg(RESET_BUFFER);
    reg.setPeakCurrent(2, 0x001E);

    TEST_ASSERT_EQUAL_UINT16(250, reg.peakCurrentOverloadPercent(2));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPeriod_ms(2));
    TEST_ASSERT_EQUAL_UINT8(0, reg.peakCurrentDutyCyclePercent(2));
    TEST_ASSERT_FALSE(reg.peakCurrentVbusDroop(2));

    reg.setPeakCurrent(2, 0x001F);  // 31 also clamps
    TEST_ASSERT_EQUAL_UINT16(250, reg.peakCurrentOverloadPercent(2));
    reg.setPeakCurrent(2, 0x0001);  // smallest nonzero step: 10 percent
    TEST_ASSERT_EQUAL_UINT16(10, reg.peakCurrentOverloadPercent(2));
}

// Peak Current 3 occupies TRM bytes 8 and 9: F9 FF read little-endian is 0xFFF9.
// Peak Current 1 and 2 stay zero, which proves the index selects the right bytes.
void test_typed_peak_current_index_selects_the_word()
{
    uint8_t buf[CAPS_SIZE] = {0};
    buf[8] = 0xF9;
    buf[9] = 0xFF;
    Caps reg(buf);

    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPercent(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPercent(2));
    TEST_ASSERT_EQUAL_UINT16(250, reg.peakCurrentOverloadPercent(3));
    TEST_ASSERT_EQUAL_UINT16(1260, reg.peakCurrentOverloadPeriod_ms(3));
    TEST_ASSERT_EQUAL_UINT8(75, reg.peakCurrentDutyCyclePercent(3));
    TEST_ASSERT_TRUE(reg.peakCurrentVbusDroop(3));
    TEST_ASSERT_FALSE(reg.peakCurrentVbusDroop(1));
}

void test_typed_peak_current_out_of_range_index_is_zero()
{
    Caps reg(ALL_ONES_DATA);  // every word 0xFFFF, so any leak would be visible
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPercent(0));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPercent(4));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPeriod_ms(0));
    TEST_ASSERT_EQUAL_UINT16(0, reg.peakCurrentOverloadPeriod_ms(4));
    TEST_ASSERT_EQUAL_UINT8(0, reg.peakCurrentDutyCyclePercent(0));
    TEST_ASSERT_EQUAL_UINT8(0, reg.peakCurrentDutyCyclePercent(255));
    TEST_ASSERT_FALSE(reg.peakCurrentVbusDroop(0));
    TEST_ASSERT_FALSE(reg.peakCurrentVbusDroop(4));
}

void test_typed_touch_temperature_standard()
{
    Caps reg(RESET_BUFFER);
    reg.setTouchTemperature(0);
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Caps::TouchTemperature::Iec60950_1);
    reg.setTouchTemperature(1);
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Caps::TouchTemperature::Iec62368_1Ts1);
    reg.setTouchTemperature(2);
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Caps::TouchTemperature::Iec62368_1Ts2);
    reg.setTouchTemperature(3);
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(reg.touchTemperatureStandard()));

    TEST_ASSERT_EQUAL_STRING("IEC 60950-1", Caps::toString(Caps::TouchTemperature::Iec60950_1));
    TEST_ASSERT_EQUAL_STRING("IEC 62368-1 TS1", Caps::toString(Caps::TouchTemperature::Iec62368_1Ts1));
    TEST_ASSERT_EQUAL_STRING("IEC 62368-1 TS2", Caps::toString(Caps::TouchTemperature::Iec62368_1Ts2));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume IEC 60950-1)", Caps::toString(static_cast<Caps::TouchTemperature>(3)));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume IEC 60950-1)", Caps::toString(static_cast<Caps::TouchTemperature>(0xFF)));
}

void test_typed_source_inputs_bits()
{
    Caps reg(RESET_BUFFER);
    TEST_ASSERT_FALSE(reg.externalSupplyPresent());
    TEST_ASSERT_FALSE(reg.externalSupplyUnconstrained());
    TEST_ASSERT_FALSE(reg.internalBatteryPresent());

    reg.setSourceInputs(0x01);
    TEST_ASSERT_TRUE(reg.externalSupplyPresent());
    TEST_ASSERT_FALSE(reg.externalSupplyUnconstrained());
    TEST_ASSERT_FALSE(reg.internalBatteryPresent());

    reg.setSourceInputs(0x03);  // present and unconstrained
    TEST_ASSERT_TRUE(reg.externalSupplyPresent());
    TEST_ASSERT_TRUE(reg.externalSupplyUnconstrained());

    reg.setSourceInputs(0x04);
    TEST_ASSERT_FALSE(reg.externalSupplyPresent());
    TEST_ASSERT_TRUE(reg.internalBatteryPresent());

    reg.setSourceInputs(0xF8);  // reserved bits 7:3 only
    TEST_ASSERT_FALSE(reg.externalSupplyPresent());
    TEST_ASSERT_FALSE(reg.externalSupplyUnconstrained());
    TEST_ASSERT_FALSE(reg.internalBatteryPresent());
}

void test_typed_source_pdp()
{
    Caps reg(RESET_BUFFER);
    // 0 is a valid rating (0 W).
    TEST_ASSERT_TRUE(reg.sourcePdpValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sourcePdp_W());

    reg.setSourcePdp(100);  // largest valid rating
    TEST_ASSERT_TRUE(reg.sourcePdpValid());
    TEST_ASSERT_EQUAL_UINT8(100, reg.sourcePdp_W());

    reg.setSourcePdp(101);  // smallest invalid value
    TEST_ASSERT_FALSE(reg.sourcePdpValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sourcePdp_W());
    // The raw accessor still shows the value.
    TEST_ASSERT_EQUAL_UINT8(101, reg.sourcePdp());
}

void test_typed_bench_value_source_pdp_is_invalid()
{
    // Hardware reads Source PDP = 0x7F (127), which the specification marks invalid.
    Caps reg(BENCH_BUFFER);
    TEST_ASSERT_EQUAL_UINT8(0x7F, reg.sourcePdp());
    TEST_ASSERT_FALSE(reg.sourcePdpValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sourcePdp_W());
}

void test_debug_print_typed_decode_lines()
{
    Caps reg(RESET_BUFFER);
    reg.setVoltageRegulation(0x05);  // 500 mA/us, IoC 90 percent
    reg.setHoldUpTime(3);
    reg.setCompliance(0x05);         // LPS and PS2
    reg.setTouchCurrent(0x02);       // ground pin
    reg.setPeakCurrent(1, 0xFFF9);
    reg.setTouchTemperature(1);
    reg.setSourceInputs(0x05);       // external present, internal battery
    reg.setSourcePdp(60);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Load Step: 500 mA/us, IoC: 90%"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Hold Up Time: 3 ms"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Compliance: LPS: Yes, PS1: No, PS2: Yes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Touch Current: low touch current EPS: No, ground pin: Yes, protective earth: No"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Peak Current 1: 250% overload, 1260 ms, 75% duty, VBUS droop: Yes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Peak Current 2: 0% overload, 0 ms, 0% duty, VBUS droop: No"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Touch Temperature standard: IEC 62368-1 TS1"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Source Inputs: external supply: Yes, unconstrained: No, internal battery: Yes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("SPR Source PDP: 60 W"));
    // The existing raw lines remain.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Peak Current 1: 0xFFF9"));
}

void test_debug_print_typed_decode_invalid_pdp_and_holdup()
{
    const std::string out = captureDebugPrint(Caps(BENCH_BUFFER));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Hold Up Time: not supported"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("SPR Source PDP: invalid (above 100 W)"));
}

void test_copy_constructor_deep_copies()
{
    Caps source(DISTINCT_BUFFER);
    Caps copy(source);

    TEST_ASSERT_NOT_EQUAL(source.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, copy.raw(), CAPS_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    Caps original(DISTINCT_BUFFER);
    Caps moved(static_cast<Caps &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, moved.raw(), CAPS_SIZE);
}

void test_copy_assignment_deep_copies()
{
    Caps source(DISTINCT_BUFFER);
    Caps target(RESET_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, target.raw(), CAPS_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    Caps source(DISTINCT_BUFFER);
    Caps target(RESET_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<Caps &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, target.raw(), CAPS_SIZE);
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_address_identity);
    RUN_TEST(test_count_constants);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_reset_value_decodes_to_zero);
    RUN_TEST(test_bench_value_decodes);
    RUN_TEST(test_distinct_fixture_decodes_every_field);
    RUN_TEST(test_peak_current_out_of_range_index_returns_zero);

    RUN_TEST(test_every_setter_round_trips_and_leaves_other_bits_unchanged);
    RUN_TEST(test_peak_current_setters_are_little_endian);
    RUN_TEST(test_peak_current_setter_out_of_range_index_is_no_op);
    RUN_TEST(test_battery_nibbles_do_not_leak_into_each_other);
    RUN_TEST(test_source_pdp_setter_masks_to_seven_bits);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_reset_and_bench_and_distinct_are_valid);
    RUN_TEST(test_battery_count_four_valid_five_invalid);
    RUN_TEST(test_reserved_bits_invalid);
    RUN_TEST(test_every_reserved_bit_individually_invalid);

    RUN_TEST(test_debug_print_distinct_fixture);
    RUN_TEST(test_debug_print_reset_zero_pads_and_reports_reserved);

    RUN_TEST(test_typed_load_step_and_ioc);
    RUN_TEST(test_typed_hold_up_time);
    RUN_TEST(test_typed_compliance_bits);
    RUN_TEST(test_typed_touch_current_bits);
    RUN_TEST(test_typed_peak_current_maximum_word);
    RUN_TEST(test_typed_peak_current_overload_above_25_clamps);
    RUN_TEST(test_typed_peak_current_index_selects_the_word);
    RUN_TEST(test_typed_peak_current_out_of_range_index_is_zero);
    RUN_TEST(test_typed_touch_temperature_standard);
    RUN_TEST(test_typed_source_inputs_bits);
    RUN_TEST(test_typed_source_pdp);
    RUN_TEST(test_typed_bench_value_source_pdp_is_invalid);
    RUN_TEST(test_debug_print_typed_decode_lines);
    RUN_TEST(test_debug_print_typed_decode_invalid_pdp_and_holdup);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
