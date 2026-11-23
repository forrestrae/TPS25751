/**
 * @file test_TPS25751EepromUpdater.cpp
 * @brief Native (host-run) unit tests for TPS25751EepromUpdater, driven
 *        through FakeCommandExecutor.
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751EepromUpdater"
 *
 * Task facts under test (TRM SLVUCR8B pages 61, 62, 81, and 82):
 *   FLrd  in: 4-byte address   out: 16 bytes from DATA byte 0, no return code
 *   FLad  in: 4-byte address   out: DATA byte 0, standard task return code
 *   FLwd  in: up to 64 bytes   out: DATA byte 0, 0x00 written, 0xFF busy
 *   FLvy  in: 4-byte address   out: DATA byte 0, 0x00 valid, 0x01 not valid
 *   GAID  in: none             out: none; the controller restarts
 * Addresses go out least significant byte first: Table 4-20 states it for
 * FLad, and hardware stage 1 verified it for FLrd on 2026-11-22.
 * Layout (SLVAFL1 Figure 2-6):
 * pointers at 0x0000 and 0x0400, bundles at 0x0800 and 0x4400.
 *
 * The write-path tests build the expected task sequence from the plan
 * (expectedUpdate()) and compare it with the fake's call log, then check the
 * fake EEPROM against the state the plan's status table gives for each result.
 * Return codes that stand for a failure are injected; no test relies on what
 * the fake itself answers where the TRM is silent. The last group exercises the
 * fake's own write-side model, and says where it pins the fake and not the
 * device (see FakeCommandExecutor.h).
 */

#include <unity.h>
#include <functional>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// --- Write-path helpers ---------------------------------------------------------

using Fault = FakeCommandExecutor::Fault;

constexpr size_t SMALL_LEN = 100;  // three 32-byte chunks and one of 4; seven compare reads

// Call indices of updateRegion() for a SMALL_LEN image with the default
// options, counted from the plan: one pre-flight read of the other pointer,
// four tasks per pointer write, FLad plus four chunks, FLvy, seven compare
// reads, then the pointer writes of steps 3 and 4.
constexpr size_t IDX_PREFLIGHT_READ = 0;
constexpr size_t IDX_STEP1_READ = 1;
constexpr size_t IDX_STEP1_FLAD = 2;
constexpr size_t IDX_STEP1_FLWD = 3;
constexpr size_t IDX_STEP1_READBACK = 4;
constexpr size_t IDX_BUNDLE_FLAD = 5;
constexpr size_t IDX_FIRST_CHUNK = 6;
constexpr size_t IDX_FLVY = 10;
constexpr size_t IDX_FIRST_COMPARE = 11;
constexpr size_t IDX_STEP3_READ = 18;
constexpr size_t IDX_STEP3_FLAD = 19;
constexpr size_t IDX_STEP3_FLWD = 20;
constexpr size_t IDX_STEP3_READBACK = 21;
constexpr size_t IDX_STEP4_READ = 22;
constexpr size_t IDX_STEP4_FLAD = 23;
constexpr size_t IDX_STEP4_FLWD = 24;
constexpr size_t IDX_STEP4_READBACK = 25;
constexpr size_t SMALL_UPDATE_CALLS = 26;

// A second synthetic bundle, standing for the new image: the same header, the
// previous build's version word 0xF8091164, and a different fill.
std::vector<uint8_t> makeNewBundle(size_t len = BUNDLE_LEN)
{
    std::vector<uint8_t> b(len);
    for (size_t i = 0; i < len; ++i) b[i] = static_cast<uint8_t>((i * 13 + 0x55) ^ (i >> 7));
    if (len >= 4) { b[0] = 0x01; b[1] = 0x00; b[2] = 0xE0; b[3] = 0xAC; }
    if (len >= 0x38) { b[0x34] = 0x64; b[0x35] = 0x11; b[0x36] = 0x09; b[0x37] = 0xF8; }
    return b;
}

// A board in the state of SLVAFL1 Figure 2-6: the old bundle in both regions,
// both pointers valid, MODE reading APP, BOOT_FLAGS reporting the EEPROM.
struct Board
{
    FakeCommandExecutor fake;
    std::vector<uint8_t> oldBundle;
    std::vector<uint8_t> newBundle;
    std::vector<uint8_t> before;  // EEPROM contents before the call under test

    explicit Board(size_t len = SMALL_LEN, size_t eepromSize = 32768)
        : fake(eepromSize), oldBundle(makeBundle(len)), newBundle(makeNewBundle(len))
    {
        loadFullFlash(fake, oldBundle);
        fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
        snapshot();
    }

    void snapshot() { before = fake.eeprom; }
};

const char *at(const char *what, size_t index, const char *detail = "")
{
    static char buf[200];
    snprintf(buf, sizeof(buf), "%s %u %s", what, static_cast<unsigned>(index), detail);
    return buf;
}

uint32_t pointerOf(const FakeCommandExecutor &f, EepromRegion r)
{
    return f.readLe32(TPS25751EepromUpdater::regionPointerAddress(r));
}

bool pointerValid(const FakeCommandExecutor &f, EepromRegion r)
{
    return pointerOf(f, r) == TPS25751EepromUpdater::bundleStart(r);
}

bool regionHolds(const FakeCommandExecutor &f, EepromRegion r, const std::vector<uint8_t> &bundle)
{
    return memcmp(&f.eeprom[TPS25751EepromUpdater::bundleStart(r)], bundle.data(), bundle.size()) == 0;
}

struct Range
{
    uint32_t addr;
    size_t len;
};

// Every EEPROM byte outside the given ranges equals the snapshot.
void assertUnchangedOutside(const Board &b, const std::vector<Range> &ranges, const char *message = nullptr)
{
    std::vector<uint8_t> expected = b.before;
    for (const Range &r : ranges)
        for (size_t i = 0; i < r.len; ++i) expected[r.addr + i] = b.fake.eeprom[r.addr + i];
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected.data(), b.fake.eeprom.data(), expected.size(), message);
}

void assertUnchanged(const Board &b, const char *message = nullptr)
{
    assertUnchangedOutside(b, {}, message);
}

// What must hold after any failure, whatever the step: a region with a valid
// pointer holds a complete bundle, the old one or the new image and never a
// mix, and at least one region has a valid pointer.
void assertStillBoots(const Board &b, const char *message = nullptr)
{
    bool anyValid = false;
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        if (!pointerValid(b.fake, r)) continue;
        anyValid = true;
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, r, b.oldBundle) || regionHolds(b.fake, r, b.newBundle),
                                 message);
    }
    TEST_ASSERT_TRUE_MESSAGE(anyValid, message);
}

// One expected executeCommand() call.
struct Expect
{
    std::string cmd;
    std::vector<uint8_t> in;
    size_t outLen;
    uint32_t timeoutMs;
};

std::vector<uint8_t> le32(uint32_t v)
{
    return {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 24)};
}

Expect flrd(uint32_t addr) { return {"FLrd", le32(addr), 16, 200}; }
Expect flad(uint32_t addr) { return {"FLad", le32(addr), 0, 200}; }
Expect flwd(std::vector<uint8_t> bytes) { return {"FLwd", std::move(bytes), 0, 200}; }
Expect flvy(uint32_t addr) { return {"FLvy", le32(addr), 0, 1000}; }

// The four tasks of writeRegionPointer(), from the plan.
void expectPointerWrite(std::vector<Expect> &script, EepromRegion r, uint32_t value)
{
    const uint32_t a = TPS25751EepromUpdater::regionPointerAddress(r);
    script.push_back(flrd(a));
    script.push_back(flad(a));
    script.push_back(flwd(le32(value)));
    script.push_back(flrd(a));
}

// The calls updateRegion() must issue, written from the plan and not from the
// implementation: the pre-flight read of the other pointer, then the four steps
// of SLVAFL1 Figure 2-3.
std::vector<Expect> expectedUpdate(EepromRegion target, const std::vector<uint8_t> &image,
                                   EepromUpdateOptions opt = {})
{
    const EepromRegion other = (target == EepromRegion::Low) ? EepromRegion::High : EepromRegion::Low;
    const uint32_t start = TPS25751EepromUpdater::bundleStart(target);
    std::vector<Expect> s;
    s.push_back(flrd(TPS25751EepromUpdater::regionPointerAddress(other)));  // pre-flight check 4
    expectPointerWrite(s, target, 0);                                       // step 1
    s.push_back(flad(start));                                               // step 2
    for (size_t off = 0; off < image.size(); off += 32) {
        const size_t n = (image.size() - off < 32) ? image.size() - off : 32;
        s.push_back(flwd(std::vector<uint8_t>(image.begin() + off, image.begin() + off + n)));
    }
    s.push_back(flvy(start));                                               // step 3
    if (opt.compareAfterWrite)
        for (size_t off = 0; off < image.size(); off += 16) s.push_back(flrd(start + static_cast<uint32_t>(off)));
    expectPointerWrite(s, target, start);
    if (opt.invalidateOldRegion) expectPointerWrite(s, other, 0);           // step 4
    return s;
}

// The step each call of expectedUpdate() belongs to.
std::vector<EepromUpdateStep> stepsOf(size_t imageLen, EepromUpdateOptions opt = {})
{
    std::vector<EepromUpdateStep> s;
    s.push_back(EepromUpdateStep::Preflight);
    s.insert(s.end(), 4, EepromUpdateStep::ErasePointer);
    s.insert(s.end(), 1 + (imageLen + 31) / 32, EepromUpdateStep::WriteBundle);
    s.push_back(EepromUpdateStep::VerifyBundle);
    if (opt.compareAfterWrite) s.insert(s.end(), (imageLen + 15) / 16, EepromUpdateStep::CompareBundle);
    s.insert(s.end(), 4, EepromUpdateStep::RestorePointer);
    if (opt.invalidateOldRegion) s.insert(s.end(), 4, EepromUpdateStep::EraseOldPointer);
    return s;
}

void assertCalls(const FakeCommandExecutor &fake, const std::vector<Expect> &expected)
{
    TEST_ASSERT_EQUAL_size_t(expected.size(), fake.calls.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        const FakeCommandExecutor::Call &c = fake.calls[i];
        TEST_ASSERT_EQUAL_STRING_MESSAGE(expected[i].cmd.c_str(), c.cmd.c_str(), at("call", i));
        TEST_ASSERT_EQUAL_size_t_MESSAGE(expected[i].in.size(), c.in.size(), at("payload length of call", i));
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected[i].in.data(), c.in.data(), c.in.size(),
                                             at("payload of call", i));
        TEST_ASSERT_EQUAL_size_t_MESSAGE(expected[i].outLen, c.outLen, at("outLen of call", i));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected[i].timeoutMs, c.timeoutMs, at("timeout of call", i));
    }
}

void append(std::vector<Expect> &script, const std::vector<Expect> &more)
{
    script.insert(script.end(), more.begin(), more.end());
}

// A pre-flight refusal: the status, no write-side task, and an unchanged EEPROM.
void assertRefusedBeforeAnyWrite(const Board &b, const TPS25751EepromUpdater &u,
                                 EepromUpdateStatus expected, EepromUpdateStatus actual)
{
    TEST_ASSERT_EQUAL_STRING(TPS25751EepromUpdater::toString(expected), TPS25751EepromUpdater::toString(actual));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLad"));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLwd"));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLvy"));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("GAID"));
    assertUnchanged(b);
    TEST_ASSERT_EQUAL_STRING("Preflight", TPS25751EepromUpdater::toString(u.lastStep()));
}

// Progress callback that records every call with the phase it reports.
struct ProgressLog
{
    struct Entry
    {
        uint32_t done;
        uint32_t total;
        EepromUpdateStep step;
        EepromRegion target;
    };
    TPS25751EepromUpdater *updater = nullptr;
    std::vector<Entry> entries;
    std::function<void(const Entry &)> onCall;
};

