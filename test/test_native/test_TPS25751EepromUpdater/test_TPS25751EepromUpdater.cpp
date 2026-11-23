/**
 * @file test_TPS25751EepromUpdater.cpp
 * @brief Native (host-run) unit tests for the read-only part of
 *        TPS25751EepromUpdater, driven through FakeCommandExecutor.
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751EepromUpdater"
 *
 * Task facts under test (TRM SLVUCR8B pages 81 and 82):
 *   FLrd  in: 4-byte address   out: 16 bytes from DATA byte 0, no return code
 *   FLvy  in: 4-byte address   out: DATA byte 0, 0x00 valid, 0x01 not valid
 * Addresses go out least significant byte first. Layout (SLVAFL1 Figure 2-6):
 * pointers at 0x0000 and 0x0400, bundles at 0x0800 and 0x4400.
 *
 * The last group exercises the fake's write-side model (FLad, FLwd, GAID),
 * which the updater does not use yet, so later write flows can rely on it.
 */

#include <unity.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

#include "TPS25751EepromUpdater.h"
#include "FakeCommandExecutor.h"
#include "CaptureStdout.h"

namespace {

constexpr uint32_t LOW_PTR = 0x0000;
constexpr uint32_t HIGH_PTR = 0x0400;
constexpr uint32_t LOW_START = 0x0800;
constexpr uint32_t HIGH_START = 0x4400;
constexpr size_t BUNDLE_LEN = 0x3800;  // the board's low-region file, 448 x 32 bytes
constexpr size_t FULL_LEN = 0x7C00;    // the board's full-flash image

// A synthetic bundle: Header_ID 0xACE00001 little-endian at offset 0, version
// word 0xF8091166 at offset 0x34, and a non-repeating fill elsewhere.
std::vector<uint8_t> makeBundle(size_t len = BUNDLE_LEN)
{
    std::vector<uint8_t> b(len);
    for (size_t i = 0; i < len; ++i) b[i] = static_cast<uint8_t>((i * 7 + 3) ^ (i >> 8));
    if (len >= 4) { b[0] = 0x01; b[1] = 0x00; b[2] = 0xE0; b[3] = 0xAC; }
    if (len >= 0x38) { b[0x34] = 0x66; b[0x35] = 0x11; b[0x36] = 0x09; b[0x37] = 0xF8; }
    return b;
}

// The full-flash layout of SLVAFL1 Figure 2-6: both pointers valid, both
// AppConfigOffset words 0, the same bundle in both regions.
void loadFullFlash(FakeCommandExecutor &fake, const std::vector<uint8_t> &bundle)
{
    fake.writeLe32(LOW_PTR, LOW_START);
    fake.writeLe32(0x03FC, 0);
    fake.writeLe32(HIGH_PTR, HIGH_START);
    fake.writeLe32(0x07FC, 0);
    fake.writeBytes(LOW_START, bundle.data(), bundle.size());
    fake.writeBytes(HIGH_START, bundle.data(), bundle.size());
}

void assertPayload(const FakeCommandExecutor::Call &c, const char *cmd,
                   std::vector<uint8_t> payload, uint32_t timeoutMs)
{
    TEST_ASSERT_EQUAL_STRING(cmd, c.cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(payload.size(), c.in.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload.data(), c.in.data(), payload.size());
    TEST_ASSERT_EQUAL_UINT32(timeoutMs, c.timeoutMs);
}

size_t writeSideCalls(const FakeCommandExecutor &fake)
{
    return fake.countOf("FLad") + fake.countOf("FLwd") + fake.countOf("GAID");
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Layout -----------------------------------------------------------------

void test_layout_addresses_match_slvafl1()
{
    TEST_ASSERT_EQUAL_HEX32(0x0000, TPS25751EepromUpdater::regionPointerAddress(EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0x0400, TPS25751EepromUpdater::regionPointerAddress(EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0x03FC, TPS25751EepromUpdater::appConfigOffsetAddress(EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0x07FC, TPS25751EepromUpdater::appConfigOffsetAddress(EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0x0800, TPS25751EepromUpdater::bundleStart(EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0x4400, TPS25751EepromUpdater::bundleStart(EepromRegion::High));
}

void test_region_capacity_default_size()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);
    TEST_ASSERT_EQUAL_UINT32(32768, u.eepromSizeBytes());
    TEST_ASSERT_EQUAL_HEX32(0x3C00, u.regionCapacity(EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0x3C00, u.regionCapacity(EepromRegion::High));
}

void test_region_capacity_follows_eeprom_size()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater big(fake, 65536);
    TEST_ASSERT_EQUAL_HEX32(0x3C00, big.regionCapacity(EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0xBC00, big.regionCapacity(EepromRegion::High));

    // An EEPROM that ends inside the low region: high has no room at all.
    TPS25751EepromUpdater small(fake, 0x2000);
    TEST_ASSERT_EQUAL_HEX32(0x1800, small.regionCapacity(EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0, small.regionCapacity(EepromRegion::High));

    TPS25751EepromUpdater tiny(fake, 0x0400);
    TEST_ASSERT_EQUAL_HEX32(0, tiny.regionCapacity(EepromRegion::Low));
}

// --- readFlash ----------------------------------------------------------------

void test_read_flash_sends_flrd_with_little_endian_address()
{
    FakeCommandExecutor fake;
    for (uint32_t i = 0; i < 16; ++i) fake.eeprom[0x4410 + i] = static_cast<uint8_t>(0xA0 + i);
    TPS25751EepromUpdater u(fake);

    uint8_t out[16] = {0};
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.readFlash(0x4410, out));

    TEST_ASSERT_EQUAL_size_t(1, fake.calls.size());
    assertPayload(fake.calls[0], "FLrd", {0x10, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_size_t(16, fake.calls[0].outLen);
    for (uint32_t i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_HEX8(0xA0 + i, out[i]);
}

void test_read_flash_encodes_all_four_address_bytes()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);
    uint8_t out[16];
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.readFlash(0xA1B2C3D4UL, out));
    assertPayload(fake.calls[0], "FLrd", {0xD4, 0xC3, 0xB2, 0xA1}, 200);
    // Past the end of the fake EEPROM: erased bytes.
    for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, out[i]);
}

void test_read_flash_data_starts_at_data_byte_0_and_ignores_return_code()
{
    // FLrd has no return-code byte: DATA[0] is the first memory byte, so a
    // nonzero "return code" must not be read as a failure.
    FakeCommandExecutor fake;
    fake.writeLe32(LOW_PTR, LOW_START);  // 00 08 00 00
    fake.eeprom[0] = 0x5A;               // makes returnCode nonzero
    TPS25751EepromUpdater u(fake);

    uint8_t out[16];
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.readFlash(LOW_PTR, out));
    TEST_ASSERT_EQUAL_HEX8(0x5A, fake.calls[0].result.returnCode);
    TEST_ASSERT_EQUAL_HEX8(0x5A, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x08, out[1]);
}

void test_read_flash_executor_failures_map_and_leave_output()
{
    const struct {
        FakeCommandExecutor::Fault fault;
        EepromUpdateStatus expected;
    } cases[] = {
        {FakeCommandExecutor::Fault::rejected(), EepromUpdateStatus::CommandRejected},
        {FakeCommandExecutor::Fault::timeout(), EepromUpdateStatus::CommandTimeout},
        {FakeCommandExecutor::Fault::i2cError(), EepromUpdateStatus::I2cError},
    };
    for (const auto &c : cases) {
        FakeCommandExecutor fake;
        fake.failCall(0, c.fault);
        TPS25751EepromUpdater u(fake);
        uint8_t out[16];
        memset(out, 0xEE, sizeof(out));
        TEST_ASSERT_EQUAL(c.expected, u.readFlash(0, out));
        for (int i = 0; i < 16; ++i) TEST_ASSERT_EQUAL_HEX8(0xEE, out[i]);
    }
}

// --- verifyBundle -------------------------------------------------------------

void test_verify_bundle_valid()
{
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    TPS25751EepromUpdater u(fake);

    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.verifyBundle(HIGH_START));
    TEST_ASSERT_EQUAL_size_t(1, fake.calls.size());
    assertPayload(fake.calls[0], "FLvy", {0x00, 0x44, 0x00, 0x00}, 1000);
    TEST_ASSERT_EQUAL_HEX8(0x00, fake.calls[0].result.returnCode);
}

void test_verify_bundle_not_valid()
{
    FakeCommandExecutor fake;  // erased: no header at 0x0800
    TPS25751EepromUpdater u(fake);
    TEST_ASSERT_EQUAL(EepromUpdateStatus::VerifyFailed, u.verifyBundle(LOW_START));
    assertPayload(fake.calls[0], "FLvy", {0x00, 0x08, 0x00, 0x00}, 1000);
    TEST_ASSERT_EQUAL_HEX8(0x01, fake.calls[0].result.returnCode);
}

void test_verify_bundle_undocumented_code_is_not_valid()
{
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.failNth("FLvy", 0, FakeCommandExecutor::Fault::returnCodeOnly(0x02));
    TPS25751EepromUpdater u(fake);
    TEST_ASSERT_EQUAL(EepromUpdateStatus::VerifyFailed, u.verifyBundle(LOW_START));
}

void test_verify_bundle_executor_failures()
{
    FakeCommandExecutor rejected, timeout, i2c;
    rejected.failCall(0, FakeCommandExecutor::Fault::rejected());
    timeout.failCall(0, FakeCommandExecutor::Fault::timeout());
    i2c.failCall(0, FakeCommandExecutor::Fault::i2cError());
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CommandRejected, TPS25751EepromUpdater(rejected).verifyBundle(LOW_START));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CommandTimeout, TPS25751EepromUpdater(timeout).verifyBundle(LOW_START));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::I2cError, TPS25751EepromUpdater(i2c).verifyBundle(LOW_START));
}

// --- readRegionPointer --------------------------------------------------------

void test_read_region_pointers()
{
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    TPS25751EepromUpdater u(fake);

    uint32_t low = 0, high = 0;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.readRegionPointer(EepromRegion::Low, low));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.readRegionPointer(EepromRegion::High, high));
    TEST_ASSERT_EQUAL_HEX32(0x00000800, low);
    TEST_ASSERT_EQUAL_HEX32(0x00004400, high);
    assertPayload(fake.calls[0], "FLrd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(fake.calls[1], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
}

