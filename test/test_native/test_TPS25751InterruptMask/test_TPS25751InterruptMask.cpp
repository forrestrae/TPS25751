/**
 * @file test_TPS25751InterruptMask.cpp
 * @brief Native (host-run) unit tests for TPS25751InterruptMask.
 *
 * Run with: pio test -e native
 *
 * INT_MASK1 (0x16) shares its 88-bit (11-byte) layout with the read-only
 * INT_EVENT1 (0x14) register (see TPS25751InterruptEvent) -- a mask bit of 1
 * means the corresponding INT_EVENT1 source is enabled (allowed to assert);
 * 0 disables/blocks it. Byte/bit placement for a bit index `n` is always
 * `byte = n / 8`, `offset = n % 8` (little-endian bit numbering, matching
 * TPS25751Register::extractBits()/setBits()).
 *
 * Representative bits exercised below, with their byte/offset math:
 *   bit  1 (pdHardreset)          -> byte 0, offset 1  -> 0x02
 *   bit  9 (overcurrent)          -> byte 1, offset 1  -> 0x02
 *   bit 30 (cmd1Complete)         -> byte 3, offset 6  -> 0x40
 *   bit 60 (liquidDetection)      -> byte 7, offset 4  -> 0x10
 *   bit 82 (i2cControllerNacked)  -> byte 10, offset 2 -> 0x04
 */

#include <unity.h>
#include <cstdio>
#include <string>

#include "TPS25751InterruptMask.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

constexpr size_t INT_MASK1_SIZE = TPS25751Registers::Registers::INT_MASK1.size;  // 11

// Five representative bits set, spread across distinct bytes (see file header
// for the byte/offset derivation of each).
constexpr uint8_t MULTI_BIT_DATA[INT_MASK1_SIZE] = {
    0x02,  // byte0: bit1  (pdHardreset)
    0x02,  // byte1: bit9  (overcurrent)
    0x00,  // byte2
    0x40,  // byte3: bit30 (cmd1Complete)
    0x00,  // byte4
    0x00,  // byte5
    0x00,  // byte6
    0x10,  // byte7: bit60 (liquidDetection)
    0x00,  // byte8
    0x00,  // byte9
    0x04,  // byte10: bit82 (i2cControllerNacked)
};

constexpr uint8_t ALL_ZEROS_DATA[INT_MASK1_SIZE] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
constexpr uint8_t ALL_ONES_DATA[INT_MASK1_SIZE] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

}  // namespace

void setUp() {}
void tearDown() {}

// --- Construction & basic validation ---------------------------------------

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751InterruptMask reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_MASK1_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(INT_MASK1_SIZE));
    TEST_ASSERT_FALSE(reg.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(0, reg.getEnabledCount());
}

void test_data_constructor_copies_buffer()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_MASK1_SIZE, reg.size());
    TEST_ASSERT_NOT_EQUAL(MULTI_BIT_DATA, reg.raw());  // deep copy
    TEST_ASSERT_EQUAL_UINT8_ARRAY(MULTI_BIT_DATA, reg.raw(), INT_MASK1_SIZE);
}

void test_explicit_length_constructor()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA, INT_MASK1_SIZE);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_TRUE(reg.isValidSize(INT_MASK1_SIZE));
    TEST_ASSERT_TRUE(reg.pdHardreset());
}

void test_reginfo_constructor_allocates_zeroed_buffer()
{
    TPS25751InterruptMask reg(TPS25751Registers::Registers::INT_MASK1);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(INT_MASK1_SIZE, reg.size());
    TEST_ASSERT_FALSE(reg.hasAnyEnabled());
}

