/**
 * @file test_TPS25751ReceivedSinkCaps.cpp
 * @brief Native (host-run) unit tests for TPS25751ReceivedSinkCaps.
 *
 * Run with: pio test -e native
 *
 * RECEIVED_SINK_CAPABILITIES is 53 bytes: byte 0 bits 2:0 hold the
 * number of valid PDOs, and PDO n sits at byte 1 + (n-1)*4, little-endian.
 *
 * This suite covers the APDO subtype handling added after a bench read on
 * 2026-11-18 found an Augmented PDO with bits 29:28 = 10b (not the SPR PPS
 * subtype 00b) in TRANSMIT_SOURCE_CAPABILITIES. A partner that advertises such
 * an APDO lands in this register the same way.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unistd.h>

#include "TPS25751ReceivedSinkCaps.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t CAPS_SIZE = TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES.size;  // 53

// Two PDOs: Fixed 5 V / 3 A (0x0001912C), then 0xE004B1F4, an Augmented PDO
// with APDO subtype 10b.
constexpr uint8_t FIXED_AND_APDO_SUBTYPE_2[CAPS_SIZE] = {
    0x02,
    0x2C, 0x91, 0x01, 0x00,
    0xF4, 0xB1, 0x04, 0xE0,
};

// Two PDOs: Fixed 5 V / 3 A, then an SPR PPS APDO 3.3-21 V / 3 A (0xC1A4213C).
constexpr uint8_t FIXED_AND_SPR_PPS[CAPS_SIZE] = {
    0x02,
    0x2C, 0x91, 0x01, 0x00,
    0x3C, 0x21, 0xA4, 0xC1,
};

// Same as FIXED_AND_SPR_PPS with min (bits 15:8) and max (bits 24:17) swapped
// so that min > max: min 21 V, max 3.3 V.
constexpr uint8_t FIXED_AND_BAD_SPR_PPS[CAPS_SIZE] = {
    0x02,
    0x2C, 0x91, 0x01, 0x00,
    0x3C, 0xD2, 0x42, 0xC0,
};

// Packs @p pdos little-endian from byte 1 and sets the PDO count in byte 0.
struct Caps {
    uint8_t buf[CAPS_SIZE];
    Caps(uint8_t count, std::initializer_list<uint32_t> pdos) {
        memset(buf, 0, sizeof(buf));
        buf[0] = count;
        size_t o = 1;
        for (uint32_t w : pdos) {
            buf[o++] = w & 0xFF;
            buf[o++] = (w >> 8) & 0xFF;
            buf[o++] = (w >> 16) & 0xFF;
            buf[o++] = (w >> 24) & 0xFF;
        }
    }
};

// Fixed 5 V / 3 A from the field layout: (100 << 10) | 300 = 0x0001912C.
constexpr uint32_t FIXED_5V_3A = (100u << 10) | 300u;

// Runs reg.debugPrint() against the stdout-backed native Stream stub and
// returns what it printed.
std::string captureDebugPrint(const TPS25751ReceivedSinkCaps &reg)
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

bool contains(const std::string &hay, const char *needle)
{
    return hay.find(needle) != std::string::npos;
}

bool semOk(const Caps &c)
{
    return TPS25751ReceivedSinkCaps(c.buf, CAPS_SIZE).isSemanticallyValid();
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751ReceivedSinkCaps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdoCount());
}

void test_non_pps_apdo_reports_subtype_and_no_pps_fields()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_APDO_SUBTYPE_2, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.getPDOType(1));
    TEST_ASSERT_EQUAL_UINT8(2, reg.getAPDOSubtype(1));
    TEST_ASSERT_FALSE(reg.isSprPpsPDO(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.getPPSMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.getPPSMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(0, reg.getPPSMaxCurrent_mA(1));
}

void test_non_pps_apdo_is_semantically_valid()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_APDO_SUBTYPE_2, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_apdo_is_decoded()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_SPR_PPS, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSprPpsPDO(1));
    TEST_ASSERT_EQUAL_UINT8(0, reg.getAPDOSubtype(1));
    TEST_ASSERT_EQUAL_UINT16(21000, reg.getPPSMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(3300, reg.getPPSMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getPPSMaxCurrent_mA(1));
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_min_above_max_is_invalid()
{
    TPS25751ReceivedSinkCaps reg(FIXED_AND_BAD_SPR_PPS, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSprPpsPDO(1));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Constructors -------------------------------------------------------------

void test_data_and_reginfo_constructors()
{
    Caps c(1, {FIXED_5V_3A});
    TPS25751ReceivedSinkCaps fromData(c.buf);
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, fromData.size());
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, fromData.getPDO(0));

    TPS25751ReceivedSinkCaps fromInfo(TPS25751Registers::Registers::RECEIVED_SINK_CAPABILITIES);
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, fromInfo.size());
    TEST_ASSERT_FALSE(fromInfo.hasSinkCaps());
}

// --- PDO access ---------------------------------------------------------------

void test_get_pdo_little_endian_and_slot_offsets()
{
    Caps c(7, {0x11223344u, 0x55667788u, 0, 0, 0, 0, 0xAABBCCDDu});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(7, reg.pdoCount());
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, reg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0x55667788u, reg.getPDO(1));
    TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, reg.getPDO(6));
}

void test_get_pdo_out_of_range_index_returns_zero()
{
    Caps c(7, {FIXED_5V_3A});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(7));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(255));
}

void test_get_pdo_past_end_of_short_buffer_returns_zero()
{
    // 8 bytes hold PDO 1 (bytes 1..4) but not PDO 2 (bytes 5..8).
    const uint8_t shortBuf[8] = {0x02, 0x2C, 0x91, 0x01, 0x00, 0x01, 0x02, 0x03};
    TPS25751ReceivedSinkCaps reg(shortBuf, sizeof(shortBuf));

    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, reg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(1));
}

void test_pdo_type_decodes_bits_31_30()
{
    Caps c(4, {FIXED_5V_3A, 0x40000000u, 0x80000000u, 0xC0000000u});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.getPDOType(0));
    TEST_ASSERT_EQUAL(PDOType::Battery, reg.getPDOType(1));
    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.getPDOType(2));
    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.getPDOType(3));
}

void test_stale_count_with_zero_pdo1_means_no_caps()
{
    // Controller leaves the old count after detach but zeroes the PDOs.
    Caps c(6, {});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(6, reg.pdoCount());
    TEST_ASSERT_FALSE(reg.hasSinkCaps());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Fixed Supply -------------------------------------------------------------

void test_fixed_supply_fields_decode()
{
    // 9 V / 2.5 A, DRP, Higher Capability, Unconstrained, USB Comm, DRD,
    // Fast Role Swap current field = 2 (bits 24:23 = 10b).
    const uint32_t pdo = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                         (1u << 25) | (2u << 23) | (180u << 10) | 250u;
    Caps c(2, {FIXED_5V_3A, pdo});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT16(9000, reg.getFixedVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(2500, reg.getFixedMaxCurrent_mA(1));
    TEST_ASSERT_TRUE(reg.getFixedDualRolePower(1));
    TEST_ASSERT_TRUE(reg.getFixedHigherCapability(1));
    TEST_ASSERT_TRUE(reg.getFixedUnconstrainedPower(1));
    TEST_ASSERT_TRUE(reg.getFixedUSBCommCapable(1));
    TEST_ASSERT_TRUE(reg.getFixedDualRoleData(1));
    TEST_ASSERT_TRUE(reg.getFixedFastRoleSwapCurrent(1));

    // PDO 1 has none of the flags set.
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getFixedVoltage_mV(0));
    TEST_ASSERT_EQUAL_UINT16(3000, reg.getFixedMaxCurrent_mA(0));
    TEST_ASSERT_FALSE(reg.getFixedDualRolePower(0));
    TEST_ASSERT_FALSE(reg.getFixedHigherCapability(0));
    TEST_ASSERT_FALSE(reg.getFixedUnconstrainedPower(0));
    TEST_ASSERT_FALSE(reg.getFixedUSBCommCapable(0));
    TEST_ASSERT_FALSE(reg.getFixedDualRoleData(0));
    TEST_ASSERT_FALSE(reg.getFixedFastRoleSwapCurrent(0));
}

// --- Battery / Variable -------------------------------------------------------

void test_battery_fields_decode()
{
    // 20 V max, 5 V min, 60 W.
    const uint32_t pdo = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    Caps c(2, {FIXED_5V_3A, pdo});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT16(20000, reg.getBatteryMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getBatteryMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT32(60000, reg.getBatteryMaxPower_mW(1));
}

void test_variable_fields_decode()
{
    // 12 V max, 5 V min, 1.5 A.
    const uint32_t pdo = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    Caps c(2, {FIXED_5V_3A, pdo});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT16(12000, reg.getVariableMaxVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(5000, reg.getVariableMinVoltage_mV(1));
    TEST_ASSERT_EQUAL_UINT16(1500, reg.getVariableMaxCurrent_mA(1));
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_no_caps()
{
    TPS25751ReceivedSinkCaps reg;
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Received Sink Capabilities Register (0x31)"));
    TEST_ASSERT_TRUE(contains(out, "PDO Count: 0"));
    TEST_ASSERT_TRUE(contains(out, "No sink capabilities received"));
}

void test_debug_print_all_pdo_types()
{
    const uint32_t fixed = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                           (1u << 25) | (2u << 23) | (100u << 10) | 300u;
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    Caps c(5, {fixed, battery, variable, 0xC1A4213Cu, 0xE004B1F4u});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "PDO Count: 5"));
    TEST_ASSERT_TRUE(contains(out, "--- PDO 1 ---"));
    TEST_ASSERT_TRUE(contains(out, "Type: Fixed Supply"));
    TEST_ASSERT_TRUE(contains(out, "Voltage: 5000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
    TEST_ASSERT_TRUE(contains(out, "Dual Role Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Higher Capability: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "USB Comm Capable: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Dual Role Data: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Fast Role Swap Current:"));
    TEST_ASSERT_TRUE(contains(out, "Type: Battery"));
    TEST_ASSERT_TRUE(contains(out, "Max Power: 60000 mW"));
    TEST_ASSERT_TRUE(contains(out, "Type: Variable Supply"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 1500 mA"));
    TEST_ASSERT_TRUE(contains(out, "Type: Augmented (PPS)"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 21000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Min Voltage: 3300 mV"));
    TEST_ASSERT_TRUE(contains(out, "Augmented, APDO subtype 2 (not SPR PPS; not decoded)"));
}

void test_debug_print_fixed_flags_clear_print_no()
{
    Caps c(1, {FIXED_5V_3A});
    TPS25751ReceivedSinkCaps reg(c.buf, CAPS_SIZE);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Dual Role Power: No"));
    TEST_ASSERT_TRUE(contains(out, "Higher Capability: No"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: No"));
    TEST_ASSERT_TRUE(contains(out, "USB Comm Capable: No"));
    TEST_ASSERT_TRUE(contains(out, "Dual Role Data: No"));
}

// --- Semantic validation ------------------------------------------------------

void test_semantic_valid_mixed_caps()
{
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    Caps c(4, {FIXED_5V_3A, battery, variable, 0xC1A4213Cu});

    TEST_ASSERT_TRUE(semOk(c));
}

void test_semantic_rejects_all_ones_and_wrong_size()
{
    uint8_t ones[CAPS_SIZE];
    memset(ones, 0xFF, sizeof(ones));
    TEST_ASSERT_FALSE(TPS25751ReceivedSinkCaps(ones, CAPS_SIZE).isSemanticallyValid());

    const uint8_t small[4] = {0, 0, 0, 0};
    TEST_ASSERT_FALSE(TPS25751ReceivedSinkCaps(small, sizeof(small)).isSemanticallyValid());
}

void test_semantic_fixed_rules()
{
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 300u})));          // zero voltage
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 180u << 10})));    // zero current

    // PDO 1 must be near 5 V: 4.75 V and 5.25 V accepted, 4.7 V, 5.3 V and 9 V rejected.
    TEST_ASSERT_TRUE(semOk(Caps(1, {(95u << 10) | 300u})));
    TEST_ASSERT_TRUE(semOk(Caps(1, {(105u << 10) | 300u})));
    TEST_ASSERT_FALSE(semOk(Caps(1, {(94u << 10) | 300u})));
    TEST_ASSERT_FALSE(semOk(Caps(1, {(106u << 10) | 300u})));
    TEST_ASSERT_FALSE(semOk(Caps(1, {(180u << 10) | 300u})));
}

void test_semantic_battery_rules()
{
    const uint32_t b = (1u << 30);
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (100u << 20) | (400u << 10) | 240u})));  // min > max
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (400u << 20) | 240u})));                  // zero min
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (100u << 10) | 240u})));                  // zero max
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (400u << 20) | (100u << 10)})));          // zero power
}

void test_semantic_variable_rules()
{
    const uint32_t v = (2u << 30);
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (100u << 20) | (240u << 10) | 150u})));  // min > max
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (240u << 20) | 150u})));                  // zero min
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (100u << 10) | 150u})));                  // zero max
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (240u << 20) | (100u << 10)})));          // zero current
}

void test_semantic_pps_zero_current_and_zero_min()
{
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 0xC1A42100u})));  // 3.3-21 V, 0 A
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 0xC1A4003Cu})));  // zero min voltage
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_non_pps_apdo_reports_subtype_and_no_pps_fields);
    RUN_TEST(test_non_pps_apdo_is_semantically_valid);
    RUN_TEST(test_spr_pps_apdo_is_decoded);
    RUN_TEST(test_spr_pps_min_above_max_is_invalid);
    RUN_TEST(test_data_and_reginfo_constructors);
    RUN_TEST(test_get_pdo_little_endian_and_slot_offsets);
    RUN_TEST(test_get_pdo_out_of_range_index_returns_zero);
    RUN_TEST(test_get_pdo_past_end_of_short_buffer_returns_zero);
    RUN_TEST(test_pdo_type_decodes_bits_31_30);
    RUN_TEST(test_stale_count_with_zero_pdo1_means_no_caps);
    RUN_TEST(test_fixed_supply_fields_decode);
    RUN_TEST(test_battery_fields_decode);
    RUN_TEST(test_variable_fields_decode);
    RUN_TEST(test_debug_print_no_caps);
    RUN_TEST(test_debug_print_all_pdo_types);
    RUN_TEST(test_debug_print_fixed_flags_clear_print_no);
    RUN_TEST(test_semantic_valid_mixed_caps);
    RUN_TEST(test_semantic_rejects_all_ones_and_wrong_size);
    RUN_TEST(test_semantic_fixed_rules);
    RUN_TEST(test_semantic_battery_rules);
    RUN_TEST(test_semantic_variable_rules);
    RUN_TEST(test_semantic_pps_zero_current_and_zero_min);
    return UNITY_END();
}