void recordProgress(uint32_t done, uint32_t total, void *ctx)
{
    ProgressLog *log = static_cast<ProgressLog *>(ctx);
    const ProgressLog::Entry e{done, total, log->updater->lastStep(), log->updater->lastTarget()};
    log->entries.push_back(e);
    if (log->onCall) log->onCall(e);
}

#define ASSERT_STATUS(expected, actual) \
    TEST_ASSERT_EQUAL_STRING(TPS25751EepromUpdater::toString(expected), TPS25751EepromUpdater::toString(actual))
#define ASSERT_STATUS_MESSAGE(expected, actual, message) \
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TPS25751EepromUpdater::toString(expected), \
                                     TPS25751EepromUpdater::toString(actual), (message))
#define ASSERT_STEP(expected, actual) \
    TEST_ASSERT_EQUAL_STRING(TPS25751EepromUpdater::toString(expected), TPS25751EepromUpdater::toString(actual))
#define ASSERT_STEP_MESSAGE(expected, actual, message) \
    TEST_ASSERT_EQUAL_STRING_MESSAGE(TPS25751EepromUpdater::toString(expected), \
                                     TPS25751EepromUpdater::toString(actual), (message))

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
    // Pins the fake's model, not documented device behaviour: the fake returns
    // 0xFF past the end of the EEPROM. What the controller returns there is
    // not in the TRM.
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

void test_select_both_valid_both_region0_flags_targets_low()
{
    // Row 1 with region0Invalid and region0EepromError both set.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    fake.setBootBit(FakeCommandExecutor::kBitRegion0Invalid, true);
    fake.setBootBit(FakeCommandExecutor::kBitRegion0EepromError, true);
    TPS25751EepromUpdater u(fake);

    EepromRegion target = EepromRegion::High;
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, u.selectTargetRegion(target));
    TEST_ASSERT_EQUAL(EepromRegion::Low, target);
    TEST_ASSERT_EQUAL_size_t(1, fake.bootFlagsReads);
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
    // Row 2. BOOT_FLAGS says region 0 is bad, but it is not consulted.
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

void test_select_stale_boot_flags_follows_pointers()
{
    // After a high-region update without a reset, BOOT_FLAGS still describes
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

void test_select_boot_flags_read_failure()
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

void test_compare_region_high_with_larger_eeprom()
{
    // A 64 KB part gives the high region 0xBC00 bytes, so an image longer than
    // the default 0x3C00 is compared, and the new limit is enforced.
    const std::vector<uint8_t> image = makeBundle(0x8000);
    FakeCommandExecutor fake(65536);
    fake.writeBytes(HIGH_START, image.data(), image.size());
    TPS25751EepromUpdater u(fake, 65536);

    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success,
                      u.compareRegion(EepromRegion::High, image.data(), image.size()));
    TEST_ASSERT_EQUAL_size_t(0x8000 / 16, fake.calls.size());
    assertPayload(fake.calls.back(), "FLrd", {0xF0, 0xC3, 0x00, 0x00}, 200);

    const std::vector<uint8_t> tooBig = makeBundle(0xBC01);
    fake.calls.clear();
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength,
                      u.compareRegion(EepromRegion::High, tooBig.data(), tooBig.size()));
    TEST_ASSERT_EQUAL_size_t(0, fake.calls.size());
    // The low region's capacity does not grow with the part.
    TEST_ASSERT_EQUAL(EepromUpdateStatus::BadImageLength,
                      u.compareRegion(EepromRegion::Low, image.data(), 0x3C01));
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

void test_dump_unaligned_start_with_short_tail()
{
    // 21 bytes from 0x0805: one full line at 0x0805 and a 5-byte tail at 0x0815.
    // Bundle bytes are (i * 7 + 3) mod 256 for offsets below 0x100.
    FakeCommandExecutor fake;
    loadFullFlash(fake, makeBundle());
    TPS25751EepromUpdater u(fake);

    EepromUpdateStatus s = EepromUpdateStatus::I2cError;
    const std::string out = captureStdout([&] { s = u.dump(0x0805, 21, Serial); });
    TEST_ASSERT_EQUAL(EepromUpdateStatus::Success, s);
    TEST_ASSERT_EQUAL_STRING(
        "00000805: 26 2d 34 3b 42 49 50 57 5e 65 6c 73 7a 81 88 8f\n"
        "00000815: 96 9d a4 ab b2\n",
        out.c_str());
    TEST_ASSERT_EQUAL_size_t(2, fake.calls.size());
    assertPayload(fake.calls[0], "FLrd", {0x05, 0x08, 0x00, 0x00}, 200);
    assertPayload(fake.calls[1], "FLrd", {0x15, 0x08, 0x00, 0x00}, 200);
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

// --- setWriteAddress ----------------------------------------------------------

void test_set_write_address_sends_flad_with_little_endian_address()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.setWriteAddress(0x4420));

    TEST_ASSERT_EQUAL_size_t(1, fake.calls.size());
    assertPayload(fake.calls[0], "FLad", {0x20, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_size_t(0, fake.calls[0].outLen);
    TEST_ASSERT_EQUAL_HEX32(0x4420, fake.writeAddress);
    // FLad alone writes nothing.
    for (uint8_t byte : fake.eeprom) TEST_ASSERT_EQUAL_HEX8(0xFF, byte);
}

void test_set_write_address_encodes_all_four_address_bytes()
{
    // What FLad returns for an address past the end of the part is not in the
    // TRM, so the return code is injected and the fake's own answer is unused.
    FakeCommandExecutor fake;
    fake.failNth("FLad", 0, Fault::returnCodeOnly(0x00));
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.setWriteAddress(0xA1B2C3D4UL));
    assertPayload(fake.calls[0], "FLad", {0xD4, 0xC3, 0xB2, 0xA1}, 200);
}

void test_set_write_address_requires_the_whole_return_byte_to_be_zero()
{
    // Table 4-1: 0x1 timed out or aborted, 0x3 rejected, 0x4 Rx buffer locked,
    // 0x5 to 0xF task-specific errors. 0x10 and 0xF0 have TaskResult 0x0 with
    // bits of 7:4 set, which Table 4-1 allows a successful task; the whole-byte
    // comparison reports them as failures, by decision.
    const uint8_t codes[] = {0x01, 0x03, 0x04, 0x0F, 0x10, 0xF0, 0xFF};
    for (uint8_t rc : codes) {
        FakeCommandExecutor fake;
        fake.failNth("FLad", 0, Fault::returnCodeOnly(rc));
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::SetAddressFailed, u.setWriteAddress(0x0800), at("return code", rc));
    }
}

void test_set_write_address_executor_failures()
{
    FakeCommandExecutor rejected, timeout, i2c;
    rejected.failCall(0, Fault::rejected());
    timeout.failCall(0, Fault::timeout());
    i2c.failCall(0, Fault::i2cError());
    ASSERT_STATUS(EepromUpdateStatus::CommandRejected, TPS25751EepromUpdater(rejected).setWriteAddress(0x0800));
    ASSERT_STATUS(EepromUpdateStatus::CommandTimeout, TPS25751EepromUpdater(timeout).setWriteAddress(0x0800));
    ASSERT_STATUS(EepromUpdateStatus::I2cError, TPS25751EepromUpdater(i2c).setWriteAddress(0x0800));
}

// --- writeData ------------------------------------------------------------------

void test_write_data_sends_flwd_and_the_address_advances()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);
    uint8_t page[32];
    for (size_t i = 0; i < sizeof(page); ++i) page[i] = static_cast<uint8_t>(0x40 + i);
    const uint8_t tail[5] = {1, 2, 3, 4, 5};

    ASSERT_STATUS(EepromUpdateStatus::Success, u.setWriteAddress(0x4400));
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeData(page, sizeof(page)));
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeData(tail, sizeof(tail)));

    TEST_ASSERT_EQUAL_size_t(3, fake.calls.size());
    assertPayload(fake.calls[1], "FLwd", std::vector<uint8_t>(page, page + sizeof(page)), 200);
    assertPayload(fake.calls[2], "FLwd", {1, 2, 3, 4, 5}, 200);
    TEST_ASSERT_EQUAL_size_t(0, fake.calls[1].outLen);

    // The address advances after each successful write (Table 4-21). That the
    // EEPROM receives exactly the bytes given is the fake's model of FLwd,
    // which hardware stage 2 has yet to confirm.
    TEST_ASSERT_EQUAL_HEX8_ARRAY(page, &fake.eeprom[0x4400], sizeof(page));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tail, &fake.eeprom[0x4420], sizeof(tail));
    TEST_ASSERT_EQUAL_HEX8(0xFF, fake.eeprom[0x4425]);
    TEST_ASSERT_EQUAL_HEX32(0x4425, fake.writeAddress);
}

void test_write_data_return_codes()
{
    // Table 4-21 documents two codes: 0x00 written and 0xFF busy. Every other
    // value is a failure with no documented code behind it.
    const struct {
        uint8_t rc;
        EepromUpdateStatus expected;
    } cases[] = {
        {0xFF, EepromUpdateStatus::FlashBusy},
        {0x01, EepromUpdateStatus::WriteFailed},
        {0x03, EepromUpdateStatus::WriteFailed},
        {0x80, EepromUpdateStatus::WriteFailed},
        {0xFE, EepromUpdateStatus::WriteFailed},
    };
    const uint8_t d[4] = {9, 9, 9, 9};
    for (const auto &c : cases) {
        FakeCommandExecutor fake;
        fake.failNth("FLwd", 0, Fault::returnCodeOnly(c.rc));
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.setWriteAddress(0x0800));
        ASSERT_STATUS_MESSAGE(c.expected, u.writeData(d, sizeof(d)), at("return code", c.rc));
        // One FLwd and nothing else: the primitive does not retry.
        TEST_ASSERT_EQUAL_size_t(2, fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(0, fake.delays.size());
    }
}

void test_write_data_rejects_bad_arguments_without_a_task()
{
    FakeCommandExecutor fake;
    TPS25751EepromUpdater u(fake);
    const uint8_t d[64] = {0};

    ASSERT_STATUS(EepromUpdateStatus::BadImageLength, u.writeData(nullptr, 4));
    ASSERT_STATUS(EepromUpdateStatus::BadImageLength, u.writeData(d, 0));
    ASSERT_STATUS(EepromUpdateStatus::BadImageLength, u.writeData(d, 33));
    ASSERT_STATUS(EepromUpdateStatus::BadImageLength, u.writeData(d, 64));
    TEST_ASSERT_EQUAL_size_t(0, fake.calls.size());

    // The limits themselves are accepted.
    ASSERT_STATUS(EepromUpdateStatus::Success, u.setWriteAddress(0x0800));
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeData(d, 1));
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeData(d, 32));
    TEST_ASSERT_EQUAL_size_t(1, fake.calls[1].in.size());
    TEST_ASSERT_EQUAL_size_t(32, fake.calls[2].in.size());
}

void test_write_data_executor_failures()
{
    const uint8_t d[4] = {1, 2, 3, 4};
    FakeCommandExecutor rejected, timeout, i2c;
    rejected.failCall(0, Fault::rejected());
    timeout.failCall(0, Fault::timeout());
    i2c.failCall(0, Fault::i2cError());
    ASSERT_STATUS(EepromUpdateStatus::CommandRejected, TPS25751EepromUpdater(rejected).writeData(d, 4));
    ASSERT_STATUS(EepromUpdateStatus::CommandTimeout, TPS25751EepromUpdater(timeout).writeData(d, 4));
    ASSERT_STATUS(EepromUpdateStatus::I2cError, TPS25751EepromUpdater(i2c).writeData(d, 4));
}

// --- writeRegionPointer ---------------------------------------------------------

