/**
 * @file test_TPS25751Status.cpp
 * @brief Native (host-run) unit tests for TPS25751Status and the
 *        TPS25751Register bitfield-extraction base class.
 *
 * Run with: pio test -e native
 *
 * Test buffers are hand-packed from the STATUS (0x1A) field table in the
 * TPS25751 TRM (Table 3-10), cross-checked against the tps25751-docs MCP
 * register data:
 *   bit 0      Plug Present
 *   bits 3:1   Connection State
 *   bit 4      Plug Orientation
 *   bit 5      Port Role
 *   bit 6      Data Role
 *   bits 21:20 VBUS Status
 *   bits 23:22 USB Host Present
 *   bits 25:24 Acting as Legacy
 *   bit 27     BIST
 *   bit 30     SoC Ack Timeout
 */

#include <unity.h>
#include <string>
#include <cstdio>

#include "TPS25751Status.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t STATUS_SIZE = TPS25751Registers::Registers::STATUS.size;  // 5

// No plug, everything idle. All-zero is a legitimate STATUS value.
constexpr uint8_t DISCONNECTED_DATA[STATUS_SIZE] = {0x00, 0x00, 0x00, 0x00, 0x00};

// Plug present, Connection State 6 (connected, no Ra), plug on CC2,
// sink role, UFP, VBUS at vSafe5V, USB host present, BIST in progress,
// SoC ack timed out.
//   byte0 = 1 | (6 << 1) | (1 << 4)        = 0x1D
//   byte2 = (1 << 4) | (3 << 6)            = 0xD0  (bits 20, 22)
//   byte3 = (1 << 3) | (1 << 6)            = 0x48  (bits 27, 30)
constexpr uint8_t CONNECTED_SINK_DATA[STATUS_SIZE] = {0x1D, 0x00, 0xD0, 0x48, 0x00};

// Plug present, Connection State 7 (connected, Ra detected), plug on CC1,
// source role, DFP, VBUS within limits, no host, acting as legacy source.
//   byte0 = 1 | (7 << 1) | (1 << 5) | (1 << 6) = 0x6F
//   byte2 = (2 << 4)                            = 0x20  (bit 20)
//   byte3 = 2                                   = 0x02  (bit 24)
constexpr uint8_t CONNECTED_SOURCE_DATA[STATUS_SIZE] = {0x6F, 0x00, 0x20, 0x02, 0x00};

// All ones: hasValidData() treats this as a bus-error signature.
constexpr uint8_t ALL_ONES_DATA[STATUS_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751Status status;

    TEST_ASSERT_TRUE(status.isValid());
    TEST_ASSERT_EQUAL_size_t(STATUS_SIZE, status.size());
    TEST_ASSERT_TRUE(status.isValidSize(STATUS_SIZE));
    TEST_ASSERT_FALSE(status.plugPresent());
}