void test_read_region_pointer_is_little_endian()
{
    FakeCommandExecutor fake;
    const uint8_t word[4] = {0x78, 0x56, 0x34, 0x12};
    fake.writeBytes(HIGH_PTR, word, 4);
    TPS25751EepromUpdater u(fake);
    uint32_t v = 0;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.readRegionPointer(EepromRegion::High, v));
    TEST_ASSERT_EQUAL_HEX32(0x12345678, v);
}

void test_read_region_pointer_failure_leaves_value()
{
    FakeCommandExecutor fake;
    fake.failCall(0, FakeCommandExecutor::Fault::timeout());
    TPS25751EepromUpdater u(fake);
    uint32_t v = 0xDEADBEEF;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CommandTimeout, u.readRegionPointer(EepromRegion::Low, v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, v);
}

// --- selectTargetRegion -------------------------------------------------------

void test_select_both_valid_healthy_low_targets_high()
{
    // Row 1, Figure 2-2 "No" branch: low booted cleanly, so update high.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::Low;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::High, target);
    TEST_ASSERT_EQUAL_size_t(1, fake.bootFlagsReads);
    TEST_ASSERT_EQUAL_size_t(2, fake.countOf("FLrd"));
    TEST_ASSERT_EQUAL_size_t(0, writeSideCalls(fake));
}