void test_write_region_pointer_four_call_sequence()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);

    // Erase the high pointer: FLrd, FLad, FLwd of four zero bytes, FLrd.
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeRegionPointer(EepromRegion::High, 0));
    std::vector<Expect> erase;
    expectPointerWrite(erase, EepromRegion::High, 0);
    assertCalls(b.fake, erase);
    assertPayload(b.fake.calls[0], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[1], "FLad", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[2], "FLwd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[3], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}});

    // Restore it: the value goes out least significant byte first.
    b.fake.calls.clear();
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeRegionPointer(EepromRegion::High, HIGH_START));
    std::vector<Expect> restore;
    expectPointerWrite(restore, EepromRegion::High, HIGH_START);
    assertCalls(b.fake, restore);
    assertPayload(b.fake.calls[2], "FLwd", {0x00, 0x44, 0x00, 0x00}, 200);
    assertUnchanged(b);

    // A pointer write is not a flow: it leaves lastStep() alone and waits for nothing.
    ASSERT_STEP(EepromUpdateStep::None, u.lastStep());
    TEST_ASSERT_EQUAL_size_t(0, b.fake.delays.size());
}

void test_write_region_pointer_low_and_byte_order()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::Success, u.writeRegionPointer(EepromRegion::Low, 0x12345678UL));
    assertPayload(b.fake.calls[0], "FLrd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[1], "FLad", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[2], "FLwd", {0x78, 0x56, 0x34, 0x12}, 200);
    const uint8_t expected[4] = {0x78, 0x56, 0x34, 0x12};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, &b.fake.eeprom[LOW_PTR], 4);
    assertUnchangedOutside(b, {{LOW_PTR, 4}});
}

void test_write_region_pointer_read_back_mismatch()
{
    // FLwd reports "written" but the word did not change.
    Board dropped;
    dropped.fake.failNth("FLwd", 0, Fault::returnCodeOnly(0x00));
    ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                  TPS25751EepromUpdater(dropped.fake).writeRegionPointer(EepromRegion::High, 0));
    TEST_ASSERT_EQUAL_size_t(4, dropped.fake.calls.size());
    assertUnchanged(dropped);

    // The word changed, but not to the value given.
    Board wrong;
    wrong.fake.failNth("FLwd", 0, Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_PTR + 1] = 0x44; }));
    ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                  TPS25751EepromUpdater(wrong.fake).writeRegionPointer(EepromRegion::High, 0));
    TEST_ASSERT_EQUAL_HEX32(0x00004400, pointerOf(wrong.fake, EepromRegion::High));
}

void test_write_region_pointer_changed_neighbor_byte()
{
    // Bytes 4 to 15 after the pointer are the neighbors; any of them changing
    // during the write is PointerNeighborsChanged, even though the word took.
    const uint32_t offsets[] = {4, 9, 15};
    for (uint32_t off : offsets) {
        Board b;
        b.fake.failNth("FLwd", 0,
                       Fault::withHook([off](FakeCommandExecutor &f) { f.eeprom[HIGH_PTR + off] ^= 0x01; }));
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerNeighborsChanged,
                              u.writeRegionPointer(EepromRegion::High, 0), at("neighbor offset", off));
        TEST_ASSERT_EQUAL_size_t(4, b.fake.calls.size());
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    }

    // The neighbors are tested before the value: a write that changed a
    // neighbor and left the word alone still reports the neighbors.
    Board both;
    Fault f = Fault::returnCodeOnly(0x00);
    f.hook = [](FakeCommandExecutor &fake) { fake.eeprom[HIGH_PTR + 4] = 0x00; };
    both.fake.failNth("FLwd", 0, f);
    ASSERT_STATUS(EepromUpdateStatus::PointerNeighborsChanged,
                  TPS25751EepromUpdater(both.fake).writeRegionPointer(EepromRegion::High, 0));
}

void test_write_region_pointer_neighbor_check_limits()
{
    // These two cases pin the limits the Doxygen states. Neither is behaviour
    // to rely on; they show what the check cannot see.

    // 1. A change past byte 15 lies outside the 16 bytes read back.
    Board far;
    far.fake.failNth("FLwd", 0, Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_PTR + 16] = 0x00; }));
    ASSERT_STATUS(EepromUpdateStatus::Success,
                  TPS25751EepromUpdater(far.fake).writeRegionPointer(EepromRegion::High, 0));
    TEST_ASSERT_EQUAL_HEX8(0x00, far.fake.eeprom[HIGH_PTR + 16]);

    // 2. A controller that wrote a whole 32-byte page out of DATA (assumption,
    //    not in the TRM). DATA bytes 4 to 15 still hold what the first FLrd
    //    read (assumption, not in the TRM: the fake keeps DATA bytes the host
    //    did not overwrite), so the neighbors are written back with their own
    //    values and compare equal, while bytes 16 to 31 are overwritten unseen.
    Board page;
    for (uint32_t i = 4; i < 16; ++i) page.fake.eeprom[HIGH_PTR + i] = static_cast<uint8_t>(0xA0 + i);
    page.snapshot();
    page.fake.failNth("FLwd", 0,
                      Fault::withHook([](FakeCommandExecutor &f) { f.writeBytes(HIGH_PTR, f.data, 32); }));
    ASSERT_STATUS(EepromUpdateStatus::Success,
                  TPS25751EepromUpdater(page.fake).writeRegionPointer(EepromRegion::High, 0));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&page.before[HIGH_PTR + 4], &page.fake.eeprom[HIGH_PTR + 4], 12);
    for (uint32_t i = 16; i < 32; ++i) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, page.before[HIGH_PTR + i]);
        TEST_ASSERT_EQUAL_HEX8(0x00, page.fake.eeprom[HIGH_PTR + i]);
    }
}

void test_write_region_pointer_failure_at_each_task()
{
    // Task 1, the first FLrd: nothing has been written.
    {
        Board b;
        b.fake.failCall(0, Fault::timeout());
        ASSERT_STATUS(EepromUpdateStatus::CommandTimeout,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_size_t(1, b.fake.calls.size());
        assertUnchanged(b);
    }
    // Task 2, FLad: a nonzero code or an executor failure, and no FLwd follows.
    {
        Board b;
        b.fake.failCall(1, Fault::returnCodeOnly(0x03));
        ASSERT_STATUS(EepromUpdateStatus::SetAddressFailed,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_size_t(2, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLwd"));
        assertUnchanged(b);
    }
    {
        Board b;
        b.fake.failCall(1, Fault::i2cError());
        ASSERT_STATUS(EepromUpdateStatus::I2cError,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLwd"));
        assertUnchanged(b);
    }
    // Task 3, FLwd: busy is not retried in a pointer write, and no read-back follows.
    {
        Board b;
        b.fake.failCall(2, Fault::busy());
        ASSERT_STATUS(EepromUpdateStatus::FlashBusy,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_size_t(3, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLwd"));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.delays.size());
        assertUnchanged(b);
    }
    {
        Board b;
        b.fake.failCall(2, Fault::returnCodeOnly(0x01));
        ASSERT_STATUS(EepromUpdateStatus::WriteFailed,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_size_t(3, b.fake.calls.size());
    }
    {
        Board b;
        b.fake.failCall(2, Fault::rejected());
        ASSERT_STATUS(EepromUpdateStatus::CommandRejected,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_size_t(3, b.fake.calls.size());
    }
    // Task 4, the read-back: the word was written but could not be confirmed.
    {
        Board b;
        b.fake.failCall(3, Fault::timeout());
        ASSERT_STATUS(EepromUpdateStatus::CommandTimeout,
                      TPS25751EepromUpdater(b.fake).writeRegionPointer(EepromRegion::High, 0));
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    }
}

// --- updateRegion: sequence and contents ---------------------------------------

void test_update_region_high_full_call_sequence()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    assertCalls(b.fake, expectedUpdate(EepromRegion::High, b.newBundle));
    TEST_ASSERT_EQUAL_size_t(SMALL_UPDATE_CALLS, b.fake.calls.size());
    // Spot checks of the script against literal bytes.
    assertPayload(b.fake.calls[IDX_PREFLIGHT_READ], "FLrd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP1_READ], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP1_FLWD], "FLwd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_BUNDLE_FLAD], "FLad", {0x00, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_size_t(32, b.fake.calls[IDX_FIRST_CHUNK].in.size());
    TEST_ASSERT_EQUAL_size_t(4, b.fake.calls[IDX_FIRST_CHUNK + 3].in.size());
    assertPayload(b.fake.calls[IDX_FLVY], "FLvy", {0x00, 0x44, 0x00, 0x00}, 1000);
    assertPayload(b.fake.calls[IDX_FIRST_COMPARE + 6], "FLrd", {0x60, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP3_READ], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP3_FLWD], "FLwd", {0x00, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP4_READ], "FLrd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP4_FLAD], "FLad", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP4_FLWD], "FLwd", {0x00, 0x00, 0x00, 0x00}, 200);

    // Pre-flight read MODE and BOOT_FLAGS once each, and nothing waited.
    TEST_ASSERT_EQUAL_size_t(1, b.fake.modeReads);
    TEST_ASSERT_EQUAL_size_t(1, b.fake.bootFlagsReads);
    TEST_ASSERT_EQUAL_size_t(0, b.fake.delays.size());

    // SLVAFL1 Figure 2-10: the new bundle in High with a valid pointer, the
    // low pointer 0, and the low region's old contents left in place.
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_EQUAL_HEX32(HIGH_START, pointerOf(b.fake, EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, SMALL_LEN}});

    ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
    TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());
}

void test_update_region_low_full_call_sequence()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::Low, b.newBundle.data(), b.newBundle.size()));

    assertCalls(b.fake, expectedUpdate(EepromRegion::Low, b.newBundle));
    assertPayload(b.fake.calls[IDX_PREFLIGHT_READ], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP1_FLAD], "FLad", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_BUNDLE_FLAD], "FLad", {0x00, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_FLVY], "FLvy", {0x00, 0x08, 0x00, 0x00}, 1000);
    assertPayload(b.fake.calls[IDX_FIRST_COMPARE], "FLrd", {0x00, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP3_FLWD], "FLwd", {0x00, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_STEP4_FLAD], "FLad", {0x00, 0x04, 0x00, 0x00}, 200);

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
    TEST_ASSERT_EQUAL_HEX32(LOW_START, pointerOf(b.fake, EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.oldBundle));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {LOW_START, SMALL_LEN}});
    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
}

void test_update_region_real_length_writes_448_chunks()
{
    Board b(BUNDLE_LEN);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    // The whole script, every chunk payload included.
    assertCalls(b.fake, expectedUpdate(EepromRegion::High, b.newBundle));
    // 14336 bytes are 448 chunks of 32; three more FLwd are the pointer writes.
    TEST_ASSERT_EQUAL_size_t(448 + 3, b.fake.countOf("FLwd"));
    // One FLad for the bundle and one per pointer write: the chunks rely on the
    // address advancing by itself.
    TEST_ASSERT_EQUAL_size_t(1 + 3, b.fake.countOf("FLad"));
    TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLvy"));
    TEST_ASSERT_EQUAL_size_t(1 + 6 + 896, b.fake.countOf("FLrd"));
    for (size_t i = 0; i < 448; ++i) {
        TEST_ASSERT_EQUAL_size_t_MESSAGE(32, b.fake.calls[IDX_FIRST_CHUNK + i].in.size(), at("chunk", i));
    }
    TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[IDX_FIRST_CHUNK + 448].cmd.c_str());

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, BUNDLE_LEN}});
}

void test_update_region_short_final_chunk()
{
    // 1000 bytes are 31 chunks of 32 and one of 8, which goes out at its true
    // length. The old bundle is longer, so the bytes after the image show
    // whether the last chunk wrote more than 8.
    Board b(BUNDLE_LEN);
    const std::vector<uint8_t> image = makeNewBundle(1000);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));

    assertCalls(b.fake, expectedUpdate(EepromRegion::High, image));
    TEST_ASSERT_EQUAL_size_t(32 + 3, b.fake.countOf("FLwd"));
    const FakeCommandExecutor::Call &last = b.fake.calls[IDX_FIRST_CHUNK + 31];
    TEST_ASSERT_EQUAL_STRING("FLwd", last.cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(8, last.in.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&image[992], last.in.data(), 8);
    // 63 compare reads: the last covers the 8 image bytes only.
    TEST_ASSERT_EQUAL_size_t(1 + 6 + 63, b.fake.countOf("FLrd"));

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, image));
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, 1000}});
}

