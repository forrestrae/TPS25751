/**
 * @file test_TPS25751BootFlags.cpp
 * @brief Native unit tests for TPS25751BootFlags (BOOT_FLAGS, 0x2D, 5 bytes).
 *
 * Bit positions from TRM SLVUCR8B p.33: Patch Header Error 0, Dead Battery 2,
 * I2C EEPROM Present 3, Region 0/1 Attempted 4/5, Region 0/1 Invalid 6/7,
 * Region 0/1 EEPROM Error 8/9, Patch Download Error 10, Region 0/1 CRC Fail
 * 12/13, System TSD 19, Patch Config Source 31:29, Revision ID 39:32.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "TPS25751BootFlags.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"
#include "CaptureStdout.h"

namespace {

using E = TPS25751BootFlags;
constexpr size_t SZ = TPS25751Registers::Registers::BOOT_FLAGS.size;  // 5

void pack(uint8_t *buf, unsigned pos, unsigned len, uint32_t value)
{
    for (unsigned i = 0; i < len; i++) {
        const unsigned b = pos + i;
        if ((value >> i) & 1u) buf[b / 8] |= static_cast<uint8_t>(1u << (b % 8));
        else buf[b / 8] &= static_cast<uint8_t>(~(1u << (b % 8)));
    }
}

bool has(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

// A register with exactly one flag bit set.
E withBit(unsigned pos)
{
    uint8_t d[SZ] = {0, 0, 0, 0, 0};
    pack(d, pos, 1, 1);
    return E(d);
}

E withSource(uint32_t src, bool eeprom)
{
    uint8_t d[SZ] = {0, 0, 0, 0, 0};
    pack(d, 29, 3, src);
    pack(d, 3, 1, eeprom ? 1 : 0);
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

    E info(TPS25751Registers::Registers::BOOT_FLAGS);
    TEST_ASSERT_EQUAL_size_t(SZ, info.size());

    const uint8_t d[SZ] = {0x04, 0, 0, 0, 0x2A};
    E ptr(d);
    TEST_ASSERT_TRUE(ptr.deadBatteryFlag());
    TEST_ASSERT_EQUAL_UINT8(0x2A, ptr.revisionId());

    E nul(nullptr);
    TEST_ASSERT_TRUE(nul.isValid());
    TEST_ASSERT_FALSE(nul.deadBatteryFlag());

    const uint8_t three[3] = {0, 0, 0};
    E sized(three, 3);
    TEST_ASSERT_EQUAL_size_t(3, sized.size());
    TEST_ASSERT_FALSE(sized.isSemanticallyValid());
    TEST_ASSERT_EQUAL_UINT8(0, sized.revisionId());
}

void test_revision_id_is_byte_4()
{
    const uint8_t d[SZ] = {0, 0, 0, 0, 0xA5};
    E r(d);
    TEST_ASSERT_EQUAL_UINT8(0xA5, r.revisionId());
    TEST_ASSERT_EQUAL(PatchConfigSource::NoneLoaded, r.patchConfigSource());
}

void test_each_flag_decodes_from_its_own_bit()
{
    struct Case {
        unsigned bit;
        bool (E::*fn)() const;
    };
    const Case cases[] = {
        {0, &E::patchHeaderError},   {2, &E::deadBatteryFlag},     {3, &E::i2cEepromPresent},
        {4, &E::region0Attempted},   {5, &E::region1Attempted},    {6, &E::region0Invalid},
        {7, &E::region1Invalid},     {8, &E::region0EepromError},  {9, &E::region1EepromError},
        {10, &E::patchDownloadError}, {12, &E::region0CrcFail},    {13, &E::region1CrcFail},
        {19, &E::systemTSD},
    };
    const size_t n = sizeof(cases) / sizeof(cases[0]);
    for (size_t i = 0; i < n; i++) {
        E r = withBit(cases[i].bit);
        // Only the targeted accessor reads true.
        for (size_t j = 0; j < n; j++) {
            const bool got = (r.*cases[j].fn)();
            TEST_ASSERT_EQUAL_MESSAGE(i == j, got, "flag decoded from wrong bit");
        }
        TEST_ASSERT_EQUAL(PatchConfigSource::NoneLoaded, r.patchConfigSource());
        TEST_ASSERT_EQUAL_UINT8(0, r.revisionId());
    }
}

void test_patch_config_source_all_values()
{
    const PatchConfigSource exp[8] = {
        PatchConfigSource::NoneLoaded, PatchConfigSource::Reserved1, PatchConfigSource::Reserved2,
        PatchConfigSource::Reserved3,  PatchConfigSource::Reserved4, PatchConfigSource::EEPROMLoaded,
        PatchConfigSource::I2CLoaded,  PatchConfigSource::Reserved7};
    for (uint32_t v = 0; v < 8; v++) {
        TEST_ASSERT_EQUAL(exp[v], withSource(v, true).patchConfigSource());
    }
}

void test_to_string_patch_config_source()
{
    TEST_ASSERT_EQUAL_STRING("None Loaded", toString(PatchConfigSource::NoneLoaded));
    TEST_ASSERT_EQUAL_STRING("Reserved (1)", toString(PatchConfigSource::Reserved1));
    TEST_ASSERT_EQUAL_STRING("Reserved (2)", toString(PatchConfigSource::Reserved2));
    TEST_ASSERT_EQUAL_STRING("Reserved (3)", toString(PatchConfigSource::Reserved3));
    TEST_ASSERT_EQUAL_STRING("Reserved (4)", toString(PatchConfigSource::Reserved4));
    TEST_ASSERT_EQUAL_STRING("EEPROM Loaded", toString(PatchConfigSource::EEPROMLoaded));
    TEST_ASSERT_EQUAL_STRING("I2C Loaded", toString(PatchConfigSource::I2CLoaded));
    TEST_ASSERT_EQUAL_STRING("Reserved (7)", toString(PatchConfigSource::Reserved7));
    TEST_ASSERT_EQUAL_STRING("Unknown", toString(static_cast<PatchConfigSource>(12)));
}

void test_validation_tiers()
{
    TEST_ASSERT_TRUE(E().hasValidData());
    TEST_ASSERT_TRUE(E().isSemanticallyValid());

    const uint8_t ones[SZ] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    E bad(ones);
    TEST_ASSERT_FALSE(bad.hasValidData());
    TEST_ASSERT_FALSE(bad.isSemanticallyValid());

    const uint8_t four[4] = {0, 0, 0, 0};
    E shortReg(four, 4);
    TEST_ASSERT_FALSE(shortReg.isValidSize(SZ));
    TEST_ASSERT_FALSE(shortReg.isSemanticallyValid());
}

void test_semantic_loaded_config_requires_eeprom()
{
    // EEPROM Loaded and I2C Loaded both need the EEPROM Present bit.
    TEST_ASSERT_TRUE(withSource(5, true).isSemanticallyValid());
    TEST_ASSERT_FALSE(withSource(5, false).isSemanticallyValid());
    TEST_ASSERT_TRUE(withSource(6, true).isSemanticallyValid());
    TEST_ASSERT_FALSE(withSource(6, false).isSemanticallyValid());
    // Other sources do not care.
    TEST_ASSERT_TRUE(withSource(0, false).isSemanticallyValid());
    TEST_ASSERT_TRUE(withSource(7, false).isSemanticallyValid());
}

void test_semantic_crc_fail_without_invalid_is_tolerated()
{
    // Region attempted with CRC fail but not marked invalid is only a warning.
    uint8_t d[SZ] = {0, 0, 0, 0, 0};
    pack(d, 4, 1, 1);   // region 0 attempted
    pack(d, 12, 1, 1);  // region 0 CRC fail
    pack(d, 5, 1, 1);   // region 1 attempted
    pack(d, 13, 1, 1);  // region 1 CRC fail
    TEST_ASSERT_TRUE(E(d).isSemanticallyValid());
}

void test_debug_print_default()
{
    const std::string out = captureDebugPrint(E());
    TEST_ASSERT_TRUE(has(out, "Boot Flags Register (0x2D)"));
    TEST_ASSERT_TRUE(has(out, "Revision ID: 0"));
    TEST_ASSERT_TRUE(has(out, "Patch Config Source: None Loaded"));
    TEST_ASSERT_TRUE(has(out, "System TSD: 0"));
    TEST_ASSERT_TRUE(has(out, "Patch Header Error: 0"));
}

void test_debug_print_with_flags_set()
{
    uint8_t d[SZ] = {0, 0, 0, 0, 0};
    pack(d, 32, 8, 7);
    pack(d, 29, 3, 5);
    pack(d, 19, 1, 1);
    pack(d, 13, 1, 1);
    pack(d, 3, 1, 1);
    pack(d, 2, 1, 1);
    const std::string out = captureDebugPrint(E(d));
    TEST_ASSERT_TRUE(has(out, "Revision ID: 7"));
    TEST_ASSERT_TRUE(has(out, "Patch Config Source: EEPROM Loaded"));
    TEST_ASSERT_TRUE(has(out, "System TSD: 1"));
    TEST_ASSERT_TRUE(has(out, "Region 1 CRC Fail: 1"));
    TEST_ASSERT_TRUE(has(out, "Region 0 CRC Fail: 0"));
    TEST_ASSERT_TRUE(has(out, "I2C EEPROM Present: 1"));
    TEST_ASSERT_TRUE(has(out, "Dead Battery Flag: 1"));
    TEST_ASSERT_TRUE(has(out, "Region 0 Attempted: 0"));
    TEST_ASSERT_TRUE(has(out, "Region 1 Invalid: 0"));
    TEST_ASSERT_TRUE(has(out, "Patch Download Error: 0"));
    TEST_ASSERT_TRUE(has(out, "Region 0 EEPROM Error: 0"));
    TEST_ASSERT_TRUE(has(out, "Region 1 EEPROM Error: 0"));
}

void test_rule_of_five()
{
    uint8_t d[SZ] = {0, 0, 0, 0, 0x11};
    pack(d, 29, 3, 6);
    pack(d, 3, 1, 1);
    E original(d);
    E copy(original);
    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(original.raw(), copy.raw(), SZ);

    E assigned;
    assigned = original;
    TEST_ASSERT_EQUAL(PatchConfigSource::I2CLoaded, assigned.patchConfigSource());

    const uint8_t *buf = original.raw();
    E moved(static_cast<E &&>(original));
    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());

    E target;
    target = static_cast<E &&>(moved);
    TEST_ASSERT_FALSE(moved.isValid());
    TEST_ASSERT_EQUAL_UINT8(0x11, target.revisionId());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_constructors);
    RUN_TEST(test_revision_id_is_byte_4);
    RUN_TEST(test_each_flag_decodes_from_its_own_bit);
    RUN_TEST(test_patch_config_source_all_values);
    RUN_TEST(test_to_string_patch_config_source);
    RUN_TEST(test_validation_tiers);
    RUN_TEST(test_semantic_loaded_config_requires_eeprom);
    RUN_TEST(test_semantic_crc_fail_without_invalid_is_tolerated);
    RUN_TEST(test_debug_print_default);
    RUN_TEST(test_debug_print_with_flags_set);
    RUN_TEST(test_rule_of_five);
    return UNITY_END();
}
