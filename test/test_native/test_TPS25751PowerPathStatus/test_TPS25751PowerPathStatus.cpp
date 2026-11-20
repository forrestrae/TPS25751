/**
 * @file test_TPS25751PowerPathStatus.cpp
 * @brief Native unit tests for TPS25751PowerPathStatus (POWER_PATH_STATUS, 0x26, 5 bytes).
 *
 * Bit positions from TRM SLVUCR8B p.27: PPCable1 Switch 1:0, PP1 Switch 8:6,
 * PP3 Switch 14:12, PP1 Overcurrent 28, PPCable1 Overcurrent 34,
 * Power Source 39:38.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751PowerPathStatus.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

using E = TPS25751PowerPathStatus;
constexpr size_t SZ = TPS25751Registers::Registers::POWER_PATH_STATUS.size;  // 5

void pack(uint8_t *buf, unsigned pos, unsigned len, uint32_t value)
{
    for (unsigned i = 0; i < len; i++) {
        const unsigned b = pos + i;
        if ((value >> i) & 1u) buf[b / 8] |= static_cast<uint8_t>(1u << (b % 8));
        else buf[b / 8] &= static_cast<uint8_t>(~(1u << (b % 8)));
    }
}

std::string captureDebugPrint(const E &reg)
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

bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

struct Fields {
    uint32_t src = 0, cableOc = 0, pp1Oc = 0, cable = 0, pp1 = 0, pp3 = 0;
};

E make(const Fields &f)
{
    uint8_t d[SZ] = {0, 0, 0, 0, 0};
    pack(d, 38, 2, f.src);
    pack(d, 34, 1, f.cableOc);
    pack(d, 28, 1, f.pp1Oc);
    pack(d, 12, 3, f.pp3);
    pack(d, 6, 3, f.pp1);
    pack(d, 0, 2, f.cable);
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

    E info(TPS25751Registers::Registers::POWER_PATH_STATUS);
    TEST_ASSERT_EQUAL_size_t(SZ, info.size());

    const uint8_t d[SZ] = {0, 0, 0, 0, 0x80};  // bit 39: Power Source = 2 (VBUS)
    E ptr(d);
    TEST_ASSERT_EQUAL(PowerSource::VBUS, ptr.powerSource());

    E nul(nullptr);
    TEST_ASSERT_TRUE(nul.isValid());
    TEST_ASSERT_EQUAL(PowerSource::Reserved0, nul.powerSource());

    const uint8_t two[2] = {0, 0};
    E sized(two, 2);
    TEST_ASSERT_EQUAL_size_t(2, sized.size());
    TEST_ASSERT_FALSE(sized.isSemanticallyValid());
    // Fields past a short buffer decode to the safe default.
    TEST_ASSERT_FALSE(sized.isPP1Overcurrent());
}

void test_power_source_all_values()
{
    Fields f;
    f.src = 0;
    TEST_ASSERT_EQUAL(PowerSource::Reserved0, make(f).powerSource());
    f.src = 1;
    TEST_ASSERT_EQUAL(PowerSource::VIN_3V3, make(f).powerSource());
    f.src = 2;
    TEST_ASSERT_EQUAL(PowerSource::VBUS, make(f).powerSource());
    f.src = 3;
    TEST_ASSERT_EQUAL(PowerSource::Reserved3, make(f).powerSource());
}

void test_overcurrent_bits_are_independent()
{
    Fields f;
    f.cableOc = 1;
    E a = make(f);
    TEST_ASSERT_TRUE(a.isPPCable1Overcurrent());
    TEST_ASSERT_FALSE(a.isPP1Overcurrent());

    Fields g;
    g.pp1Oc = 1;
    E b = make(g);
    TEST_ASSERT_FALSE(b.isPPCable1Overcurrent());
    TEST_ASSERT_TRUE(b.isPP1Overcurrent());
}

void test_switch_states_all_values()
{
    const SwitchState exp[4] = {SwitchState::Disabled, SwitchState::DisabledDueToFault,
                                SwitchState::Enabled_CC1OrOutput,
                                SwitchState::Enabled_CC2OrSystemOutput};
    for (uint32_t v = 0; v < 4; v++) {
        Fields f;
        f.cable = v;
        f.pp1 = v;
        f.pp3 = v;
        E r = make(f);
        TEST_ASSERT_EQUAL(exp[v], r.ppCable1Switch());
        TEST_ASSERT_EQUAL(exp[v], r.pp1Switch());
        TEST_ASSERT_EQUAL(exp[v], r.pp3Switch());
    }
}

void test_switch_fields_do_not_bleed()
{
    Fields f;
    f.cable = 3;
    f.pp1 = 2;
    f.pp3 = 1;
    E r = make(f);
    TEST_ASSERT_EQUAL(SwitchState::Enabled_CC2OrSystemOutput, r.ppCable1Switch());
    TEST_ASSERT_EQUAL(SwitchState::Enabled_CC1OrOutput, r.pp1Switch());
    TEST_ASSERT_EQUAL(SwitchState::DisabledDueToFault, r.pp3Switch());
}

void test_three_bit_switch_codes_above_3_fall_back_to_disabled()
{
    // PP1 and PP3 are 3-bit fields; codes 4 to 7 are undefined in the TRM. The
    // enum decoders map them to Disabled; rawPp1Switch()/rawPp3Switch() keep them.
    for (uint32_t v = 4; v < 8; v++) {
        Fields f;
        f.pp1 = v;
        f.pp3 = v;
        E r = make(f);
        TEST_ASSERT_EQUAL(SwitchState::Disabled, r.pp1Switch());
        TEST_ASSERT_EQUAL(SwitchState::Disabled, r.pp3Switch());
    }
}

void test_to_string_power_source()
{
    TEST_ASSERT_EQUAL_STRING("Reserved (0)", toString(PowerSource::Reserved0));
    TEST_ASSERT_EQUAL_STRING("VIN_3V3", toString(PowerSource::VIN_3V3));
    TEST_ASSERT_EQUAL_STRING("VBUS", toString(PowerSource::VBUS));
    TEST_ASSERT_EQUAL_STRING("Reserved (3)", toString(PowerSource::Reserved3));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<PowerSource>(9)));
}

void test_to_string_switch_state()
{
    TEST_ASSERT_EQUAL_STRING("Disabled", toString(SwitchState::Disabled));
    TEST_ASSERT_EQUAL_STRING("Disabled (Fault)", toString(SwitchState::DisabledDueToFault));
    TEST_ASSERT_EQUAL_STRING("Enabled (CC1/Output)", toString(SwitchState::Enabled_CC1OrOutput));
    TEST_ASSERT_EQUAL_STRING("Enabled (CC2/System Output)", toString(SwitchState::Enabled_CC2OrSystemOutput));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SwitchState>(9)));
}

void test_validation_tiers()
{
    TEST_ASSERT_TRUE(E().hasValidData());
    TEST_ASSERT_TRUE(E().isSemanticallyValid());

    Fields ok;
    ok.src = 2;
    ok.cable = 3;
    ok.pp1 = 2;
    ok.pp3 = 2;
    TEST_ASSERT_TRUE(make(ok).isSemanticallyValid());

    // Reserved power source codes are tolerated.
    Fields res;
    res.src = 3;
    TEST_ASSERT_TRUE(make(res).isSemanticallyValid());

    const uint8_t ones[SZ] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    E bad(ones);
    TEST_ASSERT_FALSE(bad.hasValidData());
    TEST_ASSERT_FALSE(bad.isSemanticallyValid());

    const uint8_t four[4] = {0, 0, 0, 0};
    E shortReg(four, 4);
    TEST_ASSERT_FALSE(shortReg.isValidSize(SZ));
    TEST_ASSERT_FALSE(shortReg.isSemanticallyValid());
}

void test_semantic_overcurrent_requires_switch_off()
{
    // PP_CABLE1 overcurrent with the switch still enabled is inconsistent.
    Fields f;
    f.cableOc = 1;
    f.cable = 2;
    TEST_ASSERT_FALSE(make(f).isSemanticallyValid());
    f.cable = 3;
    TEST_ASSERT_FALSE(make(f).isSemanticallyValid());
    // Disabled or disabled-due-to-fault is consistent.
    f.cable = 0;
    TEST_ASSERT_TRUE(make(f).isSemanticallyValid());
    f.cable = 1;
    TEST_ASSERT_TRUE(make(f).isSemanticallyValid());

    // Same rule for PP1.
    Fields g;
    g.pp1Oc = 1;
    g.pp1 = 2;
    TEST_ASSERT_FALSE(make(g).isSemanticallyValid());
    g.pp1 = 3;
    TEST_ASSERT_FALSE(make(g).isSemanticallyValid());
    g.pp1 = 0;
    TEST_ASSERT_TRUE(make(g).isSemanticallyValid());
    g.pp1 = 1;
    TEST_ASSERT_TRUE(make(g).isSemanticallyValid());
}

void test_debug_print()
{
    Fields f;
    f.src = 2;
    f.cableOc = 1;
    f.cable = 1;
    f.pp1 = 2;
    f.pp3 = 3;
    const std::string out = captureDebugPrint(make(f));
    TEST_ASSERT_TRUE(has(out, "Power Path Status Register (0x26)"));
    TEST_ASSERT_TRUE(has(out, "Power Source: VBUS"));
    TEST_ASSERT_TRUE(has(out, "PP_CABLE1 Overcurrent: Yes"));
    TEST_ASSERT_TRUE(has(out, "PP1 Overcurrent: No"));
    TEST_ASSERT_TRUE(has(out, "PP_CABLE1 Switch: Currently disabled (1)"));
    TEST_ASSERT_TRUE(has(out, "PP1 Switch: Enabled (system output) (2)"));
    // PP3 code 3 is a system input (SLVUCR8B p.27), not the PP_CABLE1 CC2 wording.
    TEST_ASSERT_TRUE(has(out, "PP3 Switch: Enabled (system input) (3)"));
    TEST_ASSERT_FALSE(has(out, "CC2"));
}

void test_debug_print_undefined_codes()
{
    Fields f;
    f.pp1 = 3;
    f.pp3 = 7;
    const std::string out = captureDebugPrint(make(f));
    TEST_ASSERT_TRUE(has(out, "PP1 Switch: Undefined (3)"));
    TEST_ASSERT_TRUE(has(out, "PP3 Switch: Undefined (7)"));
}

void test_switch_text_follows_the_trm_per_switch()
{
    TEST_ASSERT_EQUAL_STRING("Disabled", E::ppCable1SwitchText(0));
    TEST_ASSERT_EQUAL_STRING("Currently disabled", E::ppCable1SwitchText(1));
    TEST_ASSERT_EQUAL_STRING("CC1 enabled (system output)", E::ppCable1SwitchText(2));
    TEST_ASSERT_EQUAL_STRING("CC2 enabled (system output)", E::ppCable1SwitchText(3));
    TEST_ASSERT_EQUAL_STRING("Undefined", E::ppCable1SwitchText(4));

    TEST_ASSERT_EQUAL_STRING("Disabled", E::pp1SwitchText(0));
    TEST_ASSERT_EQUAL_STRING("Disabled due to fault", E::pp1SwitchText(1));
    TEST_ASSERT_EQUAL_STRING("Enabled (system output)", E::pp1SwitchText(2));
    TEST_ASSERT_EQUAL_STRING("Undefined", E::pp1SwitchText(3));

    TEST_ASSERT_EQUAL_STRING("Disabled", E::pp3SwitchText(0));
    TEST_ASSERT_EQUAL_STRING("Disabled due to fault", E::pp3SwitchText(1));
    TEST_ASSERT_EQUAL_STRING("Enabled (system output)", E::pp3SwitchText(2));
    TEST_ASSERT_EQUAL_STRING("Enabled (system input)", E::pp3SwitchText(3));
    TEST_ASSERT_EQUAL_STRING("Undefined", E::pp3SwitchText(4));
}

void test_raw_switch_codes()
{
    Fields f;
    f.cable = 3;
    f.pp1 = 5;
    f.pp3 = 6;
    E r = make(f);
    TEST_ASSERT_EQUAL_UINT8(3, r.rawPpCable1Switch());
    TEST_ASSERT_EQUAL_UINT8(5, r.rawPp1Switch());
    TEST_ASSERT_EQUAL_UINT8(6, r.rawPp3Switch());
}

void test_undefined_switch_codes_are_semantically_invalid()
{
    // PP1 defines codes 0 to 2 and PP3 codes 0 to 3 (SLVUCR8B p.27).
    for (uint32_t v = 0; v < 8; v++) {
        Fields a;
        a.pp1 = v;
        TEST_ASSERT_EQUAL_MESSAGE(v <= 2, make(a).isSemanticallyValid(), "PP1");
        Fields b;
        b.pp3 = v;
        TEST_ASSERT_EQUAL_MESSAGE(v <= 3, make(b).isSemanticallyValid(), "PP3");
    }
}

void test_debug_print_pp1_overcurrent_yes()
{
    Fields f;
    f.src = 1;
    f.pp1Oc = 1;
    const std::string out = captureDebugPrint(make(f));
    TEST_ASSERT_TRUE(has(out, "Power Source: VIN_3V3"));
    TEST_ASSERT_TRUE(has(out, "PP_CABLE1 Overcurrent: No"));
    TEST_ASSERT_TRUE(has(out, "PP1 Overcurrent: Yes"));
}

void test_rule_of_five()
{
    Fields f;
    f.src = 2;
    f.pp3 = 2;
    E original = make(f);
    E copy(original);
    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), SZ);

    E assigned;
    assigned = original;
    TEST_ASSERT_EQUAL(PowerSource::VBUS, assigned.powerSource());

    const uint8_t *buf = original.raw();
    E moved(static_cast<E &&>(original));
    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());

    E target;
    target = static_cast<E &&>(moved);
    TEST_ASSERT_FALSE(moved.isValid());
    TEST_ASSERT_EQUAL(SwitchState::Enabled_CC1OrOutput, target.pp3Switch());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_constructors);
    RUN_TEST(test_power_source_all_values);
    RUN_TEST(test_overcurrent_bits_are_independent);
    RUN_TEST(test_switch_states_all_values);
    RUN_TEST(test_switch_fields_do_not_bleed);
    RUN_TEST(test_three_bit_switch_codes_above_3_fall_back_to_disabled);
    RUN_TEST(test_to_string_power_source);
    RUN_TEST(test_to_string_switch_state);
    RUN_TEST(test_validation_tiers);
    RUN_TEST(test_semantic_overcurrent_requires_switch_off);
    RUN_TEST(test_debug_print);
    RUN_TEST(test_debug_print_undefined_codes);
    RUN_TEST(test_switch_text_follows_the_trm_per_switch);
    RUN_TEST(test_raw_switch_codes);
    RUN_TEST(test_undefined_switch_codes_are_semantically_invalid);
    RUN_TEST(test_debug_print_pp1_overcurrent_yes);
    RUN_TEST(test_rule_of_five);
    return UNITY_END();
}
