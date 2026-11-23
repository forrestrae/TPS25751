/**
 * @file test_TPS25751TransmitSourceInfo.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitSourceInfo.
 *
 * Run with: pio test -e native
 *
 * TX_SOURCE_INFO (0x78) is an 8-byte R/W register, reset 8000000080000000h. Bit n
 * lives in byte n / 8, bit n % 8, so the reset value is the buffer
 * {0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80} (bit 31 = byte 3 bit 7,
 * bit 63 = byte 7 bit 7).
 *
 * Fields (register bit numbers):
 *   63 PortType, 62 DPSPort, 61:50 reserved,
 *   49:41 PortMaximumPDP_0p5W, 40:32 PortGuaranteedPDP_0p5W,
 *   31 Port Type, 30:24 reserved,
 *   23:16 Port Maximum PDP, 15:8 Port Present PDP, 7:0 Port Reported PDP
 *
 * Hand-packed decode fixture (distinct value in every field):
 *   MAX = 0x155 (341), GUAR = 0x0AA (170); alternating bits catch an off-by-one
 *   on the 9-bit fields. maxPdp = 100 (0x64), present = 45 (0x2D),
 *   reported = 60 (0x3C).
 *   upper word = (1<<30 DPS) | (MAX<<9) | GUAR
 *              = 0x40000000 | 0x0002AA00 | 0x000000AA = 0x4002AAAA
 *   lower word = (1<<31) | (maxPdp<<16) | (present<<8) | reported
 *              = 0x80000000 | 0x00640000 | 0x00002D00 | 0x0000003C = 0x80642D3C
 *   (Port Type bit 63 is 0, Managed, because DPSPort requires it.)
 */

#include <unity.h>

#include <cstdio>
#include <string>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitSourceInfo.h"
#include "CaptureStdout.h"

