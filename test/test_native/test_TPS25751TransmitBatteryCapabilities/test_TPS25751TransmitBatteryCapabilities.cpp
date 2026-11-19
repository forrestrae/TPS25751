/**
 * @file test_TPS25751TransmitBatteryCapabilities.cpp
 * @brief Native (host-run) unit tests for TPS25751TransmitBatteryCapabilities.
 *
 * Run with: pio test -e native
 *
 * TX_BATTERY_CAPABILITIES (0x7D) is a 36-byte R/W register, reset all zero. It is
 * four 9-byte entries, entry i at byte 9 * i. Within an entry (all little-endian):
 *   +0..1 VID   +2..3 PID   +4..5 Design Capacity   +6..7 Last Full Charge
 *   Capacity   +8 Battery Type
 * Battery 3 starts at byte 27 and ends at byte 35 (bits 287:216), so its last
 * fields sit past bit 255; the class reaches them through the base-class
 * extractBits*() and setBits(), which take a 16-bit bit position (ADR-012). The raw accessors return field bits unchanged; the typed accessors decode
 * them per USB PD R3.3 Table 6.64 (see the Typed decode section).
 *
 * Distinct fixture: byte k = 0x40 + k for k = 0..35. Expected values, computed
 * by hand from the byte map (low byte first in the buffer):
 *   battery 0, bytes 0-8   (40 41 42 43 44 45 46 47 48)
 *     VID 0x4140, PID 0x4342, Design 0x4544, LastFull 0x4746, Type 0x48
 *   battery 1, bytes 9-17  (49 4A 4B 4C 4D 4E 4F 50 51)
 *     VID 0x4A49, PID 0x4C4B, Design 0x4E4D, LastFull 0x504F, Type 0x51
 *   battery 2, bytes 18-26 (52 53 54 55 56 57 58 59 5A)
 *     VID 0x5352, PID 0x5554, Design 0x5756, LastFull 0x5958, Type 0x5A
 *   battery 3, bytes 27-35 (5B 5C 5D 5E 5F 60 61 62 63)
 *     VID 0x5C5B, PID 0x5E5D, Design 0x605F, LastFull 0x6261, Type 0x63
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "TPS25751TransmitBatteryCapabilities.h"

namespace {

using Caps = TPS25751TransmitBatteryCapabilities;

constexpr size_t CAPS_SIZE = TPS25751Registers::Registers::TRANSMIT_BATTERY_CAPABILITIES.size;  // 36

struct Expected
{
    uint16_t vid;
    uint16_t pid;
    uint16_t design;
    uint16_t lastFull;
    uint8_t type;
};

const Expected kExpected[4] = {
    {0x4140, 0x4342, 0x4544, 0x4746, 0x48},
    {0x4A49, 0x4C4B, 0x4E4D, 0x504F, 0x51},
    {0x5352, 0x5554, 0x5756, 0x5958, 0x5A},
    {0x5C5B, 0x5E5D, 0x605F, 0x6261, 0x63},
};

uint8_t g_distinct[CAPS_SIZE];      // byte k = 0x40 + k
uint8_t g_zero[CAPS_SIZE];          // all zero (the reset value)
uint8_t g_allOnes[CAPS_SIZE];       // 0xFF bus-error signature
uint8_t g_battery3Only[CAPS_SIZE];  // only bytes 27..35 nonzero

void buildFixtures()
{
    for (size_t k = 0; k < CAPS_SIZE; k++)
    {
        g_distinct[k] = static_cast<uint8_t>(0x40 + k);
        g_zero[k] = 0;
        g_allOnes[k] = 0xFF;
        g_battery3Only[k] = 0;
    }
    // battery 3: VID bytes A1 B2 -> 0xB2A1, PID C3 D4 -> 0xD4C3,
    // Design E5 F6 -> 0xF6E5, LastFull 17 28 -> 0x2817, Type 39
    g_battery3Only[27] = 0xA1;
    g_battery3Only[28] = 0xB2;
    g_battery3Only[29] = 0xC3;
    g_battery3Only[30] = 0xD4;
    g_battery3Only[31] = 0xE5;
    g_battery3Only[32] = 0xF6;
    g_battery3Only[33] = 0x17;
    g_battery3Only[34] = 0x28;
    g_battery3Only[35] = 0x39;
}

std::string captureDebugPrint(const Caps &reg)
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

void assertAllAccessorsZero(const Caps &reg, const char *msg)
{
    for (uint8_t battery = 0; battery < 4; battery++)
    {
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, reg.vid(battery), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, reg.pid(battery), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, reg.batteryDesignCapacity(battery), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, reg.batteryLastFullChargeCapacity(battery), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, reg.batteryType(battery), msg);
    }
}

}  // namespace

void setUp() { buildFixtures(); }
void tearDown() {}

void test_address_identity()
{
    TEST_ASSERT_TRUE(Caps::kAddress == TPS25751Registers::Address::TRANSMIT_BATTERY_CAPABILITIES);
    TEST_ASSERT_EQUAL_UINT8(0x7D, static_cast<uint8_t>(Caps::kAddress));
    TEST_ASSERT_EQUAL_UINT8(36, TPS25751Registers::getRegisterSize(Caps::kAddress));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(Caps::kAddress));
}

void test_default_constructor_allocates_zeroed_buffer()
{
    Caps reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_zero, reg.raw(), CAPS_SIZE);
}

void test_data_constructor_copies_buffer()
{
    Caps reg(g_distinct);

    TEST_ASSERT_NOT_EQUAL(g_distinct, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, reg.raw(), CAPS_SIZE);
}

void test_explicit_length_constructor()
{
    Caps reg(g_distinct, CAPS_SIZE);

    TEST_ASSERT_TRUE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_EQUAL_HEX16(0x4140, reg.vid(0));
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    Caps reg(TPS25751Registers::Registers::TRANSMIT_BATTERY_CAPABILITIES);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(CAPS_SIZE, reg.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_zero, reg.raw(), CAPS_SIZE);
}

void test_wrong_size_fails_size_validation()
{
    Caps reg(g_distinct, 4);

    TEST_ASSERT_FALSE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_ones_rejected_at_data_tier()
{
    Caps reg(g_allOnes);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_zero_reset_is_valid_and_decodes_to_zero()
{
    Caps reg(g_zero);

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    assertAllAccessorsZero(reg, "reset");
}

void test_distinct_fixture_is_valid()
{
    TEST_ASSERT_TRUE(Caps(g_distinct).isSemanticallyValid());
}

void test_distinct_fixture_decodes_all_twenty_fields()
{
    Caps reg(g_distinct);

    // Battery 0
    TEST_ASSERT_EQUAL_HEX16(0x4140, reg.vid(0));
    TEST_ASSERT_EQUAL_HEX16(0x4342, reg.pid(0));
    TEST_ASSERT_EQUAL_HEX16(0x4544, reg.batteryDesignCapacity(0));
    TEST_ASSERT_EQUAL_HEX16(0x4746, reg.batteryLastFullChargeCapacity(0));
    TEST_ASSERT_EQUAL_HEX8(0x48, reg.batteryType(0));

    // Battery 1
    TEST_ASSERT_EQUAL_HEX16(0x4A49, reg.vid(1));
    TEST_ASSERT_EQUAL_HEX16(0x4C4B, reg.pid(1));
    TEST_ASSERT_EQUAL_HEX16(0x4E4D, reg.batteryDesignCapacity(1));
    TEST_ASSERT_EQUAL_HEX16(0x504F, reg.batteryLastFullChargeCapacity(1));
    TEST_ASSERT_EQUAL_HEX8(0x51, reg.batteryType(1));

    // Battery 2
    TEST_ASSERT_EQUAL_HEX16(0x5352, reg.vid(2));
    TEST_ASSERT_EQUAL_HEX16(0x5554, reg.pid(2));
    TEST_ASSERT_EQUAL_HEX16(0x5756, reg.batteryDesignCapacity(2));
    TEST_ASSERT_EQUAL_HEX16(0x5958, reg.batteryLastFullChargeCapacity(2));
    TEST_ASSERT_EQUAL_HEX8(0x5A, reg.batteryType(2));

    // Battery 3 (bytes 27 to 35, past bit 255)
    TEST_ASSERT_EQUAL_HEX16(0x5C5B, reg.vid(3));
    TEST_ASSERT_EQUAL_HEX16(0x5E5D, reg.pid(3));
    TEST_ASSERT_EQUAL_HEX16(0x605F, reg.batteryDesignCapacity(3));
    TEST_ASSERT_EQUAL_HEX16(0x6261, reg.batteryLastFullChargeCapacity(3));
    TEST_ASSERT_EQUAL_HEX8(0x63, reg.batteryType(3));
}

void test_battery_3_decodes_from_bytes_27_to_35()
{
    Caps reg(g_battery3Only);

    TEST_ASSERT_EQUAL_HEX16(0xB2A1, reg.vid(3));                          // bytes 27-28
    TEST_ASSERT_EQUAL_HEX16(0xD4C3, reg.pid(3));                          // bytes 29-30
    TEST_ASSERT_EQUAL_HEX16(0xF6E5, reg.batteryDesignCapacity(3));        // bytes 31-32
    TEST_ASSERT_EQUAL_HEX16(0x2817, reg.batteryLastFullChargeCapacity(3));  // bytes 33-34
    TEST_ASSERT_EQUAL_HEX8(0x39, reg.batteryType(3));                     // byte 35

    // Batteries 0 to 2 read 0: no field of battery 3 aliases a lower entry.
    for (uint8_t battery = 0; battery < 3; battery++)
    {
        TEST_ASSERT_EQUAL_UINT16(0, reg.vid(battery));
        TEST_ASSERT_EQUAL_UINT16(0, reg.pid(battery));
        TEST_ASSERT_EQUAL_UINT16(0, reg.batteryDesignCapacity(battery));
        TEST_ASSERT_EQUAL_UINT16(0, reg.batteryLastFullChargeCapacity(battery));
        TEST_ASSERT_EQUAL_UINT8(0, reg.batteryType(battery));
    }
}

void test_battery_3_setters_leave_battery_2_alone()
{
    Caps reg(g_distinct);

    reg.setVid(3, 0x1122);
    reg.setPid(3, 0x3344);
    reg.setBatteryDesignCapacity(3, 0x5566);
    reg.setBatteryLastFullChargeCapacity(3, 0x7788);
    reg.setBatteryType(3, 0x99);

    // Bytes 0 to 26, including byte 26 (battery 2's Battery Type), are unchanged.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, reg.raw(), 27);
    TEST_ASSERT_EQUAL_HEX8(0x5A, reg.raw()[26]);

    // Bytes 27 to 35 hold the new values, low byte first.
    const uint8_t expectedTail[9] = {0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77, 0x99};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedTail, &reg.raw()[27], 9);

    TEST_ASSERT_EQUAL_HEX16(0x1122, reg.vid(3));
    TEST_ASSERT_EQUAL_HEX16(0x3344, reg.pid(3));
    TEST_ASSERT_EQUAL_HEX16(0x5566, reg.batteryDesignCapacity(3));
    TEST_ASSERT_EQUAL_HEX16(0x7788, reg.batteryLastFullChargeCapacity(3));
    TEST_ASSERT_EQUAL_HEX8(0x99, reg.batteryType(3));
}

void test_per_entry_round_trip_changes_only_that_entry()
{
    // Start from the distinct fixture, overwrite every field of one entry, and
    // check that only that entry's 9 bytes differ from the original.
    for (uint8_t battery = 0; battery < 4; battery++)
    {
        Caps reg(g_distinct);

        reg.setVid(battery, 0xA1B2);
        reg.setPid(battery, 0xC3D4);
        reg.setBatteryDesignCapacity(battery, 0xE5F6);
        reg.setBatteryLastFullChargeCapacity(battery, 0x0718);
        reg.setBatteryType(battery, 0x29);

        uint8_t expected[CAPS_SIZE];
        memcpy(expected, g_distinct, CAPS_SIZE);
        const size_t base = 9u * battery;
        const uint8_t entry[9] = {0xB2, 0xA1, 0xD4, 0xC3, 0xF6, 0xE5, 0x18, 0x07, 0x29};
        memcpy(&expected[base], entry, 9);

        TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, reg.raw(), CAPS_SIZE, "entry round trip");

        TEST_ASSERT_EQUAL_HEX16(0xA1B2, reg.vid(battery));
        TEST_ASSERT_EQUAL_HEX16(0xC3D4, reg.pid(battery));
        TEST_ASSERT_EQUAL_HEX16(0xE5F6, reg.batteryDesignCapacity(battery));
        TEST_ASSERT_EQUAL_HEX16(0x0718, reg.batteryLastFullChargeCapacity(battery));
        TEST_ASSERT_EQUAL_HEX8(0x29, reg.batteryType(battery));
    }
}

void test_setters_are_little_endian()
{
    Caps reg;

    reg.setVid(0, 0xBEEF);

    TEST_ASSERT_EQUAL_HEX8(0xEF, reg.raw()[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, reg.raw()[1]);
}

void test_out_of_range_index_reads_zero()
{
    Caps reg(g_distinct);

    TEST_ASSERT_EQUAL_UINT16(0, reg.vid(4));
    TEST_ASSERT_EQUAL_UINT16(0, reg.pid(4));
    TEST_ASSERT_EQUAL_UINT16(0, reg.batteryDesignCapacity(4));
    TEST_ASSERT_EQUAL_UINT16(0, reg.batteryLastFullChargeCapacity(4));
    TEST_ASSERT_EQUAL_UINT8(0, reg.batteryType(4));

    TEST_ASSERT_EQUAL_UINT16(0, reg.vid(255));
    TEST_ASSERT_EQUAL_UINT8(0, reg.batteryType(255));
}

void test_out_of_range_index_setters_leave_buffer_unchanged()
{
    Caps reg(g_distinct);

    reg.setVid(4, 0xEEEE);
    reg.setPid(4, 0xEEEE);
    reg.setBatteryDesignCapacity(4, 0xEEEE);
    reg.setBatteryLastFullChargeCapacity(4, 0xEEEE);
    reg.setBatteryType(4, 0xEE);
    reg.setVid(255, 0xEEEE);
    reg.setBatteryType(255, 0xEE);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, reg.raw(), CAPS_SIZE);
}

void test_short_buffer_battery_3_reads_are_bounds_checked()
{
    // 30 bytes: byte indexes 0..29. vid(3) at 27-28 is in range; pid(3) at
    // 29-30 needs byte 30, which does not exist; the rest start past the end.
    Caps reg(g_distinct, 30);

    TEST_ASSERT_EQUAL_size_t(30, reg.size());
    TEST_ASSERT_EQUAL_HEX16(0x5C5B, reg.vid(3));
    TEST_ASSERT_EQUAL_UINT16(0, reg.pid(3));
    TEST_ASSERT_EQUAL_UINT16(0, reg.batteryDesignCapacity(3));
    TEST_ASSERT_EQUAL_UINT16(0, reg.batteryLastFullChargeCapacity(3));
    TEST_ASSERT_EQUAL_UINT8(0, reg.batteryType(3));

    // Battery 2's last byte (26) is still readable.
    TEST_ASSERT_EQUAL_HEX8(0x5A, reg.batteryType(2));
}

void test_sixteen_bit_read_needs_both_bytes()
{
    // 28 bytes: byte 27 exists but byte 28 does not, so vid(3) reads 0 rather
    // than a half-assembled value.
    Caps reg(g_distinct, 28);

    TEST_ASSERT_EQUAL_UINT16(0, reg.vid(3));
    TEST_ASSERT_EQUAL_HEX8(0x5A, reg.batteryType(2));

    // 27 bytes: byte 26 is the last byte, still readable as a Battery Type.
    Caps shorter(g_distinct, 27);
    TEST_ASSERT_EQUAL_HEX8(0x5A, shorter.batteryType(2));
    TEST_ASSERT_EQUAL_UINT16(0, shorter.vid(3));
}

void test_short_buffer_battery_3_setters_do_not_write_past_buffer()
{
    Caps reg(g_distinct, 30);

    reg.setVid(3, 0x1122);                           // bytes 27-28: in range, written
    reg.setPid(3, 0x3344);                           // bytes 29-30: byte 30 out of range, no-op
    reg.setBatteryDesignCapacity(3, 0x5566);         // bytes 31-32: no-op
    reg.setBatteryLastFullChargeCapacity(3, 0x7788); // bytes 33-34: no-op
    reg.setBatteryType(3, 0x99);                     // byte 35: no-op

    uint8_t expected[30];
    memcpy(expected, g_distinct, 30);
    expected[27] = 0x22;
    expected[28] = 0x11;

    TEST_ASSERT_EQUAL_size_t(30, reg.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, reg.raw(), 30);
    TEST_ASSERT_EQUAL_UINT8(0x5D, reg.raw()[29]);  // the byte pid(3) would have started on
}

void test_short_buffer_fails_size_validation()
{
    Caps reg(g_distinct, 30);

    TEST_ASSERT_FALSE(reg.isValidSize(CAPS_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_invalid_register_reads_zero_and_setters_do_nothing()
{
    Caps original(g_distinct);
    Caps moved(static_cast<Caps &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    assertAllAccessorsZero(original, "invalid");

    original.setVid(0, 0x1234);
    original.setVid(3, 0x1234);
    original.setBatteryType(3, 0x55);
    TEST_ASSERT_FALSE(original.isValid());
    assertAllAccessorsZero(original, "invalid after set");

    Caps defaultConstructedRegInfoZero(TPS25751Registers::RegisterInfo{TPS25751Registers::Address::TRANSMIT_BATTERY_CAPABILITIES, 0});
    TEST_ASSERT_FALSE(defaultConstructedRegInfoZero.isValid());
    TEST_ASSERT_EQUAL_UINT16(0, defaultConstructedRegInfoZero.vid(3));
    defaultConstructedRegInfoZero.setVid(3, 0x1234);
    TEST_ASSERT_FALSE(defaultConstructedRegInfoZero.isValid());
}

void test_semantic_tier_has_no_field_rule()
{
    // No reserved bits and no stated field rule: any mix of values below the
    // all-0xFF signature is valid.
    Caps reg(g_distinct);

    reg.setVid(0, 0xFFFF);
    reg.setPid(1, 0xFFFF);
    reg.setBatteryType(3, 0xFF);
    reg.setBatteryDesignCapacity(2, 0x0000);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_debug_print_distinct_fixture()
{
    const std::string out = captureDebugPrint(Caps(g_distinct));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("TX_BATTERY_CAPABILITIES (0x7D) - 36 bytes"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Raw bytes (byte 0 first):"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("  bytes 0-8: 40 41 42 43 44 45 46 47 48"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("  bytes 27-35: 5B 5C 5D 5E 5F 60 61 62 63"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 0: VID 0x4140, PID 0x4342, Design Capacity 0x4544 (17732), Last Full Charge Capacity 0x4746 (18246), Battery Type 0x48"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 3: VID 0x5C5B, PID 0x5E5D, Design Capacity 0x605F (24671), Last Full Charge Capacity 0x6261 (25185), Battery Type 0x63"));
}

void test_debug_print_reset_zero_pads()
{
    const std::string out = captureDebugPrint(Caps(g_zero));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("  bytes 9-17: 00 00 00 00 00 00 00 00 00"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 1: VID 0x0000, PID 0x0000, Design Capacity 0x0000 (0), Last Full Charge Capacity 0x0000 (0), Battery Type 0x00"));
}

// --- Typed decode (USB PD R3.3 Table 6.64) ---------------------------------------------
//
// Entry i starts at byte 9 * i: VID +0, PID +2, Design Capacity +4, Last Full Charge
// Capacity +6 (16-bit little-endian), Battery Type +8. Capacities count 0.1 Wh
// (100 mWh) steps: 0000h not present, 0001h..FFFEh known, FFFFh unknown.

// Battery 1 (entry at byte 9): Design Capacity 0x0258 = 600 steps = 60,000 mWh at
// bytes 13 and 14 (58 02); Last Full Charge FFFFh (unknown) at bytes 15 and 16;
// Battery Type 0x01 (invalid reference) at byte 17. Battery 3 (entry at byte 27):
// Design Capacity 0x0000 at bytes 31 and 32, so the battery is not present.
void test_typed_battery_1_and_3_fixture()
{
    uint8_t buf[CAPS_SIZE] = {0};
    buf[13] = 0x58;
    buf[14] = 0x02;
    buf[15] = 0xFF;
    buf[16] = 0xFF;
    buf[17] = 0x01;
    Caps reg(buf);

    TEST_ASSERT_EQUAL_UINT16(0x0258, reg.batteryDesignCapacity(1));
    TEST_ASSERT_TRUE(reg.batteryPresent(1));
    TEST_ASSERT_TRUE(reg.designCapacityKnown(1));
    TEST_ASSERT_EQUAL_UINT32(60000, reg.batteryDesignCapacity_mWh(1));
    TEST_ASSERT_FALSE(reg.lastFullChargeCapacityKnown(1));
    TEST_ASSERT_EQUAL_UINT32(0, reg.batteryLastFullChargeCapacity_mWh(1));
    TEST_ASSERT_TRUE(reg.invalidBatteryReference(1));

    TEST_ASSERT_FALSE(reg.batteryPresent(3));
    TEST_ASSERT_FALSE(reg.designCapacityKnown(3));
    TEST_ASSERT_EQUAL_UINT32(0, reg.batteryDesignCapacity_mWh(3));
    TEST_ASSERT_FALSE(reg.lastFullChargeCapacityKnown(3));
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(3));

    // Battery 0 and 2 stay untouched.
    TEST_ASSERT_FALSE(reg.batteryPresent(0));
    TEST_ASSERT_FALSE(reg.batteryPresent(2));
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(0));
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(2));
}

void test_typed_capacity_boundaries()
{
    Caps reg(g_zero);
    // 0000h: not present, not known.
    TEST_ASSERT_FALSE(reg.batteryPresent(0));
    TEST_ASSERT_FALSE(reg.designCapacityKnown(0));
    TEST_ASSERT_FALSE(reg.lastFullChargeCapacityKnown(0));

    reg.setBatteryDesignCapacity(0, 0x0001);  // smallest known: 100 mWh
    reg.setBatteryLastFullChargeCapacity(0, 0xFFFE);  // largest known: 65534 * 100 = 6,553,400 mWh
    TEST_ASSERT_TRUE(reg.batteryPresent(0));
    TEST_ASSERT_EQUAL_UINT32(100, reg.batteryDesignCapacity_mWh(0));
    TEST_ASSERT_TRUE(reg.lastFullChargeCapacityKnown(0));
    TEST_ASSERT_EQUAL_UINT32(6553400, reg.batteryLastFullChargeCapacity_mWh(0));

    reg.setBatteryDesignCapacity(0, 0xFFFF);  // unknown, but the battery is present
    TEST_ASSERT_TRUE(reg.batteryPresent(0));
    TEST_ASSERT_FALSE(reg.designCapacityKnown(0));
    TEST_ASSERT_EQUAL_UINT32(0, reg.batteryDesignCapacity_mWh(0));
}

void test_typed_battery_type_reserved_bits_do_not_set_invalid_reference()
{
    Caps reg(g_zero);
    reg.setBatteryType(2, 0xFE);  // bits 7:1 reserved, bit 0 clear
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(2));
    reg.setBatteryType(2, 0x01);
    TEST_ASSERT_TRUE(reg.invalidBatteryReference(2));
}

void test_typed_battery_3_reaches_past_bit_255()
{
    // Battery 3 Design Capacity at bytes 31 and 32 (bit 248), Last Full at 33 and 34,
    // Type at byte 35: the typed layer must reach them through the raw accessors.
    uint8_t buf[CAPS_SIZE] = {0};
    buf[31] = 0xE8;  // 0x03E8 = 1000 steps = 100,000 mWh
    buf[32] = 0x03;
    buf[33] = 0x64;  // 0x0064 = 100 steps = 10,000 mWh
    buf[34] = 0x00;
    buf[35] = 0x01;
    Caps reg(buf);

    TEST_ASSERT_TRUE(reg.batteryPresent(3));
    TEST_ASSERT_EQUAL_UINT32(100000, reg.batteryDesignCapacity_mWh(3));
    TEST_ASSERT_EQUAL_UINT32(10000, reg.batteryLastFullChargeCapacity_mWh(3));
    TEST_ASSERT_TRUE(reg.invalidBatteryReference(3));
    TEST_ASSERT_FALSE(reg.batteryPresent(0));
}

void test_typed_out_of_range_battery_index()
{
    Caps reg(g_allOnes);  // every field 0xFFFF / 0xFF, so any leak would be visible
    TEST_ASSERT_FALSE(reg.invalidBatteryReference(4));
    TEST_ASSERT_FALSE(reg.batteryPresent(4));
    TEST_ASSERT_FALSE(reg.designCapacityKnown(4));
    TEST_ASSERT_FALSE(reg.lastFullChargeCapacityKnown(4));
    TEST_ASSERT_EQUAL_UINT32(0, reg.batteryDesignCapacity_mWh(4));
    TEST_ASSERT_EQUAL_UINT32(0, reg.batteryLastFullChargeCapacity_mWh(4));
    TEST_ASSERT_FALSE(reg.batteryPresent(255));
}

void test_debug_print_typed_decode_lines()
{
    uint8_t buf[CAPS_SIZE] = {0};
    buf[13] = 0x58;
    buf[14] = 0x02;
    buf[15] = 0xFF;
    buf[16] = 0xFF;
    buf[17] = 0x01;
    const std::string out = captureDebugPrint(Caps(buf));

    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 0: not present"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 1: present, design capacity 60000 mWh, last full charge capacity unknown, invalid battery reference"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 3: not present"));
    // The existing raw line remains.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Battery 1: VID 0x0000, PID 0x0000, Design Capacity 0x0258 (600), Last Full Charge Capacity 0xFFFF (65535), Battery Type 0x01"));
}

void test_copy_constructor_deep_copies()
{
    Caps source(g_distinct);
    Caps copy(source);

    TEST_ASSERT_NOT_EQUAL(source.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, copy.raw(), CAPS_SIZE);
}

void test_move_constructor_transfers_ownership()
{
    Caps original(g_distinct);
    Caps moved(static_cast<Caps &&>(original));

    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, moved.raw(), CAPS_SIZE);
}

void test_copy_assignment_deep_copies()
{
    Caps source(g_distinct);
    Caps target(g_zero);

    target = source;

    TEST_ASSERT_NOT_EQUAL(source.raw(), target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, target.raw(), CAPS_SIZE);
}

void test_move_assignment_transfers_ownership()
{
    Caps source(g_distinct);
    Caps target(g_zero);
    const uint8_t *buf = source.raw();

    target = static_cast<Caps &&>(source);

    TEST_ASSERT_EQUAL(buf, target.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g_distinct, target.raw(), CAPS_SIZE);
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
    RUN_TEST(test_all_ones_rejected_at_data_tier);

    RUN_TEST(test_all_zero_reset_is_valid_and_decodes_to_zero);
    RUN_TEST(test_distinct_fixture_is_valid);
    RUN_TEST(test_distinct_fixture_decodes_all_twenty_fields);
    RUN_TEST(test_battery_3_decodes_from_bytes_27_to_35);

    RUN_TEST(test_battery_3_setters_leave_battery_2_alone);
    RUN_TEST(test_per_entry_round_trip_changes_only_that_entry);
    RUN_TEST(test_setters_are_little_endian);
    RUN_TEST(test_out_of_range_index_reads_zero);
    RUN_TEST(test_out_of_range_index_setters_leave_buffer_unchanged);

    RUN_TEST(test_short_buffer_battery_3_reads_are_bounds_checked);
    RUN_TEST(test_sixteen_bit_read_needs_both_bytes);
    RUN_TEST(test_short_buffer_battery_3_setters_do_not_write_past_buffer);
    RUN_TEST(test_short_buffer_fails_size_validation);
    RUN_TEST(test_invalid_register_reads_zero_and_setters_do_nothing);

    RUN_TEST(test_semantic_tier_has_no_field_rule);

    RUN_TEST(test_debug_print_distinct_fixture);
    RUN_TEST(test_debug_print_reset_zero_pads);

    RUN_TEST(test_typed_battery_1_and_3_fixture);
    RUN_TEST(test_typed_capacity_boundaries);
    RUN_TEST(test_typed_battery_type_reserved_bits_do_not_set_invalid_reference);
    RUN_TEST(test_typed_battery_3_reaches_past_bit_255);
    RUN_TEST(test_typed_out_of_range_battery_index);
    RUN_TEST(test_debug_print_typed_decode_lines);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);
    RUN_TEST(test_copy_assignment_deep_copies);
    RUN_TEST(test_move_assignment_transfers_ownership);

    return UNITY_END();
}
