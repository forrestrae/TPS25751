/**
 * @file test_TPS25751TypeCState.cpp
 * @brief Native (host-run) unit tests for TPS25751TypeCState.
 *
 * Run with: pio test -e native
 *
 * TYPEC_STATE (0x69) is 4 bytes; the class decodes (little-endian, bit n is
 * byte n/8, bit n%8):
 *   bits 7:0    CC Pin for PD  (0 none, 1 CC1, 2 CC2)
 *   bits 15:8   CC1 Pin State  (0 not connected ... 5 3.0A advertisement)
 *   bits 23:16  CC2 Pin State
 *   bits 31:24  Port State     (Type-C state machine code, 0x00 .. 0x67)
 */

#include <unity.h>

#include <cstdio>
#include <string>
#include <unistd.h>

#include "TPS25751TypeCState.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

constexpr size_t TYPEC_SIZE = TPS25751Registers::Registers::TYPEC_STATE.size;  // 4

// Builds a TYPEC_STATE buffer from its four byte-wide fields.
struct Buf {
    uint8_t b[TYPEC_SIZE];
    Buf(uint8_t pdPin, uint8_t cc1, uint8_t cc2, uint8_t portState)
        : b{pdPin, cc1, cc2, portState}
    {
    }
};

std::string captureDebugPrint(const TPS25751TypeCState &reg)
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

bool contains(const std::string &s, const char *needle)
{
    return s.find(needle) != std::string::npos;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction ------------------------------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751TypeCState reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(TYPEC_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(TYPEC_SIZE));
    TEST_ASSERT_EQUAL(TYPEC_DISABLED, reg.getPortState());
    TEST_ASSERT_EQUAL(CC_NOT_CONNECTED, reg.getCC1PinState());
    TEST_ASSERT_EQUAL(CC_NOT_CONNECTED, reg.getCC2PinState());
    TEST_ASSERT_EQUAL(CC_NO_PD_CONNECTION, reg.getCCPinForPD());
}