namespace {

using Info = TPS25751TransmitSourceInfo;
using PortType = Info::PortType;

constexpr size_t INFO_SIZE = TPS25751Registers::Registers::TRANSMIT_SOURCE_INFO.size;  // 8

constexpr uint8_t RESET_BUFFER[INFO_SIZE] = {0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80};
constexpr uint8_t ALL_ONES_DATA[INFO_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Every field nonzero and consistent with USB PD R3.3 Tables 6.29 and 6.30:
// DPS Port set with both Port Type bits Managed, and the 1 W Port Maximum PDP
// (170) equal to the 0.5 W one (341) rounded down.
constexpr uint32_t FIXTURE_UPPER = 0x4002AAAAu;
constexpr uint32_t FIXTURE_LOWER = 0x00AA2D3Cu;

// Every field nonzero, used as a base for the setter isolation tests (not
// semantically valid: DPSPort and Port Type bit 63 are both 1).
constexpr uint32_t BUSY_UPPER = 0xC002AAAAu;
constexpr uint32_t BUSY_LOWER = 0x80642D3Cu;

uint64_t pack64(uint32_t upper, uint32_t lower)
{
    return (static_cast<uint64_t>(upper) << 32) | lower;
}

void packLE(uint64_t value, uint8_t *out)
{
    for (int i = 0; i < 8; i++) out[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint64_t unpackLE(const uint8_t *in)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | in[i];
    return v;
}

Info makeInfo(uint32_t upper, uint32_t lower)
{
    uint8_t buf[INFO_SIZE];
    packLE(pack64(upper, lower), buf);
    return Info(buf);
}

Info resetWithBit(uint8_t bit)
{
    uint8_t buf[INFO_SIZE];
    packLE(pack64(0x80000000u, 0x80000000u) | (1ULL << bit), buf);
    return Info(buf);
}

uint64_t fieldMask(uint8_t pos, uint8_t len)
{
    return ((len >= 64) ? ~0ULL : ((1ULL << len) - 1)) << pos;
}

// One row per raw setter: field position and width, and the setter itself.
struct SetterCase
{
    const char *name;
    uint8_t pos;
    uint8_t len;
    void (*set)(Info &, uint32_t);
};

const SetterCase kSetterCases[] = {
    {"portTypeUpper", 63, 1, [](Info &r, uint32_t v) { r.setPortTypeUpper(static_cast<PortType>(v)); }},
    {"dpsPort", 62, 1, [](Info &r, uint32_t v) { r.setDpsPort(v != 0); }},
    {"maxPdp0p5W", 41, 9, [](Info &r, uint32_t v) { r.setRawPortMaximumPdp0p5W(static_cast<uint16_t>(v)); }},
    {"guaranteedPdp0p5W", 32, 9, [](Info &r, uint32_t v) { r.setRawPortGuaranteedPdp0p5W(static_cast<uint16_t>(v)); }},
    {"portTypeLower", 31, 1, [](Info &r, uint32_t v) { r.setPortTypeLower(static_cast<PortType>(v)); }},
    {"maxPdp", 16, 8, [](Info &r, uint32_t v) { r.setRawPortMaximumPdp(static_cast<uint8_t>(v)); }},
    {"presentPdp", 8, 8, [](Info &r, uint32_t v) { r.setRawPortPresentPdp(static_cast<uint8_t>(v)); }},
    {"reportedPdp", 0, 8, [](Info &r, uint32_t v) { r.setRawPortReportedPdp(static_cast<uint8_t>(v)); }},
};
constexpr size_t kSetterCaseCount = sizeof(kSetterCases) / sizeof(kSetterCases[0]);

// Applies a setter to a copy of the base and checks the field holds
// `expectedField` while every bit outside it is unchanged.
void assertSetterIsolated(const char *name, uint64_t base, uint8_t pos, uint8_t len,
                          void (*apply)(Info &), uint64_t expectedField)
{
    uint8_t buf[INFO_SIZE];
    packLE(base, buf);
    Info reg(buf);

    apply(reg);

    const uint64_t after = unpackLE(reg.raw());
    const uint64_t mask = fieldMask(pos, len);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(base & ~mask, after & ~mask, name);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(expectedField, (after & mask) >> pos, name);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Constants -------------------------------------------------------------------

void test_address_identity()
{
    TEST_ASSERT_TRUE(Info::kAddress == TPS25751Registers::Address::TRANSMIT_SOURCE_INFO);
    TEST_ASSERT_EQUAL_UINT8(0x78, static_cast<uint8_t>(Info::kAddress));
    TEST_ASSERT_EQUAL_UINT8(8, TPS25751Registers::getRegisterSize(Info::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(Info::kAddress));
    TEST_ASSERT_EQUAL_UINT64(0x8000000080000000ULL, unpackLE(RESET_BUFFER));
}

void test_unit_and_limit_constants()
{
    TEST_ASSERT_EQUAL_UINT16(500, Info::kHalfWattLsb_mW);
    TEST_ASSERT_EQUAL_UINT16(1000, Info::kWattLsb_mW);
    TEST_ASSERT_EQUAL_UINT16(0x1FF, Info::kMax9BitRaw);
    TEST_ASSERT_EQUAL_UINT16(0xFF, Info::kMax8BitRaw);
}

void test_reserved_masks_derivation()
{
    // Bits 30:24 of the low word, bits 61:50 = upper-word bits 29:18.
    TEST_ASSERT_EQUAL_UINT32(0x7F000000u, Info::kReservedMaskLower);
    TEST_ASSERT_EQUAL_UINT32(0x3FFC0000u, Info::kReservedMaskUpper);
    TEST_ASSERT_EQUAL_UINT64(fieldMask(24, 7) | fieldMask(50, 12),
                             pack64(Info::kReservedMaskUpper, Info::kReservedMaskLower));
}

// --- Construction ----------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    Info reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INFO_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(INFO_SIZE));
    TEST_ASSERT_EQUAL_UINT64(0ULL, unpackLE(reg.raw()));
}

void test_data_constructor_copies_buffer()
{
    Info reg(RESET_BUFFER);

    TEST_ASSERT_NOT_EQUAL(RESET_BUFFER, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(RESET_BUFFER, reg.raw(), INFO_SIZE);
}

void test_explicit_length_constructor()
{
    Info reg(RESET_BUFFER, INFO_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(INFO_SIZE));
    TEST_ASSERT_TRUE(reg.portTypeUpper() == PortType::Guaranteed);
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    Info reg(TPS25751Registers::Registers::TRANSMIT_SOURCE_INFO);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INFO_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.dpsPort());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[4] = {0x00, 0x00, 0x00, 0x80};
    Info reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(INFO_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Decode ----------------------------------------------------------------------

void test_extract_bits32_reads_upper_word_of_8_byte_buffer()
{
    Info reset(RESET_BUFFER);
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, reset.extractBits32(0, 32));
    TEST_ASSERT_EQUAL_UINT32(0x80000000u, reset.extractBits32(32, 32));

    Info fixture = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);
    TEST_ASSERT_EQUAL_UINT32(FIXTURE_LOWER, fixture.extractBits32(0, 32));
    TEST_ASSERT_EQUAL_UINT32(FIXTURE_UPPER, fixture.extractBits32(32, 32));
}

void test_reset_value_decodes()
{
    Info reg(RESET_BUFFER);

    TEST_ASSERT_TRUE(reg.portTypeUpper() == PortType::Guaranteed);
    TEST_ASSERT_TRUE(reg.portTypeLower() == PortType::Guaranteed);
    TEST_ASSERT_FALSE(reg.dpsPort());
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawPortMaximumPdp0p5W());
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawPortGuaranteedPdp0p5W());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawPortMaximumPdp());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawPortPresentPdp());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawPortReportedPdp());
    TEST_ASSERT_EQUAL_UINT32(0, reg.portMaximumPdp0p5W_mW());
    TEST_ASSERT_EQUAL_UINT32(0, reg.portGuaranteedPdp0p5W_mW());
    TEST_ASSERT_EQUAL_UINT32(0, reg.portMaximumPdp_mW());
    TEST_ASSERT_EQUAL_UINT32(0, reg.portPresentPdp_mW());
    TEST_ASSERT_EQUAL_UINT32(0, reg.portReportedPdp_mW());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_hand_packed_fixture_decodes_every_field()
{
    Info reg = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);

    TEST_ASSERT_TRUE(reg.portTypeUpper() == PortType::Managed);
    TEST_ASSERT_TRUE(reg.dpsPort());
    TEST_ASSERT_TRUE(reg.portTypeLower() == PortType::Managed);

    TEST_ASSERT_EQUAL_UINT16(0x155, reg.rawPortMaximumPdp0p5W());
    TEST_ASSERT_EQUAL_UINT32(170500, reg.portMaximumPdp0p5W_mW());
    TEST_ASSERT_EQUAL_UINT16(0x0AA, reg.rawPortGuaranteedPdp0p5W());
    TEST_ASSERT_EQUAL_UINT32(85000, reg.portGuaranteedPdp0p5W_mW());

    TEST_ASSERT_EQUAL_UINT8(170, reg.rawPortMaximumPdp());
    TEST_ASSERT_EQUAL_UINT32(170000, reg.portMaximumPdp_mW());
    TEST_ASSERT_EQUAL_UINT8(45, reg.rawPortPresentPdp());
    TEST_ASSERT_EQUAL_UINT32(45000, reg.portPresentPdp_mW());
    TEST_ASSERT_EQUAL_UINT8(60, reg.rawPortReportedPdp());
    TEST_ASSERT_EQUAL_UINT32(60000, reg.portReportedPdp_mW());

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_ninebit_maximum_decodes_without_overflow()
{
    // Raw 511 in both 9-bit fields: 511 x 500 = 255 500 mW, past uint16_t.
    Info reg = makeInfo(0x0003FFFFu, 0x80000000u);

    TEST_ASSERT_EQUAL_UINT16(511, reg.rawPortMaximumPdp0p5W());
    TEST_ASSERT_EQUAL_UINT32(255500, reg.portMaximumPdp0p5W_mW());
    TEST_ASSERT_EQUAL_UINT16(511, reg.rawPortGuaranteedPdp0p5W());
    TEST_ASSERT_EQUAL_UINT32(255500, reg.portGuaranteedPdp0p5W_mW());
}

