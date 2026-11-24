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
// options, counted from the plan: three pre-flight reads (the other pointer,
// the other AppConfigOffset word, the target's word), four tasks per pointer
// write, FLad plus the first two chunks, the read-back (one read of the target
// pointer, then four at the bundle start), FLad plus the two remaining chunks,
// FLvy, seven compare reads, the read of the target's word, then the pointer
// writes of steps 3 and 4.
constexpr size_t IDX_PREFLIGHT_READ = 0;
constexpr size_t IDX_PREFLIGHT_OTHER_WORD = 1;
constexpr size_t IDX_PREFLIGHT_TARGET_WORD = 2;
constexpr size_t IDX_STEP1_READ = 3;
constexpr size_t IDX_STEP1_FLAD = 4;
constexpr size_t IDX_STEP1_FLWD = 5;
constexpr size_t IDX_STEP1_READBACK = 6;
constexpr size_t IDX_BUNDLE_FLAD = 7;
constexpr size_t IDX_FIRST_CHUNK = 8;          // chunks 0 and 1 are calls 8 and 9
constexpr size_t IDX_EARLY_POINTER_READ = 10;
constexpr size_t IDX_EARLY_READ = 11;          // calls 11 to 14
constexpr size_t IDX_RESUME_FLAD = 15;
constexpr size_t IDX_FLVY = 18;
constexpr size_t IDX_FIRST_COMPARE = 19;
constexpr size_t IDX_WORD_CHECK = 26;
constexpr size_t IDX_STEP3_READ = 27;
constexpr size_t IDX_STEP3_FLAD = 28;
constexpr size_t IDX_STEP3_FLWD = 29;
constexpr size_t IDX_STEP3_READBACK = 30;
constexpr size_t IDX_STEP4_READ = 31;
constexpr size_t IDX_STEP4_FLAD = 32;
constexpr size_t IDX_STEP4_FLWD = 33;
constexpr size_t IDX_STEP4_READBACK = 34;
constexpr size_t SMALL_UPDATE_CALLS = 35;

// The same count for the real 14336-byte image: 448 chunks and 896 compare
// reads, so 5 FLad, 451 FLwd, 911 FLrd, and 1 FLvy.
constexpr size_t REAL_UPDATE_CALLS = 1368;

// Call index of bundle chunk k (zero-based) of an update whose image is longer
// than 64 bytes: the read-back and the second FLad sit between chunks 1 and 2.
constexpr size_t chunkIndex(size_t k)
{
    return k < 2 ? IDX_FIRST_CHUNK + k : IDX_RESUME_FLAD + 1 + (k - 2);
}

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

// makeBundle() or makeNewBundle(), with the two lengths the sweeps use built
// only once. The sweeps construct tens of thousands of boards.
std::vector<uint8_t> cachedBundle(size_t len, bool newImage)
{
    static const std::vector<uint8_t> smallOld = makeBundle(SMALL_LEN);
    static const std::vector<uint8_t> smallNew = makeNewBundle(SMALL_LEN);
    static const std::vector<uint8_t> realOld = makeBundle(BUNDLE_LEN);
    static const std::vector<uint8_t> realNew = makeNewBundle(BUNDLE_LEN);
    if (len == SMALL_LEN) return newImage ? smallNew : smallOld;
    if (len == BUNDLE_LEN) return newImage ? realNew : realOld;
    return newImage ? makeNewBundle(len) : makeBundle(len);
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
        : fake(eepromSize), oldBundle(cachedBundle(len, false)), newBundle(cachedBundle(len, true))
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
// implementation: the pre-flight reads of the other pointer, the other
// AppConfigOffset word, and the target's word, then the four steps of SLVAFL1
// Figure 2-3, with the target's word read again before the step 3 pointer write.
std::vector<Expect> expectedUpdate(EepromRegion target, const std::vector<uint8_t> &image,
                                   EepromUpdateOptions opt = {})
{
    const EepromRegion other = (target == EepromRegion::Low) ? EepromRegion::High : EepromRegion::Low;
    const uint32_t start = TPS25751EepromUpdater::bundleStart(target);
    std::vector<Expect> s;
    s.push_back(flrd(TPS25751EepromUpdater::regionPointerAddress(other)));    // pre-flight check 4
    s.push_back(flrd(TPS25751EepromUpdater::appConfigOffsetAddress(other)));  // check 4, the word
    s.push_back(flrd(TPS25751EepromUpdater::appConfigOffsetAddress(target))); // check 5
    expectPointerWrite(s, target, 0);                                         // step 1
    // Step 2: FLad, the first two chunks, the read of the 16 bytes at the target
    // pointer, the read-back of the chunks at the bundle start (last, so that
    // the last FLrd before the next FLad is inside the target region), then a
    // fresh FLad to the next unwritten byte and the remaining chunks.
    const size_t early = image.size() < 64 ? image.size() : 64;
    const auto chunks = [&](size_t from, size_t to) {
        for (size_t off = from; off < to; off += 32) {
            const size_t n = (to - off < 32) ? to - off : 32;
            s.push_back(flwd(std::vector<uint8_t>(image.begin() + off, image.begin() + off + n)));
        }
    };
    s.push_back(flad(start));
    chunks(0, early);
    s.push_back(flrd(TPS25751EepromUpdater::regionPointerAddress(target)));
    for (size_t off = 0; off < early; off += 16) s.push_back(flrd(start + static_cast<uint32_t>(off)));
    if (image.size() > early) {
        s.push_back(flad(start + static_cast<uint32_t>(early)));
        chunks(early, image.size());
    }
    s.push_back(flvy(start));                                               // step 3
    if (opt.compareAfterWrite)
        for (size_t off = 0; off < image.size(); off += 16) s.push_back(flrd(start + static_cast<uint32_t>(off)));
    s.push_back(flrd(TPS25751EepromUpdater::appConfigOffsetAddress(target))); // word check, always
    expectPointerWrite(s, target, start);
    if (opt.invalidateOldRegion) expectPointerWrite(s, other, 0);           // step 4
    return s;
}

// The step each call of expectedUpdate() belongs to.
std::vector<EepromUpdateStep> stepsOf(size_t imageLen, EepromUpdateOptions opt = {})
{
    std::vector<EepromUpdateStep> s;
    s.insert(s.end(), 3, EepromUpdateStep::Preflight);
    s.insert(s.end(), 4, EepromUpdateStep::ErasePointer);
    const size_t early = imageLen < 64 ? imageLen : 64;
    s.insert(s.end(), 1 + (imageLen + 31) / 32 + (early + 15) / 16 + 1 + (imageLen > early ? 1 : 0),
             EepromUpdateStep::WriteBundle);
    s.push_back(EepromUpdateStep::VerifyBundle);
    if (opt.compareAfterWrite) s.insert(s.end(), (imageLen + 15) / 16, EepromUpdateStep::CompareBundle);
    s.push_back(EepromUpdateStep::CheckAppConfigOffset);
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
    TEST_ASSERT_EQUAL_size_t(32, b.fake.calls[chunkIndex(0)].in.size());
    TEST_ASSERT_EQUAL_size_t(32, b.fake.calls[chunkIndex(1)].in.size());
    // The read-back after two chunks: the 16 bytes at the High pointer, then 64
    // bytes at the bundle start, then FLad to the next unwritten byte.
    assertPayload(b.fake.calls[IDX_EARLY_POINTER_READ], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ], "FLrd", {0x00, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 1], "FLrd", {0x10, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 2], "FLrd", {0x20, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 3], "FLrd", {0x30, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_RESUME_FLAD], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_size_t(32, b.fake.calls[chunkIndex(2)].in.size());
    TEST_ASSERT_EQUAL_size_t(4, b.fake.calls[chunkIndex(3)].in.size());
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
    assertPayload(b.fake.calls[IDX_EARLY_POINTER_READ], "FLrd", {0x00, 0x00, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ], "FLrd", {0x00, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 1], "FLrd", {0x10, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 2], "FLrd", {0x20, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 3], "FLrd", {0x30, 0x08, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_RESUME_FLAD], "FLad", {0x40, 0x08, 0x00, 0x00}, 200);
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
    // Two FLad for the bundle, one before the first chunk and one after the
    // read-back, and one per pointer write: within each run the chunks rely on
    // the address advancing by itself.
    TEST_ASSERT_EQUAL_size_t(2 + 3, b.fake.countOf("FLad"));
    TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLvy"));
    // Three pre-flight reads (the other pointer and both AppConfigOffset
    // words), two per pointer write, five for the read-back, 896 to compare,
    // and the word check before the step 3 pointer write: 911.
    TEST_ASSERT_EQUAL_size_t(3 + 6 + 5 + 896 + 1, b.fake.countOf("FLrd"));
    TEST_ASSERT_EQUAL_size_t(911, b.fake.countOf("FLrd"));
    TEST_ASSERT_EQUAL_size_t(REAL_UPDATE_CALLS, b.fake.calls.size());
    for (size_t i = 0; i < 448; ++i) {
        const FakeCommandExecutor::Call &chunk = b.fake.calls[chunkIndex(i)];
        TEST_ASSERT_EQUAL_STRING_MESSAGE("FLwd", chunk.cmd.c_str(), at("chunk", i));
        TEST_ASSERT_EQUAL_size_t_MESSAGE(32, chunk.in.size(), at("chunk", i));
    }
    assertPayload(b.fake.calls[IDX_RESUME_FLAD], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[chunkIndex(447) + 1].cmd.c_str());

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
    const FakeCommandExecutor::Call &last = b.fake.calls[chunkIndex(31)];
    TEST_ASSERT_EQUAL_STRING("FLwd", last.cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(8, last.in.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(&image[992], last.in.data(), 8);
    // 63 compare reads: the last covers the 8 image bytes only.
    TEST_ASSERT_EQUAL_size_t(3 + 6 + 5 + 63 + 1, b.fake.countOf("FLrd"));

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, image));
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, 1000}});
}

void test_update_region_reads_back_the_first_two_chunks()
{
    // Step 2 for both targets, call by call, from the FLad to the bundle start
    // to the chunk after the read-back. The pointer row is read first and the
    // bundle start last, so the last FLrd before the second FLad is at bundle
    // start + 48, inside the region being written.
    const struct {
        EepromRegion target;
        uint8_t startHi;    // high byte of the bundle start: 0x0800 or 0x4400
        uint8_t pointerHi;  // high byte of the target pointer address: 0x0000 or 0x0400
    } cases[] = {
        {EepromRegion::Low, 0x08, 0x00},
        {EepromRegion::High, 0x44, 0x04},
    };
    for (const auto &c : cases) {
        Board b;
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(c.target, b.newBundle.data(), b.newBundle.size()));

        const std::vector<uint8_t> &image = b.newBundle;
        const auto chunk = [&](size_t from, size_t n) {
            return std::vector<uint8_t>(image.begin() + from, image.begin() + from + n);
        };
        assertPayload(b.fake.calls[7], "FLad", {0x00, c.startHi, 0x00, 0x00}, 200);   // bundle start
        assertPayload(b.fake.calls[8], "FLwd", chunk(0, 32), 200);                    // chunk 0
        assertPayload(b.fake.calls[9], "FLwd", chunk(32, 32), 200);                   // chunk 1
        assertPayload(b.fake.calls[10], "FLrd", {0x00, c.pointerHi, 0x00, 0x00}, 200); // the target pointer
        assertPayload(b.fake.calls[11], "FLrd", {0x00, c.startHi, 0x00, 0x00}, 200);   // read back 64 bytes
        assertPayload(b.fake.calls[12], "FLrd", {0x10, c.startHi, 0x00, 0x00}, 200);
        assertPayload(b.fake.calls[13], "FLrd", {0x20, c.startHi, 0x00, 0x00}, 200);
        assertPayload(b.fake.calls[14], "FLrd", {0x30, c.startHi, 0x00, 0x00}, 200);  // last read: in the region
        assertPayload(b.fake.calls[15], "FLad", {0x40, c.startHi, 0x00, 0x00}, 200);  // next unwritten byte
        assertPayload(b.fake.calls[16], "FLwd", chunk(64, 32), 200);                  // chunk 2
        assertPayload(b.fake.calls[17], "FLwd", chunk(96, 4), 200);                   // chunk 3
        TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[18].cmd.c_str());
        for (size_t i = 10; i <= 14; ++i) TEST_ASSERT_EQUAL_size_t(16, b.fake.calls[i].outLen);
        // The read-back waits for nothing.
        TEST_ASSERT_EQUAL_size_t(0, b.fake.delays.size());
    }
}