void test_null_data_behaves_as_zeroed()
{
    TPS25751InterruptMask reg(nullptr);

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_FALSE(reg.hasAnyEnabled());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[3] = {0x02, 0x00, 0x00};
    TPS25751InterruptMask reg(undersized, sizeof(undersized));

    TEST_ASSERT_TRUE(reg.isValid());  // basic: has a buffer
    TEST_ASSERT_FALSE(reg.isValidSize(INT_MASK1_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Data validation ---------------------------------------------------------

void test_all_zeros_is_valid_data()
{
    TPS25751InterruptMask reg(ALL_ZEROS_DATA);
    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_all_ones_is_invalid_data()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);
    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Field decode: bit->byte placement at several distinct offsets ----------

void test_decode_multi_bit_pattern()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);

    TEST_ASSERT_TRUE(reg.pdHardreset());          // bit 1
    TEST_ASSERT_TRUE(reg.overcurrent());          // bit 9
    TEST_ASSERT_TRUE(reg.cmd1Complete());         // bit 30
    TEST_ASSERT_TRUE(reg.liquidDetection());      // bit 60
    TEST_ASSERT_TRUE(reg.i2cControllerNacked());  // bit 82

    // Spot-check a handful of bits that must NOT be set by this pattern.
    TEST_ASSERT_FALSE(reg.plugInsertOrRemoval());        // bit 3
    TEST_ASSERT_FALSE(reg.powerSwapComplete());          // bit 4
    TEST_ASSERT_FALSE(reg.dataSwapComplete());           // bit 5
    TEST_ASSERT_FALSE(reg.newContractAsConsumer());      // bit 12
    TEST_ASSERT_FALSE(reg.usbHostPresent());             // bit 20
    TEST_ASSERT_FALSE(reg.deviceIncompatibleError());    // bit 32
    TEST_ASSERT_FALSE(reg.extDcdcSinkSafeState());        // bit 56
    TEST_ASSERT_FALSE(reg.patchLoaded());                // bit 80
}

void test_enabled_count_and_has_any_enabled()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);
    TEST_ASSERT_TRUE(reg.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(5, reg.getEnabledCount());

    TPS25751InterruptMask none;
    TEST_ASSERT_FALSE(none.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(0, none.getEnabledCount());
}

// --- Semantic validation: any combination is legal for a mask ----------------

void test_semantic_valid_for_arbitrary_combination()
{
    TEST_ASSERT_TRUE(TPS25751InterruptMask(MULTI_BIT_DATA).isSemanticallyValid());
}

// --- Setters: round trip + neighbouring-bit preservation --------------------

void test_set_pd_hardreset_round_trip()
{
    TPS25751InterruptMask reg;  // starts all-zero
    reg.setPdHardreset(true);
    TEST_ASSERT_TRUE(reg.pdHardreset());

    reg.setPdHardreset(false);
    TEST_ASSERT_FALSE(reg.pdHardreset());
}

void test_set_pd_hardreset_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setPdHardreset(false);  // bit 1, byte 0

    TEST_ASSERT_EQUAL_UINT8(0xFD, reg.raw()[0]);  // 1111_1101
    TEST_ASSERT_FALSE(reg.pdHardreset());
    // Neighbouring bits/bytes untouched.
    TEST_ASSERT_TRUE(reg.overcurrent());          // byte 1 untouched
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.raw()[1]);
}

void test_set_plug_insert_or_removal_round_trip_and_preserves_other_bits()
{
    TPS25751InterruptMask reg;  // starts all-zero
    reg.setPlugInsertOrRemoval(true);  // bit 3, byte 0
    TEST_ASSERT_TRUE(reg.plugInsertOrRemoval());
    TEST_ASSERT_EQUAL_UINT8(0x08, reg.raw()[0]);

    // On an all-ones buffer, clearing bit 3 must leave byte0's other bits set.
    TPS25751InterruptMask ones(ALL_ONES_DATA);
    ones.setPlugInsertOrRemoval(false);
    TEST_ASSERT_EQUAL_UINT8(0xF7, ones.raw()[0]);  // 1111_0111
    TEST_ASSERT_FALSE(ones.plugInsertOrRemoval());
    TEST_ASSERT_TRUE(ones.pdHardreset());  // bit 1, same byte, untouched
}

void test_set_overcurrent_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setOvercurrent(false);  // bit 9, byte 1

    TEST_ASSERT_EQUAL_UINT8(0xFD, reg.raw()[1]);  // 1111_1101
    TEST_ASSERT_FALSE(reg.overcurrent());
    TEST_ASSERT_TRUE(reg.pdHardreset());          // byte 0 untouched
    TEST_ASSERT_EQUAL_UINT8(0xFF, reg.raw()[0]);
}

void test_set_liquid_detection_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setLiquidDetection(false);  // bit 60, byte 7

    TEST_ASSERT_EQUAL_UINT8(0xEF, reg.raw()[7]);  // 1110_1111
    TEST_ASSERT_FALSE(reg.liquidDetection());
    TEST_ASSERT_TRUE(reg.extDcdcSourceSafeState());  // bit 57, same byte, untouched
}