void test_eightbit_maximum_decodes()
{
    Info reg = makeInfo(0x80000000u, 0x80FFFFFFu);

    TEST_ASSERT_EQUAL_UINT8(255, reg.rawPortMaximumPdp());
    TEST_ASSERT_EQUAL_UINT32(255000, reg.portMaximumPdp_mW());
    TEST_ASSERT_EQUAL_UINT32(255000, reg.portPresentPdp_mW());
    TEST_ASSERT_EQUAL_UINT32(255000, reg.portReportedPdp_mW());
}

void test_port_type_bits_are_independent()
{
    Info upperOnly = makeInfo(0x80000000u, 0x00000000u);
    TEST_ASSERT_TRUE(upperOnly.portTypeUpper() == PortType::Guaranteed);
    TEST_ASSERT_TRUE(upperOnly.portTypeLower() == PortType::Managed);

    Info lowerOnly = makeInfo(0x00000000u, 0x80000000u);
    TEST_ASSERT_TRUE(lowerOnly.portTypeUpper() == PortType::Managed);
    TEST_ASSERT_TRUE(lowerOnly.portTypeLower() == PortType::Guaranteed);
}

void test_port_type_to_string()
{
    TEST_ASSERT_EQUAL_STRING("Managed Capability Port", Info::toString(PortType::Managed));
    TEST_ASSERT_EQUAL_STRING("Guaranteed Capability Port", Info::toString(PortType::Guaranteed));
}