void test_update_region_option_flags()
{
    for (int compare = 0; compare < 2; ++compare) {
        for (int invalidate = 0; invalidate < 2; ++invalidate) {
            Board b;
            TPS25751EepromUpdater u(b.fake);
            EepromUpdateOptions opt;
            opt.compareAfterWrite = (compare != 0);
            opt.invalidateOldRegion = (invalidate != 0);

            ASSERT_STATUS(EepromUpdateStatus::Success,
                          u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size(), opt));

            assertCalls(b.fake, expectedUpdate(EepromRegion::High, b.newBundle, opt));
            // Without the compare, the only FLrd calls are pre-flight and the
            // two per pointer write.
            const size_t pointerWrites = invalidate ? 3 : 2;
            TEST_ASSERT_EQUAL_size_t(1 + 2 * pointerWrites + (compare ? 7 : 0), b.fake.countOf("FLrd"));
            TEST_ASSERT_EQUAL_size_t(1 + pointerWrites, b.fake.countOf("FLad"));

            TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
            TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
            if (invalidate) {
                TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
            } else {
                // Step 4 skipped: both pointers valid, SLVAFL1 Figure 2-9.
                TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
                assertUnchangedOutside(b, {{HIGH_START, SMALL_LEN}});
            }
            ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
        }
    }
}

void test_update_region_default_options()
{
    const EepromUpdateOptions opt;
    TEST_ASSERT_TRUE(opt.compareAfterWrite);
    TEST_ASSERT_TRUE(opt.invalidateOldRegion);
}

void test_update_region_larger_eeprom_capacity()
{
    // On a 64 KB part the high region holds 0xBC00 bytes and the low region
    // still 0x3C00: a 0x8000-byte image fits High only.
    Board b(SMALL_LEN, 65536);
    const std::vector<uint8_t> image = makeNewBundle(0x8000);
    TPS25751EepromUpdater u(b.fake, 65536);

    ASSERT_STATUS(EepromUpdateStatus::BadImageLength, u.updateRegion(EepromRegion::Low, image.data(), image.size()));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    assertUnchanged(b);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));
    TEST_ASSERT_EQUAL_size_t(0x8000 / 32 + 3, b.fake.countOf("FLwd"));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, image));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
}

void test_update_region_reruns_after_an_interrupted_update()
{
    // The state an interrupted update of High leaves: its pointer 0 and the
    // region partly written. The same call again completes the update.
    Board b;
    b.fake.writeLe32(HIGH_PTR, 0);
    b.fake.writeBytes(HIGH_START, b.newBundle.data(), 40);
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
}

// --- updateRegion: pre-flight ----------------------------------------------------

void test_update_region_refuses_when_not_in_app_mode()
{
    {
        Board b;
        b.fake.setMode("PTCH");
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NotInAppMode,
                                    u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        // MODE is the first check: no flash task at all was issued.
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(0, b.fake.bootFlagsReads);
    }
    {
        Board b;
        b.fake.setMode("BOOT");
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NotInAppMode,
                                    u.updateRegion(EepromRegion::Low, b.newBundle.data(), b.newBundle.size()));
    }
    {
        Board b;
        b.fake.modeReadFails = true;
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::I2cError,
                                    u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
}

void test_update_region_refuses_without_an_eeprom()
{
    {
        Board b;
        b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, false);
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoEepromPresent,
                                    u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
    {
        Board b;
        b.fake.bootFlagsReadFails = true;
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::I2cError,
                                    u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
}

void test_update_region_refuses_a_bad_header()
{
    Board b;
    std::vector<uint8_t> image = b.newBundle;
    image[0] = 0xAC; image[1] = 0xE0; image[2] = 0x00; image[3] = 0x01;  // big-endian header bytes
    TPS25751EepromUpdater u(b.fake);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageHeader,
                                u.updateRegion(EepromRegion::High, image.data(), image.size()));

    // The example's placeholder image is 64 erased bytes and is refused the
    // same way, which is what keeps a placeholder build from writing.
    std::vector<uint8_t> placeholder(64, 0xFF);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageHeader,
                                u.updateRegion(EepromRegion::High, placeholder.data(), placeholder.size()));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
}

void test_update_region_refuses_a_short_or_missing_image()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageLength,
                                u.updateRegion(EepromRegion::High, b.newBundle.data(), 0x37));
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageLength,
                                u.updateRegion(EepromRegion::High, b.newBundle.data(), 0));
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageLength,
                                u.updateRegion(EepromRegion::High, nullptr, 0x100));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
}

void test_update_region_refuses_an_oversized_image()
{
    // One byte above the 0x3C00 capacity is refused for both regions.
    const std::vector<uint8_t> tooBig = makeNewBundle(0x3C01);
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        Board b;
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageLength,
                                    u.updateRegion(target, tooBig.data(), tooBig.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }

    // Exactly the capacity is written: 480 chunks, the last ending at 0x7FFF.
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, tooBig.data(), 0x3C00));
    TEST_ASSERT_EQUAL_size_t(480 + 3, b.fake.countOf("FLwd"));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(tooBig.data(), &b.fake.eeprom[HIGH_START], 0x3C00);
}

void test_update_region_refuses_when_other_region_is_not_bootable()
{
    // Erased, zero, and off-by-one pointers in the region being kept. Erasing
    // the target pointer in that state would leave no valid pointer at all.
    for (int kind = 0; kind < 3; ++kind) {
        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            Board b;
            const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
            const uint32_t values[3] = {0xFFFFFFFFUL, 0x00000000UL, TPS25751EepromUpdater::bundleStart(other) + 1};
            b.fake.writeLe32(TPS25751EepromUpdater::regionPointerAddress(other), values[kind]);
            b.snapshot();
            TPS25751EepromUpdater u(b.fake);
            assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                        u.updateRegion(target, b.newBundle.data(), b.newBundle.size()));
            // The one flash task issued is the read of the other pointer.
            TEST_ASSERT_EQUAL_size_t(1, b.fake.calls.size());
            TEST_ASSERT_EQUAL_STRING("FLrd", b.fake.calls[0].cmd.c_str());
        }
    }

    // The read of the other pointer fails.
    Board b;
    b.fake.failCall(IDX_PREFLIGHT_READ, Fault::rejected());
    TPS25751EepromUpdater u(b.fake);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::CommandRejected,
                                u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
}

void test_update_region_preflight_order()
{
    // With several checks failing at once, the first in the plan's order wins.
    Board b;
    std::vector<uint8_t> badImage(0x100, 0xFF);
    b.fake.writeLe32(LOW_PTR, 0);
    b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, false);
    b.fake.setMode("PTCH");
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);

    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NotInAppMode,
                                u.updateRegion(EepromRegion::High, badImage.data(), badImage.size()));
    b.fake.setMode("APP ");
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoEepromPresent,
                                u.updateRegion(EepromRegion::High, badImage.data(), badImage.size()));
    b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageHeader,
                                u.updateRegion(EepromRegion::High, badImage.data(), badImage.size()));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
}

// --- updateRegion: every failure status and the state it leaves ----------------
//
// Each test injects one fault into an update of the high region and checks the
// status, the step lastStep() names, that no task follows the failure, and the
// EEPROM against the "state afterwards" column of the plan's status table.

void test_update_region_pointer_erase_failures()
{
    // Step 1. PointerEraseFailed: the target pointer is uncertain, the target
    // region untouched, and the other region still boots.
    struct Case
    {
        const char *name;
        size_t index;
        Fault fault;
        size_t callsMade;
    };
    const Case cases[] = {
        {"FLad code", IDX_STEP1_FLAD, Fault::returnCodeOnly(0x03), IDX_STEP1_FLAD + 1},
        {"FLwd code", IDX_STEP1_FLWD, Fault::returnCodeOnly(0x01), IDX_STEP1_FLWD + 1},
        {"FLwd busy", IDX_STEP1_FLWD, Fault::busy(), IDX_STEP1_FLWD + 1},
        {"write dropped", IDX_STEP1_FLWD, Fault::returnCodeOnly(0x00), IDX_STEP1_READBACK + 1},
        {"wrong word", IDX_STEP1_FLWD,
         Fault::withHook([](FakeCommandExecutor &f) { f.writeLe32(HIGH_PTR, 0x00004401); }),
         IDX_STEP1_READBACK + 1},
    };
    for (const Case &c : cases) {
        Board b;
        b.fake.failCall(c.index, c.fault);
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerEraseFailed,
                              u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()), c.name);
        ASSERT_STEP_MESSAGE(EepromUpdateStep::ErasePointer, u.lastStep(), c.name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(c.callsMade, b.fake.calls.size(), c.name);
        // Busy is not retried in a pointer write.
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.delays.size(), c.name);

        // Nothing but the pointer word may differ, and the bundle was not started.
        assertUnchangedOutside(b, {{HIGH_PTR, 4}}, c.name);
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::High, b.oldBundle), c.name);
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), c.name);
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle), c.name);
        assertStillBoots(b, c.name);
    }
}

void test_update_region_set_address_failure_leaves_pointer_erased()
{
    // Step 2, FLad to the bundle start. SetAddressFailed: target pointer 0,
    // other region boots (SLVAFL1 Figure 2-7).
    Board b;
    b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x03));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::SetAddressFailed,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
    TEST_ASSERT_EQUAL_size_t(IDX_BUNDLE_FLAD + 1, b.fake.calls.size());

    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
}

void test_update_region_write_failure_leaves_region_partly_written()
{
    // Step 2, the third chunk returns a code Table 4-21 does not list.
    // WriteFailed: target partly written, pointer 0, other region boots.
    Board b;
    b.fake.failCall(IDX_FIRST_CHUNK + 2, Fault::returnCodeOnly(0x01));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::WriteFailed,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
    TEST_ASSERT_EQUAL_size_t(IDX_FIRST_CHUNK + 3, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(0, b.fake.delays.size());

    // Two chunks of the new image, then the old bundle's bytes.
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[HIGH_START], 64);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&b.oldBundle[64], &b.fake.eeprom[HIGH_START + 64], SMALL_LEN - 64);
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, 64}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
}

void test_update_region_busy_chunk_succeeds_on_retry_with_fresh_flad()
{
    // The third chunk reports busy once. The hook moves the fake's write
    // address away, standing for a controller whose address no longer points
    // at the chunk after a busy result (assumption, not in the TRM): the retry
    // lands in the right place only because of its fresh FLad.
    Board b;
    Fault busy = Fault::busy();
    busy.hook = [](FakeCommandExecutor &f) { f.writeAddress = 0x0100; };
    b.fake.failCall(IDX_FIRST_CHUNK + 2, busy);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    // Busy FLwd, 10 ms, FLad to the chunk's address, the same chunk again,
    // then the next chunk with no FLad of its own.
    const std::vector<uint8_t> chunk2(b.newBundle.begin() + 64, b.newBundle.begin() + 96);
    const FakeCommandExecutor::Call &busyCall = b.fake.calls[IDX_FIRST_CHUNK + 2];
    assertPayload(busyCall, "FLwd", chunk2, 200);
    TEST_ASSERT_EQUAL_HEX8(0xFF, busyCall.result.returnCode);
    assertPayload(b.fake.calls[IDX_FIRST_CHUNK + 3], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_FIRST_CHUNK + 4], "FLwd", chunk2, 200);
    TEST_ASSERT_EQUAL_STRING("FLwd", b.fake.calls[IDX_FIRST_CHUNK + 5].cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(4, b.fake.calls[IDX_FIRST_CHUNK + 5].in.size());
    TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[IDX_FIRST_CHUNK + 6].cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(SMALL_UPDATE_CALLS + 2, b.fake.calls.size());

    TEST_ASSERT_EQUAL_size_t(1, b.fake.delays.size());
    TEST_ASSERT_EQUAL_UINT32(10, b.fake.delays[0]);

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    // Nothing landed at the address the hook set.
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, SMALL_LEN}});
}