void test_set_i2c_controller_nacked_preserves_other_bits()
{
    TPS25751InterruptMask reg(ALL_ONES_DATA);

    reg.setI2cControllerNacked(false);  // bit 82, byte 10

    TEST_ASSERT_EQUAL_UINT8(0xFB, reg.raw()[10]);  // 1111_1011
    TEST_ASSERT_FALSE(reg.i2cControllerNacked());
    TEST_ASSERT_TRUE(reg.readyForPatch());  // bit 81, same byte, untouched
}

void test_set_midb_received_round_trip_and_preserves_other_bits()
{
    // Rev. B bit 29 (SLVUCR8B p.21) -> byte 3 bit 5.
    TPS25751InterruptMask reg;
    reg.setMidbReceived(true);
    TEST_ASSERT_TRUE(reg.midbReceived());
    TEST_ASSERT_EQUAL_UINT8(0x20, reg.raw()[3]);
    TEST_ASSERT_EQUAL_UINT8(1, reg.getEnabledCount());

    reg.setMidbReceived(false);
    TEST_ASSERT_FALSE(reg.midbReceived());
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[3]);

    uint8_t ones[INT_MASK1_SIZE];
    for (size_t i = 0; i < INT_MASK1_SIZE; i++) ones[i] = 0xFF;
    TPS25751InterruptMask full(ones);
    full.setMidbReceived(false);
    TEST_ASSERT_EQUAL_UINT8(0xDF, full.raw()[3]);
    TEST_ASSERT_TRUE(full.cmd1Complete());  // bit 30 untouched
}

void test_set_discover_mode_completed_round_trip_and_preserves_other_bits()
{
    // Rev. B bit 51 (SLVUCR8B p.21) -> byte 6 bit 3.
    TPS25751InterruptMask reg;
    reg.setDiscoverModeCompleted(true);
    TEST_ASSERT_TRUE(reg.discoverModeCompleted());
    TEST_ASSERT_EQUAL_UINT8(0x08, reg.raw()[6]);
    TEST_ASSERT_TRUE(reg.hasAnyEnabled());
    TEST_ASSERT_EQUAL_UINT8(1, reg.getEnabledCount());

    reg.setDiscoverModeCompleted(false);
    TEST_ASSERT_FALSE(reg.discoverModeCompleted());
    TEST_ASSERT_EQUAL_UINT8(0x00, reg.raw()[6]);

    uint8_t ones[INT_MASK1_SIZE];
    for (size_t i = 0; i < INT_MASK1_SIZE; i++) ones[i] = 0xFF;
    TPS25751InterruptMask full(ones);
    full.setDiscoverModeCompleted(false);
    TEST_ASSERT_EQUAL_UINT8(0xF7, full.raw()[6]);
}

void test_setters_no_op_on_invalid_register()
{
    // A moved-from register is !isValid(); setters must guard and no-op.
    TPS25751InterruptMask reg(MULTI_BIT_DATA);
    TPS25751InterruptMask moved(static_cast<TPS25751InterruptMask &&>(reg));

    TEST_ASSERT_FALSE(reg.isValid());
    reg.setPdHardreset(true);
    reg.setOvercurrent(false);
    TEST_ASSERT_FALSE(reg.isValid());  // still invalid; no crash
    TEST_ASSERT_TRUE(moved.pdHardreset());  // untouched
}

// --- Rule of Five -------------------------------------------------------------

void test_copy_constructor_deep_copies()
{
    TPS25751InterruptMask original(MULTI_BIT_DATA);
    TPS25751InterruptMask copy(original);

    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), INT_MASK1_SIZE);
    TEST_ASSERT_EQUAL_UINT8(original.getEnabledCount(), copy.getEnabledCount());
}

void test_move_constructor_transfers_ownership()
{
    TPS25751InterruptMask original(MULTI_BIT_DATA);
    const uint8_t *buf = original.raw();

    TPS25751InterruptMask moved(static_cast<TPS25751InterruptMask &&>(original));

    TEST_ASSERT_EQUAL(buf, moved.raw());  // buffer stolen, not copied
    TEST_ASSERT_FALSE(original.isValid());
    TEST_ASSERT_TRUE(moved.pdHardreset());
}


// --- Every source: getter, setter, bit position, debug name -----------------