// --- Setters ---------------------------------------------------------------------

void test_raw_setters_write_only_their_field()
{
    // Zero base: write the field's maximum. Busy base: write 0.
    for (size_t i = 0; i < kSetterCaseCount; i++)
    {
        const SetterCase &c = kSetterCases[i];
        const uint64_t fieldMax = (1ULL << c.len) - 1;

        uint8_t zeroBuf[INFO_SIZE] = {0};
        Info fromZero(zeroBuf);
        c.set(fromZero, static_cast<uint32_t>(fieldMax));
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(fieldMax << c.pos, unpackLE(fromZero.raw()), c.name);

        Info fromBusy = makeInfo(BUSY_UPPER, BUSY_LOWER);
        const uint64_t base = unpackLE(fromBusy.raw());
        c.set(fromBusy, 0);
        const uint64_t mask = fieldMask(c.pos, c.len);
        TEST_ASSERT_EQUAL_UINT64_MESSAGE(base & ~mask, unpackLE(fromBusy.raw()), c.name);
    }
}

void test_raw_setters_round_trip_against_decoders()
{
    Info reg = makeInfo(BUSY_UPPER, BUSY_LOWER);

    reg.setPortTypeUpper(PortType::Managed);
    reg.setPortTypeLower(PortType::Managed);
    reg.setDpsPort(false);
    reg.setRawPortMaximumPdp0p5W(0x155);
    reg.setRawPortGuaranteedPdp0p5W(0x0AA);
    reg.setRawPortMaximumPdp(100);
    reg.setRawPortPresentPdp(45);
    reg.setRawPortReportedPdp(60);

    TEST_ASSERT_TRUE(reg.portTypeUpper() == PortType::Managed);
    TEST_ASSERT_TRUE(reg.portTypeLower() == PortType::Managed);
    TEST_ASSERT_FALSE(reg.dpsPort());
    TEST_ASSERT_EQUAL_UINT16(0x155, reg.rawPortMaximumPdp0p5W());
    TEST_ASSERT_EQUAL_UINT16(0x0AA, reg.rawPortGuaranteedPdp0p5W());
    TEST_ASSERT_EQUAL_UINT8(100, reg.rawPortMaximumPdp());
    TEST_ASSERT_EQUAL_UINT8(45, reg.rawPortPresentPdp());
    TEST_ASSERT_EQUAL_UINT8(60, reg.rawPortReportedPdp());
    // Only the two Port Type bits and DPS changed relative to the fixture.
    TEST_ASSERT_EQUAL_UINT64(pack64(0x0002AAAAu, 0x00642D3Cu), unpackLE(reg.raw()));
}