void test_update_region_busy_chunk_succeeds_on_third_retry()
{
    // The first attempt and two retries report busy; the third retry is written.
    Board b;
    b.fake.failNth("FLwd", 2, Fault::busy());  // FLwd 0 is the step 1 pointer write, FLwd 1 the first chunk
    b.fake.failNth("FLwd", 3, Fault::busy());
    b.fake.failNth("FLwd", 4, Fault::busy());
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    TEST_ASSERT_EQUAL_size_t(3, b.fake.delays.size());
    for (uint32_t d : b.fake.delays) TEST_ASSERT_EQUAL_UINT32(10, d);
    // Three extra FLwd and three extra FLad, each FLad to the second chunk.
    TEST_ASSERT_EQUAL_size_t(SMALL_UPDATE_CALLS + 6, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(1 + 3 + 3, b.fake.countOf("FLad"));
    for (size_t i = 0; i < 3; ++i) {
        assertPayload(b.fake.calls[IDX_FIRST_CHUNK + 2 + 2 * i], "FLad", {0x20, 0x44, 0x00, 0x00}, 200);
    }
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
}

void test_update_region_flash_busy_after_three_retries()
{
    // Busy on the first attempt and on all three retries. FlashBusy: target
    // partly written, pointer 0, other region boots.
    Board b;
    for (size_t n = 3; n <= 6; ++n) b.fake.failNth("FLwd", n, Fault::busy());  // the third chunk, four times
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::FlashBusy,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());

    // Four attempts at the chunk, three FLad between them, three 10 ms waits,
    // and nothing after the fourth attempt.
    TEST_ASSERT_EQUAL_size_t(3, b.fake.delays.size());
    for (uint32_t d : b.fake.delays) TEST_ASSERT_EQUAL_UINT32(10, d);
    TEST_ASSERT_EQUAL_size_t(1 + 2 + 4, b.fake.countOf("FLwd"));
    TEST_ASSERT_EQUAL_size_t(1 + 1 + 3, b.fake.countOf("FLad"));
    TEST_ASSERT_EQUAL_size_t(IDX_FIRST_CHUNK + 2 + 7, b.fake.calls.size());
    TEST_ASSERT_EQUAL_STRING("FLwd", b.fake.calls.back().cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLvy"));

    TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[HIGH_START], 64);
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, 64}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
}

void test_update_region_retry_flad_failure()
{
    // The fresh FLad of a busy retry fails: SetAddressFailed, with the pointer
    // still 0 and no further write.
    Board b;
    b.fake.failCall(IDX_FIRST_CHUNK + 1, Fault::busy());
    b.fake.failCall(IDX_FIRST_CHUNK + 2, Fault::returnCodeOnly(0x03));  // the retry's FLad
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::SetAddressFailed,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
    TEST_ASSERT_EQUAL_size_t(IDX_FIRST_CHUNK + 3, b.fake.calls.size());
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, 32}});
    assertStillBoots(b);
}

void test_update_region_verify_failure_leaves_pointer_erased()
{
    // Step 3, FLvy reports not valid (0x01). VerifyFailed: target written but
    // not valid, pointer 0, other region boots (SLVAFL1 Figure 2-8).
    Board b;
    b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x01));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::VerifyFailed,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::VerifyBundle, u.lastStep());
    // No compare and no pointer write follow.
    TEST_ASSERT_EQUAL_size_t(IDX_FLVY + 1, b.fake.calls.size());

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, SMALL_LEN}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
}

void test_update_region_compare_mismatch_leaves_pointer_erased()
{
    // One bit of the second chunk is wrong in the EEPROM after the write, and
    // FLvy still reports valid. The FLvy result is injected, so the test does
    // not rest on how the fake decides validity. CompareMismatch: same state
    // as VerifyFailed.
    Board b;
    b.fake.failCall(IDX_FIRST_CHUNK + 1,
                    Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 40] ^= 0x10; }));
    b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x00));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::CompareBundle, u.lastStep());
    // Offset 40 is in the third 16-byte read; nothing follows it.
    TEST_ASSERT_EQUAL_size_t(IDX_FIRST_COMPARE + 3, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(1 + 4, b.fake.countOf("FLwd"));

    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, SMALL_LEN}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
}

void test_update_region_compare_off_trusts_flvy()
{
    // The same damaged chunk with compareAfterWrite off and FLvy reporting
    // valid: the update completes. This is what the option gives up.
    Board b;
    b.fake.failCall(IDX_FIRST_CHUNK + 1,
                    Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 40] ^= 0x10; }));
    b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x00));
    TPS25751EepromUpdater u(b.fake);
    EepromUpdateOptions opt;
    opt.compareAfterWrite = false;

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size(), opt));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
}

void test_update_region_pointer_restore_failures()
{
    // Step 3, the pointer write. PointerRestoreFailed: the target holds the
    // image, its pointer is uncertain, and the other region boots. Step 4 does
    // not run.
    struct Case
    {
        const char *name;
        size_t index;
        Fault fault;
        size_t callsMade;
    };
    const Case cases[] = {
        {"FLad code", IDX_STEP3_FLAD, Fault::returnCodeOnly(0x03), IDX_STEP3_FLAD + 1},
        {"FLwd code", IDX_STEP3_FLWD, Fault::returnCodeOnly(0x01), IDX_STEP3_FLWD + 1},
        {"FLwd busy", IDX_STEP3_FLWD, Fault::busy(), IDX_STEP3_FLWD + 1},
        {"write dropped", IDX_STEP3_FLWD, Fault::returnCodeOnly(0x00), IDX_STEP3_READBACK + 1},
    };
    for (const Case &c : cases) {
        Board b;
        b.fake.failCall(c.index, c.fault);
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerRestoreFailed,
                              u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()), c.name);
        ASSERT_STEP_MESSAGE(EepromUpdateStep::RestorePointer, u.lastStep(), c.name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(c.callsMade, b.fake.calls.size(), c.name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.delays.size(), c.name);

        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::High, b.newBundle), c.name);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, EepromRegion::High), c.name);
        assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, SMALL_LEN}}, c.name);
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), c.name);
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle), c.name);
    }
}

void test_update_region_old_pointer_erase_failures()
{
    // Step 4. OldPointerEraseFailed: the target holds the image with a valid
    // pointer, and the old pointer may still be valid (SLVAFL1 Figure 2-9).
    struct Case
    {
        const char *name;
        size_t index;
        Fault fault;
        size_t callsMade;
    };
    const Case cases[] = {
        {"FLad code", IDX_STEP4_FLAD, Fault::returnCodeOnly(0x03), IDX_STEP4_FLAD + 1},
        {"FLwd code", IDX_STEP4_FLWD, Fault::returnCodeOnly(0x01), IDX_STEP4_FLWD + 1},
        {"FLwd busy", IDX_STEP4_FLWD, Fault::busy(), IDX_STEP4_FLWD + 1},
        {"write dropped", IDX_STEP4_FLWD, Fault::returnCodeOnly(0x00), IDX_STEP4_READBACK + 1},
    };
    for (const Case &c : cases) {
        Board b;
        b.fake.failCall(c.index, c.fault);
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::OldPointerEraseFailed,
                              u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()), c.name);
        ASSERT_STEP_MESSAGE(EepromUpdateStep::EraseOldPointer, u.lastStep(), c.name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(c.callsMade, b.fake.calls.size(), c.name);

        // Both pointers valid: the low region, with the old bundle, boots first.
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::High, b.newBundle), c.name);
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::High), c.name);
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), c.name);
        assertUnchangedOutside(b, {{HIGH_START, SMALL_LEN}}, c.name);
    }
}

void test_update_region_neighbor_change_stops_the_update()
{
    // A byte after the pointer changes during the pointer write of step 1, 3,
    // or 4. PointerNeighborsChanged passes through unchanged, and the update
    // stops at that pointer write's read-back.
    const struct {
        size_t flwdIndex;
        uint32_t pointerAddr;
        EepromUpdateStep step;
        size_t callsMade;
    } cases[] = {
        {IDX_STEP1_FLWD, HIGH_PTR, EepromUpdateStep::ErasePointer, IDX_STEP1_READBACK + 1},
        {IDX_STEP3_FLWD, HIGH_PTR, EepromUpdateStep::RestorePointer, IDX_STEP3_READBACK + 1},
        {IDX_STEP4_FLWD, LOW_PTR, EepromUpdateStep::EraseOldPointer, IDX_STEP4_READBACK + 1},
    };
    for (const auto &c : cases) {
        Board b;
        const uint32_t neighbor = c.pointerAddr + 7;
        b.fake.failCall(c.flwdIndex,
                        Fault::withHook([neighbor](FakeCommandExecutor &f) { f.eeprom[neighbor] = 0x5A; }));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::PointerNeighborsChanged,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(c.step, u.lastStep());
        TEST_ASSERT_EQUAL_size_t(c.callsMade, b.fake.calls.size());
        assertStillBoots(b);
    }

    // In step 1 that means the bundle write never starts.
    Board b;
    b.fake.failCall(IDX_STEP1_FLWD, Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_PTR + 4] = 0x00; }));
    TPS25751EepromUpdater u(b.fake);
    u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size());
    TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLwd"));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.oldBundle));
}

void test_update_region_executor_failure_at_every_call()
{
    // One executor failure at each of the 26 calls, for each failure kind, for
    // both targets, with the task either not run or run before the host saw
    // the failure (a COMMAND poll that timed out after the write completed).
    // Each must return the mapped status, name the step the call belongs to,
    // issue nothing afterwards, and leave a region that boots.
    const std::vector<EepromUpdateStep> steps = stepsOf(SMALL_LEN);
    TEST_ASSERT_EQUAL_size_t(SMALL_UPDATE_CALLS, steps.size());

    const struct {
        Fault fault;
        EepromUpdateStatus expected;
    } kinds[] = {
        {Fault::rejected(), EepromUpdateStatus::CommandRejected},
        {Fault::timeout(), EepromUpdateStatus::CommandTimeout},
        {Fault::i2cError(), EepromUpdateStatus::I2cError},
    };

    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        for (int ran = 0; ran < 2; ++ran) {
            for (const auto &kind : kinds) {
                for (size_t idx = 0; idx < SMALL_UPDATE_CALLS; ++idx) {
                    Board b;
                    Fault f = kind.fault;
                    f.runTask = (ran != 0);
                    b.fake.failCall(idx, f);
                    TPS25751EepromUpdater u(b.fake);
                    const char *where = at("call", idx, TPS25751EepromUpdater::toString(kind.expected));

                    const EepromUpdateStatus s = u.updateRegion(target, b.newBundle.data(), b.newBundle.size());

                    ASSERT_STATUS_MESSAGE(kind.expected, s, where);
                    ASSERT_STEP_MESSAGE(steps[idx], u.lastStep(), where);
                    TEST_ASSERT_EQUAL_size_t_MESSAGE(idx + 1, b.fake.calls.size(), where);
                    assertStillBoots(b, where);
                    if (steps[idx] == EepromUpdateStep::Preflight) assertUnchanged(b, where);
                }
            }
        }
    }
}

