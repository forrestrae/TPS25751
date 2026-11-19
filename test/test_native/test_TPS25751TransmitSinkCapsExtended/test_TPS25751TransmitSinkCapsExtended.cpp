/**
 * @file test_TPS25751TransmitSinkCapsExtended.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitSinkCapsExtended.
 *
 * Run with: pio test -e native
 *
 * TRANSMIT_SINK_CAPABILITIES_EXTENDED_DATA_BLOCK (0x7E) is a 14-byte R/W register,
 * reset all zero. Bit n lives in byte n / 8, bit n % 8; multi-byte fields are
 * little-endian. The raw accessors return field bits unchanged; the typed accessors
 * decode them per the USB PD specification, and the Typed decode section below
 * tests those.
 *
 * Distinct fixture:
 *   byte 0 = 0x11 SKEDB Version      byte 1 = 0x22 Load Step
 *   bytes 2,3 = 0xEF,0xBE -> Sink Load Characteristics 0xBEEF
 *   byte 4 = 0x33 Compliance         byte 5 = 0x44 Touch Temperature
 *   byte 6 = 0x55 Battery Info       byte 7 = 0x66 Sink Modes
 *   byte 8 = 0x77 Sink Minimum PDP   byte 9 = 0x88 Sink Operational PDP
 *   byte 10 = 0x99 Sink Maximum PDP  bytes 11-13 = 0 (reserved)
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitSinkCapsExtended.h"

namespace {

using Sink = TPS25751TransmitSinkCapsExtended;

constexpr size_t SINK_SIZE = TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES_EXTENDED.size;  // 14

constexpr uint8_t RESET_BUFFER[SINK_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[SINK_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint8_t DISTINCT_BUFFER[SINK_SIZE] = {
    0x11, 0x22,
    0xEF, 0xBE,  // Sink Load Characteristics 0xBEEF
    0x33, 0x44, 0x55, 0x66,
    0x77, 0x88, 0x99,
    0x00, 0x00, 0x00};

void setBit(uint8_t *buf, unsigned bit, bool on)
{
    if (on) buf[bit / 8] |= static_cast<uint8_t>(1u << (bit % 8));
    else buf[bit / 8] &= static_cast<uint8_t>(~(1u << (bit % 8)));
}

// Replaces the field [pos, pos+len) of buf with value (little-endian bit order).
void replaceField(uint8_t *buf, unsigned pos, unsigned len, uint32_t value)
{
    for (unsigned i = 0; i < len; i++) setBit(buf, pos + i, (value >> i) & 1u);
}

std::string captureDebugPrint(const Sink &reg)
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
    uint32_t value;
    void (*set)(Sink &, uint32_t);
    uint32_t (*get)(const Sink &);
};

#define B8(NAME, POS, VAL, SETTER, GETTER) \
    {NAME, POS, 8, VAL, [](Sink &r, uint32_t v) { r.SETTER(static_cast<uint8_t>(v)); }, [](const Sink &r) -> uint32_t { return r.GETTER(); }}

const SetterCase kSetterCases[] = {
    B8("skedbVersion", 0, 0xA1, setSkedbVersion, skedbVersion),
    B8("loadStep", 8, 0xB2, setLoadStep, loadStep),
    {"sinkLoadCharacteristics", 16, 16, 0xA1B2, [](Sink &r, uint32_t v) { r.setSinkLoadCharacteristics(static_cast<uint16_t>(v)); }, [](const Sink &r) -> uint32_t { return r.sinkLoadCharacteristics(); }},
    B8("compliance", 32, 0xC3, setCompliance, compliance),
    B8("touchTemperature", 40, 0xD4, setTouchTemperature, touchTemperature),
    B8("batteryInfo", 48, 0xE5, setBatteryInfo, batteryInfo),
    B8("sinkModes", 56, 0xF6, setSinkModes, sinkModes),
    B8("sinkMinimumPdp", 64, 0x5A, setSinkMinimumPdp, sinkMinimumPdp),
    B8("sinkOperationalPdp", 72, 0xA5, setSinkOperationalPdp, sinkOperationalPdp),
    B8("sinkMaximumPdp", 80, 0x3C, setSinkMaximumPdp, sinkMaximumPdp),
};
#undef B8
constexpr size_t kSetterCaseCount = sizeof(kSetterCases) / sizeof(kSetterCases[0]);

}  // namespace

void setUp() {}
void tearDown() {}

void test_address_identity()
{
    TEST_ASSERT_TRUE(Sink::kAddress == TPS25751Registers::Address::TRANSMIT_SINK_CAPABILITIES_EXTENDED);
    TEST_ASSERT_EQUAL_UINT8(0x7E, static_cast<uint8_t>(Sink::kAddress));
    TEST_ASSERT_EQUAL_UINT8(14, TPS25751Registers::getRegisterSize(Sink::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(Sink::kAddress));
}

void test_default_constructor_allocates_zeroed_buffer()
{
    Sink reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(SINK_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(SINK_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), SINK_SIZE);
}

void test_data_constructor_copies_buffer()
{
    Sink reg(DISTINCT_BUFFER);

    TEST_ASSERT_NOT_EQUAL(DISTINCT_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, reg.raw(), SINK_SIZE);
}

void test_explicit_length_constructor()
{
    Sink reg(DISTINCT_BUFFER, SINK_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(SINK_SIZE));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, reg.sinkLoadCharacteristics());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    Sink reg(TPS25751Registers::Registers::TRANSMIT_SINK_CAPABILITIES_EXTENDED);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(SINK_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), SINK_SIZE);
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[4] = {0, 0, 0, 0};
    Sink reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(SINK_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_reset_value_decodes_to_zero()
{
    Sink reg(RESET_BUFFER);

    TEST_ASSERT_EQUAL_UINT8(0, reg.skedbVersion());
    TEST_ASSERT_EQUAL_UINT8(0, reg.loadStep());
    TEST_ASSERT_EQUAL_UINT16(0, reg.sinkLoadCharacteristics());
    TEST_ASSERT_EQUAL_UINT8(0, reg.compliance());
    TEST_ASSERT_EQUAL_UINT8(0, reg.touchTemperature());
    TEST_ASSERT_EQUAL_UINT8(0, reg.batteryInfo());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkModes());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkMinimumPdp());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkOperationalPdp());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkMaximumPdp());
}

void test_distinct_fixture_decodes_every_field()
{
    Sink reg(DISTINCT_BUFFER);

    TEST_ASSERT_EQUAL_HEX8(0x11, reg.skedbVersion());
    TEST_ASSERT_EQUAL_HEX8(0x22, reg.loadStep());
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, reg.sinkLoadCharacteristics());
    TEST_ASSERT_EQUAL_HEX8(0x33, reg.compliance());
    TEST_ASSERT_EQUAL_HEX8(0x44, reg.touchTemperature());
    TEST_ASSERT_EQUAL_HEX8(0x55, reg.batteryInfo());
    TEST_ASSERT_EQUAL_HEX8(0x66, reg.sinkModes());
    TEST_ASSERT_EQUAL_HEX8(0x77, reg.sinkMinimumPdp());
    TEST_ASSERT_EQUAL_HEX8(0x88, reg.sinkOperationalPdp());
    TEST_ASSERT_EQUAL_HEX8(0x99, reg.sinkMaximumPdp());
}

void test_every_setter_round_trips_and_leaves_other_bits_unchanged()
{
    for (size_t i = 0; i < kSetterCaseCount; i++)
    {
        const SetterCase &c = kSetterCases[i];
        Sink reg(DISTINCT_BUFFER);

        c.set(reg, c.value);

        uint8_t expected[SINK_SIZE];
        memcpy(expected, DISTINCT_BUFFER, SINK_SIZE);
        replaceField(expected, c.pos, c.len, c.value);

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(c.value, c.get(reg), c.name);
        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, reg.raw(), SINK_SIZE, c.name);
    }
}

void test_sink_load_characteristics_is_little_endian()
{
    Sink reg;

    reg.setSinkLoadCharacteristics(0xBEEF);

    TEST_ASSERT_EQUAL_HEX8(0xEF, reg.raw()[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, reg.raw()[3]);
}

void test_setters_no_op_on_invalid_register()
{
    Sink original(DISTINCT_BUFFER);
    Sink moved(static_cast<Sink &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    original.setSkedbVersion(0x55);
    original.setSinkLoadCharacteristics(0x1234);
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8(0, original.skedbVersion());
    TEST_ASSERT_EQUAL_UINT16(0, original.sinkLoadCharacteristics());
}

void test_all_ones_is_invalid()
{
    Sink reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_reset_and_distinct_are_valid()
{
    TEST_ASSERT_TRUE(Sink(RESET_BUFFER).isSemanticallyValid());
    TEST_ASSERT_TRUE(Sink(DISTINCT_BUFFER).isSemanticallyValid());
}

void test_reserved_bits_invalid()
{
    const unsigned bits[] = {88, 100, 111};
    for (unsigned bit : bits)
    {
        uint8_t buf[SINK_SIZE];
        memcpy(buf, DISTINCT_BUFFER, SINK_SIZE);
        setBit(buf, bit, true);

        TEST_ASSERT_FALSE_MESSAGE(Sink(buf).isSemanticallyValid(), "reserved bit set");
    }
}

void test_every_reserved_bit_individually_invalid()
{
    for (unsigned bit = 88; bit <= 111; bit++)
    {
        uint8_t buf[SINK_SIZE] = {};
        setBit(buf, bit, true);

        TEST_ASSERT_FALSE_MESSAGE(Sink(buf).isSemanticallyValid(), "reserved bit set");
    }
}

void test_pdp_bytes_in_reverse_order_are_still_valid()
{
    Sink reg(DISTINCT_BUFFER);

    reg.setSinkMinimumPdp(0xC0);
    reg.setSinkOperationalPdp(0x80);
    reg.setSinkMaximumPdp(0x10);

    TEST_ASSERT_TRUE(reg.sinkMinimumPdp() > reg.sinkOperationalPdp());
    TEST_ASSERT_TRUE(reg.sinkOperationalPdp() > reg.sinkMaximumPdp());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_debug_print_distinct_fixture()
{
    const std::string out = captureDebugPrint(Sink(DISTINCT_BUFFER));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("TRANSMIT_SINK_CAPABILITIES_EXTENDED (0x7E)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw bytes (byte 0 first): 11 22 EF BE 33 44 55 66 77 88 99 00 00 00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Sink Load Characteristics: 0xBEEF"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Sink Maximum PDP: 0x99 (153)"));
    TEST_ASSERT_EQUAL(std::string::npos, out.find("Reserved bits set"));
}

void test_debug_print_reset_zero_pads_and_reports_reserved()
{
    const std::string resetOut = captureDebugPrint(Sink(RESET_BUFFER));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Raw bytes (byte 0 first): 00 00 00 00 00 00 00 00 00 00 00 00 00 00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, resetOut.find("Sink Load Characteristics: 0x0000"));

    uint8_t buf[SINK_SIZE] = {};
    setBit(buf, 88, true);
    const std::string out = captureDebugPrint(Sink(buf));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits set (111:88): 0x000001"));
}

// --- Typed decode (USB PD R3.3 Table 6.70) ---------------------------------------------
//
// TRM byte n is SKEDB byte n + 10. The typed accessors read sub-fields of the raw
// bytes: Load Step bits 1:0; Sink Load Characteristics word bits 4:0 overload, 10:5
// period, 14:11 duty, 15 droop; Compliance bits 0 to 2; Battery Info nibbles; Sink
// Modes bits 0 to 6.

void test_typed_skedb_version()
{
    Sink reg(RESET_BUFFER);
    TEST_ASSERT_FALSE(reg.skedbVersionValid());  // 0 is invalid
    reg.setSkedbVersion(1);
    TEST_ASSERT_TRUE(reg.skedbVersionValid());
    reg.setSkedbVersion(2);
    TEST_ASSERT_FALSE(reg.skedbVersionValid());
    reg.setSkedbVersion(255);
    TEST_ASSERT_FALSE(reg.skedbVersionValid());
}

void test_typed_load_step_rate()
{
    Sink reg(RESET_BUFFER);
    TEST_ASSERT_TRUE(reg.loadStepRate() == Sink::LoadStep::Rate150mAPerUs);
    reg.setLoadStep(0x01);
    TEST_ASSERT_TRUE(reg.loadStepRate() == Sink::LoadStep::Rate500mAPerUs);
    reg.setLoadStep(0x02);
    TEST_ASSERT_EQUAL_UINT8(2, static_cast<uint8_t>(reg.loadStepRate()));
    reg.setLoadStep(0x03);
    TEST_ASSERT_EQUAL_UINT8(3, static_cast<uint8_t>(reg.loadStepRate()));
    reg.setLoadStep(0xFC);  // reserved bits 7:2 only: rate reads 0
    TEST_ASSERT_TRUE(reg.loadStepRate() == Sink::LoadStep::Rate150mAPerUs);
    // The raw byte accessor is unchanged.
    TEST_ASSERT_EQUAL_HEX8(0xFC, reg.loadStep());

    TEST_ASSERT_EQUAL_STRING("150 mA/us", Sink::toString(Sink::LoadStep::Rate150mAPerUs));
    TEST_ASSERT_EQUAL_STRING("500 mA/us", Sink::toString(Sink::LoadStep::Rate500mAPerUs));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume 150 mA/us)", Sink::toString(static_cast<Sink::LoadStep>(2)));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume 150 mA/us)", Sink::toString(static_cast<Sink::LoadStep>(3)));
}

// Sink Load word: overload 25 = 0x19, period 63 << 5 = 0x7E0, duty 15 << 11 = 0x7800,
// droop 1 << 15 = 0x8000. Sum 0x19 + 0x7E0 + 0x7800 + 0x8000 = 0xFFF9.
void test_typed_sink_load_maximum_word()
{
    Sink reg(RESET_BUFFER);
    reg.setSinkLoadCharacteristics(0xFFF9);

    TEST_ASSERT_EQUAL_UINT16(250, reg.sinkLoadOverloadPercent());     // 25 * 10
    TEST_ASSERT_EQUAL_UINT16(1260, reg.sinkLoadOverloadPeriod_ms());  // 63 * 20
    TEST_ASSERT_EQUAL_UINT8(75, reg.sinkLoadDutyCyclePercent());      // 15 * 5
    TEST_ASSERT_TRUE(reg.sinkLoadVbusDroopTolerated());
}

void test_typed_sink_load_zero_word_and_clamp()
{
    Sink reg(RESET_BUFFER);
    TEST_ASSERT_EQUAL_UINT16(0, reg.sinkLoadOverloadPercent());
    TEST_ASSERT_EQUAL_UINT16(0, reg.sinkLoadOverloadPeriod_ms());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkLoadDutyCyclePercent());
    TEST_ASSERT_FALSE(reg.sinkLoadVbusDroopTolerated());

    reg.setSinkLoadCharacteristics(0x001E);  // overload 30 is invalid and treated as 25
    TEST_ASSERT_EQUAL_UINT16(250, reg.sinkLoadOverloadPercent());
    reg.setSinkLoadCharacteristics(0x0001);
    TEST_ASSERT_EQUAL_UINT16(10, reg.sinkLoadOverloadPercent());

    // Period and duty decode even when overload is 0: 1 << 5 = 0x20 gives 20 ms.
    reg.setSinkLoadCharacteristics(0x0020);
    TEST_ASSERT_EQUAL_UINT16(0, reg.sinkLoadOverloadPercent());
    TEST_ASSERT_EQUAL_UINT16(20, reg.sinkLoadOverloadPeriod_ms());
}

void test_typed_compliance_bits()
{
    Sink reg(RESET_BUFFER);
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
    reg.setCompliance(0xF8);  // reserved bits 7:3
    TEST_ASSERT_FALSE(reg.lpsCompliant());
    TEST_ASSERT_FALSE(reg.ps1Compliant());
    TEST_ASSERT_FALSE(reg.ps2Compliant());
}

void test_typed_touch_temperature_standard()
{
    Sink reg(RESET_BUFFER);
    // 0x7E code 0 is "No applicable standard", unlike 0x77 where 0 is IEC 60950-1.
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Sink::TouchTemperature::NoApplicableStandard);
    reg.setTouchTemperature(1);
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Sink::TouchTemperature::Iec60950_1);
    reg.setTouchTemperature(2);
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Sink::TouchTemperature::Iec62368_1Ts1);
    reg.setTouchTemperature(3);
    TEST_ASSERT_TRUE(reg.touchTemperatureStandard() == Sink::TouchTemperature::Iec62368_1Ts2);
    reg.setTouchTemperature(4);
    TEST_ASSERT_EQUAL_UINT8(4, static_cast<uint8_t>(reg.touchTemperatureStandard()));

    TEST_ASSERT_EQUAL_STRING("No applicable standard", Sink::toString(Sink::TouchTemperature::NoApplicableStandard));
    TEST_ASSERT_EQUAL_STRING("IEC 60950-1", Sink::toString(Sink::TouchTemperature::Iec60950_1));
    TEST_ASSERT_EQUAL_STRING("IEC 62368-1 TS1", Sink::toString(Sink::TouchTemperature::Iec62368_1Ts1));
    TEST_ASSERT_EQUAL_STRING("IEC 62368-1 TS2", Sink::toString(Sink::TouchTemperature::Iec62368_1Ts2));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume IEC 60950-1)", Sink::toString(static_cast<Sink::TouchTemperature>(4)));
    TEST_ASSERT_EQUAL_STRING("Invalid (assume IEC 60950-1)", Sink::toString(static_cast<Sink::TouchTemperature>(255)));
}

// Battery Info 0x21: low nibble 1 fixed battery, high nibble 2 hot swappable slots.
void test_typed_battery_info_nibbles()
{
    Sink reg(RESET_BUFFER);
    reg.setBatteryInfo(0x21);
    TEST_ASSERT_EQUAL_UINT8(1, reg.fixedBatteryCount());
    TEST_ASSERT_EQUAL_UINT8(2, reg.hotSwappableBatterySlotCount());

    reg.setBatteryInfo(0xF5);  // raw nibbles: 5 and 15 are invalid and the accessor does not mask them
    TEST_ASSERT_EQUAL_UINT8(5, reg.fixedBatteryCount());
    TEST_ASSERT_EQUAL_UINT8(15, reg.hotSwappableBatterySlotCount());
}

void test_typed_sink_modes_bits()
{
    Sink reg(RESET_BUFFER);
    TEST_ASSERT_FALSE(reg.ppsChargingSupported());
    TEST_ASSERT_FALSE(reg.vconnPoweredAccessory());

    // One bit at a time: bit n of Sink Modes selects exactly one flag.
    bool (*const flags[7])(const Sink &) = {
        [](const Sink &r) { return r.ppsChargingSupported(); },
        [](const Sink &r) { return r.vbusPowered(); },
        [](const Sink &r) { return r.acSupplyPowered(); },
        [](const Sink &r) { return r.batteryPowered(); },
        [](const Sink &r) { return r.batteryEssentiallyUnlimited(); },
        [](const Sink &r) { return r.avsSupported(); },
        [](const Sink &r) { return r.vconnPoweredAccessory(); }};
    for (unsigned bit = 0; bit < 7; bit++)
    {
        reg.setSinkModes(static_cast<uint8_t>(1u << bit));
        for (unsigned f = 0; f < 7; f++) TEST_ASSERT_EQUAL(f == bit, flags[f](reg));
    }

    reg.setSinkModes(0x7F);  // every defined bit
    for (unsigned f = 0; f < 7; f++) TEST_ASSERT_TRUE(flags[f](reg));

    reg.setSinkModes(0x80);  // bit 7 is undefined: no flag
    for (unsigned f = 0; f < 7; f++) TEST_ASSERT_FALSE(flags[f](reg));
}

void test_typed_pdp_validity()
{
    Sink reg(RESET_BUFFER);
    reg.setSinkMinimumPdp(100);
    reg.setSinkOperationalPdp(100);
    reg.setSinkMaximumPdp(100);
    TEST_ASSERT_TRUE(reg.sinkMinimumPdpValid());
    TEST_ASSERT_TRUE(reg.sinkOperationalPdpValid());
    TEST_ASSERT_TRUE(reg.sinkMaximumPdpValid());
    TEST_ASSERT_EQUAL_UINT8(100, reg.sinkMinimumPdp_W());
    TEST_ASSERT_EQUAL_UINT8(100, reg.sinkOperationalPdp_W());
    TEST_ASSERT_EQUAL_UINT8(100, reg.sinkMaximumPdp_W());

    reg.setSinkMinimumPdp(101);
    reg.setSinkOperationalPdp(101);
    reg.setSinkMaximumPdp(101);
    TEST_ASSERT_FALSE(reg.sinkMinimumPdpValid());
    TEST_ASSERT_FALSE(reg.sinkOperationalPdpValid());
    TEST_ASSERT_FALSE(reg.sinkMaximumPdpValid());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkMinimumPdp_W());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkOperationalPdp_W());
    TEST_ASSERT_EQUAL_UINT8(0, reg.sinkMaximumPdp_W());

    // Each PDP is independent: only the minimum valid.
    reg.setSinkMinimumPdp(15);
    TEST_ASSERT_TRUE(reg.sinkMinimumPdpValid());
    TEST_ASSERT_EQUAL_UINT8(15, reg.sinkMinimumPdp_W());
    TEST_ASSERT_FALSE(reg.sinkOperationalPdpValid());
}

void test_debug_print_typed_decode_lines()
{
    Sink reg(RESET_BUFFER);
    reg.setSkedbVersion(1);
    reg.setLoadStep(0x01);
    reg.setSinkLoadCharacteristics(0xFFF9);
    reg.setCompliance(0x03);       // LPS and PS1
    reg.setTouchTemperature(1);
    reg.setBatteryInfo(0x21);
    reg.setSinkModes(0x03);        // PPS charging and VBUS powered
    reg.setSinkMinimumPdp(15);
    reg.setSinkOperationalPdp(45);
    reg.setSinkMaximumPdp(101);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("SKEDB Version valid: Yes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Load Step rate: 500 mA/us"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Sink Load: 250% overload, 1260 ms, 75% duty, VBUS droop tolerated: Yes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Compliance: LPS: Yes, PS1: Yes, PS2: No"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Touch Temperature standard: IEC 60950-1"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Batteries: 1 fixed, 2 hot swappable slots"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Sink Modes: PPS charging: Yes, VBUS powered: Yes, AC powered: No"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("SPR Sink Minimum PDP: 15 W"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("SPR Sink Operational PDP: 45 W"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("SPR Sink Maximum PDP: invalid (101)"));
    // The existing raw lines remain.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Sink Load Characteristics: 0xFFF9"));
}

void test_copy_constructor_deep_copies()
{
    Sink source(DISTINCT_BUFFER);
    Sink copy(source);

    TEST_ASSERT_NOT_EQUAL(source.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, copy.raw(), SINK_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    Sink original(DISTINCT_BUFFER);
    Sink moved(static_cast<Sink &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, moved.raw(), SINK_SIZE);
}

void test_copy_assignment_deep_copies()
{
    Sink source(DISTINCT_BUFFER);
    Sink target(RESET_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, target.raw(), SINK_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    Sink source(DISTINCT_BUFFER);
    Sink target(RESET_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<Sink &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DISTINCT_BUFFER, target.raw(), SINK_SIZE);
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_address_identity);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_reset_value_decodes_to_zero);
    RUN_TEST(test_distinct_fixture_decodes_every_field);

    RUN_TEST(test_every_setter_round_trips_and_leaves_other_bits_unchanged);
    RUN_TEST(test_sink_load_characteristics_is_little_endian);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_reset_and_distinct_are_valid);
    RUN_TEST(test_reserved_bits_invalid);
    RUN_TEST(test_every_reserved_bit_individually_invalid);
    RUN_TEST(test_pdp_bytes_in_reverse_order_are_still_valid);

    RUN_TEST(test_debug_print_distinct_fixture);
    RUN_TEST(test_debug_print_reset_zero_pads_and_reports_reserved);

    RUN_TEST(test_typed_skedb_version);
    RUN_TEST(test_typed_load_step_rate);
    RUN_TEST(test_typed_sink_load_maximum_word);
    RUN_TEST(test_typed_sink_load_zero_word_and_clamp);
    RUN_TEST(test_typed_compliance_bits);
    RUN_TEST(test_typed_touch_temperature_standard);
    RUN_TEST(test_typed_battery_info_nibbles);
    RUN_TEST(test_typed_sink_modes_bits);
    RUN_TEST(test_typed_pdp_validity);
    RUN_TEST(test_debug_print_typed_decode_lines);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