void test_update_region_read_back_of_a_short_image()
{
    // The shortest image checkImage() accepts, 0x38 bytes: a 32-byte chunk and
    // a 24-byte chunk, which is the whole image. The read-back covers those 56
    // bytes in four reads, the last comparing 8 bytes, after the read of the
    // target pointer, and no second FLad follows because nothing is left to
    // write. The old bundle is longer, so
    // the bytes after the image show that nothing more was written.
    {
        Board b(BUNDLE_LEN);
        const std::vector<uint8_t> image = makeNewBundle(0x38);
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));

        assertCalls(b.fake, expectedUpdate(EepromRegion::High, image));
        assertPayload(b.fake.calls[7], "FLad", {0x00, 0x44, 0x00, 0x00}, 200);
        TEST_ASSERT_EQUAL_size_t(32, b.fake.calls[8].in.size());
        TEST_ASSERT_EQUAL_size_t(24, b.fake.calls[9].in.size());
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&image[32], b.fake.calls[9].in.data(), 24);
        assertPayload(b.fake.calls[10], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
        assertPayload(b.fake.calls[11], "FLrd", {0x00, 0x44, 0x00, 0x00}, 200);
        assertPayload(b.fake.calls[12], "FLrd", {0x10, 0x44, 0x00, 0x00}, 200);
        assertPayload(b.fake.calls[13], "FLrd", {0x20, 0x44, 0x00, 0x00}, 200);
        assertPayload(b.fake.calls[14], "FLrd", {0x30, 0x44, 0x00, 0x00}, 200);
        TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[15].cmd.c_str());
        TEST_ASSERT_EQUAL_size_t(1 + 3, b.fake.countOf("FLad"));
        TEST_ASSERT_EQUAL_size_t(2 + 3, b.fake.countOf("FLwd"));
        TEST_ASSERT_EQUAL_size_t(29, b.fake.calls.size());

        // Bytes 56 to 63 of the last read are the old bundle's and are not compared.
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, image));
        assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, 0x38}});
    }

    // Exactly 64 bytes: two full chunks are the whole image, so again no second FLad.
    {
        Board b(BUNDLE_LEN);
        const std::vector<uint8_t> image = makeNewBundle(64);
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));
        assertCalls(b.fake, expectedUpdate(EepromRegion::High, image));
        TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[15].cmd.c_str());
        TEST_ASSERT_EQUAL_size_t(1 + 3, b.fake.countOf("FLad"));
    }

    // 65 bytes: one byte is left, so the FLad to bundle start + 64 and a 1-byte chunk follow.
    {
        Board b(BUNDLE_LEN);
        const std::vector<uint8_t> image = makeNewBundle(65);
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));
        assertCalls(b.fake, expectedUpdate(EepromRegion::High, image));
        assertPayload(b.fake.calls[15], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
        TEST_ASSERT_EQUAL_STRING("FLwd", b.fake.calls[16].cmd.c_str());
        TEST_ASSERT_EQUAL_size_t(1, b.fake.calls[16].in.size());
        TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[17].cmd.c_str());
        TEST_ASSERT_EQUAL_size_t(2 + 3, b.fake.countOf("FLad"));
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, image));
        assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, 65}});
    }
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
            // Without the compare, the only FLrd calls are the three of
            // pre-flight, the two per pointer write, the five of the read-back
            // in step 2, and the word check before the step 3 pointer write,
            // which no option turns off.
            const size_t pointerWrites = invalidate ? 3 : 2;
            TEST_ASSERT_EQUAL_size_t(3 + 2 * pointerWrites + 5 + (compare ? 7 : 0) + 1, b.fake.countOf("FLrd"));
            TEST_ASSERT_EQUAL_size_t(2 + pointerWrites, b.fake.countOf("FLad"));

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
    b.fake.failCall(chunkIndex(2), Fault::returnCodeOnly(0x01));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::WriteFailed,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
    TEST_ASSERT_EQUAL_size_t(chunkIndex(2) + 1, b.fake.calls.size());
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
    b.fake.failCall(chunkIndex(2), busy);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    // Busy FLwd, 10 ms, FLad to the chunk's address, the same chunk again,
    // then the next chunk with no FLad of its own.
    const std::vector<uint8_t> chunk2(b.newBundle.begin() + 64, b.newBundle.begin() + 96);
    const size_t busyIndex = chunkIndex(2);
    const FakeCommandExecutor::Call &busyCall = b.fake.calls[busyIndex];
    assertPayload(busyCall, "FLwd", chunk2, 200);
    TEST_ASSERT_EQUAL_HEX8(0xFF, busyCall.result.returnCode);
    assertPayload(b.fake.calls[busyIndex + 1], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[busyIndex + 2], "FLwd", chunk2, 200);
    TEST_ASSERT_EQUAL_STRING("FLwd", b.fake.calls[busyIndex + 3].cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(4, b.fake.calls[busyIndex + 3].in.size());
    TEST_ASSERT_EQUAL_STRING("FLvy", b.fake.calls[busyIndex + 4].cmd.c_str());
    TEST_ASSERT_EQUAL_size_t(SMALL_UPDATE_CALLS + 2, b.fake.calls.size());

    TEST_ASSERT_EQUAL_size_t(1, b.fake.delays.size());
    TEST_ASSERT_EQUAL_UINT32(10, b.fake.delays[0]);

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
    // Nothing landed at the address the hook set.
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, SMALL_LEN}});
}

void test_update_region_busy_first_chunk_is_retried_before_the_read_back()
{
    // The very first chunk reports busy once, and the hook moves the fake's
    // write address away (assumption, not in the TRM). The retry's fresh FLad
    // puts the chunk at the bundle start, and the read-back then finds it.
    Board b;
    Fault busy = Fault::busy();
    busy.hook = [](FakeCommandExecutor &f) { f.writeAddress = 0x0100; };
    b.fake.failCall(chunkIndex(0), busy);
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));

    const std::vector<uint8_t> chunk0(b.newBundle.begin(), b.newBundle.begin() + 32);
    assertPayload(b.fake.calls[8], "FLwd", chunk0, 200);                          // busy
    TEST_ASSERT_EQUAL_HEX8(0xFF, b.fake.calls[8].result.returnCode);
    assertPayload(b.fake.calls[9], "FLad", {0x00, 0x44, 0x00, 0x00}, 200);        // fresh FLad
    assertPayload(b.fake.calls[10], "FLwd", chunk0, 200);                          // chunk 0 again
    TEST_ASSERT_EQUAL_size_t(32, b.fake.calls[11].in.size());                      // chunk 1
    assertPayload(b.fake.calls[12], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);       // the pointer row
    assertPayload(b.fake.calls[13], "FLrd", {0x00, 0x44, 0x00, 0x00}, 200);       // the read-back
    assertPayload(b.fake.calls[16], "FLrd", {0x30, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[17], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
    TEST_ASSERT_EQUAL_size_t(SMALL_UPDATE_CALLS + 2, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(1, b.fake.delays.size());
    TEST_ASSERT_EQUAL_UINT32(10, b.fake.delays[0]);

    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, SMALL_LEN}});
}

void test_update_region_busy_chunk_succeeds_on_third_retry()
{
    // The second chunk, one of the two written before the read-back: the first
    // attempt and two retries report busy, and the third retry is written.
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
    TEST_ASSERT_EQUAL_size_t(2 + 3 + 3, b.fake.countOf("FLad"));
    for (size_t i = 0; i < 3; ++i) {
        assertPayload(b.fake.calls[IDX_FIRST_CHUNK + 2 + 2 * i], "FLad", {0x20, 0x44, 0x00, 0x00}, 200);
    }
    // The read-back follows the retried chunk and still reads the bundle start.
    assertPayload(b.fake.calls[IDX_EARLY_POINTER_READ + 6], "FLrd", {0x00, 0x04, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_EARLY_READ + 6], "FLrd", {0x00, 0x44, 0x00, 0x00}, 200);
    assertPayload(b.fake.calls[IDX_RESUME_FLAD + 6], "FLad", {0x40, 0x44, 0x00, 0x00}, 200);
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
    TEST_ASSERT_EQUAL_size_t(1 + 2 + 3, b.fake.countOf("FLad"));
    TEST_ASSERT_EQUAL_size_t(chunkIndex(2) + 7, b.fake.calls.size());
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

void test_update_region_read_back_mismatch_stops_the_write()
{
    // Step 2, the read-back of the first two chunks. CompareMismatch with
    // lastStep() still WriteBundle: target pointer 0, at most two chunks
    // written, other region boots, and no task after the failing read.

    // One bit of the second chunk is wrong in the EEPROM.
    {
        Board b;
        b.fake.failCall(chunkIndex(1),
                        Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 40] ^= 0x10; }));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
        // Offset 40 is in the third read of the bundle start. The fourth read,
        // the second FLad, and every later chunk are not issued.
        TEST_ASSERT_EQUAL_size_t(IDX_EARLY_READ + 3, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(1 + 2, b.fake.countOf("FLwd"));
        TEST_ASSERT_EQUAL_size_t(1 + 1, b.fake.countOf("FLad"));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLvy"));

        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
        assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, 64}});
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
    }

    // The first chunk reports success and writes nothing, so the second chunk
    // lands where the first belongs: the first read already differs.
    {
        Board b;
        b.fake.failCall(chunkIndex(0), Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
        TEST_ASSERT_EQUAL_size_t(IDX_EARLY_READ + 1, b.fake.calls.size());
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
        assertStillBoots(b);
    }
}

void test_update_region_pointer_area_change_stops_the_write()
{
    // Step 2, the read of the 16 bytes at the target pointer after the first
    // two chunks. They must be as the read-back of step 1 saw them: the word 0
    // and bytes 4 to 15 unchanged. PointerNeighborsChanged with lastStep()
    // still WriteBundle. The pointer row is read before the bundle start and
    // judged after it, so the four reads of the bundle start are the last
    // tasks issued.
    const struct {
        uint32_t offset;  // byte of the 16 that changes while a chunk is written
        uint8_t value;
    } cases[] = {
        {4, 0x01},   // first byte after the pointer word
        {15, 0x00},  // last byte the check reads
        {0, 0x01},   // the pointer word itself is no longer 0
    };
    for (const auto &c : cases) {
        Board b;
        const uint32_t addr = HIGH_PTR + c.offset;
        const uint8_t value = c.value;
        b.fake.failCall(chunkIndex(0), Fault::withHook([addr, value](FakeCommandExecutor &f) { f.eeprom[addr] = value; }));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerNeighborsChanged,
                              u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()),
                              at("pointer byte", c.offset));
        ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
        TEST_ASSERT_EQUAL_size_t(IDX_EARLY_READ + 4, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(1 + 2, b.fake.countOf("FLwd"));
        TEST_ASSERT_EQUAL_size_t(1 + 1, b.fake.countOf("FLad"));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.countOf("FLvy"));
        // The bundle start itself read back correctly.
        TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[HIGH_START], 64);
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
    }

    // The reference is what step 1 read, not 0xFF: bytes that were already
    // there before the update do not count as a change.
    Board b;
    for (uint32_t i = 4; i < 16; ++i) b.fake.eeprom[HIGH_PTR + i] = static_cast<uint8_t>(0xB0 + i);
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::Success,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, SMALL_LEN}});
}

void test_update_region_stops_on_the_trace_of_an_earlier_misplaced_write()
{
    // The 12 bytes after the target pointer already hold the first 12 bytes of
    // the image: what a bundle write that went to the pointer row leaves
    // behind. Step 2 stops before its first task. PointerNeighborsChanged with
    // lastStep() WriteBundle: target pointer 0 from step 1, nothing else
    // written, other region boots.
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        Board b;
        const uint32_t pointerAddr = TPS25751EepromUpdater::regionPointerAddress(target);
        b.fake.writeBytes(pointerAddr + 4, b.newBundle.data(), 12);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);

        ASSERT_STATUS(EepromUpdateStatus::PointerNeighborsChanged,
                      u.updateRegion(target, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());

        // The last task is the read-back of step 1. No FLad or FLwd follows it:
        // the only ones in the log are the pointer write's own.
        TEST_ASSERT_EQUAL_size_t(IDX_STEP1_READBACK + 1, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLad"));
        TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLwd"));
        // The progress callback had its done == 0 call, which is where a caller
        // can cut after step 1, and no other.
        TEST_ASSERT_EQUAL_size_t(1, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(0, log.entries[0].done);
        ASSERT_STEP(EepromUpdateStep::WriteBundle, log.entries[0].step);

        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, target));
        assertUnchangedOutside(b, {{pointerAddr, 4}});
        assertStillBoots(b);
    }
}

void test_update_region_no_false_stop_on_other_bytes_after_the_pointer()
{
    // Only the first 12 bytes of this image after the pointer stop the update.
    const std::vector<uint8_t> image = makeNewBundle(SMALL_LEN);

    // Erased bytes, the normal case: every other test in this suite.
    // Eleven of the twelve bytes match: the last differs, then the first.
    const uint32_t odd[] = {15, 4};
    for (uint32_t offset : odd) {
        Board b;
        b.fake.writeBytes(HIGH_PTR + 4, image.data(), 12);
        b.fake.eeprom[HIGH_PTR + offset] ^= 0x01;
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::Success,
                              u.updateRegion(EepromRegion::High, image.data(), image.size()),
                              at("differing byte", offset));
        assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_START, SMALL_LEN}});
    }
    // The start of a different bundle: the old one shares the four Header_ID
    // bytes with the image and differs after them.
    {
        Board b;
        b.fake.writeBytes(HIGH_PTR + 4, b.oldBundle.data(), 12);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));
    }
    // The image's bytes after the other region's pointer do not concern this target.
    {
        Board b;
        b.fake.writeBytes(LOW_PTR + 4, image.data(), 12);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(EepromRegion::High, image.data(), image.size()));
    }
}

void test_update_region_repeat_after_a_stop_in_step_2()
{
    // End to end. Attempt 1: the FLad to the bundle start reports 0x00 without
    // taking effect, the first two chunks land just past the target's pointer
    // word (the fake's model of a stale write address; assumption, not in the
    // TRM), and the read-back stops the update. Attempt 2 meets the same
    // fault, and this time the second FLad is faulted the same way. Without
    // the check on the trace, attempt 2 would take the stray bytes as its
    // reference, pass the read-back, and send the remaining 446 chunks after
    // them. With it, attempt 2 stops before any bundle write. The other region
    // boots after both attempts.
    for (int sameImage = 0; sameImage < 2; ++sameImage) {
        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            const char *where = at(sameImage ? "target already holds the image, target" : "target holds the old bundle, target",
                                   static_cast<size_t>(target));
            Board b(BUNDLE_LEN);
            if (sameImage) {
                b.fake.writeBytes(LOW_START, b.newBundle.data(), b.newBundle.size());
                b.fake.writeBytes(HIGH_START, b.newBundle.data(), b.newBundle.size());
                b.snapshot();
            }
            TPS25751EepromUpdater u(b.fake);

            // Attempt 1.
            b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
            const EepromUpdateStatus first = u.updateRegion(target, b.newBundle.data(), b.newBundle.size());
            ASSERT_STATUS_MESSAGE(sameImage ? EepromUpdateStatus::PointerNeighborsChanged
                                            : EepromUpdateStatus::CompareMismatch, first, where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, u.lastStep(), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(1 + 2, b.fake.countOf("FLwd"), where);
            assertStillBoots(b, where);

            // Attempt 2, with both bundle FLads of this attempt faulted.
            const size_t base = b.fake.calls.size();
            const size_t flwdBefore = b.fake.countOf("FLwd");
            const size_t fladBefore = b.fake.countOf("FLad");
            b.fake.failCall(base + IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
            b.fake.failCall(base + IDX_RESUME_FLAD, Fault::returnCodeOnly(0x00));
            b.snapshot();
            const EepromUpdateStatus second = u.updateRegion(target, b.newBundle.data(), b.newBundle.size());

            ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerNeighborsChanged, second, where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, u.lastStep(), where);
            // Pre-flight and step 1, and nothing after them: one FLad and one
            // FLwd, both of the pointer write.
            TEST_ASSERT_EQUAL_size_t_MESSAGE(base + IDX_STEP1_READBACK + 1, b.fake.calls.size(), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(flwdBefore + 1, b.fake.countOf("FLwd"), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(fladBefore + 1, b.fake.countOf("FLad"), where);
            // The pointer was already 0, so attempt 2 changed nothing at all.
            assertUnchanged(b, where);
            assertStillBoots(b, where);
        }
    }
}

void test_update_region_flad_after_the_read_back_fails()
{
    // Step 2, the fresh FLad to the next unwritten byte. SetAddressFailed:
    // target pointer 0, two chunks written, other region boots.
    Board b;
    b.fake.failCall(IDX_RESUME_FLAD, Fault::returnCodeOnly(0x03));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::SetAddressFailed,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::WriteBundle, u.lastStep());
    TEST_ASSERT_EQUAL_size_t(IDX_RESUME_FLAD + 1, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(1 + 2, b.fake.countOf("FLwd"));

    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[HIGH_START], 64);
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, 64}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
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
    // One bit of the third chunk is wrong in the EEPROM after the write, past
    // the 64 bytes the read-back of step 2 covers, and FLvy still reports
    // valid. The FLvy result is injected, so the test does not rest on how the
    // fake decides validity. CompareMismatch from step 3: same state as
    // VerifyFailed.
    Board b;
    b.fake.failCall(chunkIndex(2),
                    Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 70] ^= 0x10; }));
    b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x00));
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                  u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    ASSERT_STEP(EepromUpdateStep::CompareBundle, u.lastStep());
    // Offset 70 is in the fifth 16-byte read; nothing follows it.
    TEST_ASSERT_EQUAL_size_t(IDX_FIRST_COMPARE + 5, b.fake.calls.size());
    TEST_ASSERT_EQUAL_size_t(1 + 4, b.fake.countOf("FLwd"));

    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
    assertUnchangedOutside(b, {{HIGH_PTR, 4}, {HIGH_START, SMALL_LEN}});
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
    TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
}