void test_select_both_valid_region0_invalid_targets_low()
{
    // Row 1, Figure 2-2 "Yes" branch via region0Invalid.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.setBootBit(FakeCommandExecutor::kBitRegion0Invalid, true);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::High;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
}

void test_select_both_valid_region0_eeprom_error_targets_low()
{
    // Row 1, Figure 2-2 "Yes" branch via region0EepromError.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.setBootBit(FakeCommandExecutor::kBitRegion0EepromError, true);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::High;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
}

void test_select_region1_flags_do_not_affect_row_1()
{
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.setBootBit(FakeCommandExecutor::kBitRegion1Invalid, true);
    fake.setBootBit(FakeCommandExecutor::kBitRegion1EepromError, true);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::Low;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::High, target);
}

void test_select_low_valid_high_not_valid_targets_high()
{
    // Row 2. BOOT_STATUS says region 0 is bad, but it is not consulted.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.writeLe32(HIGH_PTR, 0);
    fake.setBootBit(FakeCommandExecutor::kBitRegion0Invalid, true);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::Low;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::High, target);
    TEST_ASSERT_EQUAL_size_t(0, fake.bootFlagsReads);
}

void test_select_low_not_valid_high_valid_targets_low()
{
    // Row 3.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.writeLe32(LOW_PTR, 0);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::High;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
    TEST_ASSERT_EQUAL_size_t(0, fake.bootFlagsReads);
}

void test_select_stale_boot_status_follows_pointers()
{
    // After a high-region update without a reset, BOOT_STATUS still describes
    // the boot from low (healthy), but the low pointer is now 0 and only the
    // high pointer is valid. Figure 2-2 alone would choose High and erase the
    // only valid pointer; the pointer-first rule chooses Low.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.writeLe32(LOW_PTR, 0);
    fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
    fake.setPatchConfigSource(5);  // EEPROM loaded
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::High;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
    TEST_ASSERT_EQUAL_size_t(0, fake.bootFlagsReads);
}

void test_select_neither_valid_is_no_bootable_region()
{
    // Row 4: erased (0xFFFFFFFF), zero, and off-by-one pointers are all invalid.
    const uint32_t lows[] = {0xFFFFFFFFUL, 0x00000000UL, 0x00000801UL};
    const uint32_t highs[] = {0xFFFFFFFFUL, 0x00000000UL, 0x00004401UL};
    for (size_t i = 0; i < 3; ++i) {
        FakeCommandExecutor fake;
        fake.writeLe32(LOW_PTR, lows[i]);
        fake.writeLe32(HIGH_PTR, highs[i]);
        TPS25751EepromUpdater u(fake);
        EepromRegion target = EepromRegion::High;
        TEST_ASSERT_EQUAL(EepromUpdateStatus::NoBootableRegion, u.selectTargetRegion(target));
        TEST_ASSERT_EQUAL(EepromRegion::High, target);
        TEST_ASSERT_EQUAL_size_t(0, fake.bootFlagsReads);
    }
}

void test_select_boot_status_read_failure()
{
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.bootFlagsReadFails = true;
    TPS25751EepromUpdater u(fake);
    EepromRegion target = EepromRegion::Low;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::I2cError, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
}

