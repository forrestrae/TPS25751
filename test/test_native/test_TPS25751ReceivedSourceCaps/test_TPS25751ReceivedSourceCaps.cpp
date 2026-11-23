/**
 * @file test_TPS25751ReceivedSourceCaps.cpp
 * @brief Native (host-run) unit tests for TPS25751ReceivedSourceCaps.
 *
 * Run with: pio test -e native
 *
 * RECEIVED_SOURCE_CAPABILITIES is 53 bytes: byte 0 bits 2:0 hold the
 * number of valid PDOs, and PDO n sits at byte 1 + (n-1)*4, little-endian.
 *
 * PDOs decode through pdo(i), a TPS25751Pdo in its position and role, and the
 * register validates with validatePdoList() (USB PD R3.3). The suite also covers
 * the APDO subtype handling added after a bench read on 2026-11-18 found an
 * Augmented PDO with bits 29:28 = 10b (not the SPR PPS subtype 00b) in
 * TRANSMIT_SOURCE_CAPABILITIES; a partner that advertises such an APDO lands in
 * this register the same way.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>

#include "TPS25751ReceivedSourceCaps.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t CAPS_SIZE = TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES.size;  // 53

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

bool contains(const std::string &hay, const char *needle)
{
    return hay.find(needle) != std::string::npos;
}

bool semOk(const Caps &c)
{
    return TPS25751ReceivedSourceCaps(c.buf, CAPS_SIZE).isSemanticallyValid();
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751ReceivedSourceCaps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdoCount());
}

void test_non_pps_apdo_reports_subtype_and_no_pps_fields()
{
    TPS25751ReceivedSourceCaps reg(FIXED_AND_APDO_SUBTYPE_2, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(1).type());
    TEST_ASSERT_EQUAL_UINT8(2, reg.pdo(1).apdoType());
    TEST_ASSERT_EQUAL(PdoKind::SprAvs, reg.pdo(1).kind());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(1).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(1).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(0, reg.pdo(1).ppsCurrent_mA());
}

void test_non_pps_apdo_is_semantically_valid()
{
    TPS25751ReceivedSourceCaps reg(FIXED_AND_APDO_SUBTYPE_2, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_apdo_is_decoded()
{
    TPS25751ReceivedSourceCaps reg(FIXED_AND_SPR_PPS, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PdoKind::SprPps, reg.pdo(1).kind());
    TEST_ASSERT_EQUAL_UINT8(0, reg.pdo(1).apdoType());
    TEST_ASSERT_EQUAL_UINT16(21000, reg.pdo(1).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, reg.pdo(1).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(1).ppsCurrent_mA());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_spr_pps_voltages_outside_table_6_13_are_invalid()
{
    // Max 3.3 V is not 11, 16, or 21 V (Table 6.13), so the receiver rules reject it.
    TPS25751ReceivedSourceCaps reg(FIXED_AND_BAD_SPR_PPS, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PdoKind::SprPps, reg.pdo(1).kind());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Constructors -------------------------------------------------------------

void test_data_and_reginfo_constructors()
{
    Caps c(1, {FIXED_5V_3A});
    TPS25751ReceivedSourceCaps fromData(c.buf);
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, fromData.size());
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, fromData.getPDO(0));

    TPS25751ReceivedSourceCaps fromInfo(TPS25751Registers::Registers::RECEIVED_SOURCE_CAPABILITIES);
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, fromInfo.size());
    TEST_ASSERT_FALSE(fromInfo.hasSourceCaps());
}

// --- PDO access ---------------------------------------------------------------

void test_get_pdo_little_endian_and_slot_offsets()
{
    Caps c(7, {0x11223344u, 0x55667788u, 0, 0, 0, 0, 0xAABBCCDDu});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(7, reg.pdoCount());
    TEST_ASSERT_EQUAL_UINT32(0x11223344u, reg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0x55667788u, reg.getPDO(1));
    TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, reg.getPDO(6));
}

void test_get_pdo_out_of_range_index_returns_zero()
{
    Caps c(7, {FIXED_5V_3A});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(7));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(255));
}

void test_get_pdo_past_end_of_short_buffer_returns_zero()
{
    // 8 bytes hold PDO 1 (bytes 1..4) but not PDO 2 (bytes 5..8).
    const uint8_t shortBuf[8] = {0x02, 0x2C, 0x91, 0x01, 0x00, 0x01, 0x02, 0x03};
    TPS25751ReceivedSourceCaps reg(shortBuf, sizeof(shortBuf));

    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, reg.getPDO(0));
    TEST_ASSERT_EQUAL_UINT32(0, reg.getPDO(1));
}

void test_pdo_type_decodes_bits_31_30()
{
    Caps c(4, {FIXED_5V_3A, 0x40000000u, 0x80000000u, 0xC0000000u});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL(PDOType::FixedSupply, reg.pdo(0).type());
    TEST_ASSERT_EQUAL(PDOType::Battery, reg.pdo(1).type());
    TEST_ASSERT_EQUAL(PDOType::VariableSupply, reg.pdo(2).type());
    TEST_ASSERT_EQUAL(PDOType::Augmented, reg.pdo(3).type());
}

void test_pdo_carries_position_and_source_role()
{
    Caps c(2, {FIXED_5V_3A, (180u << 10) | 250u});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(1, reg.pdo(0).position());
    TEST_ASSERT_EQUAL_UINT8(2, reg.pdo(1).position());
    TEST_ASSERT_TRUE(reg.pdo(0).role() == PdoRole::Source);
    TEST_ASSERT_EQUAL_UINT32(FIXED_5V_3A, reg.pdo(0).raw());
}

void test_pdo_index_out_of_range_is_zero_at_unknown_position()
{
    Caps c(7, {FIXED_5V_3A});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    const TPS25751Pdo p = reg.pdo(7);
    TEST_ASSERT_EQUAL_UINT32(0, p.raw());
    TEST_ASSERT_EQUAL_UINT8(TPS25751Pdo::kUnknownPosition, p.position());
    TEST_ASSERT_EQUAL_UINT8(TPS25751Pdo::kUnknownPosition, reg.pdo(255).position());
}

void test_stale_count_with_zero_pdo1_means_no_caps()
{
    // Controller leaves the old count after detach but zeroes the PDOs.
    Caps c(6, {});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(6, reg.pdoCount());
    TEST_ASSERT_FALSE(reg.hasSourceCaps());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Fixed Supply -------------------------------------------------------------

void test_fixed_supply_fields_decode()
{
    // 9 V / 2.5 A in PDO 2 with every flag bit (29:24) set; PDO 1 is 5 V / 3 A with
    // the flags Dual-Role Power, USB Suspend, Unconstrained, USB Comm, DRD, Unchunked.
    const uint32_t flagBits = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                              (1u << 25) | (1u << 24);
    const uint32_t pdo2 = flagBits | (180u << 10) | 250u;
    Caps c(2, {FIXED_5V_3A | flagBits, pdo2});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT16(9000, reg.pdo(1).fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2500, reg.pdo(1).fixedCurrent_mA());
    // Flags exist only in PDO 1 (Table 6.10): PDO 2 reads false although the bits are set.
    TEST_ASSERT_FALSE(reg.pdo(1).dualRolePower());
    TEST_ASSERT_FALSE(reg.pdo(1).usbSuspendSupported());
    TEST_ASSERT_FALSE(reg.pdo(1).unconstrainedPower());
    TEST_ASSERT_FALSE(reg.pdo(1).usbCommunicationsCapable());
    TEST_ASSERT_FALSE(reg.pdo(1).dualRoleData());
    TEST_ASSERT_FALSE(reg.pdo(1).unchunkedExtendedMessages());
    TEST_ASSERT_EQUAL_UINT32(pdo2, reg.getPDO(1));

    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(0).fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(0).fixedCurrent_mA());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbSuspendSupported());
    TEST_ASSERT_TRUE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_TRUE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_TRUE(reg.pdo(0).dualRoleData());
    TEST_ASSERT_TRUE(reg.pdo(0).unchunkedExtendedMessages());
}

// The 140 W charger: PDO 1 0x0981912C carries Unconstrained, Unchunked, and EPR
// (bits 27, 24, 23); PDOs 2 to 4 are 15 V, 20 V, and 28 V / 5 A (EPR range).
void test_charger_140w_fixture()
{
    Caps c(4, {0x0981912Cu, 0x0002D12Cu, 0x0004B12Cu, 0x000641D6u});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(4, reg.pdoCount());
    TEST_ASSERT_TRUE(reg.pdo(0).unconstrainedPower());
    TEST_ASSERT_TRUE(reg.pdo(0).unchunkedExtendedMessages());
    TEST_ASSERT_TRUE(reg.pdo(0).eprCapable());
    TEST_ASSERT_FALSE(reg.pdo(0).dualRolePower());
    TEST_ASSERT_FALSE(reg.pdo(0).usbSuspendSupported());
    TEST_ASSERT_FALSE(reg.pdo(0).usbCommunicationsCapable());
    TEST_ASSERT_FALSE(reg.pdo(0).dualRoleData());
    for (uint8_t i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL(PdoPeakCurrent::IoC, reg.pdo(i).peakCurrent());
        TEST_ASSERT_EQUAL_UINT8(0, reg.pdo(i).rawPeakCurrent());
    }
    TEST_ASSERT_FALSE(reg.pdo(1).unconstrainedPower());
    TEST_ASSERT_TRUE(semOk(c));
}

void test_peak_current_decodes_in_any_fixed_pdo()
{
    // 0x0024B12C is 20 V / 3 A with Peak Current 10b (bits 21:20).
    Caps c(3, {FIXED_5V_3A, 0x0002D12Cu, 0x0024B12Cu});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT8(2, reg.pdo(2).rawPeakCurrent());
    TEST_ASSERT_EQUAL(PdoPeakCurrent::Overload2, reg.pdo(2).peakCurrent());
}

void test_flag_bits_in_pdo3_read_false_through_pdo_but_stay_in_raw_word()
{
    // 20 V / 3 A with bits 29:27 set: reserved Device Flags above 5 V (Table 6.10).
    const uint32_t word = (7u << 27) | 0x0004B12Cu;
    Caps c(3, {FIXED_5V_3A, 0x0002D12Cu, word});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_FALSE(reg.pdo(2).dualRolePower());
    TEST_ASSERT_FALSE(reg.pdo(2).usbSuspendSupported());
    TEST_ASSERT_FALSE(reg.pdo(2).unconstrainedPower());
    TEST_ASSERT_EQUAL_UINT32(word, reg.getPDO(2));
}

// --- Battery / Variable -------------------------------------------------------

void test_battery_fields_decode()
{
    // 20 V max, 5 V min, 60 W.
    const uint32_t pdo = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    Caps c(2, {FIXED_5V_3A, pdo});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT16(20000, reg.pdo(1).batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(1).batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(60000, reg.pdo(1).batteryPower_mW());
}

void test_variable_fields_decode()
{
    // 12 V max, 5 V min, 1.5 A.
    const uint32_t pdo = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    Caps c(2, {FIXED_5V_3A, pdo});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);

    TEST_ASSERT_EQUAL_UINT16(12000, reg.pdo(1).variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, reg.pdo(1).variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(1500, reg.pdo(1).variableCurrent_mA());
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_no_caps()
{
    TPS25751ReceivedSourceCaps reg;
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Received Source Capabilities Register (0x30)"));
    TEST_ASSERT_TRUE(contains(out, "PDO Count: 0"));
    TEST_ASSERT_TRUE(contains(out, "No source capabilities received"));
}

void test_debug_print_all_pdo_types()
{
    const uint32_t fixed = (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) |
                           (1u << 25) | (1u << 24) | (100u << 10) | 300u;
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    const uint32_t variable = (2u << 30) | (240u << 20) | (100u << 10) | 150u;
    Caps c(5, {fixed, battery, variable, 0xC9A4213Cu, 0xE004B1F4u});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "PDO Count: 5"));
    TEST_ASSERT_TRUE(contains(out, "--- PDO 1 ---"));
    TEST_ASSERT_TRUE(contains(out, "Type: Fixed Supply"));
    TEST_ASSERT_TRUE(contains(out, "Voltage: 5000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 3000 mA"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "USB Suspend Supported: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: Yes"));
    TEST_ASSERT_TRUE(contains(out, "USB Communications Capable: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Data: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Unchunked Extended Messages: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Type: Battery"));
    TEST_ASSERT_TRUE(contains(out, "Max Power: 60000 mW"));
    TEST_ASSERT_TRUE(contains(out, "Type: Variable Supply"));
    TEST_ASSERT_TRUE(contains(out, "Max Current: 1500 mA"));
    TEST_ASSERT_TRUE(contains(out, "Type: SPR PPS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Power Limited: Yes"));
    TEST_ASSERT_TRUE(contains(out, "Max Voltage: 21000 mV"));
    TEST_ASSERT_TRUE(contains(out, "Min Voltage: 3300 mV"));
    TEST_ASSERT_TRUE(contains(out, "Type: SPR AVS APDO"));
    TEST_ASSERT_TRUE(contains(out, "Max Current 15 to 20 V: 5000 mA"));
}

void test_debug_print_fixed_flags_clear_print_no()
{
    Caps c(1, {FIXED_5V_3A});
    TPS25751ReceivedSourceCaps reg(c.buf, CAPS_SIZE);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "Dual-Role Power: No"));
    TEST_ASSERT_TRUE(contains(out, "USB Suspend Supported: No"));
    TEST_ASSERT_TRUE(contains(out, "Unconstrained Power: No"));
    TEST_ASSERT_TRUE(contains(out, "USB Communications Capable: No"));
    TEST_ASSERT_TRUE(contains(out, "Dual-Role Data: No"));
    TEST_ASSERT_TRUE(contains(out, "Unchunked Extended Messages: No"));
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
    TEST_ASSERT_FALSE(TPS25751ReceivedSourceCaps(ones, CAPS_SIZE).isSemanticallyValid());

    const uint8_t small[4] = {0, 0, 0, 0};
    TEST_ASSERT_FALSE(TPS25751ReceivedSourceCaps(small, sizeof(small)).isSemanticallyValid());
}

void test_semantic_fixed_rules()
{
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 300u})));          // zero voltage

    // Zero current is valid: Tables 6.8 to 6.10 allow 0 (old rule rejected it).
    TEST_ASSERT_TRUE(semOk(Caps(2, {FIXED_5V_3A, 180u << 10})));
    // Current above 500 x 10 mA is invalid.
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, (180u << 10) | 501u})));

    // PDO 1 must be exactly 5 V (100 x 50 mV): the old 4.75 to 5.25 V band is gone.
    TEST_ASSERT_TRUE(semOk(Caps(1, {(100u << 10) | 300u})));
    TEST_ASSERT_FALSE(semOk(Caps(1, {(95u << 10) | 300u})));
    TEST_ASSERT_FALSE(semOk(Caps(1, {(105u << 10) | 300u})));
    TEST_ASSERT_FALSE(semOk(Caps(1, {(180u << 10) | 300u})));
}

void test_semantic_pdo1_must_be_fixed()
{
    // A Battery PDO first is invalid even though its fields are individually valid.
    const uint32_t battery = (1u << 30) | (400u << 20) | (100u << 10) | 240u;
    TEST_ASSERT_FALSE(semOk(Caps(1, {battery})));
}

void test_semantic_battery_rules()
{
    const uint32_t b = (1u << 30);
    TEST_ASSERT_TRUE(semOk(Caps(2, {FIXED_5V_3A, b | (100u << 20) | (400u << 10) | 240u})));   // min > max: valid for a receiver
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (400u << 20) | 240u})));                  // zero min
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (100u << 10) | 240u})));                  // zero max
    TEST_ASSERT_TRUE(semOk(Caps(2, {FIXED_5V_3A, b | (400u << 20) | (100u << 10)})));           // zero power: valid
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, b | (400u << 20) | (100u << 10) | 401u})));   // power above 100 W
}

void test_semantic_variable_rules()
{
    const uint32_t v = (2u << 30);
    TEST_ASSERT_TRUE(semOk(Caps(2, {FIXED_5V_3A, v | (100u << 20) | (240u << 10) | 150u})));   // min > max: valid for a receiver
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (240u << 20) | 150u})));                  // zero min
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (100u << 10) | 150u})));                  // zero max
    TEST_ASSERT_TRUE(semOk(Caps(2, {FIXED_5V_3A, v | (240u << 20) | (100u << 10)})));           // zero current: valid
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, v | (240u << 20) | (100u << 10) | 501u})));   // current above 5 A
}

void test_semantic_pps_zero_current_and_zero_min()
{
    TEST_ASSERT_TRUE(semOk(Caps(2, {FIXED_5V_3A, 0xC1A42100u})));   // 3.3-21 V, 0 A: valid
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 0xC1A4003Cu})));  // minimum voltage 0
}

void test_semantic_invalid_apdo_type_rejected()
{
    TEST_ASSERT_FALSE(semOk(Caps(2, {FIXED_5V_3A, 0xF0000000u})));  // APDO type 11b
}

void test_pps_power_limited_flag()
{
    // 0xC9A4213C = SPR PPS 3.3-21 V / 3 A with Power Limited (bit 27) set.
    Caps limited(2, {FIXED_5V_3A, 0xC9A4213Cu});
    TPS25751ReceivedSourceCaps reg(limited.buf, CAPS_SIZE);
    TEST_ASSERT_TRUE(reg.pdo(1).ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, reg.pdo(1).ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3300, reg.pdo(1).ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, reg.pdo(1).ppsCurrent_mA());

    Caps unlimited(2, {FIXED_5V_3A, 0xC1A4213Cu});
    TEST_ASSERT_FALSE(TPS25751ReceivedSourceCaps(unlimited.buf, CAPS_SIZE).pdo(1).ppsPowerLimited());
    // Not an SPR PPS APDO: no Power Limited decode even with bit 27 set.
    Caps other(2, {FIXED_5V_3A, 0xE804B1F4u});
    TEST_ASSERT_FALSE(TPS25751ReceivedSourceCaps(other.buf, CAPS_SIZE).pdo(1).ppsPowerLimited());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_non_pps_apdo_reports_subtype_and_no_pps_fields);
    RUN_TEST(test_non_pps_apdo_is_semantically_valid);
    RUN_TEST(test_spr_pps_apdo_is_decoded);
    RUN_TEST(test_spr_pps_voltages_outside_table_6_13_are_invalid);
    RUN_TEST(test_data_and_reginfo_constructors);
    RUN_TEST(test_get_pdo_little_endian_and_slot_offsets);
    RUN_TEST(test_get_pdo_out_of_range_index_returns_zero);
    RUN_TEST(test_get_pdo_past_end_of_short_buffer_returns_zero);
    RUN_TEST(test_pdo_type_decodes_bits_31_30);
    RUN_TEST(test_pdo_carries_position_and_source_role);
    RUN_TEST(test_pdo_index_out_of_range_is_zero_at_unknown_position);
    RUN_TEST(test_stale_count_with_zero_pdo1_means_no_caps);
    RUN_TEST(test_charger_140w_fixture);
    RUN_TEST(test_peak_current_decodes_in_any_fixed_pdo);
    RUN_TEST(test_flag_bits_in_pdo3_read_false_through_pdo_but_stay_in_raw_word);
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
    RUN_TEST(test_semantic_pdo1_must_be_fixed);
    RUN_TEST(test_semantic_pps_zero_current_and_zero_min);
    RUN_TEST(test_semantic_invalid_apdo_type_rejected);
    RUN_TEST(test_pps_power_limited_flag);
    return UNITY_END();
}