void test_update_region_return_code_fault_at_every_call()
{
    // A wrong DATA byte 0 at every call of an update: 0x01 (an error code, or a
    // wrong first byte of a read), 0xFF (busy, or an erased byte), and 0x00
    // with the task not run (a write that reports success and writes nothing).
    // Whatever the status, a region must still boot; and Success must mean the
    // requested state. Some of these faults lead the fake into answers of its
    // own that the TRM does not describe (an FLwd after an FLad that did not
    // take). The assertions do not depend on which status results.
    //
    // The sweep runs at 100 bytes (26 calls) and at the real 14336 bytes (1359
    // calls). The length matters: bytes that land in the wrong place stay in
    // unused EEPROM for a short image and reach the other region for the real
    // one.
    //
    // One case is left out: the FLad to the bundle start returning 0x00
    // without taking effect. It does not keep a region bootable at the real
    // length, and test_update_region_limit_step_2_trusts_the_write_address
    // pins what it does instead.
    const uint8_t codes[] = {0x01, 0xFF, 0x00};
    const size_t lengths[] = {SMALL_LEN, BUNDLE_LEN};
    for (size_t length : lengths) {
        const size_t callCount = stepsOf(length).size();
        TEST_ASSERT_EQUAL_size_t(length == SMALL_LEN ? SMALL_UPDATE_CALLS : 1359, callCount);
        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
            for (uint8_t rc : codes) {
                for (size_t idx = 0; idx < callCount; ++idx) {
                    if (idx == IDX_BUNDLE_FLAD && rc == 0x00) continue;

                    Board b(length);
                    b.fake.failCall(idx, Fault::returnCodeOnly(rc));
                    TPS25751EepromUpdater u(b.fake);
                    const char *where =
                        at("call", idx, rc == 0x01 ? "rc 0x01" : rc == 0xFF ? "rc 0xFF" : "rc 0x00");

                    const EepromUpdateStatus s = u.updateRegion(target, b.newBundle.data(), b.newBundle.size());

                    assertStillBoots(b, where);
                    if (s == EepromUpdateStatus::Success) {
                        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, target, b.newBundle), where);
                        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, target), where);
                        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, other), where);
                        ASSERT_STEP_MESSAGE(EepromUpdateStep::Done, u.lastStep(), where);
                    } else {
                        TEST_ASSERT_TRUE_MESSAGE(u.lastStep() != EepromUpdateStep::Done, where);
                    }
                }
            }
        }
    }
}

void test_update_region_limit_step_2_trusts_the_write_address()
{
    // A stated limit of the planned sequence, pinned here so that it is not
    // mistaken for covered. It is not behaviour to rely on.
    //
    // Step 2 sends the FLad to the bundle start and then every chunk, and reads
    // nothing back until step 3. If the controller answered that FLad with 0x00
    // and still held its previous write address, the image would go there. The
    // fake models that case by writing at the stale address, which is the four
    // bytes after the target pointer that step 1 just wrote (assumption, not in
    // the TRM; no TI document describes an FLad that reports success without
    // taking effect).
    //
    // The update fails at FLvy or at the compare, with the target pointer left
    // at 0, unless the target region already held the same image: then both
    // pass on the old contents and the call returns Success (the last case
    // below). Either way the 14336 bytes have already landed on the other
    // region, so the "other region boots" column of the status table does not
    // hold. The plan fixes the sequence of SLVAFL1 Figure 2-3, so the updater
    // does not add a read-back of the first chunk; the orchestrator has raised
    // the limit with the user. updateRegion()'s Doxygen states it.

    // The target holds something else (the old bundle): the call fails.
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        Board b(BUNDLE_LEN);
        b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        const EepromUpdateStatus s = u.updateRegion(target, b.newBundle.data(), b.newBundle.size());

        // Which of the two it is depends on how the fake decides FLvy.
        TEST_ASSERT_TRUE(s == EepromUpdateStatus::VerifyFailed || s == EepromUpdateStatus::CompareMismatch);
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, target));
        // Every chunk went out before anything was read back.
        TEST_ASSERT_EQUAL_size_t(1 + 448, b.fake.countOf("FLwd"));

        // The whole image sits at the stale address.
        const uint32_t stale = TPS25751EepromUpdater::regionPointerAddress(target) + 4;
        TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[stale], BUNDLE_LEN);

        if (target == EepromRegion::Low) {
            // 0x0004 to 0x3803 covers the High pointer at 0x0400: neither
            // pointer is valid any more.
            TEST_ASSERT_FALSE(pointerValid(b.fake, EepromRegion::Low));
            TEST_ASSERT_FALSE(pointerValid(b.fake, EepromRegion::High));
        } else {
            // 0x0404 to 0x3C03 covers the Low bundle from 0x0800: the Low
            // pointer is still valid and leads to a region that is neither
            // bundle.
            TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
            TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
            TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
            TEST_ASSERT_FALSE(pointerValid(b.fake, EepromRegion::High));
        }
    }

    // Both regions already hold the image and the target is High. Nothing in
    // the sequence can tell that the chunks went elsewhere: FLvy and the
    // compare read the High region, which still holds the image from before,
    // so the pointer is restored, step 4 runs, and the call returns Success.
    {
        Board b(BUNDLE_LEN);
        b.fake.writeBytes(LOW_START, b.newBundle.data(), b.newBundle.size());
        b.fake.writeBytes(HIGH_START, b.newBundle.data(), b.newBundle.size());
        b.snapshot();
        b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
        TEST_ASSERT_EQUAL_size_t(448 + 3, b.fake.countOf("FLwd"));

        // The High region is as it was, with its pointer valid again.
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
        TEST_ASSERT_EQUAL_HEX32(HIGH_START, pointerOf(b.fake, EepromRegion::High));

        // The image sits at the stale address, 0x0404 to 0x3C03, across the
        // Low bundle, which no longer equals the image. Step 4 erased the Low
        // pointer, so in this case the misplaced bytes lie behind a pointer of
        // 0 and the High region boots.
        TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[HIGH_PTR + 4], BUNDLE_LEN);
        TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
        // Outside the two pointer words and the stale range nothing changed.
        assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_PTR + 4, BUNDLE_LEN}});
    }
}

// --- Progress callback and lastStep ------------------------------------------------

void test_progress_callback_sequence()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ProgressLog log;
    log.updater = &u;
    u.setProgressCallback(recordProgress, &log);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    // Write phase: 0, then the bytes written after each chunk, ending at the
    // total. Compare phase: 0, then the bytes compared after each 16-byte read.
    const uint32_t writeDone[] = {0, 32, 64, 96, 100};
    const uint32_t compareDone[] = {0, 16, 32, 48, 64, 80, 96, 100};
    TEST_ASSERT_EQUAL_size_t(5 + 8, log.entries.size());
    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(writeDone[i], log.entries[i].done, at("write entry", i));
        ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, log.entries[i].step, at("write entry", i));
    }
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(compareDone[i], log.entries[5 + i].done, at("compare entry", i));
        ASSERT_STEP_MESSAGE(EepromUpdateStep::CompareBundle, log.entries[5 + i].step, at("compare entry", i));
    }
    for (const ProgressLog::Entry &e : log.entries) {
        TEST_ASSERT_EQUAL_UINT32(SMALL_LEN, e.total);
        TEST_ASSERT_EQUAL(EepromRegion::High, e.target);
    }
}

void test_progress_callback_marks_the_step_boundaries()
{
    // A caller that never returns from the callback cuts the update there. At
    // done == 0 of the write phase the EEPROM must be as step 1 left it, and at
    // done == total as step 2 left it.
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ProgressLog log;
    log.updater = &u;

    struct Snapshot
    {
        bool taken = false;
        std::vector<uint8_t> eeprom;
        size_t flad = 0, flwd = 0, flvy = 0, calls = 0;
    } afterStep1, afterStep2;

    log.onCall = [&](const ProgressLog::Entry &e) {
        if (e.step != EepromUpdateStep::WriteBundle) return;
        Snapshot *s = (e.done == 0) ? &afterStep1 : (e.done == e.total) ? &afterStep2 : nullptr;
        if (s == nullptr) return;
        s->taken = true;
        s->eeprom = b.fake.eeprom;
        s->flad = b.fake.countOf("FLad");
        s->flwd = b.fake.countOf("FLwd");
        s->flvy = b.fake.countOf("FLvy");
        s->calls = b.fake.calls.size();
    };
    u.setProgressCallback(recordProgress, &log);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    // After step 1 (SLVAFL1 Figure 2-7): the pointer write is complete, with
    // its read-back, and no task of step 2 has been issued, not even its FLad.
    TEST_ASSERT_TRUE(afterStep1.taken);
    TEST_ASSERT_EQUAL_size_t(IDX_STEP1_READBACK + 1, afterStep1.calls);
    TEST_ASSERT_EQUAL_size_t(1, afterStep1.flad);
    TEST_ASSERT_EQUAL_size_t(1, afterStep1.flwd);
    std::vector<uint8_t> expected = b.before;
    for (int i = 0; i < 4; ++i) expected[HIGH_PTR + i] = 0x00;
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), afterStep1.eeprom.data(), expected.size());

    // After step 2 (Figure 2-8): the whole image is in the region, the pointer
    // is still 0, and FLvy has not been issued.
    TEST_ASSERT_TRUE(afterStep2.taken);
    TEST_ASSERT_EQUAL_size_t(IDX_FLVY, afterStep2.calls);
    TEST_ASSERT_EQUAL_size_t(0, afterStep2.flvy);
    for (size_t i = 0; i < SMALL_LEN; ++i) expected[HIGH_START + i] = b.newBundle[i];
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), afterStep2.eeprom.data(), expected.size());
}

void test_progress_callback_stops_at_a_failure()
{
    // A failed chunk makes no call: the write phase ends without done == total.
    {
        Board b;
        b.fake.failCall(IDX_FIRST_CHUNK + 2, Fault::returnCodeOnly(0x01));
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::WriteFailed,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(3, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(64, log.entries.back().done);
    }
    // FLvy fails: the write phase is complete and the compare phase never starts.
    {
        Board b;
        b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x01));
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::VerifyFailed,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(5, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(SMALL_LEN, log.entries.back().done);
        ASSERT_STEP(EepromUpdateStep::WriteBundle, log.entries.back().step);
    }
    // A mismatch in the third compare read: compare calls for 0, 16, and 32 only.
    {
        Board b;
        b.fake.failCall(IDX_FIRST_CHUNK + 1,
                        Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 40] ^= 0x10; }));
        b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(5 + 3, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(32, log.entries.back().done);
        ASSERT_STEP(EepromUpdateStep::CompareBundle, log.entries.back().step);
    }
    // A pre-flight refusal makes no call at all.
    {
        Board b;
        b.fake.setMode("PTCH");
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::NotInAppMode,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(0, log.entries.size());
    }
}

void test_progress_callback_options_retry_and_clearing()
{
    // compareAfterWrite off: the write phase only.
    {
        Board b;
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        EepromUpdateOptions opt;
        opt.compareAfterWrite = false;
        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size(), opt));
        TEST_ASSERT_EQUAL_size_t(5, log.entries.size());
        for (const ProgressLog::Entry &e : log.entries) ASSERT_STEP(EepromUpdateStep::WriteBundle, e.step);
    }
    // A chunk that needed a busy retry reports once, after it was written.
    {
        Board b;
        b.fake.failCall(IDX_FIRST_CHUNK + 1, Fault::busy());
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(5 + 8, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(64, log.entries[2].done);
    }
    // Clearing the callback stops the calls, and the read-back primitive
    // compareRegion() never makes any.
    {
        Board b;
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.compareRegion(EepromRegion::Low, b.oldBundle.data(), b.oldBundle.size()));
        TEST_ASSERT_EQUAL_size_t(0, log.entries.size());
        u.setProgressCallback(nullptr);
        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(0, log.entries.size());
    }
}