void test_data_constructor_copies_buffer()
{
    TPS25751Status status(CONNECTED_SINK_DATA);

    TEST_ASSERT_TRUE(status.isValid());
    TEST_ASSERT_EQUAL_size_t(STATUS_SIZE, status.size());
    TEST_ASSERT_NOT_EQUAL(CONNECTED_SINK_DATA, status.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(CONNECTED_SINK_DATA, status.raw(), STATUS_SIZE);
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751Status status(nullptr);

    TEST_ASSERT_TRUE(status.isValid());
    TEST_ASSERT_FALSE(status.plugPresent());
    TEST_ASSERT_EQUAL(DISCONNECTED, status.connectionState());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t shortData[3] = {0x1D, 0x00, 0xD0};
    TPS25751Status status(shortData, sizeof(shortData));

    TEST_ASSERT_TRUE(status.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(status.isValidSize(STATUS_SIZE));
    TEST_ASSERT_FALSE(status.isSemanticallyValid());
    // Fields beyond the short buffer decode to 0, not garbage.
    TEST_ASSERT_EQUAL(NOT_LEGACY, status.actingAsLegacy());
    TEST_ASSERT_FALSE(status.socAckTimeout());
}

// --- Data validation --------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    TPS25751Status status(DISCONNECTED_DATA);
    TEST_ASSERT_TRUE(status.hasValidData());
}

void test_all_ones_is_invalid_data()
{
    TPS25751Status status(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(status.hasValidData());
    TEST_ASSERT_FALSE(status.isSemanticallyValid());
}

// --- Field decode -----------------------------------------------------------

void test_disconnected_decode()
{
    TPS25751Status status(DISCONNECTED_DATA);

    TEST_ASSERT_FALSE(status.plugPresent());
    TEST_ASSERT_EQUAL(DISCONNECTED, status.connectionState());
    TEST_ASSERT_EQUAL(CC1, status.plugOrientation());
    TEST_ASSERT_EQUAL(PORT_SINK, status.portRole());
    TEST_ASSERT_EQUAL(UFP, status.dataRole());
    TEST_ASSERT_EQUAL(VBUS_AT_VSAFE0V, status.vbusStatus());
    TEST_ASSERT_EQUAL(USB_NO_HOST_PRESENT, status.usbHostPresent());
    TEST_ASSERT_EQUAL(NOT_LEGACY, status.actingAsLegacy());
    TEST_ASSERT_FALSE(status.bistActive());
    TEST_ASSERT_FALSE(status.socAckTimeout());
}

void test_connected_sink_decode()
{
    TPS25751Status status(CONNECTED_SINK_DATA);

    TEST_ASSERT_TRUE(status.plugPresent());
    TEST_ASSERT_EQUAL(CONNECTED_NO_RA_DETECTED, status.connectionState());
    TEST_ASSERT_EQUAL(CC2, status.plugOrientation());
    TEST_ASSERT_EQUAL(PORT_SINK, status.portRole());
    TEST_ASSERT_EQUAL(UFP, status.dataRole());
    TEST_ASSERT_EQUAL(VBUS_AT_VSAFE5V, status.vbusStatus());
    TEST_ASSERT_EQUAL(USB_HOST_PRESENT, status.usbHostPresent());
    TEST_ASSERT_EQUAL(NOT_LEGACY, status.actingAsLegacy());
    TEST_ASSERT_TRUE(status.bistActive());
    TEST_ASSERT_TRUE(status.socAckTimeout());
}

void test_connected_source_decode()
{
    TPS25751Status status(CONNECTED_SOURCE_DATA);

    TEST_ASSERT_TRUE(status.plugPresent());
    TEST_ASSERT_EQUAL(CONNECTED_RA_DETECTED, status.connectionState());
    TEST_ASSERT_EQUAL(CC1, status.plugOrientation());
    TEST_ASSERT_EQUAL(PORT_SOURCE, status.portRole());
    TEST_ASSERT_EQUAL(DFP, status.dataRole());
    TEST_ASSERT_EQUAL(VBUS_WITHIN_LIMITS, status.vbusStatus());
    TEST_ASSERT_EQUAL(USB_NO_HOST_PRESENT, status.usbHostPresent());
    TEST_ASSERT_EQUAL(LEGACY_SOURCE, status.actingAsLegacy());
    TEST_ASSERT_FALSE(status.bistActive());
    TEST_ASSERT_FALSE(status.socAckTimeout());
}

// --- Semantic validation ----------------------------------------------------

// Connection State values redefined in Rev. B (SLVUCR8B p.25). Byte 0:
// bit 0 Plug Present, bits 3:1 Connection State.
void test_connection_state_2h_is_corrosion_mitigation()
{
    const uint8_t data[STATUS_SIZE] = {0x05, 0x00, 0x00, 0x00, 0x00};  // plug, 2h
    TPS25751Status status(data);

    TEST_ASSERT_TRUE(status.plugPresent());
    TEST_ASSERT_EQUAL(CORROSION_MITIGATION, status.connectionState());
    TEST_ASSERT_EQUAL_STRING("Corrosion Mitigation (Ra/Ra)", toString(status.connectionState()).c_str());
}

void test_connection_state_5h_is_debug_rp_rp()
{
    const uint8_t data[STATUS_SIZE] = {0x0B, 0x00, 0x00, 0x00, 0x00};  // plug, 5h
    TPS25751Status status(data);

    TEST_ASSERT_TRUE(status.plugPresent());
    TEST_ASSERT_EQUAL(DEBUG_ACCESSORY_RP_RP, status.connectionState());
    TEST_ASSERT_EQUAL_STRING("Debug connection (Rp/Rp)", toString(status.connectionState()).c_str());
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void test_deprecated_connection_state_aliases_match_rev_b_names()
{
    // Rev. A names keep their old values so existing sketches decode the same.
    TEST_ASSERT_EQUAL_INT(CORROSION_MITIGATION, AUDIO_ACCESSORY);
    TEST_ASSERT_EQUAL_INT(DEBUG_ACCESSORY_RP_RP, RESERVED);
}
#pragma GCC diagnostic pop

void test_connection_state_1h_string()
{
    TEST_ASSERT_EQUAL_STRING("Port is disabled", toString(PORT_DISABLED).c_str());
}

void test_semantic_valid_states()
{
    TEST_ASSERT_TRUE(TPS25751Status(DISCONNECTED_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Status(CONNECTED_SINK_DATA).isSemanticallyValid());
    TEST_ASSERT_TRUE(TPS25751Status(CONNECTED_SOURCE_DATA).isSemanticallyValid());
}

void test_semantic_plug_present_but_disconnected()
{
    const uint8_t data[STATUS_SIZE] = {0x01, 0x00, 0x00, 0x00, 0x00};
    TEST_ASSERT_FALSE(TPS25751Status(data).isSemanticallyValid());
}

void test_semantic_connected_but_no_plug()
{
    const uint8_t data[STATUS_SIZE] = {0x0C, 0x00, 0x00, 0x00, 0x00};  // state 6, no plug
    TEST_ASSERT_FALSE(TPS25751Status(data).isSemanticallyValid());
}

void test_semantic_vbus_up_while_disconnected()
{
    const uint8_t data[STATUS_SIZE] = {0x00, 0x00, 0x10, 0x00, 0x00};  // vSafe5V, no plug
    TEST_ASSERT_FALSE(TPS25751Status(data).isSemanticallyValid());
}

// --- Rule of Five -----------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751Status original(CONNECTED_SINK_DATA);
    TPS25751Status copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), STATUS_SIZE);
    TEST_ASSERT_EQUAL(original.connectionState(), copy.connectionState());
}

void test_copy_assignment()
{
    TPS25751Status original(CONNECTED_SOURCE_DATA);
    TPS25751Status copy;

    copy = original;

    TEST_ASSERT_EQUAL(CONNECTED_RA_DETECTED, copy.connectionState());
    TEST_ASSERT_EQUAL(PORT_SOURCE, copy.portRole());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751Status original(CONNECTED_SINK_DATA);
    const uint8_t *buf = original.raw();

    TPS25751Status moved(static_cast<TPS25751Status &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL(CONNECTED_NO_RA_DETECTED, moved.connectionState());
}

void test_move_assignment_transfers_ownership()
{
    TPS25751Status original(CONNECTED_SOURCE_DATA);
    TPS25751Status moved;

    moved = static_cast<TPS25751Status &&>(original);

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL(DFP, moved.dataRole());
}

// --- Base-class extractBits (exposed publicly via PIO_UNIT_TESTING) ---------

void test_extract_bits_cross_byte_boundary()
{
    const uint8_t data[] = {0xAB, 0xCD};
    // bits 4..11 = high nibble of 0xAB plus low nibble of 0xCD = 0xDA
    TEST_ASSERT_EQUAL_UINT8(0xDA, TPS25751Register::extractBits(data, sizeof(data), 4, 8));
}

void test_extract_bits_rejects_bad_arguments()
{
    const uint8_t data[] = {0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT8(0, TPS25751Register::extractBits(nullptr, 2, 0, 4));
    TEST_ASSERT_EQUAL_UINT8(0, TPS25751Register::extractBits(data, sizeof(data), 0, 0));
    TEST_ASSERT_EQUAL_UINT8(0, TPS25751Register::extractBits(data, sizeof(data), 0, 9));
    // Field would run past the end of the buffer.
    TEST_ASSERT_EQUAL_UINT8(0, TPS25751Register::extractBits(data, sizeof(data), 12, 8));
}

void test_extract_bits16_unaligned()
{
    const uint8_t data[] = {0x21, 0x43, 0x65};
    // bits 4..19 of little-endian 0x654321 = 0x5432
    TEST_ASSERT_EQUAL_UINT16(0x5432, TPS25751Register::extractBits16(data, sizeof(data), 4, 16));
}

void test_extract_bits32_full_word()
{
    const uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, TPS25751Register::extractBits32(data, sizeof(data), 0, 32));
}

void test_extract_bits64_wide_field()
{
    const uint8_t data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    TEST_ASSERT_EQUAL_UINT64(0x5544332211ull, TPS25751Register::extractBits64(data, sizeof(data), 0, 40));
}


// --- toString of every enumerator ------------------------------------------

void test_to_string_connection_state_all_values()
{
    TEST_ASSERT_EQUAL_STRING("No connection", toString(DISCONNECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("Port is disabled", toString(PORT_DISABLED).c_str());
    TEST_ASSERT_EQUAL_STRING("Corrosion Mitigation (Ra/Ra)", toString(CORROSION_MITIGATION).c_str());
    TEST_ASSERT_EQUAL_STRING("Debug connection (Rd/Rd)", toString(DEBUG_ACCESSORY).c_str());
    TEST_ASSERT_EQUAL_STRING("No connection - Ra detected (Ra but no Rd)",
                             toString(NO_CONNECTION_RA_DETECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("Debug connection (Rp/Rp)", toString(DEBUG_ACCESSORY_RP_RP).c_str());
    TEST_ASSERT_EQUAL_STRING("Connection present - no Ra detected", toString(CONNECTED_NO_RA_DETECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("Connection present - Ra detected", toString(CONNECTED_RA_DETECTED).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<ConnectionState>(8)).c_str());
}

void test_to_string_vbus_status_all_values()
{
    TEST_ASSERT_EQUAL_STRING("At vSafe0V (less than 0.8V)", toString(VBUS_AT_VSAFE0V).c_str());
    TEST_ASSERT_EQUAL_STRING("At vSafe5V (4.75V to 5.5V)", toString(VBUS_AT_VSAFE5V).c_str());
    TEST_ASSERT_EQUAL_STRING("Within expected limits", toString(VBUS_WITHIN_LIMITS).c_str());
    TEST_ASSERT_EQUAL_STRING("Not within any of the other specified ranges",
                             toString(VBUS_NOT_WITHIN_LIMITS).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<VbusStatus>(4)).c_str());
}

void test_to_string_usb_host_presence_all_values()
{
    TEST_ASSERT_EQUAL_STRING("No host present", toString(USB_NO_HOST_PRESENT).c_str());
    TEST_ASSERT_EQUAL_STRING("Attached source is not data capable",
                             toString(USB_HOST_SOURCE_NOT_DATA_CAPABLE).c_str());
    TEST_ASSERT_EQUAL_STRING("Attached source is not USB PD capable",
                             toString(USB_HOST_SOURCE_NOT_PD_CAPABLE).c_str());
    TEST_ASSERT_EQUAL_STRING("Host Present", toString(USB_HOST_PRESENT).c_str());
    TEST_ASSERT_EQUAL_STRING("Reserved", toString(static_cast<UsbHostPresence>(4)).c_str());
}

void test_to_string_acting_as_legacy_all_values()
{
    TEST_ASSERT_EQUAL_STRING("PD Controller is not in a legacy (non PD) mode", toString(NOT_LEGACY).c_str());
    TEST_ASSERT_EQUAL_STRING("PD Controller is acting like a legacy sink", toString(LEGACY_SINK).c_str());
    TEST_ASSERT_EQUAL_STRING("PD Controller is acting like a legacy source", toString(LEGACY_SOURCE).c_str());
    TEST_ASSERT_EQUAL_STRING("Acting as legacy sink due to dead-battery",
                             toString(LEGACY_SINK_DEAD_BATTERY).c_str());
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<ActingAsLegacy>(4)).c_str());
}

void test_to_string_role_and_orientation()
{
    TEST_ASSERT_EQUAL_STRING("PD Controller is Source (CCx pull-up active)", toString(PORT_SOURCE).c_str());
    TEST_ASSERT_EQUAL_STRING("PD Controller is in the Sink role", toString(PORT_SINK).c_str());
    TEST_ASSERT_EQUAL_STRING("Upward-facing port (UFP)", toString(UFP).c_str());
    TEST_ASSERT_EQUAL_STRING("Downward-facing port (DFP)", toString(DFP).c_str());
    TEST_ASSERT_EQUAL_STRING("Upside-up orientation (plug CC on CC1)", toString(CC1).c_str());
    TEST_ASSERT_EQUAL_STRING("Upside-down orientation (plug CC on CC2)", toString(CC2).c_str());
}

// --- Decode of every enumerator value, one field at a time -----------------

void test_vbus_status_all_codes()
{
    // VBUS Status is bits 21:20 (byte 2 bits 5:4).
    const VbusStatus expected[4] = {VBUS_AT_VSAFE0V, VBUS_AT_VSAFE5V, VBUS_WITHIN_LIMITS, VBUS_NOT_WITHIN_LIMITS};
    for (uint8_t code = 0; code < 4; ++code) {
        const uint8_t data[STATUS_SIZE] = {0x00, 0x00, static_cast<uint8_t>(code << 4), 0x00, 0x00};
        TEST_ASSERT_EQUAL(expected[code], TPS25751Status(data).vbusStatus());
    }
}

void test_usb_host_presence_all_codes()
{
    // USB Host Present is bits 23:22 (byte 2 bits 7:6).
    const UsbHostPresence expected[4] = {USB_NO_HOST_PRESENT, USB_HOST_SOURCE_NOT_DATA_CAPABLE,
                                         USB_HOST_SOURCE_NOT_PD_CAPABLE, USB_HOST_PRESENT};
    for (uint8_t code = 0; code < 4; ++code) {
        const uint8_t data[STATUS_SIZE] = {0x00, 0x00, static_cast<uint8_t>(code << 6), 0x00, 0x00};
        TEST_ASSERT_EQUAL(expected[code], TPS25751Status(data).usbHostPresent());
    }
}

void test_acting_as_legacy_all_codes()
{
    // Acting As Legacy is bits 25:24 (byte 3 bits 1:0).
    const ActingAsLegacy expected[4] = {NOT_LEGACY, LEGACY_SINK, LEGACY_SOURCE, LEGACY_SINK_DEAD_BATTERY};
    for (uint8_t code = 0; code < 4; ++code) {
        const uint8_t data[STATUS_SIZE] = {0x00, 0x00, 0x00, code, 0x00};
        TEST_ASSERT_EQUAL(expected[code], TPS25751Status(data).actingAsLegacy());
    }
}

void test_connection_state_all_codes()
{
    const ConnectionState expected[8] = {DISCONNECTED, PORT_DISABLED, CORROSION_MITIGATION, DEBUG_ACCESSORY,
                                         NO_CONNECTION_RA_DETECTED, DEBUG_ACCESSORY_RP_RP,
                                         CONNECTED_NO_RA_DETECTED, CONNECTED_RA_DETECTED};
    for (uint8_t code = 0; code < 8; ++code) {
        const uint8_t data[STATUS_SIZE] = {static_cast<uint8_t>(code << 1), 0x00, 0x00, 0x00, 0x00};
        TEST_ASSERT_EQUAL(expected[code], TPS25751Status(data).connectionState());
    }
}

// --- Semantic validation: remaining branches -------------------------------

void test_semantic_host_present_while_disconnected()
{
    // USB Host Present = 1 (source not data capable) with Connection State 0 and no plug.
    const uint8_t data[STATUS_SIZE] = {0x00, 0x00, 0x40, 0x00, 0x00};
    TEST_ASSERT_FALSE(TPS25751Status(data).isSemanticallyValid());
}

void test_semantic_vbus_within_limits_while_disconnected()
{
    const uint8_t data[STATUS_SIZE] = {0x00, 0x00, 0x20, 0x00, 0x00};  // VBUS code 2
    TEST_ASSERT_FALSE(TPS25751Status(data).isSemanticallyValid());
}

void test_semantic_every_connected_state_with_plug_is_valid()
{
    for (uint8_t code = 1; code < 8; ++code) {
        const uint8_t data[STATUS_SIZE] = {static_cast<uint8_t>((code << 1) | 1), 0x00, 0x00, 0x00, 0x00};
        TEST_ASSERT_TRUE(TPS25751Status(data).isSemanticallyValid());
    }
}

// --- debugPrint --------------------------------------------------------------

namespace {

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void test_debug_print_disconnected()
{
    const std::string out = captureDebugPrint(TPS25751Status(DISCONNECTED_DATA));

    TEST_ASSERT_TRUE(contains(out, "=== Status Register (0x1A) ==="));
    TEST_ASSERT_TRUE(contains(out, "  Plug Present: No\n"));
    TEST_ASSERT_TRUE(contains(out, "  Connection State: No connection\n"));
    TEST_ASSERT_TRUE(contains(out, "  Plug Orientation: Upside-up orientation (plug CC on CC1)\n"));
    TEST_ASSERT_TRUE(contains(out, "  Port Role: PD Controller is in the Sink role\n"));
    TEST_ASSERT_TRUE(contains(out, "  Data Role: Upward-facing port (UFP)\n"));
    TEST_ASSERT_TRUE(contains(out, "  VBUS Status: At vSafe0V (less than 0.8V)\n"));
    TEST_ASSERT_TRUE(contains(out, "  USB Host Present: No host present\n"));
    TEST_ASSERT_TRUE(contains(out, "  Acting As Legacy: PD Controller is not in a legacy (non PD) mode\n"));
    TEST_ASSERT_TRUE(contains(out, "  BIST Active: No BIST in progress\n"));
    TEST_ASSERT_TRUE(contains(out, "  SoC ACK Timeout: SoC has not responded timely\n"));
}

void test_debug_print_connected_sink()
{
    const std::string out = captureDebugPrint(TPS25751Status(CONNECTED_SINK_DATA));

    TEST_ASSERT_TRUE(contains(out, "  Plug Present: Yes\n"));
    TEST_ASSERT_TRUE(contains(out, "  Connection State: Connection present - no Ra detected\n"));
    TEST_ASSERT_TRUE(contains(out, "  Plug Orientation: Upside-down orientation (plug CC on CC2)\n"));
    TEST_ASSERT_TRUE(contains(out, "  Port Role: PD Controller is in the Sink role\n"));
    TEST_ASSERT_TRUE(contains(out, "  VBUS Status: At vSafe5V (4.75V to 5.5V)\n"));
    TEST_ASSERT_TRUE(contains(out, "  USB Host Present: Host Present\n"));
    TEST_ASSERT_TRUE(contains(out, "  BIST Active: BIST in progress\n"));
    TEST_ASSERT_TRUE(contains(out, "  SoC ACK Timeout: SoC has responded timely\n"));
}

void test_debug_print_connected_source()
{
    const std::string out = captureDebugPrint(TPS25751Status(CONNECTED_SOURCE_DATA));

    TEST_ASSERT_TRUE(contains(out, "  Connection State: Connection present - Ra detected\n"));
    TEST_ASSERT_TRUE(contains(out, "  Port Role: PD Controller is Source (CCx pull-up active)\n"));
    TEST_ASSERT_TRUE(contains(out, "  Data Role: Downward-facing port (DFP)\n"));
    TEST_ASSERT_TRUE(contains(out, "  VBUS Status: Within expected limits\n"));
    TEST_ASSERT_TRUE(contains(out, "  Acting As Legacy: PD Controller is acting like a legacy source\n"));
}

// --- Runner -----------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);

    RUN_TEST(test_disconnected_decode);
    RUN_TEST(test_connected_sink_decode);
    RUN_TEST(test_connected_source_decode);

    RUN_TEST(test_connection_state_2h_is_corrosion_mitigation);
    RUN_TEST(test_connection_state_5h_is_debug_rp_rp);
    RUN_TEST(test_connection_state_1h_string);
    RUN_TEST(test_deprecated_connection_state_aliases_match_rev_b_names);

    RUN_TEST(test_semantic_valid_states);
    RUN_TEST(test_semantic_plug_present_but_disconnected);
    RUN_TEST(test_semantic_connected_but_no_plug);
    RUN_TEST(test_semantic_vbus_up_while_disconnected);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_copy_assignment);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_move_assignment_transfers_ownership);

    RUN_TEST(test_extract_bits_cross_byte_boundary);
    RUN_TEST(test_extract_bits_rejects_bad_arguments);
    RUN_TEST(test_extract_bits16_unaligned);
    RUN_TEST(test_extract_bits32_full_word);
    RUN_TEST(test_extract_bits64_wide_field);

    RUN_TEST(test_to_string_connection_state_all_values);
    RUN_TEST(test_to_string_vbus_status_all_values);
    RUN_TEST(test_to_string_usb_host_presence_all_values);
    RUN_TEST(test_to_string_acting_as_legacy_all_values);
    RUN_TEST(test_to_string_role_and_orientation);
    RUN_TEST(test_vbus_status_all_codes);
    RUN_TEST(test_usb_host_presence_all_codes);
    RUN_TEST(test_acting_as_legacy_all_codes);
    RUN_TEST(test_connection_state_all_codes);
    RUN_TEST(test_semantic_host_present_while_disconnected);
    RUN_TEST(test_semantic_vbus_within_limits_while_disconnected);
    RUN_TEST(test_semantic_every_connected_state_with_plug_is_valid);
    RUN_TEST(test_debug_print_disconnected);
    RUN_TEST(test_debug_print_connected_sink);
    RUN_TEST(test_debug_print_connected_source);

    return UNITY_END();
}