void test_unit_setters_round_trip_and_touch_only_their_field()
{
    const uint64_t base = pack64(BUSY_UPPER, BUSY_LOWER);

    assertSetterIsolated("maxPdp0p5W_mW", base, 41, 9,
                         [](Info &r) { r.setPortMaximumPdp0p5W_mW(15000); }, 30);
    assertSetterIsolated("guaranteedPdp0p5W_mW", base, 32, 9,
                         [](Info &r) { r.setPortGuaranteedPdp0p5W_mW(12500); }, 25);
    assertSetterIsolated("maxPdp_mW", base, 16, 8,
                         [](Info &r) { r.setPortMaximumPdp_mW(20000); }, 20);
    assertSetterIsolated("presentPdp_mW", base, 8, 8,
                         [](Info &r) { r.setPortPresentPdp_mW(15000); }, 15);
    assertSetterIsolated("reportedPdp_mW", base, 0, 8,
                         [](Info &r) { r.setPortReportedPdp_mW(9000); }, 9);

    Info reg = makeInfo(BUSY_UPPER, BUSY_LOWER);
    reg.setPortMaximumPdp0p5W_mW(15000);
    TEST_ASSERT_EQUAL_UINT32(15000, reg.portMaximumPdp0p5W_mW());
    reg.setPortReportedPdp_mW(9000);
    TEST_ASSERT_EQUAL_UINT32(9000, reg.portReportedPdp_mW());
}

void test_ninebit_raw_setters_mask_to_field_width()
{
    uint8_t zeroBuf[INFO_SIZE] = {0};

    // 0x3FF must not reach bit 50, the lowest reserved bit above the field.
    Info maxField(zeroBuf);
    maxField.setRawPortMaximumPdp0p5W(0x3FF);
    TEST_ASSERT_EQUAL_UINT16(0x1FF, maxField.rawPortMaximumPdp0p5W());
    TEST_ASSERT_EQUAL_UINT64(0ULL, unpackLE(maxField.raw()) & (1ULL << 50));
    TEST_ASSERT_EQUAL_UINT64(fieldMask(41, 9), unpackLE(maxField.raw()));

    // 0x3FF must not reach bit 41, the maximum field's LSB.
    Info guaranteedFromZero(zeroBuf);
    guaranteedFromZero.setRawPortGuaranteedPdp0p5W(0x3FF);
    TEST_ASSERT_EQUAL_UINT16(0x1FF, guaranteedFromZero.rawPortGuaranteedPdp0p5W());
    TEST_ASSERT_EQUAL_UINT64(0ULL, unpackLE(guaranteedFromZero.raw()) & (1ULL << 41));
    TEST_ASSERT_EQUAL_UINT16(0, guaranteedFromZero.rawPortMaximumPdp0p5W());

    // With the maximum field set, bit 41 stays set.
    Info guaranteedFromMax = makeInfo(0x0003FE00u, 0);
    guaranteedFromMax.setRawPortGuaranteedPdp0p5W(0x3FF);
    TEST_ASSERT_EQUAL_UINT64(1ULL << 41, unpackLE(guaranteedFromMax.raw()) & (1ULL << 41));
    TEST_ASSERT_EQUAL_UINT16(0x1FF, guaranteedFromMax.rawPortMaximumPdp0p5W());
}

void test_unit_setters_clamp_instead_of_wrapping()
{
    uint8_t zeroBuf[INFO_SIZE] = {0};

    Info half(zeroBuf);
    half.setPortMaximumPdp0p5W_mW(300000);  // 600 raw: would wrap to 88 if masked
    TEST_ASSERT_EQUAL_UINT16(511, half.rawPortMaximumPdp0p5W());
    TEST_ASSERT_EQUAL_UINT32(255500, half.portMaximumPdp0p5W_mW());
    TEST_ASSERT_EQUAL_UINT64(0ULL, unpackLE(half.raw()) & (1ULL << 50));

    Info halfG(zeroBuf);
    halfG.setPortGuaranteedPdp0p5W_mW(300000);
    TEST_ASSERT_EQUAL_UINT16(511, halfG.rawPortGuaranteedPdp0p5W());
    TEST_ASSERT_EQUAL_UINT16(0, halfG.rawPortMaximumPdp0p5W());

    Info whole(zeroBuf);
    whole.setPortMaximumPdp_mW(300000);  // 300 raw: would wrap to 44 if masked
    TEST_ASSERT_EQUAL_UINT8(255, whole.rawPortMaximumPdp());
    whole.setPortPresentPdp_mW(300000);
    TEST_ASSERT_EQUAL_UINT8(255, whole.rawPortPresentPdp());
    whole.setPortReportedPdp_mW(0xFFFFFFFFu);
    TEST_ASSERT_EQUAL_UINT8(255, whole.rawPortReportedPdp());
    // Nothing above bit 23 was touched.
    TEST_ASSERT_EQUAL_UINT64(0x00FFFFFFULL, unpackLE(whole.raw()));
}