void test_last_step_and_last_target_initial_values()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STEP(EepromUpdateStep::None, u.lastStep());
    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());

    // The primitives and the read-back calls leave both alone.
    uint8_t out[16];
    uint32_t pointer = 0;
    EepromRegion target = EepromRegion::Low;
    u.readFlash(0, out);
    u.readRegionPointer(EepromRegion::High, pointer);
    u.selectTargetRegion(target);
    u.verifyBundle(HIGH_START);
    u.compareRegion(EepromRegion::High, b.oldBundle.data(), b.oldBundle.size());
    u.setWriteAddress(HIGH_PTR);
    u.writeRegionPointer(EepromRegion::High, HIGH_START);
    ASSERT_STEP(EepromUpdateStep::None, u.lastStep());
    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
}

// --- updateInactiveRegion -----------------------------------------------------------

void test_update_inactive_region_targets_high_from_full_flash()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));

    // Selection reads both pointers, then updateRegion(High) with the defaults.
    std::vector<Expect> script = {flrd(LOW_PTR), flrd(HIGH_PTR)};
    append(script, expectedUpdate(EepromRegion::High, b.newBundle));
    assertCalls(b.fake, script);
    // BOOT_FLAGS: once for selection (both pointers valid), once for pre-flight.
    TEST_ASSERT_EQUAL_size_t(2, b.fake.bootFlagsReads);

    TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());
    ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
}

void test_update_inactive_region_targets_low_when_region_0_failed()
{
    // SLVAFL1 Figure 2-2, "Yes" branch: region 0 was invalid at boot, so the
    // controller booted High and the update goes to Low.
    Board b;
    b.fake.setBootBit(FakeCommandExecutor::kBitRegion0Invalid, true);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
}

void test_update_inactive_region_twice_without_reset_follows_the_pointers()
{
    // After the first update BOOT_FLAGS still describes the boot from Low, but
    // the low pointer is 0. Figure 2-2 alone would write High again and erase
    // the only valid pointer in step 1; the pointer-first rule writes Low.
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());

    b.fake.calls.clear();
    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    // The high pointer was valid until step 4 of the second update: the first
    // write-side task of that update went to the low pointer.
    assertPayload(b.fake.calls[4], "FLad", {0x00, 0x00, 0x00, 0x00}, 200);
}

void test_update_inactive_region_refusals_write_nothing()
{
    // Neither pointer valid.
    {
        Board b;
        b.fake.writeLe32(LOW_PTR, 0);
        b.fake.writeLe32(HIGH_PTR, 0xFFFFFFFFUL);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                    u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));
    }
    // A pointer read fails during selection.
    {
        Board b;
        b.fake.failCall(1, Fault::timeout());
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::CommandTimeout,
                                    u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(2, b.fake.calls.size());
    }
    // PTCH mode: selection reads the pointers, then pre-flight refuses.
    {
        Board b;
        b.fake.setMode("PTCH");
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NotInAppMode,
                                    u.updateInactiveRegion(b.newBundle.data(), b.newBundle.size()));
    }
    // A bad image.
    {
        Board b;
        std::vector<uint8_t> erased(0x100, 0xFF);
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageHeader,
                                    u.updateInactiveRegion(erased.data(), erased.size()));
    }
}

// --- updateBothRegions --------------------------------------------------------------

void test_update_both_regions_ends_with_both_pointers_valid()
{
    Board b(BUNDLE_LEN);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));

    // Selection, updateRegion(High) with the defaults, then updateRegion(Low)
    // without step 4.
    EepromUpdateOptions second;
    second.invalidateOldRegion = false;
    std::vector<Expect> script = {flrd(LOW_PTR), flrd(HIGH_PTR)};
    append(script, expectedUpdate(EepromRegion::High, b.newBundle));
    append(script, expectedUpdate(EepromRegion::Low, b.newBundle, second));
    assertCalls(b.fake, script);
    TEST_ASSERT_EQUAL_size_t(2 * 448 + 3 + 2, b.fake.countOf("FLwd"));

    // SLVAFL1 Figure 2-6 with the new bundle: both regions equal the image and
    // both pointers are valid, so nothing outside the two regions changed.
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_EQUAL_HEX32(LOW_START, pointerOf(b.fake, EepromRegion::Low));
    TEST_ASSERT_EQUAL_HEX32(HIGH_START, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{LOW_START, BUNDLE_LEN}, {HIGH_START, BUNDLE_LEN}});

    ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
}

void test_update_both_regions_from_a_single_valid_pointer()
{
    // The state a single-region update leaves. Whichever pointer is valid, the
    // first pass writes the other region and the second pass the valid one.
    for (EepromRegion erased : {EepromRegion::Low, EepromRegion::High}) {
        Board b;
        b.fake.writeLe32(TPS25751EepromUpdater::regionPointerAddress(erased), 0);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));

        TEST_ASSERT_EQUAL(TPS25751EepromUpdater::otherRegion(erased), u.lastTarget());
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    }
}

void test_update_both_regions_second_pass_failure_keeps_the_first_region()
{
    // FLvy fails in the second pass (the low region). The first pass is
    // complete: High holds the new bundle with a valid pointer and boots.
    Board b;
    b.fake.failNth("FLvy", 1, Fault::returnCodeOnly(0x01));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::VerifyFailed, u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));

    TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
    ASSERT_STEP(EepromUpdateStep::VerifyBundle, u.lastStep());
    TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls.back().cmd.c_str());
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
    assertStillBoots(b);
}

void test_update_both_regions_checks_the_image_against_both_regions_first()
{
    // A 64 KB part: 0x8000 bytes fit the high region and not the low one. The
    // call refuses before the first pass, which alone would have succeeded.
    Board b(SMALL_LEN, 65536);
    const std::vector<uint8_t> image = makeNewBundle(0x8000);
    TPS25751EepromUpdater u(b.fake, 65536);

    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageLength,
                                u.updateBothRegions(image.data(), image.size()));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(0, b.fake.modeReads);

    std::vector<uint8_t> erased(0x100, 0xFF);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageHeader,
                                u.updateBothRegions(erased.data(), erased.size()));
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::BadImageLength, u.updateBothRegions(nullptr, 0x100));
    TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
}

void test_update_both_regions_refusals_write_nothing()
{
    {
        Board b;
        b.fake.writeLe32(LOW_PTR, 0);
        b.fake.writeLe32(HIGH_PTR, 0);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                    u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));
    }
    {
        Board b;
        b.fake.setMode("PTCH");
        TPS25751EepromUpdater u(b.fake);
        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NotInAppMode,
                                    u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));
    }
}

void test_update_both_regions_executor_failure_at_every_call()
{
    // 2 selection reads, 26 calls for the first pass, 22 for the second (no
    // step 4). A timeout at each one, with the task not run or run: the call
    // stops there and a region still boots.
    const size_t total = 2 + SMALL_UPDATE_CALLS + (SMALL_UPDATE_CALLS - 4);
    {
        Board b;
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(total, b.fake.calls.size());
    }
    for (int ran = 0; ran < 2; ++ran) {
        for (size_t idx = 0; idx < total; ++idx) {
            Board b;
            Fault f = Fault::timeout();
            f.runTask = (ran != 0);
            b.fake.failCall(idx, f);
            TPS25751EepromUpdater u(b.fake);
            const char *where = at("call", idx, ran ? "task ran" : "task did not run");

            ASSERT_STATUS_MESSAGE(EepromUpdateStatus::CommandTimeout,
                                  u.updateBothRegions(b.newBundle.data(), b.newBundle.size()), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(idx + 1, b.fake.calls.size(), where);
            assertStillBoots(b, where);
        }
    }
}

// --- coldReset -------------------------------------------------------------------------

void test_cold_reset_issues_gaid_and_waits_for_app()
{
    FakeCommandExecutor fake;
    fake.rebootDurationMs = 1000;
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());

    // GAID has no input and no output (TRM Table 4-3).
    TEST_ASSERT_EQUAL_size_t(1, fake.calls.size());
    TEST_ASSERT_EQUAL_STRING("GAID", fake.calls[0].cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(0, fake.calls[0].in.size());
    TEST_ASSERT_EQUAL_size_t(0, fake.calls[0].outLen);
    TEST_ASSERT_EQUAL_UINT32(GAID_CMD_TIMEOUT_MS, fake.calls[0].timeoutMs);
    TEST_ASSERT_EQUAL_UINT32(3000, GAID_CMD_TIMEOUT_MS);
    TEST_ASSERT_EQUAL_size_t(1, fake.resetCount);

    // 100 ms, then MODE every 50 ms. MODE reads fail until 1000 ms: reads at
    // 100, 150, ..., 1000, the last one APP.
    TEST_ASSERT_EQUAL_UINT32(100, fake.delays[0]);
    for (size_t i = 1; i < fake.delays.size(); ++i) TEST_ASSERT_EQUAL_UINT32(50, fake.delays[i]);
    TEST_ASSERT_EQUAL_size_t(19, fake.delays.size());
    TEST_ASSERT_EQUAL_size_t(19, fake.modeReads);
    TEST_ASSERT_EQUAL_UINT32(1000, fake.nowMs());
    ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
}

void test_cold_reset_treats_timeout_and_i2c_error_as_reset_issued()
{
    // The controller restarted, but the executor saw a timeout or a NAK
    // instead of a completed task. MODE later reads APP.
    const Fault kinds[] = {Fault::timeout(), Fault::i2cError()};
    for (const Fault &kind : kinds) {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 400;
        Fault f = kind;
        f.runTask = true;
        fake.failNth("GAID", 0, f);
        TPS25751EepromUpdater u(fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());
        TEST_ASSERT_EQUAL_size_t(1, fake.resetCount);
        TEST_ASSERT_EQUAL_UINT32(400, fake.nowMs());
        // Reads at 100, 150, ..., 400.
        TEST_ASSERT_EQUAL_size_t(7, fake.modeReads);
        ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
    }
}

void test_cold_reset_rejected_is_not_a_reset()
{
    FakeCommandExecutor fake;
    fake.failNth("GAID", 0, Fault::rejected());
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::CommandRejected, u.coldReset());
    TEST_ASSERT_EQUAL_size_t(0, fake.resetCount);
    TEST_ASSERT_EQUAL_size_t(0, fake.delays.size());
    TEST_ASSERT_EQUAL_size_t(0, fake.modeReads);
    ASSERT_STEP(EepromUpdateStep::Reset, u.lastStep());
}

void test_cold_reset_times_out_when_mode_never_returns()
{
    // MODE reads keep failing: ResetTimeout once 5000 ms have passed.
    FakeCommandExecutor fake;
    fake.modeReadFails = true;
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::ResetTimeout, u.coldReset());
    TEST_ASSERT_EQUAL_UINT32(5000, fake.nowMs());
    // Reads at 100, 150, ..., 5000.
    TEST_ASSERT_EQUAL_size_t(99, fake.modeReads);
    TEST_ASSERT_EQUAL_size_t(1, fake.calls.size());
    ASSERT_STEP(EepromUpdateStep::Reset, u.lastStep());
}

void test_cold_reset_times_out_when_mode_is_not_app()
{
    // The controller comes back in PTCH (no bootable region): MODE reads
    // succeed and never say APP.
    FakeCommandExecutor fake;
    fake.rebootDurationMs = 200;
    fake.setModeAfterReset("PTCH");
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::ResetTimeout, u.coldReset(1000));
    TEST_ASSERT_EQUAL_UINT32(1000, fake.nowMs());
    TEST_ASSERT_EQUAL_size_t(19, fake.modeReads);
}

void test_cold_reset_wait_argument()
{
    // The wait runs from the return of GAID, on the executor's clock, and one
    // MODE read is always made, after the 100 ms settle time.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 50;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset(0));
        TEST_ASSERT_EQUAL_size_t(1, fake.modeReads);
        TEST_ASSERT_EQUAL_UINT32(100, fake.nowMs());
    }
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 500;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetTimeout, u.coldReset(0));
        TEST_ASSERT_EQUAL_size_t(1, fake.modeReads);
    }
    // 300 ms is enough for a 300 ms reboot, the read at 300 being the last
    // one allowed, and not for a 301 ms one.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 300;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset(300));
    }
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 301;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetTimeout, u.coldReset(300));
        TEST_ASSERT_EQUAL_UINT32(300, fake.nowMs());
    }
    // Time the GAID task itself took does not count against the wait.
    {
        FakeCommandExecutor fake;
        fake.msPerCommand = 2500;
        fake.rebootDurationMs = 400;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset(500));
        TEST_ASSERT_EQUAL_UINT32(2500 + 400, fake.nowMs());
    }
}