void test_update_region_compare_off_trusts_flvy()
{
    // The same damaged third chunk with compareAfterWrite off and FLvy
    // reporting valid: the update completes. This is what the option gives up.
    // The read-back of step 2 is not an option, but it covers the first two
    // chunks only.
    Board b;
    b.fake.failCall(chunkIndex(2),
                    Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 70] ^= 0x10; }));
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
    // One executor failure at every call of an update, for each failure kind,
    // for both targets, with the task either not run or run before the host saw
    // the failure (a COMMAND poll that timed out after the write completed).
    // Each must return the mapped status, name the step the call belongs to,
    // issue nothing afterwards, and leave a region that boots. The sweep runs
    // at 100 bytes (35 calls) and at the real 14336 bytes (1368 calls), so it
    // covers the read-back calls of step 2 at both lengths.
    //
    // At 100 bytes every call is swept. At the real length every call is swept
    // except among the 896 compare reads of step 3, which differ only in their
    // address: there the first, every 16th, and the last are swept. A failure
    // at a late compare read costs a whole update to reach, and the full sweep
    // took longer than the rest of the suite together.
    const struct {
        Fault fault;
        EepromUpdateStatus expected;
    } kinds[] = {
        {Fault::rejected(), EepromUpdateStatus::CommandRejected},
        {Fault::timeout(), EepromUpdateStatus::CommandTimeout},
        {Fault::i2cError(), EepromUpdateStatus::I2cError},
    };

    const size_t lengths[] = {SMALL_LEN, BUNDLE_LEN};
    for (size_t length : lengths) {
        const std::vector<EepromUpdateStep> steps = stepsOf(length);
        TEST_ASSERT_EQUAL_size_t(length == SMALL_LEN ? SMALL_UPDATE_CALLS : REAL_UPDATE_CALLS, steps.size());
        // The read-back calls belong to step 2 at both lengths.
        for (size_t idx = IDX_BUNDLE_FLAD; idx <= IDX_RESUME_FLAD; ++idx) {
            ASSERT_STEP(EepromUpdateStep::WriteBundle, steps[idx]);
        }
        size_t firstCompare = 0;
        while (steps[firstCompare] != EepromUpdateStep::CompareBundle) ++firstCompare;
        size_t swept = 0;

        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            for (int ran = 0; ran < 2; ++ran) {
                for (const auto &kind : kinds) {
                    for (size_t idx = 0; idx < steps.size(); ++idx) {
                        if (length == BUNDLE_LEN && steps[idx] == EepromUpdateStep::CompareBundle) {
                            const bool lastCompare = (steps[idx + 1] != EepromUpdateStep::CompareBundle);
                            if ((idx - firstCompare) % 16 != 0 && !lastCompare) continue;
                        }
                        ++swept;
                        Board b(length);
                        b.fake.logPayloads = false;
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
        // 12 variants of each swept call: two targets, two "ran" settings, three kinds.
        // 100 bytes: all 35 calls. Real length: 1368 calls less the 896 compare
        // reads, plus the 57 of them that are swept (56 at every 16th, and the last).
        TEST_ASSERT_EQUAL_size_t(12 * (length == SMALL_LEN ? 35 : 1368 - 896 + 57), swept);
    }
}

void test_update_region_return_code_fault_at_every_call()
{
    // A wrong DATA byte 0 at every call of an update: 0x01 (an error code, or a
    // wrong first byte of a read), 0xFF (busy, or an erased byte), and 0x00
    // with the task not run (a write that reports success and writes nothing,
    // or an FLad that reports success and sets no address). Whatever the
    // status, a region must still boot; and Success must mean the requested
    // state. Some of these faults lead the fake into answers of its own that
    // the TRM does not describe (an FLwd after an FLad that did not take). The
    // assertions do not depend on which status results.
    //
    // The sweep runs at 100 bytes (35 calls) and at the real 14336 bytes (1368
    // calls). The length matters: bytes that land in the wrong place stay in
    // unused EEPROM for a short image and would reach the other region for the
    // real one. No case is left out. The bundle FLad that returns 0x00 without
    // taking effect, which once wrote the whole image across the other region,
    // is stopped by the read-back after two chunks.
    //
    // The second FLad of step 2 returning 0x00 without taking effect passes
    // here only under the fake's default model, in which FLrd does not move
    // the write address (assumption, not in the TRM): the address then still
    // points at the next unwritten byte, and the missing FLad changes nothing.
    // test_update_region_read_order_when_flrd_moves_the_write_address covers
    // the model in which FLrd does move it.
    const uint8_t codes[] = {0x01, 0xFF, 0x00};
    const size_t lengths[] = {SMALL_LEN, BUNDLE_LEN};
    for (size_t length : lengths) {
        const size_t callCount = stepsOf(length).size();
        TEST_ASSERT_EQUAL_size_t(length == SMALL_LEN ? SMALL_UPDATE_CALLS : REAL_UPDATE_CALLS, callCount);
        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
            for (uint8_t rc : codes) {
                for (size_t idx = 0; idx < callCount; ++idx) {
                    Board b(length);
                    b.fake.logPayloads = false;
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

void test_update_region_read_back_bounds_a_misplaced_bundle_write()
{
    // The case the read-back in step 2 exists for. The controller answers the
    // FLad to the bundle start with 0x00 and still holds its previous write
    // address. The fake models that by writing at the stale address, which is
    // the four bytes after the target pointer that step 1 just wrote
    // (assumption, not in the TRM; no TI document describes an FLad that
    // reports success without taking effect).
    //
    // Without the read-back the whole 14336-byte image went there, across the
    // other region's pointer or bundle. With it the update stops after two
    // chunks: the 64 stray bytes lie just past the target's pointer word, in
    // unused EEPROM, and the other region still boots.
    for (int sameImage = 0; sameImage < 2; ++sameImage) {
        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
            const char *where = at(sameImage ? "target already holds the image, target" : "target holds the old bundle, target",
                                   static_cast<size_t>(target));
            Board b(BUNDLE_LEN);
            if (sameImage) {
                b.fake.writeBytes(LOW_START, b.newBundle.data(), b.newBundle.size());
                b.fake.writeBytes(HIGH_START, b.newBundle.data(), b.newBundle.size());
                b.snapshot();
            }
            b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
            TPS25751EepromUpdater u(b.fake);

            const EepromUpdateStatus s = u.updateRegion(target, b.newBundle.data(), b.newBundle.size());

            ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, u.lastStep(), where);
            if (sameImage) {
                // The bundle start reads back as the image, because it held the
                // image before. The 12 bytes after the pointer word give it away.
                ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerNeighborsChanged, s, where);
                TEST_ASSERT_EQUAL_size_t_MESSAGE(IDX_EARLY_READ + 4, b.fake.calls.size(), where);
            } else {
                // The first 16 bytes at the bundle start are not the image. The
                // pointer row changed as well; the bundle mismatch is judged first.
                ASSERT_STATUS_MESSAGE(EepromUpdateStatus::CompareMismatch, s, where);
                TEST_ASSERT_EQUAL_size_t_MESSAGE(IDX_EARLY_READ + 1, b.fake.calls.size(), where);
            }
            // Two chunks went out, and nothing after the failed check.
            TEST_ASSERT_EQUAL_size_t_MESSAGE(1 + 2, b.fake.countOf("FLwd"), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.countOf("FLvy"), where);

            // The stray bytes: the first 64 image bytes just past the target's
            // pointer word, and nothing else anywhere.
            const uint32_t pointerAddr = TPS25751EepromUpdater::regionPointerAddress(target);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, target), where);
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(b.newBundle.data(), &b.fake.eeprom[pointerAddr + 4], 64, where);
            assertUnchangedOutside(b, {{pointerAddr, 4 + 64}}, where);

            // The other region is intact behind a valid pointer.
            TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, other), where);
            TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, other, sameImage ? b.newBundle : b.oldBundle), where);
            assertStillBoots(b, where);
        }
    }
}

void test_update_region_read_order_when_flrd_moves_the_write_address()
{
    // The fake's opt-in model of a device whose FLrd leaves the write address
    // at the address it read plus 16 (hypothetical; the TRM does not say
    // whether a read moves the write address, and the fake's default is that it
    // does not).

    // With every FLad taking effect the update is unaffected: each run of
    // writes begins with its own FLad, after whatever was read before it.
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        Board b(BUNDLE_LEN);
        b.fake.flrdMovesWriteAddress = true;
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(target, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_TRUE(regionHolds(b.fake, target, b.newBundle));
        TEST_ASSERT_TRUE(pointerValid(b.fake, target));
        assertUnchangedOutside(b, {{TPS25751EepromUpdater::regionPointerAddress(TPS25751EepromUpdater::otherRegion(target)), 4},
                                   {TPS25751EepromUpdater::bundleStart(target), BUNDLE_LEN}});
    }

    // The double case the read order is for: FLrd moves the write address, and
    // the second FLad of step 2 reports 0x00 without taking effect. The 446
    // remaining chunks then start wherever the last FLrd left the address. The
    // pointer row is read first and the bundle start last, so that last read is
    // at bundle start + 48 and the address is bundle start + 64: the next
    // unwritten byte. The remainder lands where it belongs, the call returns
    // Success, and the target region boots with the complete image. Had the
    // pointer row been read last, the remainder would have started 16 bytes
    // past the target's pointer, across the other region.
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
        Board b(BUNDLE_LEN);
        b.fake.flrdMovesWriteAddress = true;
        b.fake.failCall(IDX_RESUME_FLAD, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.updateRegion(target, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());

        // The last read before the faulted FLad was inside the target region.
        const uint32_t start = TPS25751EepromUpdater::bundleStart(target);
        const FakeCommandExecutor::Call &lastRead = b.fake.calls[IDX_RESUME_FLAD - 1];
        assertPayload(lastRead, "FLrd", le32(start + 48), 200);

        // The remainder landed at bundle start + 64: the region holds the whole
        // image, and nothing outside it changed but the other pointer, which
        // step 4 erased as requested.
        TEST_ASSERT_TRUE(regionHolds(b.fake, target, b.newBundle));
        TEST_ASSERT_TRUE(pointerValid(b.fake, target));
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, other));
        TEST_ASSERT_TRUE(regionHolds(b.fake, other, b.oldBundle));
        assertUnchangedOutside(b, {{TPS25751EepromUpdater::regionPointerAddress(other), 4}, {start, BUNDLE_LEN}});
        assertStillBoots(b);
    }
}

void test_update_region_limit_of_the_read_back_in_step_2()
{
    // Stated limits of the read-back, pinned here so that they are not mistaken
    // for covered. None is behaviour to rely on. Each moves the fake's write
    // address, by a hook or by the fake's opt-in model of an FLrd that moves it,
    // which stands for a controller whose address is not where FLad put it
    // (assumption, not in the TRM). updateRegion()'s Doxygen states the limits.

    // 1. The read-back checks the first two chunks only. Here the write address
    //    moves into the other region after the fifth chunk. The remaining 443
    //    chunks land in the Low bundle, and nothing notices until step 3. The
    //    call fails with the target pointer at 0, but the Low pointer is still
    //    valid and now leads to a region that is neither bundle.
    {
        Board b(BUNDLE_LEN);
        b.fake.failCall(chunkIndex(4),
                        Fault::withHook([](FakeCommandExecutor &f) { f.writeAddress = LOW_START + 0x100; }));
        TPS25751EepromUpdater u(b.fake);

        const EepromUpdateStatus s = u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size());

        // Which of the two it is depends on how the fake decides FLvy.
        TEST_ASSERT_TRUE(s == EepromUpdateStatus::VerifyFailed || s == EepromUpdateStatus::CompareMismatch);
        TEST_ASSERT_TRUE(u.lastStep() == EepromUpdateStep::VerifyBundle ||
                         u.lastStep() == EepromUpdateStep::CompareBundle);
        TEST_ASSERT_EQUAL_size_t(1 + 448, b.fake.countOf("FLwd"));
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::High));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(&b.newBundle[5 * 32], &b.fake.eeprom[LOW_START + 0x100], BUNDLE_LEN - 5 * 32);
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));
        TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::Low, b.oldBundle));
        TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
    }

    // 2. When the target already holds the image, the bundle start reads back
    //    correctly whatever happened, and only a write just past the target's
    //    pointer word is seen. Here both regions hold the image, the stale
    //    address lies inside the Low bundle, and the FLad to the bundle start
    //    does not take. The first two chunks land in the Low bundle unseen. The
    //    FLad after the read-back then takes, so the other 446 chunks go where
    //    they belong and the call returns Success, with 64 misplaced bytes.
    {
        Board b(BUNDLE_LEN);
        b.fake.writeBytes(LOW_START, b.newBundle.data(), b.newBundle.size());
        b.fake.writeBytes(HIGH_START, b.newBundle.data(), b.newBundle.size());
        b.snapshot();
        b.fake.failCall(IDX_STEP1_FLWD,
                        Fault::withHook([](FakeCommandExecutor &f) { f.writeAddress = LOW_START + 0x200; }));
        b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());

        // The High region and pointer are as requested, and step 4 erased the
        // Low pointer, so the damaged Low bundle sits behind a pointer of 0.
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
        TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[LOW_START + 0x200], 64);
        TEST_ASSERT_FALSE(regionHolds(b.fake, EepromRegion::Low, b.newBundle));
        // Exactly 64 bytes are misplaced: nothing else changed but the Low pointer.
        assertUnchangedOutside(b, {{LOW_PTR, 4}, {LOW_START + 0x200, 64}});
    }

    // 3. The check on the target's pointer row sees a stale write address only
    //    if that address is just past the pointer word. In the model in which
    //    FLrd moves the write address, step 1's own read-back leaves it 16 bytes
    //    past the pointer, outside the row the check reads. Both regions hold
    //    the image and the FLad to the bundle start does not take: the first
    //    two chunks land at pointer + 16 unseen, the second FLad takes, and the
    //    call returns Success with 64 misplaced bytes in the pointer area.
    {
        Board b(BUNDLE_LEN);
        b.fake.writeBytes(LOW_START, b.newBundle.data(), b.newBundle.size());
        b.fake.writeBytes(HIGH_START, b.newBundle.data(), b.newBundle.size());
        b.snapshot();
        b.fake.flrdMovesWriteAddress = true;
        b.fake.failCall(IDX_BUNDLE_FLAD, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_TRUE(regionHolds(b.fake, EepromRegion::High, b.newBundle));
        TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::High));
        TEST_ASSERT_EQUAL_HEX8_ARRAY(b.newBundle.data(), &b.fake.eeprom[HIGH_PTR + 16], 64);
        // The 12 bytes the checks look at are untouched, so a later update
        // finds no trace either.
        for (uint32_t i = 4; i < 16; ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, b.fake.eeprom[HIGH_PTR + i]);
        assertUnchangedOutside(b, {{LOW_PTR, 4}, {HIGH_PTR + 16, 64}});
    }
}