namespace {

struct SourceCase {
    uint8_t bit;
    bool (TPS25751InterruptMask::*get)() const;
    void (TPS25751InterruptMask::*set)(bool);
    const char *name;
};

// Bit numbers follow INT_EVENT1 / INT_MASK1 in TRM SLVUCR8B (rev B).
constexpr SourceCase kSources[] = {
    {82, &TPS25751InterruptMask::i2cControllerNacked, &TPS25751InterruptMask::setI2cControllerNacked, "I2C Controller NACKed"},
    {81, &TPS25751InterruptMask::readyForPatch, &TPS25751InterruptMask::setReadyForPatch, "Ready for Patch"},
    {80, &TPS25751InterruptMask::patchLoaded, &TPS25751InterruptMask::setPatchLoaded, "Patch Loaded"},
    {66, &TPS25751InterruptMask::mbrdBufferReady, &TPS25751InterruptMask::setMbrdBufferReady, "MBRD Buffer Ready"},
    {65, &TPS25751InterruptMask::txMemoryBufferEmpty, &TPS25751InterruptMask::setTxMemoryBufferEmpty, "TX Memory Buffer Empty"},
    {60, &TPS25751InterruptMask::liquidDetection, &TPS25751InterruptMask::setLiquidDetection, "Liquid Detection"},
    {57, &TPS25751InterruptMask::extDcdcSourceSafeState, &TPS25751InterruptMask::setExtDcdcSourceSafeState, "Ext DCDC Source Safe State"},
    {56, &TPS25751InterruptMask::extDcdcSinkSafeState, &TPS25751InterruptMask::setExtDcdcSinkSafeState, "Ext DCDC Sink Safe State"},
    {51, &TPS25751InterruptMask::discoverModeCompleted, &TPS25751InterruptMask::setDiscoverModeCompleted, "Discover Mode Completed"},
    {46, &TPS25751InterruptMask::unableToSourceError, &TPS25751InterruptMask::setUnableToSourceError, "Unable to Source Error"},
    {43, &TPS25751InterruptMask::plugEarlyNotification, &TPS25751InterruptMask::setPlugEarlyNotification, "Plug Early Notification"},
    {42, &TPS25751InterruptMask::sinkTransitionCompleted, &TPS25751InterruptMask::setSinkTransitionCompleted, "Sink Transition Completed"},
    {39, &TPS25751InterruptMask::messageDataError, &TPS25751InterruptMask::setMessageDataError, "Message Data Error"},
    {38, &TPS25751InterruptMask::protocolError, &TPS25751InterruptMask::setProtocolError, "Protocol Error"},
    {36, &TPS25751InterruptMask::missingGetCapabilitiesError, &TPS25751InterruptMask::setMissingGetCapabilitiesError, "Missing Get Capabilities Error"},
    {35, &TPS25751InterruptMask::powerEventOccurredError, &TPS25751InterruptMask::setPowerEventOccurredError, "Power Event Occurred Error"},
    {34, &TPS25751InterruptMask::canProvideVoltageLaterError, &TPS25751InterruptMask::setCanProvideVoltageLaterError, "Can Provide Voltage/Current Later"},
    {33, &TPS25751InterruptMask::cannotProvideVoltageError, &TPS25751InterruptMask::setCannotProvideVoltageError, "Cannot Provide Voltage/Current"},
    {32, &TPS25751InterruptMask::deviceIncompatibleError, &TPS25751InterruptMask::setDeviceIncompatibleError, "Device Incompatible Error"},
    {30, &TPS25751InterruptMask::cmd1Complete, &TPS25751InterruptMask::setCmd1Complete, "CMD1 Complete"},
    {29, &TPS25751InterruptMask::midbReceived, &TPS25751InterruptMask::setMidbReceived, "MIDB Received"},
    {27, &TPS25751InterruptMask::pdStatusUpdated, &TPS25751InterruptMask::setPdStatusUpdated, "PD Status Updated"},
    {26, &TPS25751InterruptMask::statusUpdated, &TPS25751InterruptMask::setStatusUpdated, "Status Updated"},
    {24, &TPS25751InterruptMask::powerStatusUpdated, &TPS25751InterruptMask::setPowerStatusUpdated, "Power Status Updated"},
    {23, &TPS25751InterruptMask::powerPathSwitchChanged, &TPS25751InterruptMask::setPowerPathSwitchChanged, "Power Path Switch Changed"},
    {21, &TPS25751InterruptMask::usbHostNoLongerPresent, &TPS25751InterruptMask::setUsbHostNoLongerPresent, "USB Host No Longer Present"},
    {20, &TPS25751InterruptMask::usbHostPresent, &TPS25751InterruptMask::setUsbHostPresent, "USB Host Present"},
    {18, &TPS25751InterruptMask::dataSwapRequested, &TPS25751InterruptMask::setDataSwapRequested, "Data Swap Requested"},
    {17, &TPS25751InterruptMask::powerSwapRequested, &TPS25751InterruptMask::setPowerSwapRequested, "Power Swap Requested"},
    {15, &TPS25751InterruptMask::sinkCapMessageReceived, &TPS25751InterruptMask::setSinkCapMessageReceived, "Sink Cap Message Received"},
    {14, &TPS25751InterruptMask::sourceCapMessageReceived, &TPS25751InterruptMask::setSourceCapMessageReceived, "Source Cap Message Received"},
    {13, &TPS25751InterruptMask::newContractAsProvider, &TPS25751InterruptMask::setNewContractAsProvider, "New Contract as Provider"},
    {12, &TPS25751InterruptMask::newContractAsConsumer, &TPS25751InterruptMask::setNewContractAsConsumer, "New Contract as Consumer"},
    {9, &TPS25751InterruptMask::overcurrent, &TPS25751InterruptMask::setOvercurrent, "Overcurrent"},
    {5, &TPS25751InterruptMask::dataSwapComplete, &TPS25751InterruptMask::setDataSwapComplete, "Data Swap Complete"},
    {4, &TPS25751InterruptMask::powerSwapComplete, &TPS25751InterruptMask::setPowerSwapComplete, "Power Swap Complete"},
    {3, &TPS25751InterruptMask::plugInsertOrRemoval, &TPS25751InterruptMask::setPlugInsertOrRemoval, "Plug Insert or Removal"},
    {1, &TPS25751InterruptMask::pdHardreset, &TPS25751InterruptMask::setPdHardreset, "PD Hard Reset"},
};

constexpr size_t kNumSources = sizeof(kSources) / sizeof(kSources[0]);

bool contains(const std::string &haystack, const char *needle)
{
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

void test_each_source_setter_sets_only_its_bit_and_getter_reads_it()
{
    TEST_ASSERT_EQUAL_size_t(38, kNumSources);
    for (size_t i = 0; i < kNumSources; ++i) {
        const SourceCase &c = kSources[i];
        TPS25751InterruptMask reg;
        (reg.*c.set)(true);

        char msg[48];
        snprintf(msg, sizeof(msg), "bit %u", (unsigned)c.bit);
        TEST_ASSERT_TRUE_MESSAGE((reg.*c.get)(), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, reg.getEnabledCount(), msg);
        for (size_t b = 0; b < INT_MASK1_SIZE; ++b) {
            const uint8_t expected = (b == c.bit / 8u) ? static_cast<uint8_t>(1u << (c.bit % 8u)) : 0x00;
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(expected, reg.raw()[b], msg);
        }

        (reg.*c.set)(false);
        TEST_ASSERT_FALSE_MESSAGE((reg.*c.get)(), msg);
        TEST_ASSERT_FALSE_MESSAGE(reg.hasAnyEnabled(), msg);
    }
}

void test_each_source_getter_ignores_every_other_bit()
{
    for (size_t i = 0; i < kNumSources; ++i) {
        TPS25751InterruptMask reg(ALL_ONES_DATA);
        (reg.*kSources[i].set)(false);
        char msg[48];
        snprintf(msg, sizeof(msg), "bit %u", (unsigned)kSources[i].bit);
        TEST_ASSERT_FALSE_MESSAGE((reg.*kSources[i].get)(), msg);
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(kNumSources - 1, reg.getEnabledCount(), msg);
    }
}

void test_enabled_count_with_every_source_set()
{
    TPS25751InterruptMask reg;
    for (size_t i = 0; i < kNumSources; ++i) (reg.*kSources[i].set)(true);
    TEST_ASSERT_EQUAL_UINT8(kNumSources, reg.getEnabledCount());
    TEST_ASSERT_TRUE(reg.hasAnyEnabled());
}

// --- debugPrint ---------------------------------------------------------------

void test_debug_print_all_masked()
{
    TPS25751InterruptMask reg;
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(contains(out, "=== Interrupt Mask Register (0x16) ==="));
    TEST_ASSERT_TRUE(contains(out, "No interrupt sources enabled (all masked)"));
    TEST_ASSERT_FALSE(contains(out, "Enabled Sources:"));
    TEST_ASSERT_FALSE(contains(out, "[+]"));
}

void test_debug_print_lists_each_source_alone()
{
    for (size_t i = 0; i < kNumSources; ++i) {
        TPS25751InterruptMask reg;
        (reg.*kSources[i].set)(true);
        const std::string out = captureDebugPrint(reg);

        std::string line = std::string("  [+] ") + kSources[i].name + "\n";
        TEST_ASSERT_TRUE_MESSAGE(contains(out, "Enabled Sources: 1\n"), kSources[i].name);
        TEST_ASSERT_TRUE_MESSAGE(contains(out, line.c_str()), kSources[i].name);
        size_t count = 0;
        for (size_t pos = out.find("[+]"); pos != std::string::npos; pos = out.find("[+]", pos + 1)) ++count;
        TEST_ASSERT_EQUAL_size_t_MESSAGE(1, count, kSources[i].name);
        TEST_ASSERT_FALSE_MESSAGE(contains(out, "No interrupt sources"), kSources[i].name);
    }
}

void test_debug_print_every_source_enabled()
{
    TPS25751InterruptMask reg;
    for (size_t i = 0; i < kNumSources; ++i) (reg.*kSources[i].set)(true);
    const std::string out = captureDebugPrint(reg);

    char expected[40];
    snprintf(expected, sizeof(expected), "Enabled Sources: %u\n", (unsigned)kNumSources);
    TEST_ASSERT_TRUE(contains(out, expected));
    for (size_t i = 0; i < kNumSources; ++i) {
        std::string line = std::string("  [+] ") + kSources[i].name + "\n";
        TEST_ASSERT_TRUE_MESSAGE(contains(out, line.c_str()), kSources[i].name);
    }
}

void test_set_midb_and_discover_mode_bits()
{
    // Rev B additions: MIDB Received is bit 29 (byte 3, 0x20), Discover Mode
    // Completed is bit 51 (byte 6, 0x08). SLVUCR8B p.21.
    TPS25751InterruptMask reg;
    reg.setMidbReceived(true);
    reg.setDiscoverModeCompleted(true);
    TEST_ASSERT_EQUAL_UINT8(0x20, reg.raw()[3]);
    TEST_ASSERT_EQUAL_UINT8(0x08, reg.raw()[6]);
    TEST_ASSERT_TRUE(reg.midbReceived());
    TEST_ASSERT_TRUE(reg.discoverModeCompleted());
}

void test_invalid_register_reads_disabled_and_semantically_invalid()
{
    TPS25751InterruptMask reg(MULTI_BIT_DATA);
    TPS25751InterruptMask moved(static_cast<TPS25751InterruptMask &&>(reg));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
    TEST_ASSERT_FALSE(reg.pdHardreset());
}

void test_semantic_invalid_for_wrong_size()
{
    const uint8_t shortBuf[3] = {0x02, 0x00, 0x00};
    TPS25751InterruptMask reg(shortBuf, sizeof(shortBuf));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Runner -------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_constructor);
    RUN_TEST(test_reginfo_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_null_data_behaves_as_zeroed);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_zeros_is_valid_data);
    RUN_TEST(test_all_ones_is_invalid_data);

