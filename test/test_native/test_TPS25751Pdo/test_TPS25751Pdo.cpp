/**
 * @file test_TPS25751Pdo.cpp
 * @brief Native (host-run) unit tests for TPS25751Pdo and validatePdoList().
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751Pdo"
 *
 * Fixtures come from USB PD R3.3 V1.0 Tables 6.8 to 6.15 (usb-pd-docs) and from the
 * bench. Bench words, read on 2026-11-22 with an Apple 140 W charger attached and
 * confirmed on the wire with a ChargerLAB KM003C:
 *   0x30 RECEIVED_SOURCE_CAPABILITIES
 *     PDO 1 0x0981912C: Fixed, 5 V (100), 3 A (300); bit 27 Unconstrained Power,
 *       bit 24 Unchunked Extended Messages, bit 23 EPR Capable; Peak Current 00b
 *     PDO 2 0x0002D12C: Fixed, 9 V (180), 3 A
 *     PDO 3 0x0004B12C: Fixed, 15 V (300), 3 A
 *     PDO 4 0x000641D6: Fixed, 20 V (400), 4.7 A (470)
 *   0x32 TRANSMIT_SOURCE_CAPABILITIES (the board's own EEPROM configuration)
 *     0x2001912C (5 V 3 A, Dual-Role Power), 0x0002D12C (9 V 3 A),
 *     0x0004B12C (15 V 3 A), 0x000641F4 (20 V 5 A), and
 *     0xE004B1F4: SPR AVS Source APDO, bits 19:10 = 300 (3.0 A from 9 to 15 V),
 *       bits 9:0 = 500 (5.0 A from 15 to 20 V), Peak Current 00b
 *   0x33 TRANSMIT_SINK_CAPABILITIES
 *     0x1001912C (5 V 3 A, bit 28 Higher Capability), 0x0002D12C, 0x0004B12C,
 *     0x0006412C (20 V 3 A)
 */

#include <unity.h>

#include <string>

#include "CaptureStdout.h"
#include "TPS25751Pdo.h"
#include "fixtures/PdCapture_2026_11_23_apple_140w_tps25751.h"

namespace {

TPS25751Pdo src(uint32_t word, uint8_t position) { return TPS25751Pdo(word, position, PdoRole::Source); }
TPS25751Pdo snk(uint32_t word, uint8_t position) { return TPS25751Pdo(word, position, PdoRole::Sink); }

const uint32_t kRx140W[] = {0x0981912C, 0x0002D12C, 0x0004B12C, 0x000641D6};
const uint32_t kTxSource[] = {0x2001912C, 0x0002D12C, 0x0004B12C, 0x000641F4, 0xE004B1F4};
const uint32_t kTxSink[] = {0x1001912C, 0x0002D12C, 0x0004B12C, 0x0006412C};

// Builds a PDO list from words, positions 1..count, in one role.
template <size_t N>
void build(TPS25751Pdo (&out)[N], const uint32_t *words, uint8_t count, PdoRole role)
{
    for (uint8_t i = 0; i < count && i < N; i++) out[i] = TPS25751Pdo(words[i], i + 1, role);
}

bool validate(const uint32_t *words, uint8_t count, PdoRole role, PdoListRules rules)
{
    TPS25751Pdo list[7];
    build(list, words, count, role);
    return validatePdoList(list, count, rules);
}

// The real capture of the same charger (test/fixtures/pd-captures/), see the wire tests.
namespace cap = PdCapture_2026_11_23_apple_140w_tps25751;

}  // namespace

void setUp() {}
void tearDown() {}

// --- Kind ---------------------------------------------------------------------------