// --- AppConfigOffset checks of updateRegion() (decided 2026-11-23) -------------------

void test_update_region_refuses_a_kept_region_behind_a_nonzero_word()
{
    // Pre-flight check 4: the region being kept is bootable only if its
    // pointer is valid and its AppConfigOffset word reads 0. A valid pointer
    // over another word gives NoBootableRegion after two FLrd, nothing written.
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
        Board b;
        b.fake.writeLe32(TPS25751EepromUpdater::appConfigOffsetAddress(other), 0x00000100);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);

        assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                    u.updateRegion(target, b.newBundle.data(), b.newBundle.size()));
        const std::vector<Expect> script = {flrd(TPS25751EepromUpdater::regionPointerAddress(other)),
                                            flrd(TPS25751EepromUpdater::appConfigOffsetAddress(other))};
        assertCalls(b.fake, script);
    }
    // A kept pointer that is not valid is refused without reading its word.
    Board b;
    b.fake.writeLe32(LOW_PTR, 0);
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_EQUAL_size_t(1, b.fake.calls.size());
}

void test_update_region_refuses_a_target_behind_a_nonzero_word()
{
    // Pre-flight check 5: the target's word must read 0, because this call
    // does not write it. Otherwise AppConfigOffsetNotZero after three FLrd,
    // nothing written. 0xFFFFFFFF is the erased value.
    const uint32_t words[] = {0x00000001, 0xFFFFFFFF};
    for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
        for (uint32_t word : words) {
            Board b;
            b.fake.writeLe32(TPS25751EepromUpdater::appConfigOffsetAddress(target), word);
            b.snapshot();
            TPS25751EepromUpdater u(b.fake);

            assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::AppConfigOffsetNotZero,
                                        u.updateRegion(target, b.newBundle.data(), b.newBundle.size()));
            TEST_ASSERT_EQUAL(target, u.lastTarget());
            const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
            const std::vector<Expect> script = {flrd(TPS25751EepromUpdater::regionPointerAddress(other)),
                                                flrd(TPS25751EepromUpdater::appConfigOffsetAddress(other)),
                                                flrd(TPS25751EepromUpdater::appConfigOffsetAddress(target))};
            assertCalls(b.fake, script);
        }
    }
}

void test_update_region_checks_the_kept_word_before_the_target_word()
{
    // Both words not 0: check 4 runs first and reports NoBootableRegion
    // without reading the target's word.
    Board b;
    b.fake.writeLe32(0x03FC, 0x00000100);
    b.fake.writeLe32(0x07FC, 0x00000200);
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);
    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::NoBootableRegion,
                                u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_EQUAL_size_t(2, b.fake.calls.size());
}

void test_update_both_regions_stops_before_writing_a_target_behind_a_nonzero_word()
{
    // The conflict case of the plan: Low valid with word 0, High a valid
    // pointer over a word that is not 0. Selection follows BOOT_FLAGS to High;
    // the first pass stops in its pre-flight with AppConfigOffsetNotZero, no
    // FLwd is issued, and the EEPROM is unchanged, so Low still boots.
    Board b;
    b.fake.writeLe32(0x07FC, 0xFFFFFFFF);
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);

    assertRefusedBeforeAnyWrite(b, u, EepromUpdateStatus::AppConfigOffsetNotZero,
                                u.updateBothRegions(b.newBundle.data(), b.newBundle.size()));
    TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());
    TEST_ASSERT_TRUE(pointerValid(b.fake, EepromRegion::Low));

    // The opposite case: the kept region's word is not 0. NoBootableRegion,
    // nothing written.
    Board c;
    c.fake.writeLe32(0x03FC, 0x00000100);
    c.snapshot();
    TPS25751EepromUpdater v(c.fake);
    assertRefusedBeforeAnyWrite(c, v, EepromUpdateStatus::NoBootableRegion,
                                v.updateBothRegions(c.newBundle.data(), c.newBundle.size()));
}

void test_update_region_checks_the_target_word_before_the_pointer_write()
{
    // Step 3: after FLvy and the optional compare, one FLrd of the target's
    // word, which must still read 0. A word that changed during the update
    // (here a hook on FLvy sets it) returns CompareMismatch with lastStep()
    // CheckAppConfigOffset; the target pointer stays 0 and the other region's
    // pointer stays valid. The check runs with and without the compare.
    for (int compare = 0; compare < 2; ++compare) {
        for (EepromRegion target : {EepromRegion::Low, EepromRegion::High}) {
            const EepromRegion other = TPS25751EepromUpdater::otherRegion(target);
            const uint32_t word = TPS25751EepromUpdater::appConfigOffsetAddress(target);
            Board b;
            b.fake.failNth("FLvy", 0, Fault::withHook([word](FakeCommandExecutor &f) { f.writeLe32(word, 1); }));
            TPS25751EepromUpdater u(b.fake);
            EepromUpdateOptions opt;
            opt.compareAfterWrite = (compare != 0);
            const char *where = at(compare ? "compare, target" : "no compare, target", static_cast<size_t>(target));

            ASSERT_STATUS_MESSAGE(EepromUpdateStatus::CompareMismatch,
                                  u.updateRegion(target, b.newBundle.data(), b.newBundle.size(), opt), where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::CheckAppConfigOffset, u.lastStep(), where);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("FLrd", b.fake.calls.back().cmd.c_str(), where);
            assertPayload(b.fake.calls.back(), "FLrd", le32(word), 200);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, target), where);
            TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, other), where);
            TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, target, b.newBundle), where);
            // FLwd: the step 1 pointer erase and the four chunks; no pointer write after.
            TEST_ASSERT_EQUAL_size_t_MESSAGE(1 + 4, b.fake.countOf("FLwd"), where);
        }
    }
}

void test_update_region_executor_failure_on_the_word_reads()
{
    // The three new FLrd map executor failures as every read does, with
    // lastStep() Preflight for the two pre-flight reads and
    // CheckAppConfigOffset for the read before the pointer write.
    const struct {
        size_t index;
        EepromUpdateStep step;
    } cases[] = {
        {IDX_PREFLIGHT_OTHER_WORD, EepromUpdateStep::Preflight},
        {IDX_PREFLIGHT_TARGET_WORD, EepromUpdateStep::Preflight},
        {IDX_WORD_CHECK, EepromUpdateStep::CheckAppConfigOffset},
    };
    const struct {
        Fault fault;
        EepromUpdateStatus expected;
    } kinds[] = {
        {Fault::rejected(), EepromUpdateStatus::CommandRejected},
        {Fault::timeout(), EepromUpdateStatus::CommandTimeout},
        {Fault::i2cError(), EepromUpdateStatus::I2cError},
    };
    for (const auto &c : cases) {
        for (const auto &k : kinds) {
            Board b;
            b.fake.failCall(c.index, k.fault);
            TPS25751EepromUpdater u(b.fake);
            const char *where = at("call", c.index, TPS25751EepromUpdater::toString(k.expected));

            ASSERT_STATUS_MESSAGE(k.expected, u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()),
                                  where);
            ASSERT_STEP_MESSAGE(c.step, u.lastStep(), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(c.index + 1, b.fake.calls.size(), where);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(c.step == EepromUpdateStep::Preflight ? HIGH_START : 0,
                                            pointerOf(b.fake, EepromRegion::High), where);
            assertStillBoots(b, where);
            if (c.step == EepromUpdateStep::Preflight) assertUnchanged(b, where);
        }
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
        b.fake.failCall(chunkIndex(2), Fault::returnCodeOnly(0x01));
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
    // A mismatch in the fifth compare read: compare calls for 0, 16, 32, 48,
    // and 64 only.
    {
        Board b;
        b.fake.failCall(chunkIndex(2),
                        Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 70] ^= 0x10; }));
        b.fake.failCall(IDX_FLVY, Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(5 + 5, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(64, log.entries.back().done);
        ASSERT_STEP(EepromUpdateStep::CompareBundle, log.entries.back().step);
    }
    // The read-back of step 2 fails: the write phase reported 0, 32, and 64,
    // and the read-back itself reports nothing.
    {
        Board b;
        b.fake.failCall(chunkIndex(1),
                        Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 40] ^= 0x10; }));
        TPS25751EepromUpdater u(b.fake);
        ProgressLog log;
        log.updater = &u;
        u.setProgressCallback(recordProgress, &log);
        ASSERT_STATUS(EepromUpdateStatus::CompareMismatch,
                      u.updateRegion(EepromRegion::High, b.newBundle.data(), b.newBundle.size()));
        TEST_ASSERT_EQUAL_size_t(3, log.entries.size());
        TEST_ASSERT_EQUAL_UINT32(64, log.entries.back().done);
        ASSERT_STEP(EepromUpdateStep::WriteBundle, log.entries.back().step);
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
    assertPayload(b.fake.calls[6], "FLad", {0x00, 0x00, 0x00, 0x00}, 200);
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
    // 2 selection reads, 35 calls for the first pass, 31 for the second (no
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

// --- programBlank ------------------------------------------------------------------------

namespace {

// A board after a PBM load in front of an unusable EEPROM: every byte erased to
// 0xFF, MODE reading APP, BOOT_FLAGS reporting the EEPROM. Hardware stage 5 saw
// the controller report the EEPROM after a PBM boot, on an EEPROM with both
// pointers 0; an erased one was not tested. image is the bundle to program.
struct BlankBoard
{
    FakeCommandExecutor fake;
    std::vector<uint8_t> image;
    std::vector<uint8_t> before;

    explicit BlankBoard(size_t len = SMALL_LEN, size_t eepromSize = 32768)
        : fake(eepromSize), image(cachedBundle(len, true))
    {
        fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
        snapshot();
    }

    void snapshot() { before = fake.eeprom; }
};

uint32_t appConfigOffsetOf(const FakeCommandExecutor &f, EepromRegion r)
{
    return f.readLe32(TPS25751EepromUpdater::appConfigOffsetAddress(r));
}

// SLVAFL1 Figure 2-6 with image in both regions, on an otherwise erased part:
// what the full-flash binary holds.
std::vector<uint8_t> fullFlashLayout(const std::vector<uint8_t> &image, size_t eepromSize = 32768)
{
    FakeCommandExecutor f(eepromSize);
    loadFullFlash(f, image);
    return f.eeprom;
}

void assertEeprom(const FakeCommandExecutor &f, const std::vector<uint8_t> &expected, const char *message = nullptr)
{
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), f.eeprom.size(), message);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected.data(), f.eeprom.data(), expected.size(), message);
}

// The four tasks of a checked word write, from the plan: FLrd, FLad, the
// 4-byte FLwd, FLrd.
void expectWordWrite(std::vector<Expect> &script, uint32_t addr, uint32_t value)
{
    script.push_back(flrd(addr));
    script.push_back(flad(addr));
    script.push_back(flwd(le32(value)));
    script.push_back(flrd(addr));
}

// Steps 2 and 3 of an update with the compare on, from the plan, for any
// image: the same sequence expectedUpdate() writes out for updateRegion().
void expectBundleSteps(std::vector<Expect> &s, EepromRegion target, const std::vector<uint8_t> &image)
{
    const uint32_t start = TPS25751EepromUpdater::bundleStart(target);
    const size_t early = image.size() < 64 ? image.size() : 64;
    const auto chunks = [&](size_t from, size_t to) {
        for (size_t off = from; off < to; off += 32) {
            const size_t n = (to - off < 32) ? to - off : 32;
            s.push_back(flwd(std::vector<uint8_t>(image.begin() + off, image.begin() + off + n)));
        }
    };
    s.push_back(flad(start));
    chunks(0, early);
    s.push_back(flrd(TPS25751EepromUpdater::regionPointerAddress(target)));
    for (size_t off = 0; off < early; off += 16) s.push_back(flrd(start + static_cast<uint32_t>(off)));
    if (image.size() > early) {
        s.push_back(flad(start + static_cast<uint32_t>(early)));
        chunks(early, image.size());
    }
    s.push_back(flvy(start));
    for (size_t off = 0; off < image.size(); off += 16) s.push_back(flrd(start + static_cast<uint32_t>(off)));
    // programBlank() only: the AppConfigOffset word, read right before the pointer.
    s.push_back(flrd(TPS25751EepromUpdater::appConfigOffsetAddress(target)));
    expectPointerWrite(s, target, start);
}