void test_select_pointer_read_failures()
{
    FakeCommandExecutor lowFails;
    loadFullFlash(lowFails, makeBundle());
    lowFails.failCall(0, FakeCommandExecutor::Fault::rejected());
    EepromRegion target = EepromRegion::Low;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CommandRejected,
                      TPS25751EepromUpdater(lowFails).selectTargetRegion(target));
    TEST_ASSERT_EQUAL_size_t(1, lowFails.calls.size());

    FakeCommandExecutor highFails;
    loadFullFlash(highFails, makeBundle());
    highFails.failCall(1, FakeCommandExecutor::Fault::i2cError());
    TEST_ASSERT_EQUAL(EepromUpdateStatus::I2cError,
                      TPS25751EepromUpdater(highFails).selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
}

// --- compareRegion ------------------------------------------------------------

void test_compare_region_match_low_and_high()
{
    const std::vector<uint8_t> bundle = makeBundle();
    FakeCommandExecutor fake;
    loadFullFlash(fake, bundle);
    TPS25751EepromUpdater u(fake);

    uint32_t mismatch = 0xDEADBEEF;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success,
                      u.compareRegion(EepromRegion::Low, bundle.data(), bundle.size(), &mismatch));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, mismatch);
    TEST_ASSERT_EQUAL_size_t(BUNDLE_LEN / 16, fake.calls.size());
    assertPayload(fake.calls[0], "FLrd", {0x00, 0x08, 0x00, 0x00}, 200);
    assertPayload(fake.calls[1], "FLrd", {0x10, 0x08, 0x00, 0x00}, 200);
    assertPayload(fake.calls.back(), "FLrd", {0xF0, 0x3F, 0x00, 0x00}, 200);

    fake.calls.clear();
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success,
                      u.compareRegion(EepromRegion::High, bundle.data(), bundle.size()));
    assertPayload(fake.calls[0], "FLrd", {0x00, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_size_t(0, writeSideCalls(fake));
}

void test_compare_region_mismatch_reports_image_offset()
{
    const std::vector<uint8_t> bundle = makeBundle();
    FakeCommandExecutor fake;
    loadFullFlash(fake, bundle);
    fake.eeprom[HIGH_START + 0x1234] ^= 0x01;
    TPS25751EepromUpdater u(fake);

    uint32_t mismatch = 0;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CompareMismatch,
                      u.compareRegion(EepromRegion::High, bundle.data(), bundle.size(), &mismatch));
    TEST_ASSERT_EQUAL_HEX32(0x1234, mismatch);
    // Stops at the chunk holding the first difference.
    TEST_ASSERT_EQUAL_size_t(0x1234 / 16 + 1, fake.calls.size());

    // Without a firstMismatch pointer the status is the same.
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CompareMismatch,
                      u.compareRegion(EepromRegion::High, bundle.data(), bundle.size()));
    // The low region is untouched and still matches.
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success,
                      u.compareRegion(EepromRegion::Low, bundle.data(), bundle.size()));
}

void test_compare_region_length_not_multiple_of_16()
{
    // 37 bytes: two full reads and one read of which only 5 bytes count. The
    // EEPROM byte after the image differs and must be ignored.
    const std::vector<uint8_t> bundle = makeBundle(0x100);
    FakeCommandExecutor fake;
    fake.writeBytes(LOW_START, bundle.data(), 37);
    fake.eeprom[LOW_START + 37] = static_cast<uint8_t>(bundle[37] ^ 0xFF);
    TPS25751EepromUpdater u(fake);

    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success,
                      u.compareRegion(EepromRegion::Low, bundle.data(), 37));
    TEST_ASSERT_EQUAL_size_t(3, fake.calls.size());
    assertPayload(fake.calls[2], "FLrd", {0x20, 0x08, 0x00, 0x00}, 200);

    // A difference in the short final chunk is found at its offset.
    fake.eeprom[LOW_START + 36] ^= 0x80;
    uint32_t mismatch = 0;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CompareMismatch,
                      u.compareRegion(EepromRegion::Low, bundle.data(), 37, &mismatch));
    TEST_ASSERT_EQUAL_UINT32(36, mismatch);
}

void test_compare_region_rejects_bad_arguments()
{
    const std::vector<uint8_t> big = makeBundle(0x3C01);
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);

    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, u.compareRegion(EepromRegion::Low, nullptr, 16));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, u.compareRegion(EepromRegion::Low, big.data(), 0));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength,
                      u.compareRegion(EepromRegion::High, big.data(), big.size()));
    TEST_ASSERT_EQUAL_size_t(0, fake.calls.size());

    // Exactly the capacity is allowed (erased EEPROM vs. a non-erased image: mismatch).
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CompareMismatch,
                      u.compareRegion(EepromRegion::High, big.data(), 0x3C00));
}