void test_kind_from_pdo_and_apdo_type()
{
    TEST_ASSERT_TRUE(PdoKind::Fixed == src(0x0981912C, 1).kind());
    TEST_ASSERT_TRUE(PdoKind::Battery == src(0x40000000, 2).kind());
    TEST_ASSERT_TRUE(PdoKind::Variable == src(0x80000000, 2).kind());
    TEST_ASSERT_TRUE(PdoKind::SprPps == src(0xC0000000, 2).kind());
    TEST_ASSERT_TRUE(PdoKind::EprAvs == src(0xD0000000, 2).kind());
    TEST_ASSERT_TRUE(PdoKind::SprAvs == src(0xE004B1F4, 5).kind());
    TEST_ASSERT_TRUE(PdoKind::InvalidApdo == src(0xF0000000, 2).kind());
    TEST_ASSERT_EQUAL_UINT8(0, src(0x30000000, 2).apdoType());  // Fixed: no APDO type
    TEST_ASSERT_EQUAL_UINT8(2, src(0xE004B1F4, 5).apdoType());
}

// --- Fixed PDO 1 flags (decision 1) --------------------------------------------------

void test_charger_pdo1_flags_decode_at_position_1()
{
    const TPS25751Pdo p = src(kRx140W[0], 1);
    TEST_ASSERT_EQUAL_UINT16(5000, p.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, p.fixedCurrent_mA());
    TEST_ASSERT_TRUE(p.unconstrainedPower());
    TEST_ASSERT_TRUE(p.unchunkedExtendedMessages());
    TEST_ASSERT_TRUE(p.eprCapable());
    TEST_ASSERT_FALSE(p.dualRolePower());
    TEST_ASSERT_FALSE(p.usbSuspendSupported());
    TEST_ASSERT_FALSE(p.usbCommunicationsCapable());
    TEST_ASSERT_FALSE(p.dualRoleData());
    TEST_ASSERT_TRUE(PdoPeakCurrent::IoC == p.peakCurrent());
}

void test_flag_bits_in_a_later_fixed_pdo_read_false()
{
    // 15 V 3 A with bits 29:23 all set: Table 6.10 makes them reserved Device Flags.
    const uint32_t word = 0x0004B12Cu | (0x7Fu << 23);
    const TPS25751Pdo p = src(word, 3);
    TEST_ASSERT_EQUAL_HEX32(word, p.raw());
    TEST_ASSERT_EQUAL_UINT16(15000, p.fixedVoltage_mV());
    TEST_ASSERT_FALSE(p.dualRolePower());
    TEST_ASSERT_FALSE(p.usbSuspendSupported());
    TEST_ASSERT_FALSE(p.unconstrainedPower());
    TEST_ASSERT_FALSE(p.usbCommunicationsCapable());
    TEST_ASSERT_FALSE(p.dualRoleData());
    TEST_ASSERT_FALSE(p.unchunkedExtendedMessages());
    TEST_ASSERT_FALSE(p.eprCapable());
}

void test_unknown_position_reads_no_flags()
{
    TEST_ASSERT_FALSE(src(kRx140W[0], TPS25751Pdo::kUnknownPosition).unconstrainedPower());
}

void test_peak_current_is_per_pdo_for_a_source()
{
    // PDO 3 with Peak Current 10b in bits 21:20.
    const TPS25751Pdo p = src(0x0004B12Cu | (2u << 20), 3);
    TEST_ASSERT_EQUAL_UINT8(2, p.rawPeakCurrent());
    TEST_ASSERT_TRUE(PdoPeakCurrent::Overload2 == p.peakCurrent());
    TEST_ASSERT_EQUAL_UINT16(15000, p.fixedVoltage_mV());  // voltage field untouched
    // A Sink leaves bits 21:20 reserved (Tables 6.9, 6.10).
    TEST_ASSERT_EQUAL_UINT8(0, snk(0x0004B12Cu | (2u << 20), 3).rawPeakCurrent());
    TEST_ASSERT_EQUAL_STRING("Overload capabilities 2", toString(p.peakCurrent()));
    TEST_ASSERT_EQUAL_STRING("IoC (default)", toString(PdoPeakCurrent::IoC));
}