// The calls programBlank() must issue, from the decision of 2026-11-23: the
// pre-flight reads of both pointers, both AppConfigOffset words to 0 (Low,
// then High), then for Low and then High the pointer to 0 and steps 2 and 3
// of an update with the word read before the pointer write.
std::vector<Expect> expectedProgramBlank(const std::vector<uint8_t> &image)
{
    std::vector<Expect> s = {flrd(LOW_PTR), flrd(HIGH_PTR)};
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High})
        expectWordWrite(s, TPS25751EepromUpdater::appConfigOffsetAddress(r), 0);
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        expectPointerWrite(s, r, 0);
        expectBundleSteps(s, r, image);
    }
    return s;
}

// The step and the region of every call of expectedProgramBlank().
struct BlankCall
{
    EepromUpdateStep step;
    EepromRegion target;
};

std::vector<BlankCall> blankCallsOf(size_t len)
{
    std::vector<BlankCall> c(2, BlankCall{EepromUpdateStep::Preflight, EepromRegion::Low});
    const size_t early = len < 64 ? len : 64;
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High})
        c.insert(c.end(), 4, BlankCall{EepromUpdateStep::ClearAppConfigOffset, r});
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        const auto add = [&](size_t n, EepromUpdateStep step) { c.insert(c.end(), n, BlankCall{step, r}); };
        add(4, EepromUpdateStep::ErasePointer);
        add(1 + (len + 31) / 32 + (early + 15) / 16 + 1 + (len > early ? 1 : 0), EepromUpdateStep::WriteBundle);
        add(1, EepromUpdateStep::VerifyBundle);
        add((len + 15) / 16, EepromUpdateStep::CompareBundle);
        add(1, EepromUpdateStep::CheckAppConfigOffset);
        add(4, EepromUpdateStep::RestorePointer);
    }
    return c;
}

// 2 pre-flight reads, 8 calls for the two AppConfigOffset words, and 28 calls
// per region for SMALL_LEN; 2, 8, and 1361 per region for the real 14336 bytes.
constexpr size_t SMALL_BLANK_CALLS = 66;
constexpr size_t REAL_BLANK_CALLS = 2732;
constexpr size_t BLANK_PASS_BASE = 2 + 8;  // index of the Low pass's first call

// Offsets of the four calls of one AppConfigOffset word write.
constexpr size_t WORD_READ = 0;
constexpr size_t WORD_FLAD = 1;
constexpr size_t WORD_FLWD = 2;
constexpr size_t WORD_READBACK = 3;

// Offsets of the calls of one pass from its first call. They hold for any
// image longer than 64 bytes up to PASS_RESUME_FLAD.
constexpr size_t PASS_PTR_READ = 0;
constexpr size_t PASS_PTR_FLAD = 1;
constexpr size_t PASS_PTR_FLWD = 2;
constexpr size_t PASS_BUNDLE_FLAD = 4;
constexpr size_t PASS_EARLY_READ = 8;  // the first read at the bundle start
constexpr size_t PASS_RESUME_FLAD = 12;

size_t passCalls(size_t len)
{
    return (blankCallsOf(len).size() - BLANK_PASS_BASE) / 2;
}

// Global call index of offset in the pass for region r.
size_t blankIdx(EepromRegion r, size_t offset, size_t len = SMALL_LEN)
{
    return BLANK_PASS_BASE + (r == EepromRegion::High ? passCalls(len) : 0) + offset;
}

// Global call index of offset in the word write for region r.
size_t wordIdx(EepromRegion r, size_t offset)
{
    return 2 + (r == EepromRegion::High ? 4 : 0) + offset;
}

// Global call index of the n-th call (zero-based) of a pass's step.
size_t blankStepIdx(EepromRegion r, EepromUpdateStep step, size_t n, size_t len = SMALL_LEN)
{
    const std::vector<BlankCall> calls = blankCallsOf(len);
    for (size_t i = 0; i < calls.size(); ++i) {
        if (calls[i].target == r && calls[i].step == step) return i + n;
    }
    return calls.size();
}

// A pre-flight refusal: the status, no write-side task, an unchanged EEPROM,
// and lastStep() Preflight with lastTarget() Low.
void assertBlankRefused(const BlankBoard &b, const TPS25751EepromUpdater &u, EepromUpdateStatus expected,
                        EepromUpdateStatus actual, const char *message = nullptr)
{
    ASSERT_STATUS_MESSAGE(expected, actual, message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.countOf("FLad"), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.countOf("FLwd"), message);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.countOf("FLvy"), message);
    assertEeprom(b.fake, b.before, message);
    ASSERT_STEP_MESSAGE(EepromUpdateStep::Preflight, u.lastStep(), message);
    TEST_ASSERT_EQUAL_MESSAGE(EepromRegion::Low, u.lastTarget(), message);
}

// What must hold after any programBlank() call that started from an erased
// part. A valid pointer leads only to a region that holds the image, compared,
// behind an AppConfigOffset word of 0. After a failure in the High pass (not
// in its word write) the Low pass is complete. Success means the full-flash
// layout.
void assertBlankInvariant(const BlankBoard &b, const TPS25751EepromUpdater &u, EepromUpdateStatus s,
                          const char *where)
{
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        if (!pointerValid(b.fake, r)) continue;
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, r, b.image), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, r), where);
    }
    if (s == EepromUpdateStatus::Success) {
        ASSERT_STEP_MESSAGE(EepromUpdateStep::Done, u.lastStep(), where);
        assertEeprom(b.fake, fullFlashLayout(b.image, b.fake.eeprom.size()), where);
        return;
    }
    TEST_ASSERT_TRUE_MESSAGE(u.lastStep() != EepromUpdateStep::Done, where);
    if (u.lastTarget() == EepromRegion::High && u.lastStep() != EepromUpdateStep::ClearAppConfigOffset) {
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), where);
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::Low, b.image), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::Low), where);
    }
}

}  // namespace

void test_program_blank_full_call_sequence()
{
    {
        BlankBoard b;
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));

        assertCalls(b.fake, expectedProgramBlank(b.image));
        TEST_ASSERT_EQUAL_size_t(SMALL_BLANK_CALLS, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(SMALL_BLANK_CALLS, blankCallsOf(SMALL_LEN).size());
        ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
        TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());
        // MODE and BOOT_FLAGS are read once each, in pre-flight.
        TEST_ASSERT_EQUAL_size_t(1, b.fake.modeReads);
        TEST_ASSERT_EQUAL_size_t(1, b.fake.bootFlagsReads);
    }

    // The real length: 2732 tasks, as the Doxygen states. Two word writes (4
    // tasks each), then per region 450 FLwd (pointer, 448 chunks, pointer), 4
    // FLad, 906 FLrd (two per pointer write, the pointer row, four early
    // reads, 896 compare reads, the word check), and 1 FLvy.
    {
        BlankBoard b(BUNDLE_LEN);
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));

        assertCalls(b.fake, expectedProgramBlank(b.image));
        TEST_ASSERT_EQUAL_size_t(REAL_BLANK_CALLS, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(REAL_BLANK_CALLS, blankCallsOf(BUNDLE_LEN).size());
        TEST_ASSERT_EQUAL_size_t(2 + 2 * 450, b.fake.countOf("FLwd"));
        TEST_ASSERT_EQUAL_size_t(2 + 2 * 4, b.fake.countOf("FLad"));
        TEST_ASSERT_EQUAL_size_t(2 + 2 * 2 + 2 * 906, b.fake.countOf("FLrd"));
        TEST_ASSERT_EQUAL_size_t(2, b.fake.countOf("FLvy"));
    }
}

void test_program_blank_on_a_blank_eeprom_equals_the_full_flash_layout()
{
    // The real image, the short image of the sequence tests, the smallest image
    // checkImage() accepts (shorter than the two-chunk read-back), and the
    // largest that fits the Low region, which ends at the High bundle start.
    const size_t lengths[] = {BUNDLE_LEN, SMALL_LEN, TPS25751EepromUpdater::kMinImageLength, 0x3C00};
    for (size_t length : lengths) {
        BlankBoard b(length);
        TPS25751EepromUpdater u(b.fake);
        const char *where = at("length", length);

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()), where);

        // Byte for byte: both pointers, both AppConfigOffset words 0, the image
        // in both regions, and 0xFF everywhere else.
        assertEeprom(b.fake, fullFlashLayout(b.image), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(LOW_START, pointerOf(b.fake, EepromRegion::Low), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(HIGH_START, pointerOf(b.fake, EepromRegion::High), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::Low), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::High), where);
        assertCalls(b.fake, expectedProgramBlank(b.image));
    }

    // The part the board carries holds the 31744-byte full-flash image: the
    // layout ends at 0x7C00 for the real length, and the last 1 KB stays erased.
    BlankBoard b(BUNDLE_LEN);
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));
    for (size_t i = FULL_LEN; i < b.fake.eeprom.size(); ++i) TEST_ASSERT_EQUAL_HEX8(0xFF, b.fake.eeprom[i]);
}

void test_program_blank_from_a_corrupt_start()
{
    // Old contents, pointers that are not valid, and AppConfigOffset words that
    // are not 0. Every byte the layout defines is overwritten, so the result
    // equals the full-flash layout when the old bundles have the new length.
    const struct {
        uint32_t lowPointer;
        uint32_t highPointer;
    } pointers[] = {
        {0x00000000, 0x00000000},  // both erased, as after two interrupted updates
        {0xFFFFFFFF, 0x12345678},  // never written, and garbage
        {0x00004400, 0x00000800},  // each region's start in the other pointer
        {0x00000801, 0x000043FF},  // one off
    };
    for (int sameImage = 0; sameImage < 2; ++sameImage) {
        for (const auto &p : pointers) {
            BlankBoard b(BUNDLE_LEN);
            const std::vector<uint8_t> old = sameImage ? b.image : makeBundle(BUNDLE_LEN);
            b.fake.writeBytes(LOW_START, old.data(), old.size());
            b.fake.writeBytes(HIGH_START, old.data(), old.size());
            b.fake.writeLe32(LOW_PTR, p.lowPointer);
            b.fake.writeLe32(HIGH_PTR, p.highPointer);
            b.fake.writeLe32(0x03FC, 0x00000100);
            b.fake.writeLe32(0x07FC, 0xDEADBEEF);
            b.snapshot();
            TPS25751EepromUpdater u(b.fake);
            const char *where = at(sameImage ? "same image, low pointer" : "old image, low pointer", p.lowPointer);

            ASSERT_STATUS_MESSAGE(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()), where);
            assertEeprom(b.fake, fullFlashLayout(b.image), where);
        }
    }
}

void test_program_blank_leaves_bytes_outside_the_layout()
{
    // programBlank() writes the two pointers, the two AppConfigOffset words,
    // and the two bundles. Nothing else is erased: old bytes past the end of a
    // shorter image, and other bytes in the pointer area, stay.
    BlankBoard b(SMALL_LEN);
    const std::vector<uint8_t> old = makeBundle(0x1000);
    b.fake.writeBytes(LOW_START, old.data(), old.size());
    b.fake.writeBytes(HIGH_START, old.data(), old.size());
    const uint8_t junk[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    b.fake.writeBytes(0x0004, junk, sizeof(junk));
    b.fake.writeBytes(0x0123, junk, sizeof(junk));
    b.fake.writeBytes(0x7F00, junk, sizeof(junk));
    b.snapshot();
    TPS25751EepromUpdater u(b.fake);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));

    std::vector<uint8_t> expected = b.before;
    const auto put = [&expected](uint32_t addr, const uint8_t *src, size_t len) {
        memcpy(&expected[addr], src, len);
    };
    const std::vector<uint8_t> low = le32(LOW_START);
    const std::vector<uint8_t> high = le32(HIGH_START);
    const std::vector<uint8_t> zero = le32(0);
    put(LOW_PTR, low.data(), 4);
    put(0x03FC, zero.data(), 4);
    put(HIGH_PTR, high.data(), 4);
    put(0x07FC, zero.data(), 4);
    put(LOW_START, b.image.data(), b.image.size());
    put(HIGH_START, b.image.data(), b.image.size());
    assertEeprom(b.fake, expected);
    TEST_ASSERT_EQUAL_HEX8(old[SMALL_LEN], b.fake.eeprom[LOW_START + SMALL_LEN]);
}

void test_program_blank_refuses_a_non_blank_eeprom()
{
    // A valid pointer means the EEPROM can boot. Each case is refused after the
    // two pointer reads, with no FLad or FLwd.
    const struct {
        uint32_t lowPointer;
        uint32_t highPointer;
        const char *name;
    } cases[] = {
        {LOW_START, 0, "Low valid only"},
        {0, HIGH_START, "High valid only"},
        {LOW_START, HIGH_START, "both valid"},
        {LOW_START, 0xFFFFFFFF, "Low valid, High erased to 0xFF"},
    };
    for (const auto &c : cases) {
        BlankBoard b;
        b.fake.writeLe32(LOW_PTR, c.lowPointer);
        b.fake.writeLe32(HIGH_PTR, c.highPointer);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        // An earlier call left lastTarget() at High; programBlank() resets it.
        ASSERT_STATUS(EepromUpdateStatus::BadImageLength, u.updateRegion(EepromRegion::High, nullptr, 0));
        TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());

        assertBlankRefused(b, u, EepromUpdateStatus::EepromNotBlank,
                           u.programBlank(b.image.data(), b.image.size()), c.name);
        const std::vector<Expect> script = {flrd(LOW_PTR), flrd(HIGH_PTR)};
        assertCalls(b.fake, script);
    }

    // The full-flash layout itself, as the board ships.
    Board full;
    TPS25751EepromUpdater u(full.fake);
    assertRefusedBeforeAnyWrite(full, u, EepromUpdateStatus::EepromNotBlank,
                                u.programBlank(full.newBundle.data(), full.newBundle.size()));
}