void test_compare_region_executor_failure_midway()
{
    const std::vector<uint8_t> bundle = makeBundle();
    FakeCommandExecutor fake;
    loadFullFlash(fake, bundle);
    fake.failCall(5, FakeCommandExecutor::Fault::timeout());
    TPS25751EepromUpdater u(fake);

    uint32_t mismatch = 0xDEADBEEF;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CommandTimeout,
                      u.compareRegion(EepromRegion::Low, bundle.data(), bundle.size(), &mismatch));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, mismatch);
    TEST_ASSERT_EQUAL_size_t(6, fake.calls.size());
}

// --- dump -------------------------------------------------------------------

void test_dump_formats_lines_for_xxd()
{
    FakeCommandExecutor fake;
    for (uint32_t i = 0; i < 40; ++i) fake.eeprom[i] = static_cast<uint8_t>(i * 0x0B);
    TPS25751EepromUpdater u(fake);

    EepromUpdateStatus s = EepromUpdateStatus::I2cError;
    const std::string out = captureStdout([&] { s = u.dump(0, 40, Serial); });
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, s);
    TEST_ASSERT_EQUAL_STRING(
        "00000000: 00 0b 16 21 2c 37 42 4d 58 63 6e 79 84 8f 9a a5\n"
        "00000010: b0 bb c6 d1 dc e7 f2 fd 08 13 1e 29 34 3f 4a 55\n"
        "00000020: 60 6b 76 81 8c 97 a2 ad\n",
        out.c_str());
    TEST_ASSERT_EQUAL_size_t(3, fake.calls.size());
}

void test_dump_prints_absolute_addresses()
{
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    TPS25751EepromUpdater u(fake);

    const std::string out = captureStdout([&] { u.dump(0x43F0, 32, Serial); });
    TEST_ASSERT_EQUAL_STRING(
        "000043f0: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff\n"
        "00004400: 01 00 e0 ac 1f 26 2d 34 3b 42 49 50 57 5e 65 6c\n",
        out.c_str());
}

void test_dump_zero_length_prints_nothing()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);
    EepromUpdateStatus s = EepromUpdateStatus::I2cError;
    const std::string out = captureStdout([&] { s = u.dump(0, 0, Serial); });
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, s);
    TEST_ASSERT_EQUAL_STRING("", out.c_str());
    TEST_ASSERT_EQUAL_size_t(0, fake.calls.size());
}

void test_dump_stops_at_first_failure()
{
    FakeCommandExecutor fake;
    fake.failCall(1, FakeCommandExecutor::Fault::rejected());
    TPS25751EepromUpdater u(fake);
    EepromUpdateStatus s = EepromUpdateStatus::Success;
    const std::string out = captureStdout([&] { s = u.dump(0, 64, Serial); });
    TEST_ASSERT_EQUAL(EepromUpdateStatus::CommandRejected, s);
    TEST_ASSERT_EQUAL_STRING("00000000: ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff\n", out.c_str());
    TEST_ASSERT_EQUAL_size_t(2, fake.calls.size());
}

void test_dump_full_image_round_trips()
{
    // Dump 0x0000-0x7C00 as the example does and parse it back the way
    // xxd -r would: every line's address and bytes must rebuild the EEPROM.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    TPS25751EepromUpdater u(fake);

    EepromUpdateStatus s = EepromUpdateStatus::I2cError;
    const std::string out = captureStdout([&] { s = u.dump(0, FULL_LEN, Serial); });
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, s);
    TEST_ASSERT_EQUAL_size_t(FULL_LEN / 16, fake.calls.size());

    std::vector<uint8_t> rebuilt(FULL_LEN, 0x00);
    size_t lines = 0;
    size_t pos = 0;
    while (pos < out.size()) {
        const size_t eol = out.find('\n', pos);
        TEST_ASSERT_TRUE(eol != std::string::npos);
        const std::string line = out.substr(pos, eol - pos);
        pos = eol + 1;
        TEST_ASSERT_EQUAL_size_t(57, line.size());
        TEST_ASSERT_EQUAL_INT(':', line[8]);
        const unsigned long addr = strtoul(line.substr(0, 8).c_str(), nullptr, 16);
        TEST_ASSERT_EQUAL_UINT32(lines * 16, addr);
        for (size_t i = 0; i < 16; ++i) {
            TEST_ASSERT_EQUAL_INT(' ', line[9 + i * 3]);
            rebuilt[addr + i] = static_cast<uint8_t>(strtoul(line.substr(10 + i * 3, 2).c_str(), nullptr, 16));
        }
        ++lines;
    }
    TEST_ASSERT_EQUAL_size_t(FULL_LEN / 16, lines);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(fake.eeprom.data(), rebuilt.data(), FULL_LEN);
    TEST_ASSERT_EQUAL_size_t(0, writeSideCalls(fake));
}

// --- checkImage and imageVersion ------------------------------------------------