    RUN_TEST(test_decode_multi_bit_pattern);
    RUN_TEST(test_enabled_count_and_has_any_enabled);

    RUN_TEST(test_semantic_valid_for_arbitrary_combination);

    RUN_TEST(test_set_pd_hardreset_round_trip);
    RUN_TEST(test_set_pd_hardreset_preserves_other_bits);
    RUN_TEST(test_set_plug_insert_or_removal_round_trip_and_preserves_other_bits);
    RUN_TEST(test_set_overcurrent_preserves_other_bits);
    RUN_TEST(test_set_liquid_detection_preserves_other_bits);
    RUN_TEST(test_set_i2c_controller_nacked_preserves_other_bits);
    RUN_TEST(test_set_midb_received_round_trip_and_preserves_other_bits);
    RUN_TEST(test_set_discover_mode_completed_round_trip_and_preserves_other_bits);
    RUN_TEST(test_setters_no_op_on_invalid_register);

    RUN_TEST(test_copy_constructor_deep_copies);
    RUN_TEST(test_move_constructor_transfers_ownership);

    RUN_TEST(test_each_source_setter_sets_only_its_bit_and_getter_reads_it);
    RUN_TEST(test_each_source_getter_ignores_every_other_bit);
    RUN_TEST(test_enabled_count_with_every_source_set);
    RUN_TEST(test_debug_print_all_masked);
    RUN_TEST(test_debug_print_lists_each_source_alone);
    RUN_TEST(test_debug_print_every_source_enabled);
    RUN_TEST(test_set_midb_and_discover_mode_bits);
    RUN_TEST(test_invalid_register_reads_disabled_and_semantically_invalid);
    RUN_TEST(test_semantic_invalid_for_wrong_size);

    return UNITY_END();
}