void test_program_blank_preflight_failures()
{
    {
        BlankBoard b;
        b.fake.setMode("PTCH");
        TPS25751EepromUpdater u(b.fake);
        assertBlankRefused(b, u, EepromUpdateStatus::NotInAppMode, u.programBlank(b.image.data(), b.image.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(0, b.fake.bootFlagsReads);
    }
    {
        BlankBoard b;
        b.fake.modeReadFails = true;
        TPS25751EepromUpdater u(b.fake);
        assertBlankRefused(b, u, EepromUpdateStatus::I2cError, u.programBlank(b.image.data(), b.image.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
    {
        BlankBoard b;
        b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, false);
        TPS25751EepromUpdater u(b.fake);
        assertBlankRefused(b, u, EepromUpdateStatus::NoEepromPresent, u.programBlank(b.image.data(), b.image.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
    {
        BlankBoard b;
        b.fake.bootFlagsReadFails = true;
        TPS25751EepromUpdater u(b.fake);
        assertBlankRefused(b, u, EepromUpdateStatus::I2cError, u.programBlank(b.image.data(), b.image.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
    {
        // Image checks: no task at all.
        BlankBoard b;
        TPS25751EepromUpdater u(b.fake);
        const std::vector<uint8_t> erased(0x100, 0xFF);
        assertBlankRefused(b, u, EepromUpdateStatus::BadImageHeader, u.programBlank(erased.data(), erased.size()));
        assertBlankRefused(b, u, EepromUpdateStatus::BadImageLength, u.programBlank(nullptr, 0x100));
        assertBlankRefused(b, u, EepromUpdateStatus::BadImageLength,
                           u.programBlank(b.image.data(), TPS25751EepromUpdater::kMinImageLength - 1));
        const std::vector<uint8_t> big = makeNewBundle(0x3C01);
        assertBlankRefused(b, u, EepromUpdateStatus::BadImageLength, u.programBlank(big.data(), big.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
    {
        // A 64 KB part: 0x3C01 bytes fit the High region but not the Low one,
        // and both receive the image.
        BlankBoard b(SMALL_LEN, 65536);
        const std::vector<uint8_t> image = makeNewBundle(0x3C01);
        TPS25751EepromUpdater u(b.fake, 65536);
        assertBlankRefused(b, u, EepromUpdateStatus::BadImageLength, u.programBlank(image.data(), image.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
    }
    {
        // Order: MODE before BOOT_FLAGS before the image before the pointers.
        BlankBoard b;
        b.fake.setMode("PTCH");
        b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, false);
        b.fake.writeLe32(LOW_PTR, LOW_START);
        b.snapshot();
        const std::vector<uint8_t> erased(0x100, 0xFF);
        TPS25751EepromUpdater u(b.fake);
        assertBlankRefused(b, u, EepromUpdateStatus::NotInAppMode, u.programBlank(erased.data(), erased.size()));
        b.fake.setMode("APP ");
        assertBlankRefused(b, u, EepromUpdateStatus::NoEepromPresent, u.programBlank(erased.data(), erased.size()));
        b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
        assertBlankRefused(b, u, EepromUpdateStatus::BadImageHeader, u.programBlank(erased.data(), erased.size()));
        TEST_ASSERT_EQUAL_size_t(0, b.fake.calls.size());
        assertBlankRefused(b, u, EepromUpdateStatus::EepromNotBlank, u.programBlank(b.image.data(), b.image.size()));
    }
    {
        // A pointer read that fails: its executor status, and nothing written.
        const struct {
            size_t index;
            Fault fault;
            EepromUpdateStatus expected;
        } cases[] = {
            {0, Fault::timeout(), EepromUpdateStatus::CommandTimeout},
            {0, Fault::rejected(), EepromUpdateStatus::CommandRejected},
            {1, Fault::i2cError(), EepromUpdateStatus::I2cError},
        };
        for (const auto &c : cases) {
            BlankBoard b;
            b.fake.failCall(c.index, c.fault);
            TPS25751EepromUpdater u(b.fake);
            assertBlankRefused(b, u, c.expected, u.programBlank(b.image.data(), b.image.size()));
            TEST_ASSERT_EQUAL_size_t(c.index + 1, b.fake.calls.size());
        }
    }
}

void test_program_blank_pointer_erase_failures()
{
    // A failure of the pointer write that opens a pass: PointerEraseFailed, as
    // step 1 of updateRegion() maps it, or PointerNeighborsChanged unchanged.
    // Both AppConfigOffset words were written 0 before the pass began.
    const auto neighbor = [](EepromRegion r) {
        const uint32_t addr = TPS25751EepromUpdater::regionPointerAddress(r) + 9;
        return Fault::withHook([addr](FakeCommandExecutor &f) { f.eeprom[addr] = 0x5A; });
    };
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        const struct {
            size_t offset;
            Fault fault;
            EepromUpdateStatus expected;
            size_t callsAfter;  // calls issued after the faulted one
        } cases[] = {
            {PASS_PTR_FLAD, Fault::returnCodeOnly(0x01), EepromUpdateStatus::PointerEraseFailed, 0},
            {PASS_PTR_FLWD, Fault::returnCodeOnly(0x01), EepromUpdateStatus::PointerEraseFailed, 0},
            {PASS_PTR_FLWD, Fault::busy(), EepromUpdateStatus::PointerEraseFailed, 0},
            {PASS_PTR_FLWD, Fault::returnCodeOnly(0x00), EepromUpdateStatus::PointerEraseFailed, 1},
            {PASS_PTR_FLWD, neighbor(r), EepromUpdateStatus::PointerNeighborsChanged, 1},
            {PASS_PTR_READ, Fault::timeout(), EepromUpdateStatus::CommandTimeout, 0},
        };
        for (const auto &c : cases) {
            BlankBoard b;
            const size_t idx = blankIdx(r, c.offset);
            b.fake.failCall(idx, c.fault);
            TPS25751EepromUpdater u(b.fake);
            const char *where = at("call", idx, TPS25751EepromUpdater::toString(c.expected));

            const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

            ASSERT_STATUS_MESSAGE(c.expected, s, where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::ErasePointer, u.lastStep(), where);
            TEST_ASSERT_EQUAL_MESSAGE(r, u.lastTarget(), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(idx + 1 + c.callsAfter, b.fake.calls.size(), where);
            // Both words read 0; nothing of the region beyond its pointer word was written.
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::Low), where);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::High), where);
            TEST_ASSERT_FALSE_MESSAGE(regionHolds(b.fake, r, b.image), where);
            assertBlankInvariant(b, u, s, where);
            if (r == EepromRegion::Low) {
                TEST_ASSERT_FALSE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), where);
                TEST_ASSERT_FALSE_MESSAGE(pointerValid(b.fake, EepromRegion::High), where);
            }
        }
    }
}

void test_program_blank_app_config_offset_write_failures()
{
    // The two word writes come first, Low then High. A failure passes through
    // as the task or the check raised it, lastStep() is ClearAppConfigOffset,
    // and lastTarget() names the word: SetAddressFailed from FLad, WriteFailed
    // or FlashBusy from FLwd (no retry, as for a pointer), CompareMismatch
    // when the word does not read back as 0. No pointer has been written.
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        const struct {
            size_t offset;
            Fault fault;
            EepromUpdateStatus expected;
            size_t callsAfter;
        } cases[] = {
            {WORD_FLAD, Fault::returnCodeOnly(0x03), EepromUpdateStatus::SetAddressFailed, 0},
            {WORD_FLWD, Fault::returnCodeOnly(0x01), EepromUpdateStatus::WriteFailed, 0},
            {WORD_FLWD, Fault::busy(), EepromUpdateStatus::FlashBusy, 0},
            {WORD_FLWD, Fault::returnCodeOnly(0x00), EepromUpdateStatus::CompareMismatch, 1},
            {WORD_READ, Fault::i2cError(), EepromUpdateStatus::I2cError, 0},
            {WORD_READBACK, Fault::timeout(), EepromUpdateStatus::CommandTimeout, 0},
        };
        for (const auto &c : cases) {
            BlankBoard b;
            const size_t idx = wordIdx(r, c.offset);
            b.fake.failCall(idx, c.fault);
            TPS25751EepromUpdater u(b.fake);
            const char *where = at("call", idx, TPS25751EepromUpdater::toString(c.expected));

            const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

            ASSERT_STATUS_MESSAGE(c.expected, s, where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::ClearAppConfigOffset, u.lastStep(), where);
            TEST_ASSERT_EQUAL_MESSAGE(r, u.lastTarget(), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(idx + 1 + c.callsAfter, b.fake.calls.size(), where);
            // No pointer and no bundle byte written; for High, the Low word reads 0.
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFFFFFFFF, pointerOf(b.fake, EepromRegion::Low), where);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0xFFFFFFFF, pointerOf(b.fake, EepromRegion::High), where);
            TEST_ASSERT_EQUAL_size_t_MESSAGE(0, b.fake.countOf("FLvy"), where);
            if (r == EepromRegion::High) {
                TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::Low), where);
            }
            std::vector<uint8_t> expected = b.before;
            for (uint32_t a : {0x03FCu, 0x07FCu}) memcpy(&expected[a], &b.fake.eeprom[a], 4);
            assertEeprom(b.fake, expected, where);
            assertBlankInvariant(b, u, s, where);
        }
    }
}

void test_program_blank_app_config_offset_rows()
{
    // The 16 bytes read at 0x03FC run into the High pointer word; those at
    // 0x07FC into the first 12 bytes at 0x0800, which this call has not
    // written yet. The check compares them before and after the write, so old
    // contents there do not make the call refuse, and a change is reported.

    // Low word: a byte of the High pointer word changes during the write.
    {
        BlankBoard b;
        const size_t idx = wordIdx(EepromRegion::Low, WORD_FLWD);
        b.fake.failCall(idx, Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_PTR + 1] = 0x44; }));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::PointerNeighborsChanged, u.programBlank(b.image.data(), b.image.size()));
        ASSERT_STEP(EepromUpdateStep::ClearAppConfigOffset, u.lastStep());
        TEST_ASSERT_EQUAL(EepromRegion::Low, u.lastTarget());
        TEST_ASSERT_EQUAL_size_t(idx + 2, b.fake.calls.size());
        TEST_ASSERT_EQUAL_size_t(1, b.fake.countOf("FLwd"));
    }

    // High word: a byte at 0x0805 changes during the write. The call stops
    // before any pointer or bundle write, so no pointer is valid.
    {
        BlankBoard b;
        const size_t idx = wordIdx(EepromRegion::High, WORD_FLWD);
        b.fake.failCall(idx, Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[LOW_START + 5] ^= 0xFF; }));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::PointerNeighborsChanged, u.programBlank(b.image.data(), b.image.size()));
        ASSERT_STEP(EepromUpdateStep::ClearAppConfigOffset, u.lastStep());
        TEST_ASSERT_EQUAL(EepromRegion::High, u.lastTarget());
        TEST_ASSERT_EQUAL_size_t(idx + 2, b.fake.calls.size());
        TEST_ASSERT_FALSE(pointerValid(b.fake, EepromRegion::Low));
        TEST_ASSERT_FALSE(pointerValid(b.fake, EepromRegion::High));
        TEST_ASSERT_EQUAL_size_t(2, b.fake.countOf("FLwd"));
    }

    // A corrupt part with an old bundle at 0x0800: its Header_ID sits next to
    // the High word, and the call is not refused for it.
    {
        BlankBoard b;
        const std::vector<uint8_t> old = makeBundle(SMALL_LEN);
        b.fake.writeBytes(LOW_START, old.data(), old.size());
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        b.fake.failCall(wordIdx(EepromRegion::High, WORD_READ), Fault::withHook([](FakeCommandExecutor &f) {
            TEST_ASSERT_EQUAL_HEX32(TPS25751EepromUpdater::kBundleHeaderId,
                                    static_cast<uint32_t>(f.data[4]) | (static_cast<uint32_t>(f.data[5]) << 8) |
                                        (static_cast<uint32_t>(f.data[6]) << 16) |
                                        (static_cast<uint32_t>(f.data[7]) << 24));
        }));
        ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));
        assertEeprom(b.fake, fullFlashLayout(b.image));
    }
}

void test_program_blank_checks_the_word_before_each_pointer_write()
{
    // Right before each final pointer write the region's word is read once
    // more and must be 0. A word that changed since it was written stops the
    // pass with CompareMismatch, lastStep() CheckAppConfigOffset, and the
    // pointer still 0. Here a hook on the pass's FLvy sets the word to 1.
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        BlankBoard b;
        const uint32_t word = TPS25751EepromUpdater::appConfigOffsetAddress(r);
        const size_t idx = blankStepIdx(r, EepromUpdateStep::VerifyBundle, 0);
        b.fake.failCall(idx, Fault::withHook([word](FakeCommandExecutor &f) { f.writeLe32(word, 1); }));
        TPS25751EepromUpdater u(b.fake);
        const char *where = at("region", static_cast<size_t>(r));

        const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::CompareMismatch, s, where);
        ASSERT_STEP_MESSAGE(EepromUpdateStep::CheckAppConfigOffset, u.lastStep(), where);
        TEST_ASSERT_EQUAL_MESSAGE(r, u.lastTarget(), where);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(blankStepIdx(r, EepromUpdateStep::CheckAppConfigOffset, 0) + 1,
                                         b.fake.calls.size(), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, r), where);
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, r, b.image), where);
        assertBlankInvariant(b, u, s, where);
    }
    // A failed read there passes through.
    BlankBoard b;
    b.fake.failCall(blankStepIdx(EepromRegion::Low, EepromUpdateStep::CheckAppConfigOffset, 0), Fault::timeout());
    TPS25751EepromUpdater u(b.fake);
    ASSERT_STATUS(EepromUpdateStatus::CommandTimeout, u.programBlank(b.image.data(), b.image.size()));
    ASSERT_STEP(EepromUpdateStep::CheckAppConfigOffset, u.lastStep());
    TEST_ASSERT_EQUAL_HEX32(0, pointerOf(b.fake, EepromRegion::Low));
}