void test_check_image_accepts_valid_bundle()
{
    const std::vector<uint8_t> b = makeBundle();
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, TPS25751EepromUpdater::checkImage(b.data(), b.size(), 0x3C00));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, TPS25751EepromUpdater::checkImage(b.data(), 0x38, 0x3C00));
    const std::vector<uint8_t> full = makeBundle(0x3C00);
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, TPS25751EepromUpdater::checkImage(full.data(), full.size(), 0x3C00));
}

void test_check_image_rejects_bad_length()
{
    const std::vector<uint8_t> b = makeBundle(0x3C01);
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, TPS25751EepromUpdater::checkImage(nullptr, 0x100, 0x3C00));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, TPS25751EepromUpdater::checkImage(b.data(), 0, 0x3C00));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, TPS25751EepromUpdater::checkImage(b.data(), 0x37, 0x3C00));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, TPS25751EepromUpdater::checkImage(b.data(), 0x3C01, 0x3C00));
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength, TPS25751EepromUpdater::checkImage(b.data(), 0x100, 0x80));
}

void test_check_image_rejects_bad_header()
{
    std::vector<uint8_t> b = makeBundle();
    // Big-endian header bytes: the word reads 0x0100E0AC, not 0xACE00001.
    b[0] = 0xAC; b[1] = 0xE0; b[2] = 0x00; b[3] = 0x01;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageHeader, TPS25751EepromUpdater::checkImage(b.data(), b.size(), 0x3C00));
    std::vector<uint8_t> erased(0x100, 0xFF);
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageHeader, TPS25751EepromUpdater::checkImage(erased.data(), erased.size(), 0x3C00));
}

void test_image_version()
{
    const std::vector<uint8_t> b = makeBundle();
    TEST_ASSERT_EQUAL_HEX32(0xF8091166, TPS25751EepromUpdater::imageVersion(b.data(), b.size()));
    TEST_ASSERT_EQUAL_HEX32(0xF8091166, TPS25751EepromUpdater::imageVersion(b.data(), 0x38));
    TEST_ASSERT_EQUAL_HEX32(0, TPS25751EepromUpdater::imageVersion(b.data(), 0x37));
    TEST_ASSERT_EQUAL_HEX32(0, TPS25751EepromUpdater::imageVersion(nullptr, 0x100));

    std::vector<uint8_t> prev = makeBundle();
    prev[0x34] = 0x64;  // the previous build, F8091164
    TEST_ASSERT_EQUAL_HEX32(0xF8091164, TPS25751EepromUpdater::imageVersion(prev.data(), prev.size()));
}

// --- toString -----------------------------------------------------------------

void test_to_string_covers_every_status()
{
    const struct {
        EepromUpdateStatus s;
        const char *name;
    } all[] = {
        {EepromUpdateStatus::Success, "Success"},
        {EepromUpdateStatus::NotInAppMode, "NotInAppMode"},
        {EepromUpdateStatus::NoEepromPresent, "NoEepromPresent"},
        {EepromUpdateStatus::BadImageHeader, "BadImageHeader"},
        {EepromUpdateStatus::BadImageLength, "BadImageLength"},
        {EepromUpdateStatus::NoBootableRegion, "NoBootableRegion"},
        {EepromUpdateStatus::EepromNotBlank, "EepromNotBlank"},
        {EepromUpdateStatus::PointerEraseFailed, "PointerEraseFailed"},
        {EepromUpdateStatus::SetAddressFailed, "SetAddressFailed"},
        {EepromUpdateStatus::WriteFailed, "WriteFailed"},
        {EepromUpdateStatus::FlashBusy, "FlashBusy"},
        {EepromUpdateStatus::VerifyFailed, "VerifyFailed"},
        {EepromUpdateStatus::CompareMismatch, "CompareMismatch"},
        {EepromUpdateStatus::PointerRestoreFailed, "PointerRestoreFailed"},
        {EepromUpdateStatus::OldPointerEraseFailed, "OldPointerEraseFailed"},
        {EepromUpdateStatus::PointerNeighborsChanged, "PointerNeighborsChanged"},
        {EepromUpdateStatus::ResetTimeout, "ResetTimeout"},
        {EepromUpdateStatus::CommandRejected, "CommandRejected"},
        {EepromUpdateStatus::CommandTimeout, "CommandTimeout"},
        {EepromUpdateStatus::I2cError, "I2cError"},
    };
    for (const auto &e : all) TEST_ASSERT_EQUAL_STRING(e.name, TPS25751EepromUpdater::toString(e.s));
    TEST_ASSERT_EQUAL_UINT8(20, sizeof(all) / sizeof(all[0]));
    TEST_ASSERT_EQUAL_UINT8(19, static_cast<uint8_t>(EepromUpdateStatus::I2cError));
    TEST_ASSERT_EQUAL_STRING("Unknown", TPS25751EepromUpdater::toString(static_cast<EepromUpdateStatus>(200)));
}

void test_to_string_region()
{
    TEST_ASSERT_EQUAL_STRING("Low", TPS25751EepromUpdater::toString(EepromRegion::Low));
    TEST_ASSERT_EQUAL_STRING("High", TPS25751EepromUpdater::toString(EepromRegion::High));
}