void test_unit_setters_floor()
{
    uint8_t zeroBuf[INFO_SIZE] = {0};
    Info reg(zeroBuf);

    reg.setPortMaximumPdp_mW(15999);
    TEST_ASSERT_EQUAL_UINT8(15, reg.rawPortMaximumPdp());

    reg.setPortMaximumPdp0p5W_mW(1499);
    TEST_ASSERT_EQUAL_UINT16(2, reg.rawPortMaximumPdp0p5W());

    reg.setPortMaximumPdp0p5W_mW(499);
    TEST_ASSERT_EQUAL_UINT16(0, reg.rawPortMaximumPdp0p5W());
}

void test_setters_no_op_on_invalid_register()
{
    Info original(RESET_BUFFER);
    Info moved(static_cast<Info &&>(original));

    // original has been moved from and holds no buffer.
    TEST_ASSERT_FALSE(original.isValid());
    original.setDpsPort(true);
    original.setRawPortMaximumPdp0p5W(10);
    original.setPortReportedPdp_mW(5000);
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_FALSE(original.dpsPort());
    TEST_ASSERT_EQUAL_UINT16(0, original.rawPortMaximumPdp0p5W());
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    Info reg(ALL_ONES_DATA);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_zeros_is_valid()
{
    Info reg;

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_dps_with_upper_port_type_guaranteed_is_invalid()
{
    // DPSPort = 1 and bit 63 = 1.
    Info reg = makeInfo(0xC0000000u, 0x80000000u);

    TEST_ASSERT_TRUE(reg.dpsPort());
    TEST_ASSERT_TRUE(reg.portTypeUpper() == PortType::Guaranteed);
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_dps_with_lower_port_type_guaranteed_is_invalid()
{
    // DPSPort = 1, bit 63 = 0, bit 31 = 1. The TRM names bit 63 only, but USB PD
    // R3.3 Table 6.30 requires Port Type 0 in both SIDO1 (bit 31) and SIDO2.
    Info reg = makeInfo(0x40000000u, 0x80000000u);

    TEST_ASSERT_TRUE(reg.dpsPort());
    TEST_ASSERT_TRUE(reg.portTypeUpper() == PortType::Managed);
    TEST_ASSERT_TRUE(reg.portTypeLower() == PortType::Guaranteed);
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_dps_with_both_port_types_managed_is_valid()
{
    Info reg = makeInfo(0x40000000u, 0x00000000u);

    TEST_ASSERT_TRUE(reg.dpsPort());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_port_types_may_differ_without_dps()
{
    // The Table 6.30 rule applies only with DPS Port set.
    TEST_ASSERT_TRUE(makeInfo(0x80000000u, 0x00000000u).isSemanticallyValid());
    TEST_ASSERT_TRUE(makeInfo(0x00000000u, 0x80000000u).isSemanticallyValid());
}

// --- Port Maximum PDP agreement (USB PD R3.3 Table 6.29) --------------------------

void test_maximum_pdp_equal_to_rounded_down_half_watt_value_is_valid()
{
    Info even = makeInfo(0x80000000u, 0x80000000u);
    even.setRawPortMaximumPdp0p5W(200);  // 100 W
    even.setRawPortMaximumPdp(100);
    TEST_ASSERT_TRUE(even.isSemanticallyValid());

    Info odd = makeInfo(0x80000000u, 0x80000000u);
    odd.setRawPortMaximumPdp0p5W(31);    // 15.5 W rounds down to 15 W
    odd.setRawPortMaximumPdp(15);
    TEST_ASSERT_TRUE(odd.isSemanticallyValid());

    Info top = makeInfo(0x80000000u, 0x80000000u);
    top.setRawPortMaximumPdp0p5W(511);   // 255.5 W rounds down to 255 W
    top.setRawPortMaximumPdp(255);
    TEST_ASSERT_TRUE(top.isSemanticallyValid());
}

void test_maximum_pdp_rounded_up_is_invalid()
{
    Info reg = makeInfo(0x80000000u, 0x80000000u);
    reg.setRawPortMaximumPdp0p5W(31);  // 15.5 W
    reg.setRawPortMaximumPdp(16);

    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_maximum_pdp_set_in_one_word_only_is_invalid()
{
    Info lowerOnly = makeInfo(0x80000000u, 0x80000000u);
    lowerOnly.setRawPortMaximumPdp(100);
    TEST_ASSERT_FALSE(lowerOnly.isSemanticallyValid());

    Info upperOnly = makeInfo(0x80000000u, 0x80000000u);
    upperOnly.setRawPortMaximumPdp0p5W(200);
    TEST_ASSERT_FALSE(upperOnly.isSemanticallyValid());

    // One 0.5 W step rounds down to 0 W, so a lower word of 0 still agrees.
    Info halfWatt = makeInfo(0x80000000u, 0x80000000u);
    halfWatt.setRawPortMaximumPdp0p5W(1);
    TEST_ASSERT_TRUE(halfWatt.isSemanticallyValid());
}

void test_hardware_value_after_sink_attach_is_valid()
{
    // Bench read on 2026-11-19, board sourcing an iPhone: the firmware rewrote
    // the lower word to 64 64 00 80 (Reported and Present PDP 100 W, Maximum
    // PDP 0) and left the upper word at the reset.
    const uint8_t hw[INFO_SIZE] = {0x64, 0x64, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80};
    Info reg(hw);

    TEST_ASSERT_EQUAL_UINT8(100, reg.rawPortPresentPdp());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_guaranteed_above_maximum_is_invalid()
{
    Info reg = makeInfo(0x80000000u, 0x80000000u);
    reg.setRawPortMaximumPdp0p5W(100);
    reg.setRawPortMaximumPdp(50);
    reg.setRawPortGuaranteedPdp0p5W(101);

    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_guaranteed_with_zero_maximum_is_valid()
{
    Info reg = makeInfo(0x80000000u, 0x80000000u);
    reg.setRawPortGuaranteedPdp0p5W(50);

    TEST_ASSERT_EQUAL_UINT16(0, reg.rawPortMaximumPdp0p5W());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_guaranteed_equal_to_maximum_is_valid()
{
    Info reg = makeInfo(0x80000000u, 0x80000000u);
    reg.setRawPortMaximumPdp0p5W(100);
    reg.setRawPortMaximumPdp(50);
    reg.setRawPortGuaranteedPdp0p5W(100);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_reserved_bits_in_each_range_are_invalid()
{
    TEST_ASSERT_FALSE(resetWithBit(24).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(30).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(50).isSemanticallyValid());
    TEST_ASSERT_FALSE(resetWithBit(61).isSemanticallyValid());
}

void test_every_reserved_bit_individually_invalid()
{
    for (uint8_t bit = 24; bit <= 30; bit++)
    {
        TEST_ASSERT_FALSE_MESSAGE(resetWithBit(bit).isSemanticallyValid(), "lower reserved range");
    }
    for (uint8_t bit = 50; bit <= 61; bit++)
    {
        TEST_ASSERT_FALSE_MESSAGE(resetWithBit(bit).isSemanticallyValid(), "upper reserved range");
    }
}

void test_bits_next_to_reserved_ranges_are_valid()
{
    // Bits 23 and 49 are the top bits of defined fields, not reserved. Set
    // together they read 128 W and 256 x 0.5 W, which agree (Table 6.29).
    uint8_t buf[INFO_SIZE];
    packLE(pack64(0x80000000u, 0x80000000u) | (1ULL << 23) | (1ULL << 49), buf);
    TEST_ASSERT_TRUE(Info(buf).isSemanticallyValid());
}

// --- Debug print -----------------------------------------------------------------

void test_debug_print_reset_value()
{
    Info reg(RESET_BUFFER);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("TX_SOURCE_INFO (0x78)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw: 0x8000000080000000"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Type (bit 63): Guaranteed Capability Port"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Type (bit 31): Guaranteed Capability Port"));
    TEST_ASSERT_EQUAL(std::string::npos, out.find("Reserved bits set"));
}

void test_debug_print_fixture_fields()
{
    Info reg = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw: 0x4002AAAA00AA2D3C"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("DPS Port (bit 62): Yes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Maximum PDP (0.5 W units): 341 (170500 mW)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Guaranteed PDP (0.5 W units): 170 (85000 mW)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Maximum PDP (1 W units): 170 (170000 mW)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Present PDP (1 W units): 45 (45000 mW)"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Port Reported PDP (1 W units): 60 (60000 mW)"));
}

void test_debug_print_reports_reserved_bits()
{
    Info reg = resetWithBit(50);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Reserved bits set: 0x0004000000000000"));
}

// --- Rule of Five ----------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    Info original = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);
    Info copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), INFO_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    Info original = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);
    const uint8_t *buf = original.raw();

    Info moved(static_cast<Info &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT64(pack64(FIXTURE_UPPER, FIXTURE_LOWER), unpackLE(moved.raw()));
}

void test_copy_assignment_deep_copies()
{
    Info source = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);
    Info target(RESET_BUFFER);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source.raw(), target.raw(), INFO_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    Info source = makeInfo(FIXTURE_UPPER, FIXTURE_LOWER);
    Info target(RESET_BUFFER);
    const uint8_t *buf = source.raw();

    target = static_cast<Info &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT64(pack64(FIXTURE_UPPER, FIXTURE_LOWER), unpackLE(target.raw()));
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_address_identity);
    RUN_TEST(test_unit_and_limit_constants);
    RUN_TEST(test_reserved_masks_derivation);

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_extract_bits32_reads_upper_word_of_8_byte_buffer);
    RUN_TEST(test_reset_value_decodes);
    RUN_TEST(test_hand_packed_fixture_decodes_every_field);
    RUN_TEST(test_ninebit_maximum_decodes_without_overflow);
    RUN_TEST(test_eightbit_maximum_decodes);
    RUN_TEST(test_port_type_bits_are_independent);
    RUN_TEST(test_port_type_to_string);

    RUN_TEST(test_raw_setters_write_only_their_field);
    RUN_TEST(test_raw_setters_round_trip_against_decoders);
    RUN_TEST(test_unit_setters_round_trip_and_touch_only_their_field);
    RUN_TEST(test_ninebit_raw_setters_mask_to_field_width);
    RUN_TEST(test_unit_setters_clamp_instead_of_wrapping);
    RUN_TEST(test_unit_setters_floor);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_all_zeros_is_valid);
    RUN_TEST(test_dps_with_upper_port_type_guaranteed_is_invalid);
    RUN_TEST(test_dps_with_lower_port_type_guaranteed_is_invalid);
    RUN_TEST(test_dps_with_both_port_types_managed_is_valid);
    RUN_TEST(test_port_types_may_differ_without_dps);
    RUN_TEST(test_maximum_pdp_equal_to_rounded_down_half_watt_value_is_valid);
    RUN_TEST(test_maximum_pdp_rounded_up_is_invalid);
    RUN_TEST(test_maximum_pdp_set_in_one_word_only_is_invalid);
    RUN_TEST(test_hardware_value_after_sink_attach_is_valid);
    RUN_TEST(test_guaranteed_above_maximum_is_invalid);
    RUN_TEST(test_guaranteed_with_zero_maximum_is_valid);
    RUN_TEST(test_guaranteed_equal_to_maximum_is_valid);
    RUN_TEST(test_reserved_bits_in_each_range_are_invalid);
    RUN_TEST(test_every_reserved_bit_individually_invalid);
    RUN_TEST(test_bits_next_to_reserved_ranges_are_valid);

    RUN_TEST(test_debug_print_reset_value);
    RUN_TEST(test_debug_print_fixture_fields);
    RUN_TEST(test_debug_print_reports_reserved_bits);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