void test_sink_pdo1_flags()
{
    const TPS25751Pdo p = snk(kTxSink[0], 1);
    TEST_ASSERT_TRUE(p.higherCapability());
    TEST_ASSERT_FALSE(p.usbSuspendSupported());  // bit 28 is Higher Capability for a Sink
    TEST_ASSERT_FALSE(p.unchunkedExtendedMessages());
    TEST_ASSERT_FALSE(p.eprCapable());
    TEST_ASSERT_TRUE(FastRoleSwapCurrent::NotSupported == p.fastRoleSwapCurrent());
    // Bit 28 read through a Source PDO 1 is USB Suspend Supported, not Higher Capability.
    TEST_ASSERT_TRUE(src(kTxSink[0], 1).usbSuspendSupported());
    TEST_ASSERT_FALSE(src(kTxSink[0], 1).higherCapability());
}

void test_sink_fast_role_swap_current()
{
    const uint32_t word = 0x0001912Cu | (3u << 23);  // 5 V 3 A, FRS 11b
    TEST_ASSERT_TRUE(FastRoleSwapCurrent::Current3_0A == snk(word, 1).fastRoleSwapCurrent());
    TEST_ASSERT_EQUAL_UINT8(3, snk(word, 1).rawFastRoleSwapCurrent());
    TEST_ASSERT_EQUAL_UINT8(0, snk(word, 2).rawFastRoleSwapCurrent());  // PDO 1 only
    TEST_ASSERT_EQUAL_UINT8(0, src(word, 1).rawFastRoleSwapCurrent());  // Sink only
    TEST_ASSERT_TRUE(src(word, 1).unchunkedExtendedMessages());          // bit 24 for a Source
    TEST_ASSERT_TRUE(src(word, 1).eprCapable());                         // bit 23 for a Source
    TEST_ASSERT_EQUAL_STRING("3.0 A at 5 V", toString(FastRoleSwapCurrent::Current3_0A));
}

// --- Battery, Variable, PPS ----------------------------------------------------------