// --- FakeCommandExecutor write-side model ----------------------------------------

void test_fake_flad_flwd_write_and_auto_increment()
{
    FakeCommandExecutor fake;
    const uint8_t addr[4] = {0x00, 0x44, 0x00, 0x00};
    const uint8_t a[4] = {1, 2, 3, 4};
    const uint8_t b[3] = {5, 6, 7};
    TPS25751TaskResult r = fake.executeCommand(TPS25751FourCC::of("FLad"), addr, 4, nullptr, 0, 200);
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success, r.status);
    TEST_ASSERT_EQUAL_HEX8(0x00, r.returnCode);
    r = fake.executeCommand(TPS25751FourCC::of("FLwd"), a, 4, nullptr, 0, 200);
    TEST_ASSERT_EQUAL_HEX8(0x00, r.returnCode);
    r = fake.executeCommand(TPS25751FourCC::of("FLwd"), b, 3, nullptr, 0, 200);
    TEST_ASSERT_EQUAL_HEX8(0x00, r.returnCode);
    const uint8_t expected[8] = {1, 2, 3, 4, 5, 6, 7, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, &fake.eeprom[0x4400], 8);
    TEST_ASSERT_EQUAL_HEX32(0x4407, fake.writeAddress);
}

void test_fake_flwd_without_flad_and_out_of_range_are_rejected()
{
    FakeCommandExecutor fake;
    const uint8_t d[2] = {0, 0};
    TEST_ASSERT_EQUAL_HEX8(0x03, fake.executeCommand(TPS25751FourCC::of("FLwd"), d, 2, nullptr, 0, 200).returnCode);
    const uint8_t past[4] = {0x00, 0x80, 0x00, 0x00};  // 0x8000 = end of 32 KB
    TEST_ASSERT_EQUAL_HEX8(0x03, fake.executeCommand(TPS25751FourCC::of("FLad"), past, 4, nullptr, 0, 200).returnCode);
    const uint8_t last[4] = {0xFF, 0x7F, 0x00, 0x00};
    TEST_ASSERT_EQUAL_HEX8(0x00, fake.executeCommand(TPS25751FourCC::of("FLad"), last, 4, nullptr, 0, 200).returnCode);
    TEST_ASSERT_EQUAL_HEX8(0x03, fake.executeCommand(TPS25751FourCC::of("FLwd"), d, 2, nullptr, 0, 200).returnCode);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake.eeprom[0x7FFF]);
}

void test_fake_busy_fault_writes_nothing_and_hook_runs()
{
    FakeCommandExecutor fake;
    const uint8_t addr[4] = {0x00, 0x08, 0x00, 0x00};
    const uint8_t d[4] = {9, 9, 9, 9};
    fake.failNth("FLwd", 0, FakeCommandExecutor::Fault::busy());
    fake.failNth("FLwd", 1, FakeCommandExecutor::Fault::withHook(
        [](FakeCommandExecutor &f) { f.eeprom[0x0804] = 0x42; }));
    fake.executeCommand(TPS25751FourCC::of("FLad"), addr, 4, nullptr, 0, 200);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake.executeCommand(TPS25751FourCC::of("FLwd"), d, 4, nullptr, 0, 200).returnCode);
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake.eeprom[0x0800]);
    TEST_ASSERT_EQUAL_HEX8(0x00, fake.executeCommand(TPS25751FourCC::of("FLwd"), d, 4, nullptr, 0, 200).returnCode);
    TEST_ASSERT_EQUAL_HEX8(9, fake.eeprom[0x0800]);
    TEST_ASSERT_EQUAL_HEX8(0x42, fake.eeprom[0x0804]);
}

void test_fake_failure_with_run_task_still_writes()
{
    FakeCommandExecutor fake;
    const uint8_t addr[4] = {0x00, 0x00, 0x00, 0x00};
    const uint8_t d[4] = {0x00, 0x00, 0x00, 0x00};
    FakeCommandExecutor::Fault f = FakeCommandExecutor::Fault::timeout();
    f.runTask = true;
    fake.failNth("FLwd", 0, f);
    fake.executeCommand(TPS25751FourCC::of("FLad"), addr, 4, nullptr, 0, 200);
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Timeout,
                      fake.executeCommand(TPS25751FourCC::of("FLwd"), d, 4, nullptr, 0, 200).status);
    TEST_ASSERT_EQUAL_HEX32(0, fake.readLe32(0));
}