void test_cold_reset_is_safe_across_a_clock_wrap()
{
    // The executor's clock wraps during the wait: 64 ms after GAID, so before
    // the first MODE read, and 512 ms after it, so between two reads. Elapsed
    // time must be measured the same either way, for a reset that completes
    // and for one that times out.
    const uint32_t starts[] = {0xFFFFFFC0UL, 0xFFFFFE00UL};
    for (uint32_t start : starts) {
        {
            FakeCommandExecutor fake;
            fake.now = start;
            fake.rebootDurationMs = 1000;
            TPS25751EepromUpdater u(fake);
            ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());
            TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(start + 1000), fake.nowMs());
            TEST_ASSERT_EQUAL_size_t(19, fake.modeReads);
        }
        {
            FakeCommandExecutor fake;
            fake.now = start;
            fake.modeReadFails = true;
            TPS25751EepromUpdater u(fake);
            ASSERT_STATUS(EepromUpdateStatus::ResetTimeout, u.coldReset(2000));
            TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(start + 2000), fake.nowMs());
            TEST_ASSERT_EQUAL_size_t(39, fake.modeReads);
        }
    }
}

void test_cold_reset_cannot_tell_an_undelivered_gaid_from_a_nak()
{
    // Pins a limit the Doxygen states. An I2C failure that kept GAID from the
    // controller looks like a NAK during the reboot, so the call reports
    // Success although nothing restarted. The caller confirms with VERSION
    // and BOOT_FLAGS.
    FakeCommandExecutor fake;
    fake.failNth("GAID", 0, Fault::i2cError());
    TPS25751EepromUpdater u(fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());
    TEST_ASSERT_EQUAL_size_t(0, fake.resetCount);
    TEST_ASSERT_EQUAL_size_t(1, fake.modeReads);
}

void test_cold_reset_does_not_touch_the_eeprom()
{
    Board b;
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());
    assertUnchanged(b);
    TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLad") + b.fake.countOf("FLwd") + b.fake.countOf("FLrd"));
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

void test_to_string_covers_every_step()
{
    const struct {
        EepromUpdateStep step;
        const char *name;
    } all[] = {
        {EepromUpdateStep::None, "None"},
        {EepromUpdateStep::Preflight, "Preflight"},
        {EepromUpdateStep::ErasePointer, "ErasePointer"},
        {EepromUpdateStep::WriteBundle, "WriteBundle"},
        {EepromUpdateStep::VerifyBundle, "VerifyBundle"},
        {EepromUpdateStep::CompareBundle, "CompareBundle"},
        {EepromUpdateStep::RestorePointer, "RestorePointer"},
        {EepromUpdateStep::EraseOldPointer, "EraseOldPointer"},
        {EepromUpdateStep::Reset, "Reset"},
        {EepromUpdateStep::Done, "Done"},
    };
    for (const auto &e : all) TEST_ASSERT_EQUAL_STRING(e.name, TPS25751EepromUpdater::toString(e.step));
    TEST_ASSERT_EQUAL_UINT8(10, sizeof(all) / sizeof(all[0]));
    TEST_ASSERT_EQUAL_UINT8(9, static_cast<uint8_t>(EepromUpdateStep::Done));
    TEST_ASSERT_EQUAL_STRING("Unknown", TPS25751EepromUpdater::toString(static_cast<EepromUpdateStep>(200)));
}

void test_other_region()
{
    TEST_ASSERT_EQUAL(EepromRegion::High, TPS25751EepromUpdater::otherRegion(EepromRegion::Low));
    TEST_ASSERT_EQUAL(EepromRegion::Low, TPS25751EepromUpdater::otherRegion(EepromRegion::High));
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
    // Pins the fake's model, not documented device behaviour. TRM Table 4-21
    // gives FLwd only 0x00 and 0xFF, and Table 4-20 says nothing about an FLad
    // past the end; the fake's 0x03 in these cases is its own assumption.
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

void test_fake_unknown_code_is_rejected_and_boot_flags_reads()
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
    RUN_TEST(test_select_both_valid_both_region0_flags_targets_low);
    RUN_TEST(test_select_region1_flags_do_not_affect_row_1);
    RUN_TEST(test_select_low_valid_high_not_valid_targets_high);
    RUN_TEST(test_select_low_not_valid_high_valid_targets_low);
    RUN_TEST(test_select_stale_boot_flags_follows_pointers);
    RUN_TEST(test_select_neither_valid_is_no_bootable_region);
    RUN_TEST(test_select_boot_flags_read_failure);
    RUN_TEST(test_select_pointer_read_failures);

    RUN_TEST(test_compare_region_match_low_and_high);
    RUN_TEST(test_compare_region_mismatch_reports_image_offset);
    RUN_TEST(test_compare_region_length_not_multiple_of_16);
    RUN_TEST(test_compare_region_rejects_bad_arguments);
    RUN_TEST(test_compare_region_executor_failure_midway);
    RUN_TEST(test_compare_region_high_with_larger_eeprom);

    RUN_TEST(test_dump_formats_lines_for_xxd);
    RUN_TEST(test_dump_prints_absolute_addresses);
    RUN_TEST(test_dump_unaligned_start_with_short_tail);
    RUN_TEST(test_dump_zero_length_prints_nothing);
    RUN_TEST(test_dump_stops_at_first_failure);
    RUN_TEST(test_dump_full_image_round_trips);

    RUN_TEST(test_check_image_accepts_valid_bundle);
    RUN_TEST(test_check_image_rejects_bad_length);
    RUN_TEST(test_check_image_rejects_bad_header);
    RUN_TEST(test_image_version);

    RUN_TEST(test_set_write_address_sends_flad_with_little_endian_address);
    RUN_TEST(test_set_write_address_encodes_all_four_address_bytes);
    RUN_TEST(test_set_write_address_requires_the_whole_return_byte_to_be_zero);
    RUN_TEST(test_set_write_address_executor_failures);
    RUN_TEST(test_write_data_sends_flwd_and_the_address_advances);
    RUN_TEST(test_write_data_return_codes);
    RUN_TEST(test_write_data_rejects_bad_arguments_without_a_task);
    RUN_TEST(test_write_data_executor_failures);
    RUN_TEST(test_write_region_pointer_four_call_sequence);
    RUN_TEST(test_write_region_pointer_low_and_byte_order);
    RUN_TEST(test_write_region_pointer_read_back_mismatch);
    RUN_TEST(test_write_region_pointer_changed_neighbor_byte);
    RUN_TEST(test_write_region_pointer_neighbor_check_limits);
    RUN_TEST(test_write_region_pointer_failure_at_each_task);
    RUN_TEST(test_update_region_high_full_call_sequence);
    RUN_TEST(test_update_region_low_full_call_sequence);
    RUN_TEST(test_update_region_real_length_writes_448_chunks);
    RUN_TEST(test_update_region_short_final_chunk);
    RUN_TEST(test_update_region_option_flags);
    RUN_TEST(test_update_region_default_options);
    RUN_TEST(test_update_region_larger_eeprom_capacity);
    RUN_TEST(test_update_region_reruns_after_an_interrupted_update);
    RUN_TEST(test_update_region_refuses_when_not_in_app_mode);
    RUN_TEST(test_update_region_refuses_without_an_eeprom);
    RUN_TEST(test_update_region_refuses_a_bad_header);
    RUN_TEST(test_update_region_refuses_a_short_or_missing_image);
    RUN_TEST(test_update_region_refuses_an_oversized_image);
    RUN_TEST(test_update_region_refuses_when_other_region_is_not_bootable);
    RUN_TEST(test_update_region_preflight_order);
    RUN_TEST(test_update_region_pointer_erase_failures);
    RUN_TEST(test_update_region_set_address_failure_leaves_pointer_erased);
    RUN_TEST(test_update_region_write_failure_leaves_region_partly_written);
    RUN_TEST(test_update_region_busy_chunk_succeeds_on_retry_with_fresh_flad);
    RUN_TEST(test_update_region_busy_chunk_succeeds_on_third_retry);
    RUN_TEST(test_update_region_flash_busy_after_three_retries);
    RUN_TEST(test_update_region_retry_flad_failure);
    RUN_TEST(test_update_region_verify_failure_leaves_pointer_erased);
    RUN_TEST(test_update_region_compare_mismatch_leaves_pointer_erased);
    RUN_TEST(test_update_region_compare_off_trusts_flvy);
    RUN_TEST(test_update_region_pointer_restore_failures);
    RUN_TEST(test_update_region_old_pointer_erase_failures);
    RUN_TEST(test_update_region_neighbor_change_stops_the_update);
    RUN_TEST(test_update_region_executor_failure_at_every_call);
    RUN_TEST(test_update_region_return_code_fault_at_every_call);
    RUN_TEST(test_update_region_limit_step_2_trusts_the_write_address);
    RUN_TEST(test_progress_callback_sequence);
    RUN_TEST(test_progress_callback_marks_the_step_boundaries);
    RUN_TEST(test_progress_callback_stops_at_a_failure);
    RUN_TEST(test_progress_callback_options_retry_and_clearing);
    RUN_TEST(test_last_step_and_last_target_initial_values);
    RUN_TEST(test_update_inactive_region_targets_high_from_full_flash);
    RUN_TEST(test_update_inactive_region_targets_low_when_region_0_failed);
    RUN_TEST(test_update_inactive_region_twice_without_reset_follows_the_pointers);
    RUN_TEST(test_update_inactive_region_refusals_write_nothing);
    RUN_TEST(test_update_both_regions_ends_with_both_pointers_valid);
    RUN_TEST(test_update_both_regions_from_a_single_valid_pointer);
    RUN_TEST(test_update_both_regions_second_pass_failure_keeps_the_first_region);
    RUN_TEST(test_update_both_regions_checks_the_image_against_both_regions_first);
    RUN_TEST(test_update_both_regions_refusals_write_nothing);
    RUN_TEST(test_update_both_regions_executor_failure_at_every_call);
    RUN_TEST(test_cold_reset_issues_gaid_and_waits_for_app);
    RUN_TEST(test_cold_reset_treats_timeout_and_i2c_error_as_reset_issued);
    RUN_TEST(test_cold_reset_rejected_is_not_a_reset);
    RUN_TEST(test_cold_reset_times_out_when_mode_never_returns);
    RUN_TEST(test_cold_reset_times_out_when_mode_is_not_app);
    RUN_TEST(test_cold_reset_wait_argument);
    RUN_TEST(test_cold_reset_is_safe_across_a_clock_wrap);
    RUN_TEST(test_cold_reset_cannot_tell_an_undelivered_gaid_from_a_nak);
    RUN_TEST(test_cold_reset_does_not_touch_the_eeprom);

    RUN_TEST(test_to_string_covers_every_status);
    RUN_TEST(test_to_string_region);
    RUN_TEST(test_to_string_covers_every_step);
    RUN_TEST(test_other_region);

    RUN_TEST(test_fake_flad_flwd_write_and_auto_increment);
    RUN_TEST(test_fake_flwd_without_flad_and_out_of_range_are_rejected);
    RUN_TEST(test_fake_busy_fault_writes_nothing_and_hook_runs);
    RUN_TEST(test_fake_failure_with_run_task_still_writes);
    RUN_TEST(test_fake_gaid_reboot_and_clock);
    RUN_TEST(test_fake_unknown_code_is_rejected_and_boot_flags_reads);

    return UNITY_END();
}