void test_battery_round_trip()
{
    const uint32_t word = TPS25751Pdo::encodeBattery(20000, 5000, 100000);
    const TPS25751Pdo p = src(word, 2);
    TEST_ASSERT_TRUE(PdoKind::Battery == p.kind());
    TEST_ASSERT_EQUAL_UINT16(20000, p.batteryMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, p.batteryMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT32(100000, p.batteryPower_mW());
    TEST_ASSERT_EQUAL_UINT16(400, p.rawBatteryPower());
    TEST_ASSERT_EQUAL_UINT16(0, p.fixedVoltage_mV());  // not Fixed
}

void test_variable_round_trip()
{
    const uint32_t word = TPS25751Pdo::encodeVariable(20000, 16000, 2500);
    const TPS25751Pdo p = src(word, 2);
    TEST_ASSERT_TRUE(PdoKind::Variable == p.kind());
    TEST_ASSERT_EQUAL_UINT16(20000, p.variableMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(16000, p.variableMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(2500, p.variableCurrent_mA());
}

void test_pps_source_and_sink()
{
    const uint32_t word = TPS25751Pdo::encodeSprPps(21000, 5000, 3000, true);
    const TPS25751Pdo s = src(word, 5);
    TEST_ASSERT_TRUE(PdoKind::SprPps == s.kind());
    TEST_ASSERT_EQUAL_UINT8(210, s.rawPpsMaxVoltage());
    TEST_ASSERT_EQUAL_UINT16(21000, s.ppsMaxVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(5000, s.ppsMinVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, s.ppsCurrent_mA());
    TEST_ASSERT_TRUE(s.ppsPowerLimited());
    // Table 6.14: bit 27 is reserved in the Sink APDO.
    TEST_ASSERT_FALSE(snk(word, 5).ppsPowerLimited());
    TEST_ASSERT_EQUAL_UINT16(21000, snk(word, 5).ppsMaxVoltage_mV());
}

// --- SPR AVS -------------------------------------------------------------------------

void test_board_avs_source_apdo()
{
    const TPS25751Pdo p = src(0xE004B1F4, 5);
    TEST_ASSERT_EQUAL_UINT16(300, p.rawAvsCurrent15V());
    TEST_ASSERT_EQUAL_UINT16(3000, p.avsCurrent15V_mA());
    TEST_ASSERT_EQUAL_UINT16(500, p.rawAvsCurrent20V());
    TEST_ASSERT_EQUAL_UINT16(5000, p.avsCurrent20V_mA());
    TEST_ASSERT_TRUE(PdoPeakCurrent::IoC == p.avsPeakCurrent());
    TEST_ASSERT_TRUE(p.isValidForReceiver());
    TEST_ASSERT_EQUAL_HEX32(0xE004B1F4, TPS25751Pdo::encodeSprAvsSource(3000, 5000));
}

void test_avs_peak_current_and_sink_form()
{
    const uint32_t word = TPS25751Pdo::encodeSprAvsSource(3000, 0, PdoPeakCurrent::Overload3);
    TEST_ASSERT_TRUE(PdoPeakCurrent::Overload3 == src(word, 2).avsPeakCurrent());
    // The deprecated SPR AVS Sink APDO is identified but not decoded.
    const TPS25751Pdo sinkAvs = snk(word, 2);
    TEST_ASSERT_TRUE(PdoKind::SprAvs == sinkAvs.kind());
    TEST_ASSERT_EQUAL_UINT16(0, sinkAvs.avsCurrent15V_mA());
    TEST_ASSERT_EQUAL_HEX32(word, sinkAvs.raw());
    TEST_ASSERT_TRUE(sinkAvs.isValidForReceiver());
}

// --- Receiver validity (decision 4) --------------------------------------------------

void test_receiver_validity_limits()
{
    TEST_ASSERT_TRUE(src(TPS25751Pdo::encodeFixed(9000, 0), 2).isValidForReceiver());     // zero current allowed
    TEST_ASSERT_TRUE(src(TPS25751Pdo::encodeFixed(9000, 5000), 2).isValidForReceiver());  // 500
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeFixed(9000, 5010), 2).isValidForReceiver()); // 501
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeFixed(0, 1000), 2).isValidForReceiver());    // zero voltage

    TEST_ASSERT_TRUE(src(TPS25751Pdo::encodeBattery(20000, 5000, 0), 2).isValidForReceiver());
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeBattery(20000, 5000, 100250), 2).isValidForReceiver());  // 401
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeBattery(20000, 0, 1000), 2).isValidForReceiver());

    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeVariable(20000, 16000, 5010), 2).isValidForReceiver());
    TEST_ASSERT_TRUE(src(TPS25751Pdo::encodeVariable(20000, 16000, 0), 2).isValidForReceiver());

    TEST_ASSERT_TRUE(src(TPS25751Pdo::encodeSprPps(11000, 3300, 5000), 5).isValidForReceiver());
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeSprPps(11000, 3300, 5050), 5).isValidForReceiver());  // 101
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeSprPps(20000, 5000, 3000), 5).isValidForReceiver());  // max 200
    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeSprPps(21000, 4000, 3000), 5).isValidForReceiver()); // min 40
    TEST_ASSERT_TRUE(snk(TPS25751Pdo::encodeSprPps(20000, 5000, 3000), 5).isValidForReceiver());  // sink: 50..210
    TEST_ASSERT_FALSE(snk(TPS25751Pdo::encodeSprPps(22000, 5000, 3000), 5).isValidForReceiver());
    TEST_ASSERT_FALSE(snk(TPS25751Pdo::encodeSprPps(21000, 3300, 3000), 5).isValidForReceiver());

    TEST_ASSERT_FALSE(src(TPS25751Pdo::encodeSprAvsSource(5010, 0), 5).isValidForReceiver());
    TEST_ASSERT_FALSE(src(0xF0000000, 2).isValidForReceiver());  // APDO type 11b
    TEST_ASSERT_TRUE(src(0xD0000000, 2).isValidForReceiver());   // EPR AVS: not checked
}

// --- List validation (decision 4) ----------------------------------------------------