void test_data_constructor_copies_buffer()
{
    const Buf buf(1, 5, 0, 0x61);
    TPS25751TypeCState reg(buf.b);

    TEST_ASSERT_NOT_EQUAL(buf.b, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(buf.b, reg.raw(), TYPEC_SIZE);
}

void test_explicit_length_constructor()
{
    const uint8_t shortData[2] = {1, 5};
    TPS25751TypeCState reg(shortData, sizeof(shortData));

    TEST_ASSERT_EQUAL_size_t(2, reg.size());
    TEST_ASSERT_FALSE(reg.isValidSize(TYPEC_SIZE));
    // Fields past the buffer decode to 0.
    TEST_ASSERT_EQUAL(CC_NOT_CONNECTED, reg.getCC2PinState());
    TEST_ASSERT_EQUAL(TYPEC_DISABLED, reg.getPortState());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751TypeCState reg(TPS25751Registers::Registers::TYPEC_STATE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(TYPEC_SIZE, reg.size());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751TypeCState reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL(TYPEC_DISABLED, reg.getPortState());
}

// --- Port state decode (bits 31:24) -----------------------------------------

void test_port_state_decodes_every_code()
{
    struct Case { uint8_t code; TypeCPortState expected; };
    const Case cases[] = {
        {0x00, TYPEC_DISABLED},
        {0x05, TYPEC_ERROR_RECOVERY},
        {0x24, TYPEC_UNATTACHED_ACCESSORY},
        {0x2B, TYPEC_ATTACH_WAIT_ACCESSORY},
        {0x45, TYPEC_TRY_SRC},
        {0x4E, TYPEC_TRY_WAIT_SNK},
        {0x4F, TYPEC_TRY_SNK},
        {0x50, TYPEC_TRY_WAIT_SRC},
        {0x60, TYPEC_ATTACHED_SRC},
        {0x61, TYPEC_ATTACHED_SNK},
        {0x62, TYPEC_AUDIO_ACCESSORY},
        {0x63, TYPEC_DEBUG_ACCESSORY},
        {0x64, TYPEC_ATTACH_WAIT_SRC},
        {0x65, TYPEC_ATTACH_WAIT_SNK},
        {0x66, TYPEC_UNATTACHED_SNK},
        {0x67, TYPEC_UNATTACHED_SRC},
    };

    for (const Case &c : cases) {
        const Buf buf(0, 0, 0, c.code);
        TPS25751TypeCState reg(buf.b);
        TEST_ASSERT_EQUAL_MESSAGE(c.expected, reg.getPortState(), "port state code");
        TEST_ASSERT_EQUAL_UINT8(c.code, static_cast<uint8_t>(reg.getPortState()));
    }
}

void test_port_state_undefined_code_falls_back_to_disabled()
{
    const Buf buf(0, 0, 0, 0x99);
    TPS25751TypeCState reg(buf.b);

    TEST_ASSERT_EQUAL(TYPEC_DISABLED, reg.getPortState());
}

// --- CC pin decode -----------------------------------------------------------

void test_cc_pin_states_decode_each_value()
{
    const CCPinState expected[] = {CC_NOT_CONNECTED,           CC_RA_DETECTED,
                                   CC_RD_DETECTED,             CC_USB_DEFAULT_ADVERTISEMENT,
                                   CC_1_5A_ADVERTISEMENT,      CC_3_0A_ADVERTISEMENT};

    for (uint8_t v = 0; v <= 5; v++) {
        const Buf buf(0, v, static_cast<uint8_t>(5 - v), 0);
        TPS25751TypeCState reg(buf.b);

        TEST_ASSERT_EQUAL(expected[v], reg.getCC1PinState());
        TEST_ASSERT_EQUAL(expected[5 - v], reg.getCC2PinState());
    }
}

void test_cc_pin_state_out_of_range_clamps_to_not_connected()
{
    const Buf buf(0, 6, 0xFF, 0);
    TPS25751TypeCState reg(buf.b);

    TEST_ASSERT_EQUAL(CC_NOT_CONNECTED, reg.getCC1PinState());
    TEST_ASSERT_EQUAL(CC_NOT_CONNECTED, reg.getCC2PinState());
}

void test_cc_pin_for_pd_decodes_each_value()
{
    TEST_ASSERT_EQUAL(CC_NO_PD_CONNECTION, TPS25751TypeCState(Buf(0, 0, 0, 0).b).getCCPinForPD());
    TEST_ASSERT_EQUAL(CC1_USED_FOR_PD, TPS25751TypeCState(Buf(1, 0, 0, 0).b).getCCPinForPD());
    TEST_ASSERT_EQUAL(CC2_USED_FOR_PD, TPS25751TypeCState(Buf(2, 0, 0, 0).b).getCCPinForPD());
}

void test_cc_pin_for_pd_out_of_range_clamps_to_no_connection()
{
    TEST_ASSERT_EQUAL(CC_NO_PD_CONNECTION, TPS25751TypeCState(Buf(3, 0, 0, 0).b).getCCPinForPD());
}

void test_fields_are_independent()
{
    // PD on CC2, CC1 = Rd (2), CC2 = 1.5 A advertisement (4), Attached.SNK
    const Buf buf(2, 2, 4, 0x61);
    TPS25751TypeCState reg(buf.b);

    TEST_ASSERT_EQUAL(CC2_USED_FOR_PD, reg.getCCPinForPD());
    TEST_ASSERT_EQUAL(CC_RD_DETECTED, reg.getCC1PinState());
    TEST_ASSERT_EQUAL(CC_1_5A_ADVERTISEMENT, reg.getCC2PinState());
    TEST_ASSERT_EQUAL(TYPEC_ATTACHED_SNK, reg.getPortState());
}

// --- toString ----------------------------------------------------------------

void test_to_string_port_state_all_values()
{
    TEST_ASSERT_EQUAL_STRING("Disabled", toString(TYPEC_DISABLED).c_str());
    TEST_ASSERT_EQUAL_STRING("ErrorRecovery", toString(TYPEC_ERROR_RECOVERY).c_str());
    TEST_ASSERT_EQUAL_STRING("Unattached.Accessory", toString(TYPEC_UNATTACHED_ACCESSORY).c_str());
    TEST_ASSERT_EQUAL_STRING("AttachWait.Accessory", toString(TYPEC_ATTACH_WAIT_ACCESSORY).c_str());
    TEST_ASSERT_EQUAL_STRING("Try.SRC", toString(TYPEC_TRY_SRC).c_str());
    TEST_ASSERT_EQUAL_STRING("TryWait.SNK", toString(TYPEC_TRY_WAIT_SNK).c_str());
    TEST_ASSERT_EQUAL_STRING("Try.SNK", toString(TYPEC_TRY_SNK).c_str());
    TEST_ASSERT_EQUAL_STRING("TryWait.SRC", toString(TYPEC_TRY_WAIT_SRC).c_str());
    TEST_ASSERT_EQUAL_STRING("Attached.SRC", toString(TYPEC_ATTACHED_SRC).c_str());
    TEST_ASSERT_EQUAL_STRING("Attached.SNK", toString(TYPEC_ATTACHED_SNK).c_str());
    TEST_ASSERT_EQUAL_STRING("AudioAccessory", toString(TYPEC_AUDIO_ACCESSORY).c_str());
    TEST_ASSERT_EQUAL_STRING("DebugAccessory", toString(TYPEC_DEBUG_ACCESSORY).c_str());
    TEST_ASSERT_EQUAL_STRING("AttachWait.SRC", toString(TYPEC_ATTACH_WAIT_SRC).c_str());
    TEST_ASSERT_EQUAL_STRING("AttachWait.SNK", toString(TYPEC_ATTACH_WAIT_SNK).c_str());
    TEST_ASSERT_EQUAL_STRING("Unattached.SNK", toString(TYPEC_UNATTACHED_SNK).c_str());
    TEST_ASSERT_EQUAL_STRING("Unattached.SRC", toString(TYPEC_UNATTACHED_SRC).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<TypeCPortState>(0x99)).c_str());
}

void test_to_string_cc_pin_state_all_values()
{
    TEST_ASSERT_EQUAL_STRING("Not connected", toString(CC_NOT_CONNECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("Ra detected (Source only)", toString(CC_RA_DETECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("Rd detected (Source only)", toString(CC_RD_DETECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("USB Default Advertisement detected (Sink only)",
                             toString(CC_USB_DEFAULT_ADVERTISEMENT).c_str());
    TEST_ASSERT_EQUAL_STRING("1.5A Advertisement detected (Sink only)",
                             toString(CC_1_5A_ADVERTISEMENT).c_str());
    TEST_ASSERT_EQUAL_STRING("3.0A Advertisement detected (Sink only)",
                             toString(CC_3_0A_ADVERTISEMENT).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<CCPinState>(9)).c_str());
}

void test_to_string_cc_pin_for_pd_all_values()
{
    TEST_ASSERT_EQUAL_STRING("Not connected", toString(CC_NO_PD_CONNECTION).c_str());
    TEST_ASSERT_EQUAL_STRING("CC1 is used for USB PD communication", toString(CC1_USED_FOR_PD).c_str());
    TEST_ASSERT_EQUAL_STRING("CC2 is used for USB PD communication", toString(CC2_USED_FOR_PD).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<CCPinForPD>(7)).c_str());
}

// --- Validation --------------------------------------------------------------

void test_all_ones_is_invalid_data()
{
    const uint8_t ones[TYPEC_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF};
    TPS25751TypeCState reg(ones);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_wrong_size_fails_semantic_validation()
{
    const uint8_t shortData[2] = {0, 0};
    TPS25751TypeCState reg(shortData, sizeof(shortData));

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_invalid_register_fails_semantic_validation()
{
    TPS25751TypeCState original;
    TPS25751TypeCState moved(static_cast<TPS25751TypeCState &&>(original));

    TEST_ASSERT_FALSE(original.isSemanticallyValid());
}

void test_semantic_valid_unattached()
{
    // All-zero: disabled, no CC pins, no PD pin.
    TEST_ASSERT_TRUE(TPS25751TypeCState().isSemanticallyValid());
}

void test_semantic_valid_pd_on_cc1_when_cc1_connected()
{
    const Buf buf(1, 5, 0, 0x61);
    TEST_ASSERT_TRUE(TPS25751TypeCState(buf.b).isSemanticallyValid());
}

void test_semantic_valid_pd_on_cc2_when_cc2_connected()
{
    const Buf buf(2, 0, 3, 0x61);
    TEST_ASSERT_TRUE(TPS25751TypeCState(buf.b).isSemanticallyValid());
}

void test_semantic_invalid_pd_on_cc1_when_cc1_not_connected()
{
    const Buf buf(1, 0, 5, 0x61);
    TEST_ASSERT_FALSE(TPS25751TypeCState(buf.b).isSemanticallyValid());
}

void test_semantic_invalid_pd_on_cc2_when_cc2_not_connected()
{
    const Buf buf(2, 5, 0, 0x61);
    TEST_ASSERT_FALSE(TPS25751TypeCState(buf.b).isSemanticallyValid());
}

// --- debugPrint --------------------------------------------------------------

void test_debug_print_lists_decoded_fields()
{
    const Buf buf(1, 5, 2, 0x61);
    const std::string out = captureDebugPrint(TPS25751TypeCState(buf.b));

    TEST_ASSERT_TRUE(contains(out, "=== Type C State Register (0x69) ==="));
    TEST_ASSERT_TRUE(contains(out, "Port State: Attached.SNK"));
    TEST_ASSERT_TRUE(contains(out, "CC1 Pin State: 3.0A Advertisement detected (Sink only)"));
    TEST_ASSERT_TRUE(contains(out, "CC2 Pin State: Rd detected (Source only)"));
    TEST_ASSERT_TRUE(contains(out, "CC Pin for PD: CC1 is used for USB PD communication"));
}

void test_debug_print_default_register()
{
    const std::string out = captureDebugPrint(TPS25751TypeCState());

    TEST_ASSERT_TRUE(contains(out, "Port State: Disabled"));
    TEST_ASSERT_TRUE(contains(out, "CC Pin for PD: Not connected"));
}

// --- Rule of Five ------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    const Buf buf(2, 0, 4, 0x60);
    TPS25751TypeCState original(buf.b);
    TPS25751TypeCState copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL(TYPEC_ATTACHED_SRC, copy.getPortState());
    TEST_ASSERT_EQUAL(CC_1_5A_ADVERTISEMENT, copy.getCC2PinState());
}

void test_copy_assignment_deep_copies()
{
    const Buf buf(1, 2, 0, 0x45);
    TPS25751TypeCState original(buf.b);
    TPS25751TypeCState target;
    target = original;

    TEST_ASSERT_NOT_EQUAL(original.raw(), target.raw());
    TEST_ASSERT_EQUAL(TYPEC_TRY_SRC, target.getPortState());
}

void test_move_constructor_transfers_ownership()
{
    const Buf buf(1, 5, 0, 0x61);
    TPS25751TypeCState original(buf.b);
    const uint8_t *raw = original.raw();

    TPS25751TypeCState moved(static_cast<TPS25751TypeCState &&>(original));

    TEST_ASSERT_EQUAL(raw, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL(TYPEC_ATTACHED_SNK, moved.getPortState());
}

void test_move_assignment_transfers_ownership()
{
    const Buf buf(1, 5, 0, 0x61);
    TPS25751TypeCState original(buf.b);
    const uint8_t *raw = original.raw();
    TPS25751TypeCState target;

    target = static_cast<TPS25751TypeCState &&>(original);

    TEST_ASSERT_EQUAL(raw, target.raw());
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL(CC1_USED_FOR_PD, target.getCCPinForPD());
}

// --- Runner ------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);

    RUN_TEST(test_port_state_decodes_every_code);
    RUN_TEST(test_port_state_undefined_code_falls_back_to_disabled);
    RUN_TEST(test_cc_pin_states_decode_each_value);
    RUN_TEST(test_cc_pin_state_out_of_range_clamps_to_not_connected);
    RUN_TEST(test_cc_pin_for_pd_decodes_each_value);
    RUN_TEST(test_cc_pin_for_pd_out_of_range_clamps_to_no_connection);
    RUN_TEST(test_fields_are_independent);

    RUN_TEST(test_to_string_port_state_all_values);
    RUN_TEST(test_to_string_cc_pin_state_all_values);
    RUN_TEST(test_to_string_cc_pin_for_pd_all_values);

    RUN_TEST(test_all_ones_is_invalid_data);
    RUN_TEST(test_wrong_size_fails_semantic_validation);
    RUN_TEST(test_invalid_register_fails_semantic_validation);
    RUN_TEST(test_semantic_valid_unattached);
    RUN_TEST(test_semantic_valid_pd_on_cc1_when_cc1_connected);
    RUN_TEST(test_semantic_valid_pd_on_cc2_when_cc2_connected);
    RUN_TEST(test_semantic_invalid_pd_on_cc1_when_cc1_not_connected);
    RUN_TEST(test_semantic_invalid_pd_on_cc2_when_cc2_not_connected);

    RUN_TEST(test_debug_print_lists_decoded_fields);
    RUN_TEST(test_debug_print_default_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