void test_fake_gaid_reboot_and_clock()
{
    FakeCommandExecutor fake;
    fake.rebootDurationMs = 300;
    fake.msPerCommand = 2;
    fake.setModeAfterReset("PTCH");
    TPS25751Mode mode;
    TEST_ASSERT_TRUE(fake.readMode(mode));
    TEST_ASSERT_TRUE(mode.isApplicationMode());

    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success,
                      fake.executeCommand(TPS25751FourCC::of("GAID"), nullptr, 0, nullptr, 0, 200).status);
    TEST_ASSERT_EQUAL_size_t(1, fake.resetCount);
    TEST_ASSERT_EQUAL_UINT32(2, fake.nowMs());
    TEST_ASSERT_FALSE(fake.readMode(mode));
    TPS25751BootFlags flags;
    TEST_ASSERT_FALSE(fake.readBootFlags(flags));
    fake.delayMs(299);
    TEST_ASSERT_FALSE(fake.readMode(mode));
    fake.delayMs(1);
    TEST_ASSERT_TRUE(fake.readMode(mode));
    TEST_ASSERT_TRUE(mode.isPatchMode());
    TEST_ASSERT_EQUAL_size_t(2, fake.delays.size());

    fake.modeReadFails = true;
    TEST_ASSERT_FALSE(fake.readMode(mode));
}

void test_fake_unknown_code_is_rejected_and_boot_status_reads()
{
    FakeCommandExecutor fake;
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Rejected,
                      fake.executeCommand(TPS25751FourCC::of("XXXX"), nullptr, 0, nullptr, 0, 200).status);
    fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
    fake.setBootBit(FakeCommandExecutor::kBitRegion0Invalid, true);
    fake.setBootBit(FakeCommandExecutor::kBitRegion0Invalid, false);
    fake.setPatchConfigSource(5);
    TPS25751BootFlags flags;
    TEST_ASSERT_TRUE(fake.readBootFlags(flags));
    TEST_ASSERT_TRUE(flags.i2cEepromPresent());
    TEST_ASSERT_FALSE(flags.region0Invalid());
    TEST_ASSERT_EQUAL(PatchConfigSource::EEPROMLoaded, flags.patchConfigSource());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_layout_addresses_match_slvafl1);
    RUN_TEST(test_region_capacity_default_size);
    RUN_TEST(test_region_capacity_follows_eeprom_size);

    RUN_TEST(test_read_flash_sends_flrd_with_little_endian_address);
    RUN_TEST(test_read_flash_encodes_all_four_address_bytes);
    RUN_TEST(test_read_flash_data_starts_at_data_byte_0_and_ignores_return_code);
    RUN_TEST(test_read_flash_executor_failures_map_and_leave_output);

    RUN_TEST(test_verify_bundle_valid);
    RUN_TEST(test_verify_bundle_not_valid);
    RUN_TEST(test_verify_bundle_undocumented_code_is_not_valid);
    RUN_TEST(test_verify_bundle_executor_failures);

    RUN_TEST(test_read_region_pointers);
    RUN_TEST(test_read_region_pointer_is_little_endian);
    RUN_TEST(test_read_region_pointer_failure_leaves_value);

    RUN_TEST(test_select_both_valid_healthy_low_targets_high);
    RUN_TEST(test_select_both_valid_region0_invalid_targets_low);
    RUN_TEST(test_select_both_valid_region0_eeprom_error_targets_low);
    RUN_TEST(test_select_region1_flags_do_not_affect_row_1);
    RUN_TEST(test_select_low_valid_high_not_valid_targets_high);
    RUN_TEST(test_select_low_not_valid_high_valid_targets_low);
    RUN_TEST(test_select_stale_boot_status_follows_pointers);
    RUN_TEST(test_select_neither_valid_is_no_bootable_region);
    RUN_TEST(test_select_boot_status_read_failure);
    RUN_TEST(test_select_pointer_read_failures);

    RUN_TEST(test_compare_region_match_low_and_high);
    RUN_TEST(test_compare_region_mismatch_reports_image_offset);
    RUN_TEST(test_compare_region_length_not_multiple_of_16);
    RUN_TEST(test_compare_region_rejects_bad_arguments);
    RUN_TEST(test_compare_region_executor_failure_midway);

    RUN_TEST(test_dump_formats_lines_for_xxd);
    RUN_TEST(test_dump_prints_absolute_addresses);
    RUN_TEST(test_dump_zero_length_prints_nothing);
    RUN_TEST(test_dump_stops_at_first_failure);
    RUN_TEST(test_dump_full_image_round_trips);

    RUN_TEST(test_check_image_accepts_valid_bundle);
    RUN_TEST(test_check_image_rejects_bad_length);
    RUN_TEST(test_check_image_rejects_bad_header);
    RUN_TEST(test_image_version);

    RUN_TEST(test_to_string_covers_every_status);
    RUN_TEST(test_to_string_region);

    RUN_TEST(test_fake_flad_flwd_write_and_auto_increment);
    RUN_TEST(test_fake_flwd_without_flad_and_out_of_range_are_rejected);
    RUN_TEST(test_fake_busy_fault_writes_nothing_and_hook_runs);
    RUN_TEST(test_fake_failure_with_run_task_still_writes);
    RUN_TEST(test_fake_gaid_reboot_and_clock);
    RUN_TEST(test_fake_unknown_code_is_rejected_and_boot_status_reads);

    return UNITY_END();
}