void test_bench_lists_pass()
{
    TEST_ASSERT_TRUE(validate(kRx140W, 4, PdoRole::Source, PdoListRules::ReceivedSource));
    TEST_ASSERT_TRUE(validate(kTxSource, 5, PdoRole::Source, PdoListRules::TransmitSource));
    TEST_ASSERT_TRUE(validate(kTxSink, 4, PdoRole::Sink, PdoListRules::TransmitSink));
    TEST_ASSERT_TRUE(validatePdoList(nullptr, 0, PdoListRules::ReceivedSource));  // no capabilities
}

void test_pdo1_must_be_fixed_5v_exactly()
{
    uint32_t words[] = {TPS25751Pdo::encodeFixed(4950, 3000), 0x0002D12C};
    TEST_ASSERT_FALSE(validate(words, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    words[0] = TPS25751Pdo::encodeFixed(5050, 3000);
    TEST_ASSERT_FALSE(validate(words, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    words[0] = TPS25751Pdo::encodeVariable(5000, 5000, 3000);
    TEST_ASSERT_FALSE(validate(words, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    words[0] = TPS25751Pdo::encodeFixed(5000, 0);
    TEST_ASSERT_TRUE(validate(words, 2, PdoRole::Source, PdoListRules::ReceivedSource));
}

void test_order_is_a_transmit_rule()
{
    const uint32_t words[] = {0x0001912C, TPS25751Pdo::encodeSprPps(21000, 5000, 3000), 0x0002D12C};
    TEST_ASSERT_TRUE(validate(words, 3, PdoRole::Source, PdoListRules::ReceivedSource));
    TEST_ASSERT_FALSE(validate(words, 3, PdoRole::Source, PdoListRules::TransmitSource));
    const uint32_t avsAfterPps[] = {0x0001912C, TPS25751Pdo::encodeSprPps(21000, 5000, 3000), 0xE004B1F4};
    TEST_ASSERT_FALSE(validate(avsAfterPps, 3, PdoRole::Source, PdoListRules::TransmitSource));
}

void test_variable_80_percent_is_a_transmit_source_rule()
{
    uint32_t words[] = {0x0001912C, TPS25751Pdo::encodeVariable(20000, 15000, 3000)};
    TEST_ASSERT_FALSE(validate(words, 2, PdoRole::Source, PdoListRules::TransmitSource));
    TEST_ASSERT_TRUE(validate(words, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    TEST_ASSERT_TRUE(validate(words, 2, PdoRole::Sink, PdoListRules::TransmitSink));
    words[1] = TPS25751Pdo::encodeVariable(20000, 16000, 3000);  // exactly 80%
    TEST_ASSERT_TRUE(validate(words, 2, PdoRole::Source, PdoListRules::TransmitSource));
}

void test_avs_currents_must_match_fixed_pdos_when_transmitted()
{
    uint32_t words[5];
    for (int i = 0; i < 5; i++) words[i] = kTxSource[i];
    words[4] = TPS25751Pdo::encodeSprAvsSource(2000, 5000);  // 15 V Fixed says 3 A
    TEST_ASSERT_FALSE(validate(words, 5, PdoRole::Source, PdoListRules::TransmitSource));
    TEST_ASSERT_TRUE(validate(words, 5, PdoRole::Source, PdoListRules::ReceivedSource));
    words[4] = TPS25751Pdo::encodeSprAvsSource(3000, 3000);  // 20 V Fixed says 5 A
    TEST_ASSERT_FALSE(validate(words, 5, PdoRole::Source, PdoListRules::TransmitSource));

    // Without a 20 V Fixed PDO the 20 V current Shall be 0.
    const uint32_t to15V[] = {0x0001912C, 0x0004B12C, TPS25751Pdo::encodeSprAvsSource(3000, 0)};
    TEST_ASSERT_TRUE(validate(to15V, 3, PdoRole::Source, PdoListRules::TransmitSource));
    const uint32_t to15VBad[] = {0x0001912C, 0x0004B12C, TPS25751Pdo::encodeSprAvsSource(3000, 1000)};
    TEST_ASSERT_FALSE(validate(to15VBad, 3, PdoRole::Source, PdoListRules::TransmitSource));
}

void test_transmit_sink_rejects_avs_and_transmit_rejects_epr()
{
    const uint32_t sinkAvs[] = {0x0001912C, 0xE004B000};
    TEST_ASSERT_FALSE(validate(sinkAvs, 2, PdoRole::Sink, PdoListRules::TransmitSink));
    TEST_ASSERT_TRUE(validate(sinkAvs, 2, PdoRole::Sink, PdoListRules::ReceivedSink));

    const uint32_t epr[] = {0x0001912C, 0xD0000000};
    TEST_ASSERT_FALSE(validate(epr, 2, PdoRole::Source, PdoListRules::TransmitSource));
    TEST_ASSERT_TRUE(validate(epr, 2, PdoRole::Source, PdoListRules::ReceivedSource));
}

void test_minimum_above_maximum_is_a_transmit_rule()
{
    const uint32_t variable[] = {0x0001912C, TPS25751Pdo::encodeVariable(9000, 12000, 1000)};
    TEST_ASSERT_TRUE(validate(variable, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    TEST_ASSERT_FALSE(validate(variable, 2, PdoRole::Source, PdoListRules::TransmitSource));
    TEST_ASSERT_TRUE(validate(variable, 2, PdoRole::Sink, PdoListRules::ReceivedSink));
    TEST_ASSERT_FALSE(validate(variable, 2, PdoRole::Sink, PdoListRules::TransmitSink));

    const uint32_t battery[] = {0x0001912C, TPS25751Pdo::encodeBattery(9000, 12000, 15000)};
    TEST_ASSERT_TRUE(validate(battery, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    TEST_ASSERT_FALSE(validate(battery, 2, PdoRole::Source, PdoListRules::TransmitSource));
    TEST_ASSERT_FALSE(validate(battery, 2, PdoRole::Sink, PdoListRules::TransmitSink));

    // Sink PPS: 9 V maximum, 12 V minimum, both inside Table 6.14's 5 to 21 V.
    const uint32_t pps[] = {0x0001912C, TPS25751Pdo::encodeSprPps(9000, 12000, 3000)};
    TEST_ASSERT_TRUE(validate(pps, 2, PdoRole::Sink, PdoListRules::ReceivedSink));
    TEST_ASSERT_FALSE(validate(pps, 2, PdoRole::Sink, PdoListRules::TransmitSink));

    // Equal minimum and maximum is allowed.
    const uint32_t equal[] = {0x0001912C, TPS25751Pdo::encodeBattery(12000, 12000, 15000)};
    TEST_ASSERT_TRUE(validate(equal, 2, PdoRole::Sink, PdoListRules::TransmitSink));
}

void test_an_invalid_pdo_fails_every_rule_set()
{
    const uint32_t words[] = {0x0001912C, TPS25751Pdo::encodeFixed(9000, 5010)};
    TEST_ASSERT_FALSE(validate(words, 2, PdoRole::Source, PdoListRules::ReceivedSource));
    TEST_ASSERT_FALSE(validate(words, 2, PdoRole::Sink, PdoListRules::ReceivedSink));
}

// --- Debug output --------------------------------------------------------------------

void test_debug_print_shows_flags_only_for_pdo1()
{
    const std::string first = captureStdout([] { src(0x0981912C, 1).debugPrint(Serial, "  "); });
    TEST_ASSERT_NOT_NULL(strstr(first.c_str(), "  Type: Fixed Supply\n"));
    TEST_ASSERT_NOT_NULL(strstr(first.c_str(), "    Voltage: 5000 mV\n"));
    TEST_ASSERT_NOT_NULL(strstr(first.c_str(), "    Peak Current: IoC (default) (0)\n"));
    TEST_ASSERT_NOT_NULL(strstr(first.c_str(), "    Unconstrained Power: Yes\n"));
    TEST_ASSERT_NOT_NULL(strstr(first.c_str(), "    EPR Capable: Yes\n"));

    const std::string later = captureStdout([] { src(0x000641D6, 4).debugPrint(Serial, "  "); });
    TEST_ASSERT_NOT_NULL(strstr(later.c_str(), "    Max Current: 4700 mA\n"));
    TEST_ASSERT_NOT_NULL(strstr(later.c_str(), "    Peak Current: IoC (default) (0)\n"));
    TEST_ASSERT_NULL(strstr(later.c_str(), "Unconstrained"));
}

void test_debug_print_avs_and_unexpected_kinds()
{
    const std::string avs = captureStdout([] { src(0xE004B1F4, 5).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(avs.c_str(), "Type: SPR AVS APDO\n"));
    TEST_ASSERT_NOT_NULL(strstr(avs.c_str(), "  Max Current 9 to 15 V: 3000 mA\n"));
    TEST_ASSERT_NOT_NULL(strstr(avs.c_str(), "  Max Current 15 to 20 V: 5000 mA\n"));

    const std::string sinkAvs = captureStdout([] { snk(0xE004B000, 2).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(sinkAvs.c_str(), "Deprecated SPR AVS Sink APDO, not decoded: 0xE004B000"));

    const std::string epr = captureStdout([] { src(0xD0000000, 2).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(epr.c_str(), "EPR AVS APDO (unexpected on an SPR port, not decoded)"));
    TEST_ASSERT_NOT_NULL(strstr(epr.c_str(), "  Raw: 0xD0000000\n"));
}

void test_debug_print_sink_pdo1()
{
    const std::string out = captureStdout([] { snk(0x1001912C, 1).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "  Max Current: 3000 mA\n"));  // Table 6.9 name
    const std::string later = captureStdout([] { snk(0x0004B12C, 3).debugPrint(Serial, ""); });
    TEST_ASSERT_NOT_NULL(strstr(later.c_str(), "  Operational Current: 3000 mA\n"));  // Table 6.10, Sink
    TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "  Higher Capability: Yes\n"));
    TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "  Fast Role Swap Current: Not supported (0)\n"));
    TEST_ASSERT_NULL(strstr(out.c_str(), "Peak Current"));
}

// --- Wire: Source_Capabilities from the 2026-11-23 capture --------------------------
//
// Message header 0x41A1 (USB PD R3.3 Table 6.1, Message Header; usb-pd-docs does not
// serve it): bits 4:0 = 1 Source_Capabilities, bit 5 = 1 DFP, bits 7:6 = 10b
// Revision 3.x, bit 8 = 1 Source, bits 14:12 = 4 data objects, bit 15 = 0.
// PDO fields by hand from Table 6.8 (PDO 1) and Table 6.10 (PDOs 2 to 4):
//   0x0981912C: bits 31:30 00b Fixed; 29 DRP 0; 28 USB Suspend 0; 27 Unconstrained 1;
//               26 USB Comm 0; 25 DRD 0; 24 Unchunked 1; 23 EPR 1; 21:20 Peak 00b;
//               19:10 = 100 (5000 mV, 50 mV units); 9:0 = 300 (3000 mA, 10 mA units)
//   0x0002D12C: Fixed; 29:22 0; 21:20 00b; 19:10 = 180 (9000 mV); 9:0 = 300 (3000 mA)
//   0x0004B12C: Fixed; 29:22 0; 21:20 00b; 19:10 = 300 (15000 mV); 9:0 = 300 (3000 mA)
//   0x000641D6: Fixed; 29:22 0; 21:20 00b; 19:10 = 400 (20000 mV); 9:0 = 470 (4700 mA)

void test_wire_source_capabilities_header()
{
    TEST_ASSERT_TRUE(cap::kHasSourceCapabilities);
    TEST_ASSERT_FALSE(cap::kSourceCapabilitiesEpr);
    TEST_ASSERT_EQUAL_HEX16(0x41A1, cap::kSourceCapabilitiesHeader);
    TEST_ASSERT_EQUAL_size_t(4, cap::kSourceCapabilitiesPdoCount);
}

void test_wire_source_capabilities_match_the_bench_words()
{
    // The wire carries the words read from 0x30 on the bench (kRx140W), bit for bit.
    TEST_ASSERT_EQUAL_HEX32_ARRAY(kRx140W, cap::kSourceCapabilitiesPdos, 4);
}

void test_wire_source_capabilities_decode_per_tables_6_8_and_6_10()
{
    const uint16_t mV[4] = {5000, 9000, 15000, 20000};
    const uint16_t mA[4] = {3000, 3000, 3000, 4700};
    for (uint8_t i = 0; i < 4; i++) {
        const TPS25751Pdo p = src(cap::kSourceCapabilitiesPdos[i], i + 1);
        TEST_ASSERT_TRUE(PdoKind::Fixed == p.kind());
        TEST_ASSERT_EQUAL_UINT16(mV[i], p.fixedVoltage_mV());
        TEST_ASSERT_EQUAL_UINT16(mA[i], p.fixedCurrent_mA());
        TEST_ASSERT_TRUE(PdoPeakCurrent::IoC == p.peakCurrent());
        TEST_ASSERT_TRUE(p.isValidForReceiver());
    }
    const TPS25751Pdo first = src(cap::kSourceCapabilitiesPdos[0], 1);
    TEST_ASSERT_FALSE(first.dualRolePower());             // bit 29
    TEST_ASSERT_FALSE(first.usbSuspendSupported());       // bit 28
    TEST_ASSERT_TRUE(first.unconstrainedPower());         // bit 27
    TEST_ASSERT_FALSE(first.usbCommunicationsCapable());  // bit 26
    TEST_ASSERT_FALSE(first.dualRoleData());              // bit 25
    TEST_ASSERT_TRUE(first.unchunkedExtendedMessages());  // bit 24
    TEST_ASSERT_TRUE(first.eprCapable());                 // bit 23
    TEST_ASSERT_TRUE(validate(cap::kSourceCapabilitiesPdos, 4, PdoRole::Source, PdoListRules::ReceivedSource));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_kind_from_pdo_and_apdo_type);
    RUN_TEST(test_charger_pdo1_flags_decode_at_position_1);
    RUN_TEST(test_flag_bits_in_a_later_fixed_pdo_read_false);
    RUN_TEST(test_unknown_position_reads_no_flags);
    RUN_TEST(test_peak_current_is_per_pdo_for_a_source);
    RUN_TEST(test_sink_pdo1_flags);
    RUN_TEST(test_sink_fast_role_swap_current);
    RUN_TEST(test_battery_round_trip);
    RUN_TEST(test_variable_round_trip);
    RUN_TEST(test_pps_source_and_sink);
    RUN_TEST(test_board_avs_source_apdo);
    RUN_TEST(test_avs_peak_current_and_sink_form);
    RUN_TEST(test_receiver_validity_limits);
    RUN_TEST(test_bench_lists_pass);
    RUN_TEST(test_pdo1_must_be_fixed_5v_exactly);
    RUN_TEST(test_order_is_a_transmit_rule);
    RUN_TEST(test_variable_80_percent_is_a_transmit_source_rule);
    RUN_TEST(test_avs_currents_must_match_fixed_pdos_when_transmitted);
    RUN_TEST(test_transmit_sink_rejects_avs_and_transmit_rejects_epr);
    RUN_TEST(test_minimum_above_maximum_is_a_transmit_rule);
    RUN_TEST(test_an_invalid_pdo_fails_every_rule_set);
    RUN_TEST(test_debug_print_shows_flags_only_for_pdo1);
    RUN_TEST(test_debug_print_avs_and_unexpected_kinds);
    RUN_TEST(test_debug_print_sink_pdo1);
    RUN_TEST(test_wire_source_capabilities_header);
    RUN_TEST(test_wire_source_capabilities_match_the_bench_words);
    RUN_TEST(test_wire_source_capabilities_decode_per_tables_6_8_and_6_10);
    return UNITY_END();
}
