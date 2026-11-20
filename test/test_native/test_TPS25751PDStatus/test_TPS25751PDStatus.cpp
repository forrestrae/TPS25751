/**
 * @file test_TPS25751PDStatus.cpp
 * @brief Native unit tests for TPS25751PDStatus (PD_STATUS, 0x40, 4 bytes).
 *
 * Fixtures are packed at the documented bit positions: bits 3:2 CC Pullup,
 * bits 5:4 Port Type, bit 6 Present PD Role, bits 12:8 Soft Reset Details,
 * bits 21:16 Hard Reset Details.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751PDStatus.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

using E = TPS25751PDStatus;
constexpr size_t SZ = TPS25751Registers::Registers::PD_STATUS.size;  // 4

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

E make(uint32_t pullup, uint32_t portType, uint32_t role, uint32_t soft, uint32_t hard)
{
    uint8_t d[SZ] = {0, 0, 0, 0};
    pack(d, 2, 2, pullup);
    pack(d, 4, 2, portType);
    pack(d, 6, 1, role);
    pack(d, 8, 5, soft);
    pack(d, 16, 6, hard);
    return E(d);
}

const char *const HARD_STR[0x13] = {
    "No Hard Reset", "Received from Port Partner", "Requested by Host",
    "Invalid DR_Swap during Active Mode", "Discharge Failed", "No Response Timeout",
    "Send Soft Reset", "Sink Select Capability", "Sink Transition Sink",
    "Sink Wait for Capabilities", "Soft Reset", "Source On Timeout",
    "Source Capability Response", "Source Send Capabilities", "Sourcing Fault",
    "Unable to Source", "FRS Failure", "Unexpected Message", "VCONN Recovery Timeout"};

const char *const SOFT_STR[0x1C] = {
    "No Soft Reset", "Received from Port Partner", "Reserved", "Reserved",
    "Invalid Source Capabilities", "Message Retries Exhausted", "Unexpected Accept",
    "Unexpected Control Message", "Unexpected GetSinkCap", "Unexpected GetSourceCap",
    "Unexpected GotoMin", "Unexpected PS_RDY", "Unexpected Ping", "Unexpected Reject",
    "Unexpected Request", "Unexpected Sink Capabilities", "Unexpected Source Capabilities",
    "Unexpected Swap", "Unexpected Wait Capabilities", "Unknown Control Message",
    "Unknown Data Message", "Initialize SOP' Controller", "Initialize SOP'' Controller",
    "Unexpected Extended Message", "Unknown Extended Message", "Unexpected Data Message",
    "Unexpected Not Supported", "Unexpected Get_Status"};

}  // namespace

void setUp() {}
void tearDown() {}

void test_constructors()
{
    E def;
    TEST_ASSERT_TRUE(def.isValid());
    TEST_ASSERT_EQUAL_size_t(SZ, def.size());

    E info(TPS25751Registers::Registers::PD_STATUS);
    TEST_ASSERT_EQUAL_size_t(SZ, info.size());

    const uint8_t d[SZ] = {0x40, 0, 0, 0};
    E ptr(d);
    TEST_ASSERT_EQUAL(E::PDRole::SOURCE, ptr.getPDRole());

    E nul(nullptr);
    TEST_ASSERT_TRUE(nul.isValid());
    TEST_ASSERT_EQUAL(E::PDRole::SINK, nul.getPDRole());

    const uint8_t two[2] = {0, 0};
    E sized(two, 2);
    TEST_ASSERT_EQUAL_size_t(2, sized.size());
    TEST_ASSERT_FALSE(sized.isSemanticallyValid());
}

void test_pd_role()
{
    TEST_ASSERT_EQUAL(E::PDRole::SINK, make(0, 0, 0, 0, 0).getPDRole());
    TEST_ASSERT_EQUAL(E::PDRole::SOURCE, make(0, 0, 1, 0, 0).getPDRole());
    TEST_ASSERT_EQUAL_STRING("Sink", make(0, 0, 0, 0, 0).getPDRoleString());
    TEST_ASSERT_EQUAL_STRING("Source", make(0, 0, 1, 0, 0).getPDRoleString());
}

void test_port_type()
{
    TEST_ASSERT_EQUAL(E::PortType::SINK_SOURCE, make(0, 0, 0, 0, 0).getPortType());
    TEST_ASSERT_EQUAL(E::PortType::SINK, make(0, 1, 0, 0, 0).getPortType());
    TEST_ASSERT_EQUAL(E::PortType::SOURCE, make(0, 2, 0, 0, 0).getPortType());
    TEST_ASSERT_EQUAL(E::PortType::SOURCE_SINK, make(0, 3, 0, 0, 0).getPortType());
    TEST_ASSERT_EQUAL_STRING("Sink/Source", make(0, 0, 0, 0, 0).getPortTypeString());
    TEST_ASSERT_EQUAL_STRING("Sink", make(0, 1, 0, 0, 0).getPortTypeString());
    TEST_ASSERT_EQUAL_STRING("Source", make(0, 2, 0, 0, 0).getPortTypeString());
    TEST_ASSERT_EQUAL_STRING("Source/Sink", make(0, 3, 0, 0, 0).getPortTypeString());
}

void test_cc_pullup()
{
    TEST_ASSERT_EQUAL(E::CCPullup::NOT_DETECTED, make(0, 0, 0, 0, 0).getCCPullup());
    TEST_ASSERT_EQUAL(E::CCPullup::USB_DEFAULT, make(1, 0, 0, 0, 0).getCCPullup());
    TEST_ASSERT_EQUAL(E::CCPullup::CURRENT_1_5A, make(2, 0, 0, 0, 0).getCCPullup());
    TEST_ASSERT_EQUAL(E::CCPullup::CURRENT_3_0A, make(3, 0, 0, 0, 0).getCCPullup());
    TEST_ASSERT_EQUAL_STRING("Not Detected", make(0, 0, 0, 0, 0).getCCPullupString());
    TEST_ASSERT_EQUAL_STRING("USB Default", make(1, 0, 0, 0, 0).getCCPullupString());
    TEST_ASSERT_EQUAL_STRING("1.5 A (SinkTxNG)", make(2, 0, 0, 0, 0).getCCPullupString());
    TEST_ASSERT_EQUAL_STRING("3.0 A (SinkTxOK)", make(3, 0, 0, 0, 0).getCCPullupString());
}

void test_hard_reset_reason_every_code()
{
    for (uint32_t v = 0; v <= 0x12; v++) {
        E r = make(0, 0, 0, 0, v);
        TEST_ASSERT_EQUAL_UINT8(v, static_cast<uint8_t>(r.getHardResetReason()));
        TEST_ASSERT_EQUAL_STRING(HARD_STR[v], r.getHardResetReasonString());
        TEST_ASSERT_TRUE(r.isSemanticallyValid());
    }
}

void test_hard_reset_reason_undefined_codes()
{
    // 0x13 to 0x3F are undefined: the decoder falls back to No Hard Reset
    // and the semantic tier rejects the register.
    for (uint32_t v = 0x13; v <= 0x3F; v += 0x0B) {
        E r = make(0, 0, 0, 0, v);
        TEST_ASSERT_EQUAL(E::HardResetReason::NO_HARD_RESET, r.getHardResetReason());
        TEST_ASSERT_FALSE(r.isSemanticallyValid());
    }
}

void test_soft_reset_reason_every_code()
{
    for (uint32_t v = 0; v <= 0x1B; v++) {
        E r = make(0, 0, 0, v, 0);
        TEST_ASSERT_EQUAL_UINT8(v, static_cast<uint8_t>(r.getSoftResetReason()));
        TEST_ASSERT_EQUAL_STRING(SOFT_STR[v], r.getSoftResetReasonString());
        TEST_ASSERT_TRUE(r.isSemanticallyValid());
    }
}

void test_soft_reset_reason_undefined_codes()
{
    for (uint32_t v = 0x1C; v <= 0x1F; v++) {
        E r = make(0, 0, 0, v, 0);
        TEST_ASSERT_EQUAL(E::SoftResetReason::NO_SOFT_RESET, r.getSoftResetReason());
        TEST_ASSERT_FALSE(r.isSemanticallyValid());
    }
}

void test_fields_are_independent()
{
    E r = make(3, 2, 1, 0x0B, 0x05);
    TEST_ASSERT_EQUAL(E::CCPullup::CURRENT_3_0A, r.getCCPullup());
    TEST_ASSERT_EQUAL(E::PortType::SOURCE, r.getPortType());
    TEST_ASSERT_EQUAL(E::PDRole::SOURCE, r.getPDRole());
    TEST_ASSERT_EQUAL(E::SoftResetReason::UNEXPECTED_PS_RDY, r.getSoftResetReason());
    TEST_ASSERT_EQUAL(E::HardResetReason::NO_RESPONSE_TIMEOUT, r.getHardResetReason());
}

void test_validation_tiers()
{
    TEST_ASSERT_TRUE(E().hasValidData());
    TEST_ASSERT_TRUE(E().isSemanticallyValid());
    TEST_ASSERT_TRUE(make(3, 3, 1, 0x1B, 0x12).isSemanticallyValid());

    const uint8_t ones[SZ] = {0xFF, 0xFF, 0xFF, 0xFF};
    E bad(ones);
    TEST_ASSERT_FALSE(bad.hasValidData());
    TEST_ASSERT_FALSE(bad.isSemanticallyValid());

    const uint8_t three[3] = {0, 0, 0};
    E shortReg(three, 3);
    TEST_ASSERT_TRUE(shortReg.isValid());
    TEST_ASSERT_FALSE(shortReg.isValidSize(SZ));
    TEST_ASSERT_FALSE(shortReg.isSemanticallyValid());
}

void test_debug_print_idle_omits_reset_lines()
{
    const std::string out = captureDebugPrint(make(0, 0, 0, 0, 0));
    TEST_ASSERT_TRUE(has(out, "PD Status Register (0x40)"));
    TEST_ASSERT_TRUE(has(out, "PD Role: Sink"));
    TEST_ASSERT_TRUE(has(out, "Port Type: Sink/Source"));
    TEST_ASSERT_TRUE(has(out, "CC Pullup: Not Detected"));
    TEST_ASSERT_FALSE(has(out, "Hard Reset Reason"));
    TEST_ASSERT_FALSE(has(out, "Soft Reset Reason"));
}

void test_debug_print_with_resets()
{
    const std::string out = captureDebugPrint(make(3, 2, 1, 0x05, 0x02));
    TEST_ASSERT_TRUE(has(out, "PD Role: Source"));
    TEST_ASSERT_TRUE(has(out, "Port Type: Source"));
    TEST_ASSERT_TRUE(has(out, "CC Pullup: 3.0 A (SinkTxOK)"));
    TEST_ASSERT_TRUE(has(out, "Hard Reset Reason: Requested by Host"));
    TEST_ASSERT_TRUE(has(out, "Soft Reset Reason: Message Retries Exhausted"));
}

void test_rule_of_five()
{
    E original = make(2, 1, 1, 0x04, 0x07);
    E copy(original);
    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), SZ);

    E assigned;
    assigned = original;
    TEST_ASSERT_EQUAL(E::HardResetReason::SINK_SELECT_CAPABILITY, assigned.getHardResetReason());

    const uint8_t *buf = original.raw();
    E moved(static_cast<E &&>(original));
    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());

    E target;
    target = static_cast<E &&>(moved);
    TEST_ASSERT_FALSE(moved.isValid());
    TEST_ASSERT_EQUAL(E::SoftResetReason::INVALID_SOURCE_CAPABILITIES, target.getSoftResetReason());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_constructors);
    RUN_TEST(test_pd_role);
    RUN_TEST(test_port_type);
    RUN_TEST(test_cc_pullup);
    RUN_TEST(test_hard_reset_reason_every_code);
    RUN_TEST(test_hard_reset_reason_undefined_codes);
    RUN_TEST(test_soft_reset_reason_every_code);
    RUN_TEST(test_soft_reset_reason_undefined_codes);
    RUN_TEST(test_fields_are_independent);
    RUN_TEST(test_validation_tiers);
    RUN_TEST(test_debug_print_idle_omits_reset_lines);
    RUN_TEST(test_debug_print_with_resets);
    RUN_TEST(test_rule_of_five);
    return UNITY_END();
}
