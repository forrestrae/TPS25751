/**
 * @file test_TPS25751PowerStatus.cpp
 * @brief Native unit tests for TPS25751PowerStatus (POWER_STATUS, 0x3F, 2 bytes).
 *
 * Fixtures are packed at the TRM bit positions: bit 0 power connection, bit 1
 * source/sink, bits 3:2 Type-C current, bits 7:4 charger detect, bits 9:8
 * charger advertise.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "TPS25751PowerStatus.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

using E = TPS25751PowerStatus;
constexpr size_t SZ = TPS25751Registers::Registers::POWER_STATUS.size;  // 2

// Packs value into buf at the documented bit position (little-endian).
void pack(uint8_t *buf, unsigned pos, unsigned len, uint32_t value)
{
    for (unsigned i = 0; i < len; i++) {
        const unsigned b = pos + i;
        if ((value >> i) & 1u) buf[b / 8] |= static_cast<uint8_t>(1u << (b % 8));
        else buf[b / 8] &= static_cast<uint8_t>(~(1u << (b % 8)));
    }
}

bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

E make(uint32_t conn, uint32_t srcSink, uint32_t cur, uint32_t det, uint32_t adv)
{
    uint8_t d[SZ] = {0, 0};
    pack(d, 0, 1, conn);
    pack(d, 1, 1, srcSink);
    pack(d, 2, 2, cur);
    pack(d, 4, 4, det);
    pack(d, 8, 2, adv);
    return E(d);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_constructors()
{
    E def;
    TEST_ASSERT_TRUE(def.isValid());
    TEST_ASSERT_EQUAL_size_t(SZ, def.size());
    TEST_ASSERT_FALSE(def.isPowerConnection());

    E info(TPS25751Registers::Registers::POWER_STATUS);
    TEST_ASSERT_EQUAL_size_t(SZ, info.size());

    const uint8_t d[SZ] = {0x01, 0x00};
    E ptr(d);
    TEST_ASSERT_TRUE(ptr.isPowerConnection());

    E nul(nullptr);
    TEST_ASSERT_TRUE(nul.isValid());
    TEST_ASSERT_FALSE(nul.isPowerConnection());

    const uint8_t three[3] = {0, 0, 0};
    E sized(three, 3);
    TEST_ASSERT_EQUAL_size_t(3, sized.size());
    TEST_ASSERT_FALSE(sized.isSemanticallyValid());
}

void test_connection_and_role_bits()
{
    E r = make(1, 1, 0, 0, 0);
    TEST_ASSERT_TRUE(r.isPowerConnection());
    TEST_ASSERT_TRUE(r.isSourceSink());
    TEST_ASSERT_TRUE(r.isProvidingPower());
    TEST_ASSERT_FALSE(r.isRequestingPower());

    E q = make(1, 0, 0, 0, 0);
    TEST_ASSERT_FALSE(q.isSourceSink());
    TEST_ASSERT_FALSE(q.isProvidingPower());
    TEST_ASSERT_TRUE(q.isRequestingPower());
}

void test_type_c_current_all_values()
{
    TEST_ASSERT_EQUAL(E::TypeCCurrent::USB_DEFAULT, make(1, 0, 0, 0, 0).getTypeCCurrent());
    TEST_ASSERT_EQUAL(E::TypeCCurrent::CURRENT_1_5A, make(1, 0, 1, 0, 0).getTypeCCurrent());
    TEST_ASSERT_EQUAL(E::TypeCCurrent::CURRENT_3_0A, make(1, 0, 2, 0, 0).getTypeCCurrent());
    TEST_ASSERT_EQUAL(E::TypeCCurrent::EXPLICIT_PD_CONTRACT, make(1, 0, 3, 0, 0).getTypeCCurrent());
    TEST_ASSERT_EQUAL_STRING("USB Default Current", make(1, 0, 0, 0, 0).getTypeCCurrentString());
    TEST_ASSERT_EQUAL_STRING("1.5 A", make(1, 0, 1, 0, 0).getTypeCCurrentString());
    TEST_ASSERT_EQUAL_STRING("3.0 A", make(1, 0, 2, 0, 0).getTypeCCurrentString());
    TEST_ASSERT_EQUAL_STRING("Explicit PD Contract", make(1, 0, 3, 0, 0).getTypeCCurrentString());
}

void test_charger_detect_all_values()
{
    const E::ChargerDetectStatus exp[10] = {
        E::ChargerDetectStatus::DISABLED_OR_NOT_RUN, E::ChargerDetectStatus::IN_PROGRESS,
        E::ChargerDetectStatus::COMPLETE_NONE, E::ChargerDetectStatus::COMPLETE_SDP,
        E::ChargerDetectStatus::COMPLETE_BC12_CDP, E::ChargerDetectStatus::COMPLETE_BC12_DCP,
        E::ChargerDetectStatus::COMPLETE_DIVIDER1_DCP, E::ChargerDetectStatus::COMPLETE_DIVIDER2_DCP,
        E::ChargerDetectStatus::COMPLETE_DIVIDER3_DCP, E::ChargerDetectStatus::COMPLETE_1_2V_DCP};
    const char *str[10] = {"Disabled/Not Run", "In Progress", "Complete - None Detected",
                           "Complete - SDP", "Complete - BC 1.2 CDP", "Complete - BC 1.2 DCP",
                           "Complete - Divider1 DCP", "Complete - Divider2 DCP",
                           "Complete - Divider3 DCP", "Complete - 1.2V DCP"};
    for (uint32_t v = 0; v < 10; v++) {
        E r = make(1, 0, 0, v, 0);
        TEST_ASSERT_EQUAL(exp[v], r.getChargerDetectStatus());
        TEST_ASSERT_EQUAL_STRING(str[v], r.getChargerDetectStatusString());
    }
}

void test_charger_detect_out_of_range_falls_back()
{
    // Codes 10 to 15 are undefined; the decoder returns Disabled/Not Run.
    for (uint32_t v = 10; v < 16; v++) {
        E r = make(1, 0, 0, v, 0);
        TEST_ASSERT_EQUAL(E::ChargerDetectStatus::DISABLED_OR_NOT_RUN, r.getChargerDetectStatus());
    }
}

void test_charger_advertise_all_values()
{
    TEST_ASSERT_EQUAL(E::ChargerAdvertiseStatus::DISABLED_OR_NOT_RUN, make(1, 0, 0, 0, 0).getChargerAdvertiseStatus());
    TEST_ASSERT_EQUAL(E::ChargerAdvertiseStatus::IN_PROCESS, make(1, 0, 0, 0, 1).getChargerAdvertiseStatus());
    TEST_ASSERT_EQUAL(E::ChargerAdvertiseStatus::COMPLETE, make(1, 0, 0, 0, 2).getChargerAdvertiseStatus());
    TEST_ASSERT_EQUAL(E::ChargerAdvertiseStatus::RESERVED, make(1, 0, 0, 0, 3).getChargerAdvertiseStatus());
    TEST_ASSERT_EQUAL_STRING("Disabled/Not Run", make(1, 0, 0, 0, 0).getChargerAdvertiseStatusString());
    TEST_ASSERT_EQUAL_STRING("In Process", make(1, 0, 0, 0, 1).getChargerAdvertiseStatusString());
    TEST_ASSERT_EQUAL_STRING("Complete", make(1, 0, 0, 0, 2).getChargerAdvertiseStatusString());
    TEST_ASSERT_EQUAL_STRING("Reserved", make(1, 0, 0, 0, 3).getChargerAdvertiseStatusString());
}

void test_fields_are_independent()
{
    E r = make(1, 1, 2, 5, 2);
    TEST_ASSERT_TRUE(r.isPowerConnection());
    TEST_ASSERT_TRUE(r.isSourceSink());
    TEST_ASSERT_EQUAL(E::TypeCCurrent::CURRENT_3_0A, r.getTypeCCurrent());
    TEST_ASSERT_EQUAL(E::ChargerDetectStatus::COMPLETE_BC12_DCP, r.getChargerDetectStatus());
    TEST_ASSERT_EQUAL(E::ChargerAdvertiseStatus::COMPLETE, r.getChargerAdvertiseStatus());
}

void test_validation_tiers()
{
    TEST_ASSERT_TRUE(make(1, 1, 2, 5, 2).hasValidData());
    TEST_ASSERT_TRUE(make(1, 1, 2, 5, 2).isSemanticallyValid());
    // Not connected with a completed detection is unusual but accepted.
    TEST_ASSERT_TRUE(make(0, 0, 0, 5, 0).isSemanticallyValid());
    TEST_ASSERT_TRUE(E().isSemanticallyValid());

    // Reserved advertise status (3) fails.
    TEST_ASSERT_FALSE(make(1, 0, 0, 0, 3).isSemanticallyValid());

    // All 0xFF is a bus-error signature.
    const uint8_t ones[SZ] = {0xFF, 0xFF};
    E bad(ones);
    TEST_ASSERT_FALSE(bad.hasValidData());
    TEST_ASSERT_FALSE(bad.isSemanticallyValid());

    // Wrong size.
    const uint8_t one[1] = {0x01};
    E shortReg(one, 1);
    TEST_ASSERT_TRUE(shortReg.isValid());
    TEST_ASSERT_FALSE(shortReg.isValidSize(SZ));
    TEST_ASSERT_FALSE(shortReg.isSemanticallyValid());
}

void test_semantic_invalid_when_detect_code_out_of_range()
{
    // Detect code 12 is undefined; the semantic check reads the raw bits and rejects it.
    TEST_ASSERT_FALSE(make(1, 0, 0, 12, 0).isSemanticallyValid());
}

void test_debug_print_connected()
{
    const std::string out = captureDebugPrint(make(1, 1, 2, 5, 2));
    TEST_ASSERT_TRUE(has(out, "Power Status Register (0x3F)"));
    TEST_ASSERT_TRUE(has(out, "Power Connection: Yes"));
    TEST_ASSERT_TRUE(has(out, "Role: Providing Power (Sink)"));
    TEST_ASSERT_TRUE(has(out, "Type-C Current: 3.0 A"));
    TEST_ASSERT_TRUE(has(out, "Charger Detect: Complete - BC 1.2 DCP"));
    TEST_ASSERT_TRUE(has(out, "Charger Advertise: Complete"));
}

void test_debug_print_requesting_role()
{
    const std::string out = captureDebugPrint(make(1, 0, 1, 1, 1));
    TEST_ASSERT_TRUE(has(out, "Role: Requesting Power (Source)"));
    TEST_ASSERT_TRUE(has(out, "Type-C Current: 1.5 A"));
}

void test_debug_print_disconnected_omits_details()
{
    const std::string out = captureDebugPrint(make(0, 0, 0, 0, 0));
    TEST_ASSERT_TRUE(has(out, "Power Connection: No"));
    TEST_ASSERT_FALSE(has(out, "Role:"));
    TEST_ASSERT_FALSE(has(out, "Charger Detect"));
}

void test_rule_of_five()
{
    E original = make(1, 1, 3, 4, 1);
    E copy(original);
    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), SZ);

    E assigned;
    assigned = original;
    TEST_ASSERT_EQUAL(E::ChargerDetectStatus::COMPLETE_BC12_CDP, assigned.getChargerDetectStatus());

    const uint8_t *buf = original.raw();
    E moved(static_cast<E &&>(original));
    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());

    E target;
    target = static_cast<E &&>(moved);
    TEST_ASSERT_FALSE(moved.isValid());
    TEST_ASSERT_EQUAL(E::TypeCCurrent::EXPLICIT_PD_CONTRACT, target.getTypeCCurrent());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_constructors);
    RUN_TEST(test_connection_and_role_bits);
    RUN_TEST(test_type_c_current_all_values);
    RUN_TEST(test_charger_detect_all_values);
    RUN_TEST(test_charger_detect_out_of_range_falls_back);
    RUN_TEST(test_charger_advertise_all_values);
    RUN_TEST(test_fields_are_independent);
    RUN_TEST(test_validation_tiers);
    RUN_TEST(test_semantic_invalid_when_detect_code_out_of_range);
    RUN_TEST(test_debug_print_connected);
    RUN_TEST(test_debug_print_requesting_role);
    RUN_TEST(test_debug_print_disconnected_omits_details);
    RUN_TEST(test_rule_of_five);
    return UNITY_END();
}