void test_program_blank_high_pass_failure_keeps_the_low_region()
{
    // After a failure in the High pass the Low pass is complete: the Low region
    // holds the image, its AppConfigOffset word is 0, and its pointer is valid.
    // The High word is 0 too: it was written before either pass.
    const auto corruptHigh = Fault::withHook([](FakeCommandExecutor &f) { f.eeprom[HIGH_START + 0x50] ^= 0xFF; });
    const struct {
        EepromUpdateStep step;
        size_t n;
        Fault fault;
        EepromUpdateStatus expected;
    } cases[] = {
        {EepromUpdateStep::ErasePointer, 1, Fault::returnCodeOnly(0x01), EepromUpdateStatus::PointerEraseFailed},
        {EepromUpdateStep::VerifyBundle, 0, Fault::returnCodeOnly(0x01), EepromUpdateStatus::VerifyFailed},
        {EepromUpdateStep::VerifyBundle, 0, corruptHigh, EepromUpdateStatus::CompareMismatch},
        // Call 9 of step 2 for 100 bytes: the first chunk after the read-back.
        {EepromUpdateStep::WriteBundle, 9, Fault::returnCodeOnly(0x01), EepromUpdateStatus::WriteFailed},
        {EepromUpdateStep::RestorePointer, 1, Fault::returnCodeOnly(0x01), EepromUpdateStatus::PointerRestoreFailed},
    };
    for (const auto &c : cases) {
        BlankBoard b;
        const size_t idx = blankStepIdx(EepromRegion::High, c.step, c.n);
        b.fake.failCall(idx, c.fault);
        TPS25751EepromUpdater u(b.fake);
        const char *where = at("call", idx, TPS25751EepromUpdater::toString(c.expected));

        const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

        ASSERT_STATUS_MESSAGE(c.expected, s, where);
        TEST_ASSERT_EQUAL_MESSAGE(EepromRegion::High, u.lastTarget(), where);
        TEST_ASSERT_FALSE_MESSAGE(pointerValid(b.fake, EepromRegion::High), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::High), where);
        // The Low half of the full-flash layout is in place.
        const std::vector<uint8_t> layout = fullFlashLayout(b.image);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(&layout[0], &b.fake.eeprom[0], HIGH_PTR, where);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(&layout[0x0800], &b.fake.eeprom[0x0800], HIGH_START - 0x0800, where);
        assertBlankInvariant(b, u, s, where);

        // From this state updateRegion() completes the layout: the Low region is
        // bootable and the High word is already 0.
        b.fake.setBootBit(FakeCommandExecutor::kBitI2cEepromPresent, true);
        EepromUpdateOptions keepLow;
        keepLow.invalidateOldRegion = false;
        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::Success,
                              u.updateRegion(EepromRegion::High, b.image.data(), b.image.size(), keepLow), where);
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), where);
        TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::High), where);
        TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, EepromRegion::High, b.image), where);
    }
    // The compare of the High pass is the step that caught the corrupted byte.
    BlankBoard b;
    b.fake.failCall(blankStepIdx(EepromRegion::High, EepromUpdateStep::VerifyBundle, 0), corruptHigh);
    TPS25751EepromUpdater u(b.fake);
    u.programBlank(b.image.data(), b.image.size());
    ASSERT_STEP(EepromUpdateStep::CompareBundle, u.lastStep());
}

void test_program_blank_stops_on_the_trace_of_an_earlier_misplaced_write()
{
    // Step 2's first check runs unchanged in each pass: when the 12 bytes after
    // the region pointer already hold the start of the image, the pass stops
    // before its bundle FLad, after its pointer erase.
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        BlankBoard b;
        b.fake.writeBytes(TPS25751EepromUpdater::regionPointerAddress(r) + 4, b.image.data(), 12);
        b.snapshot();
        TPS25751EepromUpdater u(b.fake);
        const char *where = at("region", static_cast<size_t>(r));

        ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerNeighborsChanged,
                              u.programBlank(b.image.data(), b.image.size()), where);
        ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, u.lastStep(), where);
        TEST_ASSERT_EQUAL_MESSAGE(r, u.lastTarget(), where);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(blankIdx(r, PASS_BUNDLE_FLAD), b.fake.calls.size(), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, r), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::Low), where);
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, EepromRegion::High), where);
        TEST_ASSERT_TRUE_MESSAGE(r == EepromRegion::High || !pointerValid(b.fake, EepromRegion::High), where);
        assertBlankInvariant(b, u, EepromUpdateStatus::PointerNeighborsChanged, where);
    }
}

void test_program_blank_executor_failure_at_every_call()
{
    // One executor failure at every call, for each failure kind, with the task
    // not run or run before the host saw the failure. Each returns the mapped
    // status, names the step and the region the call belongs to, issues nothing
    // afterwards, and leaves the invariant of assertBlankInvariant(). At the
    // real length every call is swept except among the compare reads of each
    // pass, where the first, every 16th, and the last are, as in the
    // updateRegion() sweep.
    const struct {
        Fault fault;
        EepromUpdateStatus expected;
    } kinds[] = {
        {Fault::rejected(), EepromUpdateStatus::CommandRejected},
        {Fault::timeout(), EepromUpdateStatus::CommandTimeout},
        {Fault::i2cError(), EepromUpdateStatus::I2cError},
    };
    const size_t lengths[] = {SMALL_LEN, BUNDLE_LEN};
    for (size_t length : lengths) {
        const std::vector<BlankCall> calls = blankCallsOf(length);
        TEST_ASSERT_EQUAL_size_t(length == SMALL_LEN ? SMALL_BLANK_CALLS : REAL_BLANK_CALLS, calls.size());
        // Position of each call within its run of compare reads.
        std::vector<size_t> compareOrdinal(calls.size(), 0);
        for (size_t i = 1; i < calls.size(); ++i) {
            if (calls[i].step == EepromUpdateStep::CompareBundle && calls[i - 1].step == EepromUpdateStep::CompareBundle)
                compareOrdinal[i] = compareOrdinal[i - 1] + 1;
        }
        size_t swept = 0;
        for (int ran = 0; ran < 2; ++ran) {
            for (const auto &kind : kinds) {
                for (size_t idx = 0; idx < calls.size(); ++idx) {
                    if (length == BUNDLE_LEN && calls[idx].step == EepromUpdateStep::CompareBundle) {
                        const bool last = (calls[idx + 1].step != EepromUpdateStep::CompareBundle);
                        if (compareOrdinal[idx] % 16 != 0 && !last) continue;
                    }
                    ++swept;
                    BlankBoard b(length);
                    b.fake.logPayloads = false;
                    Fault f = kind.fault;
                    f.runTask = (ran != 0);
                    b.fake.failCall(idx, f);
                    TPS25751EepromUpdater u(b.fake);
                    const char *where = at("call", idx, TPS25751EepromUpdater::toString(kind.expected));

                    const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

                    ASSERT_STATUS_MESSAGE(kind.expected, s, where);
                    ASSERT_STEP_MESSAGE(calls[idx].step, u.lastStep(), where);
                    TEST_ASSERT_EQUAL_MESSAGE(calls[idx].target, u.lastTarget(), where);
                    TEST_ASSERT_EQUAL_size_t_MESSAGE(idx + 1, b.fake.calls.size(), where);
                    assertBlankInvariant(b, u, s, where);
                    if (calls[idx].step == EepromUpdateStep::Preflight) assertEeprom(b.fake, b.before, where);
                }
            }
        }
        // 6 variants of each swept call. Real length: 2730 calls less the 1792
        // compare reads, plus the 57 of each pass's 896 that are swept.
        TEST_ASSERT_EQUAL_size_t(6 * (length == SMALL_LEN ? SMALL_BLANK_CALLS : REAL_BLANK_CALLS - 1792 + 2 * 57),
                                 swept);
    }
}

void test_program_blank_return_code_fault_at_every_call()
{
    // A wrong DATA byte 0 at every call: 0x01, 0xFF, and 0x00 with the task not
    // run (a write that writes nothing, an FLad that sets no address, a read
    // that returns stale DATA). Whatever the status, the invariant of
    // assertBlankInvariant() holds, and Success means the full-flash layout.
    // The compare reads of the real length are thinned as in the executor
    // sweep. The fake's answers where the TRM is silent (an FLwd at a stale
    // address) are assumptions; the assertions do not depend on the status.
    const uint8_t codes[] = {0x01, 0xFF, 0x00};
    const size_t lengths[] = {SMALL_LEN, BUNDLE_LEN};
    for (size_t length : lengths) {
        const std::vector<BlankCall> calls = blankCallsOf(length);
        std::vector<size_t> compareOrdinal(calls.size(), 0);
        for (size_t i = 1; i < calls.size(); ++i) {
            if (calls[i].step == EepromUpdateStep::CompareBundle && calls[i - 1].step == EepromUpdateStep::CompareBundle)
                compareOrdinal[i] = compareOrdinal[i - 1] + 1;
        }
        for (uint8_t rc : codes) {
            for (size_t idx = 0; idx < calls.size(); ++idx) {
                if (length == BUNDLE_LEN && calls[idx].step == EepromUpdateStep::CompareBundle) {
                    const bool last = (calls[idx + 1].step != EepromUpdateStep::CompareBundle);
                    if (compareOrdinal[idx] % 16 != 0 && !last) continue;
                }
                BlankBoard b(length);
                b.fake.logPayloads = false;
                b.fake.failCall(idx, Fault::returnCodeOnly(rc));
                TPS25751EepromUpdater u(b.fake);
                const char *where = at("call", idx, rc == 0x01 ? "rc 0x01" : rc == 0xFF ? "rc 0xFF" : "rc 0x00");

                const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

                assertBlankInvariant(b, u, s, where);
            }
        }
    }
}

void test_program_blank_read_back_and_a_misplaced_bundle_write()
{
    // The modelled fault of the updateRegion() read-back test: the FLad to the
    // bundle start reports 0x00 without taking effect, and the chunks go to
    // the stale write address (assumption, not in the TRM). The last write
    // before that FLad is the pass's pointer erase, as in updateRegion(), so
    // the stale address is the region pointer plus 4.
    for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
        const uint32_t pointerAddr = TPS25751EepromUpdater::regionPointerAddress(r);

        // 1. Blank region: the read-back stops the call because the bundle
        //    start is erased. The stray bytes are the 64 after the pointer word.
        {
            BlankBoard b(BUNDLE_LEN);
            b.fake.failCall(blankIdx(r, PASS_BUNDLE_FLAD, BUNDLE_LEN), Fault::returnCodeOnly(0x00));
            TPS25751EepromUpdater u(b.fake);
            const char *where = at("blank, region", static_cast<size_t>(r));

            const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

            ASSERT_STATUS_MESSAGE(EepromUpdateStatus::CompareMismatch, s, where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, u.lastStep(), where);
            TEST_ASSERT_EQUAL_MESSAGE(r, u.lastTarget(), where);
            TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, pointerOf(b.fake, r), where);
            TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(b.image.data(), &b.fake.eeprom[pointerAddr + 4], 64, where);
            assertBlankInvariant(b, u, s, where);
        }

        // 2. The region already holds the image: the bundle start reads back
        //    right, and the pointer-row check stops the call.
        {
            BlankBoard b(BUNDLE_LEN);
            b.fake.writeBytes(LOW_START, b.image.data(), b.image.size());
            b.fake.writeBytes(HIGH_START, b.image.data(), b.image.size());
            b.fake.writeLe32(LOW_PTR, 0);
            b.fake.writeLe32(HIGH_PTR, 0);
            b.snapshot();
            b.fake.failCall(blankIdx(r, PASS_BUNDLE_FLAD, BUNDLE_LEN), Fault::returnCodeOnly(0x00));
            TPS25751EepromUpdater u(b.fake);
            const char *where = at("same image, region", static_cast<size_t>(r));

            const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

            ASSERT_STATUS_MESSAGE(EepromUpdateStatus::PointerNeighborsChanged, s, where);
            ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, u.lastStep(), where);
            TEST_ASSERT_FALSE_MESSAGE(pointerValid(b.fake, r), where);
            assertBlankInvariant(b, u, s, where);
        }
    }

    // 3. Limit inherited from updateRegion(): with the fake's opt-in model of
    //    an FLrd that moves the write address, the erase's read-back leaves it
    //    at pointer + 16, outside the row the check reads. When the region
    //    already holds the image the call succeeds with 64 stray bytes there,
    //    in unused EEPROM.
    {
        BlankBoard b(BUNDLE_LEN);
        b.fake.writeBytes(HIGH_START, b.image.data(), b.image.size());
        b.snapshot();
        b.fake.flrdMovesWriteAddress = true;
        b.fake.failCall(blankIdx(EepromRegion::High, PASS_BUNDLE_FLAD, BUNDLE_LEN), Fault::returnCodeOnly(0x00));
        TPS25751EepromUpdater u(b.fake);

        ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));
        std::vector<uint8_t> expected = fullFlashLayout(b.image);
        memcpy(&expected[HIGH_PTR + 16], b.image.data(), 64);
        assertEeprom(b.fake, expected);
    }

    // 4. With every FLad taking effect, the moving-address model changes nothing.
    {
        BlankBoard b(BUNDLE_LEN);
        b.fake.flrdMovesWriteAddress = true;
        TPS25751EepromUpdater u(b.fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));
        assertEeprom(b.fake, fullFlashLayout(b.image));
    }
}

void test_program_blank_flad_without_effect_at_every_flad()
{
    // The sweep behind the step order decided on 2026-11-23: an FLad that
    // reports 0x00 without taking effect, at each of the 10 FLad calls of the
    // call, from three start states, with and without the fake's model of an
    // FLrd that moves the write address (both assumptions, not in the TRM).
    // The outcome that must never occur is a valid pointer over a region that
    // does not hold the image or behind a word that is not 0. Success must
    // leave both regions holding the image behind valid pointers and words of
    // 0; stray bytes outside the bundles and the four words are allowed, and
    // test_program_blank_read_back_and_a_misplaced_bundle_write pins where.
    enum Start { Blank, OldImage, SameImage };
    size_t runs = 0;
    for (int start = Blank; start <= SameImage; ++start) {
        for (int moves = 0; moves < 2; ++moves) {
            const std::vector<BlankCall> calls = blankCallsOf(BUNDLE_LEN);
            std::vector<size_t> fladCalls;
            {
                BlankBoard probe(BUNDLE_LEN);
                TPS25751EepromUpdater pu(probe.fake);
                pu.programBlank(probe.image.data(), probe.image.size());
                for (size_t i = 0; i < probe.fake.calls.size(); ++i)
                    if (probe.fake.calls[i].cmd == "FLad") fladCalls.push_back(i);
            }
            TEST_ASSERT_EQUAL_size_t(10, fladCalls.size());
            for (size_t idx : fladCalls) {
                ++runs;
                BlankBoard b(BUNDLE_LEN);
                b.fake.logPayloads = false;
                if (start != Blank) {
                    const std::vector<uint8_t> content = (start == SameImage) ? b.image : makeBundle(BUNDLE_LEN);
                    b.fake.writeBytes(LOW_START, content.data(), content.size());
                    b.fake.writeBytes(HIGH_START, content.data(), content.size());
                    b.fake.writeLe32(LOW_PTR, 0);
                    b.fake.writeLe32(HIGH_PTR, 0x12345678);
                    b.fake.writeLe32(0x03FC, (start == SameImage) ? 0 : 0x100);
                    b.fake.writeLe32(0x07FC, (start == SameImage) ? 0 : 0xDEADBEEF);
                    b.snapshot();
                }
                b.fake.flrdMovesWriteAddress = (moves != 0);
                b.fake.failCall(idx, Fault::returnCodeOnly(0x00));
                TPS25751EepromUpdater u(b.fake);
                char where[96];
                snprintf(where, sizeof(where), "start %d, moves %d, call %u", start, moves,
                         static_cast<unsigned>(idx));

                const EepromUpdateStatus s = u.programBlank(b.image.data(), b.image.size());

                for (EepromRegion r : {EepromRegion::Low, EepromRegion::High}) {
                    if (!pointerValid(b.fake, r)) continue;
                    TEST_ASSERT_TRUE_MESSAGE(regionHolds(b.fake, r, b.image), where);
                    TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, appConfigOffsetOf(b.fake, r), where);
                }
                if (s == EepromUpdateStatus::Success) {
                    TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::Low), where);
                    TEST_ASSERT_TRUE_MESSAGE(pointerValid(b.fake, EepromRegion::High), where);
                }
            }
        }
    }
    TEST_ASSERT_EQUAL_size_t(3 * 2 * 10, runs);
}

void test_program_blank_progress_sequence()
{
    // Per region, the write phase and the compare phase as for updateRegion().
    // The write phase's done == 0 call comes after the AppConfigOffset word has
    // read back as 0 and before the bundle FLad.
    BlankBoard b;
    TPS25751EepromUpdater u(b.fake);
    ProgressLog log;
    log.updater = &u;
    std::vector<size_t> callsAtZero;
    std::vector<uint32_t> appConfigAtZero;
    log.onCall = [&](const ProgressLog::Entry &e) {
        if (e.done == 0 && e.step == EepromUpdateStep::WriteBundle) {
            callsAtZero.push_back(b.fake.calls.size());
            appConfigAtZero.push_back(appConfigOffsetOf(b.fake, e.target));
        }
    };
    u.setProgressCallback(recordProgress, &log);

    ASSERT_STATUS(EepromUpdateStatus::Success, u.programBlank(b.image.data(), b.image.size()));

    const uint32_t writeDone[] = {0, 32, 64, 96, 100};
    const uint32_t compareDone[] = {0, 16, 32, 48, 64, 80, 96, 100};
    TEST_ASSERT_EQUAL_size_t(2 * (5 + 8), log.entries.size());
    for (size_t pass = 0; pass < 2; ++pass) {
        const EepromRegion r = pass == 0 ? EepromRegion::Low : EepromRegion::High;
        const size_t base = pass * 13;
        for (size_t i = 0; i < 5; ++i) {
            const ProgressLog::Entry &e = log.entries[base + i];
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(writeDone[i], e.done, at("write entry", base + i));
            ASSERT_STEP_MESSAGE(EepromUpdateStep::WriteBundle, e.step, at("write entry", base + i));
            TEST_ASSERT_EQUAL_MESSAGE(r, e.target, at("write entry", base + i));
        }
        for (size_t i = 0; i < 8; ++i) {
            const ProgressLog::Entry &e = log.entries[base + 5 + i];
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(compareDone[i], e.done, at("compare entry", base + 5 + i));
            ASSERT_STEP_MESSAGE(EepromUpdateStep::CompareBundle, e.step, at("compare entry", base + 5 + i));
            TEST_ASSERT_EQUAL_MESSAGE(r, e.target, at("compare entry", base + 5 + i));
        }
    }
    for (const ProgressLog::Entry &e : log.entries) TEST_ASSERT_EQUAL_UINT32(SMALL_LEN, e.total);

    TEST_ASSERT_EQUAL_size_t(2, callsAtZero.size());
    TEST_ASSERT_EQUAL_size_t(blankIdx(EepromRegion::Low, PASS_BUNDLE_FLAD), callsAtZero[0]);
    TEST_ASSERT_EQUAL_size_t(blankIdx(EepromRegion::High, PASS_BUNDLE_FLAD), callsAtZero[1]);
    TEST_ASSERT_EQUAL_HEX32(0, appConfigAtZero[0]);
    TEST_ASSERT_EQUAL_HEX32(0, appConfigAtZero[1]);

    // A failure ends the calls: an FLvy failure in the Low pass leaves its
    // write phase complete and no compare phase.
    BlankBoard f;
    f.fake.failNth("FLvy", 0, Fault::returnCodeOnly(0x01));
    TPS25751EepromUpdater fu(f.fake);
    ProgressLog flog;
    flog.updater = &fu;
    fu.setProgressCallback(recordProgress, &flog);
    ASSERT_STATUS(EepromUpdateStatus::VerifyFailed, fu.programBlank(f.image.data(), f.image.size()));
    TEST_ASSERT_EQUAL_size_t(5, flog.entries.size());
    TEST_ASSERT_EQUAL_UINT32(SMALL_LEN, flog.entries.back().done);
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
    // MODE read is always made, after the 100 ms settle time. A 50 ms reboot
    // is over by then, so that read is APP with no failure seen before it.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 50;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetNotObserved, u.coldReset(0));
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

void test_cold_reset_requires_an_observed_restart()
{
    // Success needs evidence that the controller went away after GAID: the
    // executor's I2CError on GAID, or a failed MODE read. APP without either
    // is ResetNotObserved, returned at the first APP read.

    // GAID completes and MODE reads APP at once: no evidence. The fake scripts
    // this with a reboot of 0 ms, so no MODE read fails.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 0;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetNotObserved, u.coldReset());
        TEST_ASSERT_EQUAL_size_t(1, fake.resetCount);
        TEST_ASSERT_EQUAL_size_t(1, fake.modeReads);
        TEST_ASSERT_EQUAL_UINT32(100, fake.nowMs());
        ASSERT_STEP(EepromUpdateStep::Reset, u.lastStep());
    }
    // A GAID the controller never acted on, with the executor reporting
    // Success or Timeout: the hazard the rule exists for.
    {
        const Fault kinds[] = {Fault::returnCodeOnly(0x00), Fault::timeout()};
        for (const Fault &kind : kinds) {
            FakeCommandExecutor fake;
            fake.failNth("GAID", 0, kind);
            TPS25751EepromUpdater u(fake);
            ASSERT_STATUS(EepromUpdateStatus::ResetNotObserved, u.coldReset());
            TEST_ASSERT_EQUAL_size_t(0, fake.resetCount);
            TEST_ASSERT_EQUAL_size_t(1, fake.modeReads);
            ASSERT_STEP(EepromUpdateStep::Reset, u.lastStep());
        }
    }
    // The cost of the rule, pinned: a restart shorter than the 100 ms settle
    // time happened but is reported as not observed.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = GAID_SETTLE_MS;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetNotObserved, u.coldReset());
        TEST_ASSERT_EQUAL_size_t(1, fake.resetCount);
    }
    // One failed MODE read is enough evidence.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = GAID_SETTLE_MS + 1;
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());
        TEST_ASSERT_EQUAL_size_t(2, fake.modeReads);
        ASSERT_STEP(EepromUpdateStep::Done, u.lastStep());
    }
    // So is an I2CError on GAID with every MODE read succeeding. Stage 3 saw
    // the I2CError and a failed first MODE read as well; here the reboot is
    // over before the first read, so the I2CError alone is the evidence.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 0;
        Fault f = Fault::i2cError();
        f.runTask = true;
        fake.failNth("GAID", 0, f);
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::Success, u.coldReset());
        TEST_ASSERT_EQUAL_size_t(1, fake.modeReads);
    }
    // A MODE read that succeeds and is not APP is not evidence: the call does
    // not read MODE before GAID, so it cannot know the controller was in APP
    // then. PTCH twice and then APP, with no failed read, is ResetNotObserved.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 0;
        fake.modeReadScript = {"PTCH", "PTCH"};
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetNotObserved, u.coldReset());
        TEST_ASSERT_EQUAL_size_t(3, fake.modeReads);
        TEST_ASSERT_EQUAL_UINT32(200, fake.nowMs());
    }
    // A MODE that reads PTCH without ever failing is neither: the call waits
    // for APP and times out.
    {
        FakeCommandExecutor fake;
        fake.rebootDurationMs = 0;
        fake.setModeAfterReset("PTCH");
        TPS25751EepromUpdater u(fake);
        ASSERT_STATUS(EepromUpdateStatus::ResetTimeout, u.coldReset(500));
        TEST_ASSERT_EQUAL_UINT32(500, fake.nowMs());
    }
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
        {EepromUpdateStatus::AppConfigOffsetNotZero, "AppConfigOffsetNotZero"},
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
        {EepromUpdateStatus::ResetNotObserved, "ResetNotObserved"},
        {EepromUpdateStatus::CommandRejected, "CommandRejected"},
        {EepromUpdateStatus::CommandTimeout, "CommandTimeout"},
        {EepromUpdateStatus::I2cError, "I2cError"},
    };
    for (const auto &e : all) TEST_ASSERT_EQUAL_STRING(e.name, TPS25751EepromUpdater::toString(e.s));
    TEST_ASSERT_EQUAL_UINT8(22, sizeof(all) / sizeof(all[0]));
    TEST_ASSERT_EQUAL_UINT8(21, static_cast<uint8_t>(EepromUpdateStatus::I2cError));
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
        {EepromUpdateStep::ClearAppConfigOffset, "ClearAppConfigOffset"},
        {EepromUpdateStep::WriteBundle, "WriteBundle"},
        {EepromUpdateStep::VerifyBundle, "VerifyBundle"},
        {EepromUpdateStep::CompareBundle, "CompareBundle"},
        {EepromUpdateStep::CheckAppConfigOffset, "CheckAppConfigOffset"},
        {EepromUpdateStep::RestorePointer, "RestorePointer"},
        {EepromUpdateStep::EraseOldPointer, "EraseOldPointer"},
        {EepromUpdateStep::Reset, "Reset"},
        {EepromUpdateStep::Done, "Done"},
    };
    for (const auto &e : all) TEST_ASSERT_EQUAL_STRING(e.name, TPS25751EepromUpdater::toString(e.step));
    TEST_ASSERT_EQUAL_UINT8(12, sizeof(all) / sizeof(all[0]));
    TEST_ASSERT_EQUAL_UINT8(11, static_cast<uint8_t>(EepromUpdateStep::Done));
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
    RUN_TEST(test_update_region_reads_back_the_first_two_chunks);
    RUN_TEST(test_update_region_read_back_of_a_short_image);
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
    RUN_TEST(test_update_region_busy_first_chunk_is_retried_before_the_read_back);
    RUN_TEST(test_update_region_busy_chunk_succeeds_on_third_retry);
    RUN_TEST(test_update_region_flash_busy_after_three_retries);
    RUN_TEST(test_update_region_retry_flad_failure);
    RUN_TEST(test_update_region_read_back_mismatch_stops_the_write);
    RUN_TEST(test_update_region_pointer_area_change_stops_the_write);
    RUN_TEST(test_update_region_stops_on_the_trace_of_an_earlier_misplaced_write);
    RUN_TEST(test_update_region_no_false_stop_on_other_bytes_after_the_pointer);
    RUN_TEST(test_update_region_repeat_after_a_stop_in_step_2);
    RUN_TEST(test_update_region_flad_after_the_read_back_fails);
    RUN_TEST(test_update_region_verify_failure_leaves_pointer_erased);
    RUN_TEST(test_update_region_compare_mismatch_leaves_pointer_erased);
    RUN_TEST(test_update_region_compare_off_trusts_flvy);
    RUN_TEST(test_update_region_pointer_restore_failures);
    RUN_TEST(test_update_region_old_pointer_erase_failures);
    RUN_TEST(test_update_region_neighbor_change_stops_the_update);
    RUN_TEST(test_update_region_executor_failure_at_every_call);
    RUN_TEST(test_update_region_return_code_fault_at_every_call);
    RUN_TEST(test_update_region_read_back_bounds_a_misplaced_bundle_write);
    RUN_TEST(test_update_region_read_order_when_flrd_moves_the_write_address);
    RUN_TEST(test_update_region_limit_of_the_read_back_in_step_2);
    RUN_TEST(test_update_region_refuses_a_kept_region_behind_a_nonzero_word);
    RUN_TEST(test_update_region_refuses_a_target_behind_a_nonzero_word);
    RUN_TEST(test_update_region_checks_the_kept_word_before_the_target_word);
    RUN_TEST(test_update_both_regions_stops_before_writing_a_target_behind_a_nonzero_word);
    RUN_TEST(test_update_region_checks_the_target_word_before_the_pointer_write);
    RUN_TEST(test_update_region_executor_failure_on_the_word_reads);
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
    RUN_TEST(test_program_blank_full_call_sequence);
    RUN_TEST(test_program_blank_on_a_blank_eeprom_equals_the_full_flash_layout);
    RUN_TEST(test_program_blank_from_a_corrupt_start);
    RUN_TEST(test_program_blank_leaves_bytes_outside_the_layout);
    RUN_TEST(test_program_blank_refuses_a_non_blank_eeprom);
    RUN_TEST(test_program_blank_preflight_failures);
    RUN_TEST(test_program_blank_pointer_erase_failures);
    RUN_TEST(test_program_blank_app_config_offset_write_failures);
    RUN_TEST(test_program_blank_app_config_offset_rows);
    RUN_TEST(test_program_blank_checks_the_word_before_each_pointer_write);
    RUN_TEST(test_program_blank_high_pass_failure_keeps_the_low_region);
    RUN_TEST(test_program_blank_stops_on_the_trace_of_an_earlier_misplaced_write);
    RUN_TEST(test_program_blank_executor_failure_at_every_call);
    RUN_TEST(test_program_blank_return_code_fault_at_every_call);
    RUN_TEST(test_program_blank_read_back_and_a_misplaced_bundle_write);
    RUN_TEST(test_program_blank_flad_without_effect_at_every_flad);
    RUN_TEST(test_program_blank_progress_sequence);
    RUN_TEST(test_cold_reset_issues_gaid_and_waits_for_app);
    RUN_TEST(test_cold_reset_treats_timeout_and_i2c_error_as_reset_issued);
    RUN_TEST(test_cold_reset_rejected_is_not_a_reset);
    RUN_TEST(test_cold_reset_times_out_when_mode_never_returns);
    RUN_TEST(test_cold_reset_times_out_when_mode_is_not_app);
    RUN_TEST(test_cold_reset_wait_argument);
    RUN_TEST(test_cold_reset_is_safe_across_a_clock_wrap);
    RUN_TEST(test_cold_reset_cannot_tell_an_undelivered_gaid_from_a_nak);
    RUN_TEST(test_cold_reset_requires_an_observed_restart);
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
