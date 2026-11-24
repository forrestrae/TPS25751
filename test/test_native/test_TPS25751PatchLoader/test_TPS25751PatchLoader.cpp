/**
 * @file test_TPS25751PatchLoader.cpp
 * @brief Native tests of TPS25751PatchLoader::loadPatchBundle(), driven through
 *        the TwoWire fake (Wire.h) and the scripted target
 *        (FakeTps25751Target.h).
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751PatchLoader"
 *
 * The sequence under test is SLVAFV8A section 4 (steps 1 to 17, pages 7 to 12,
 * and the code on pages 17 and 18) and TRM SLVUCR8B Figure 5-1 (page 88). The
 * step the documents and the hardware both care about: after PBMc completes,
 * wait 20 ms before reading DATA (SLVAFV8A step 15, page 11; the code on page
 * 18; Figure 5-1). Hardware stage 5 of the EEPROM-update plan saw a DATA read
 * made at once fail with 28 of 65 bytes.
 */

#include <unity.h>
#include <cstring>
#include <string>
#include <vector>

#include "TPS25751.h"
#include "FakeTps25751Target.h"
#include "CaptureStdout.h"

namespace {

using Txn = FakeTps25751Target::Txn;
using Kind = FakeTps25751Target::Kind;
using Fault = FakeTps25751Target::Fault;
using TaskScript = FakeTps25751Target::TaskScript;

constexpr uint8_t REG_MODE = FakeTps25751Target::kRegMode;
constexpr uint8_t REG_COMMAND = FakeTps25751Target::kRegCommand;
constexpr uint8_t REG_DATA = FakeTps25751Target::kRegData;

std::vector<uint8_t> makeImage(size_t len = 100)
{
    std::vector<uint8_t> b(len);
    for (size_t i = 0; i < len; ++i) b[i] = static_cast<uint8_t>((i * 7 + 3) ^ (i >> 8));
    return b;
}

struct Rig
{
    FakeTps25751Target dev;
    TPS25751 pd;
    std::vector<uint8_t> image = makeImage();

    Rig() : pd(Wire, 0x20)
    {
        fake_arduino::reset();
        Wire = TwoWire();
        Wire.target = &dev;
    }
    ~Rig() { Wire.target = nullptr; }

    PatchLoadStatus load(int irqPin = -1)
    {
        TPS25751PatchLoader loader(pd, irqPin);
        return loader.loadPatchBundle(image.data(), image.size());
    }

    // Index of the COMMAND write that carries this 4CC.
    long commandWrite(const char *code) const
    {
        for (size_t i = 0; i < dev.log.size(); ++i) {
            const Txn &t = dev.log[i];
            if (dev.isRegWrite(t, REG_COMMAND) && t.bytes.size() == 6 && memcmp(&t.bytes[2], code, 4) == 0)
                return static_cast<long>(i);
        }
        return -1;
    }

    // Index of the first COMMAND read after `from` that returned 4 zero bytes.
    long commandClear(long from) const
    {
        for (size_t i = static_cast<size_t>(from) + 1; i < dev.log.size(); ++i) {
            const Txn &t = dev.log[i];
            if (dev.isRead(t, REG_COMMAND) && t.bytes.size() == 5 && t.bytes[1] == 0 && t.bytes[2] == 0 &&
                t.bytes[3] == 0 && t.bytes[4] == 0)
                return static_cast<long>(i);
        }
        return -1;
    }

    // Indices of reads of a register at or after `from`.
    std::vector<size_t> reads(uint8_t reg, long from = 0) const
    {
        std::vector<size_t> out;
        for (size_t i = static_cast<size_t>(from); i < dev.log.size(); ++i)
            if (dev.isRead(dev.log[i], reg)) out.push_back(i);
        return out;
    }
};

const char *name(PatchLoadStatus s) { return TPS25751PatchLoader::toString(s); }

#define ASSERT_LOAD(expected, actual) TEST_ASSERT_EQUAL_STRING(name(expected), name(actual))

// A fault that makes every read of a register fail with a short read, while
// the stub clock is inside [fromMs, toMs).
Fault shortReads(int reg, uint64_t fromMs, uint64_t toMs, size_t bytes = 0)
{
    Fault f;
    f.kind = 1;
    f.reg = reg;
    f.fromUs = fromMs * 1000;
    f.toUs = toMs * 1000;
    f.readBytes = bytes;
    return f;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- The documented sequence ---------------------------------------------------------

void test_load_follows_the_documented_sequence()
{
    Rig r;
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const std::vector<Txn> &log = r.dev.log;

    // Step 2: MODE reads PTCH.
    TEST_ASSERT_TRUE(r.dev.isRead(log[1], REG_MODE));
    // Steps 4 to 9: the PBMs configuration in DATA, little-endian bundle size,
    // target 0x30, timeout 0x31 (SLVAFV8A Table 4-1), then PBMs, its COMMAND
    // poll, and DATA read at once (steps 8 and 9 read DATA right after the
    // COMMAND clear).
    const uint8_t config[] = {0x09, 6, 100, 0, 0, 0, 0x30, 0x31};
    TEST_ASSERT_TRUE(r.dev.isRegWrite(log[2], REG_DATA));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(config, log[2].bytes.data(), sizeof(config));
    const long pbms = r.commandWrite("PBMs");
    TEST_ASSERT_EQUAL_INT(3, pbms);
    const long pbmsClear = r.commandClear(pbms);
    TEST_ASSERT_TRUE(pbmsClear > pbms);
    TEST_ASSERT_TRUE(r.dev.isRead(log[pbmsClear + 2], REG_DATA));
    TEST_ASSERT_EQUAL_UINT64(log[pbmsClear].timeUs, log[pbmsClear + 2].timeUs);

    // Step 10: the image in 32-byte raw writes to 0x30, then PBMc.
    TEST_ASSERT_EQUAL_HEX8_ARRAY(r.image.data(), r.dev.burst.data(), r.image.size());
    size_t bursts = r.dev.count([](const Txn &t) { return t.addr == 0x30; });
    TEST_ASSERT_EQUAL_size_t(4, bursts);
    const long pbmc = r.commandWrite("PBMc");
    TEST_ASSERT_EQUAL_INT(pbmsClear + 2 + 1 + 4, pbmc);
    const long pbmcClear = r.commandClear(pbmc);

    // Steps 15 to 17: 20 ms, then DATA, then MODE, and nothing else.
    const std::vector<size_t> dataReads = r.reads(REG_DATA, pbmc);
    TEST_ASSERT_EQUAL_size_t(1, dataReads.size());
    TEST_ASSERT_EQUAL_UINT64(log[pbmcClear].timeUs + 20000, log[dataReads[0]].timeUs);
    const std::vector<size_t> modeReads = r.reads(REG_MODE, pbmc);
    TEST_ASSERT_EQUAL_size_t(1, modeReads.size());
    TEST_ASSERT_TRUE(modeReads[0] > dataReads[0]);
    TEST_ASSERT_EQUAL_size_t(modeReads[0] + 1, log.size());
}

void test_no_data_read_within_20_ms_of_pbmc_completing()
{
    // The regression test for the defect hardware stage 5 found: the old code
    // read DATA in the same instant COMMAND cleared after PBMc.
    Rig r;
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const long pbmc = r.commandWrite("PBMc");
    const long clear = r.commandClear(pbmc);
    TEST_ASSERT_TRUE(clear > 0);
    const uint64_t clearUs = r.dev.log[clear].timeUs;
    for (size_t i = static_cast<size_t>(clear) + 1; i < r.dev.log.size(); ++i) {
        const Txn &t = r.dev.log[i];
        if (r.dev.isRead(t, REG_DATA)) TEST_ASSERT_TRUE_MESSAGE(t.timeUs >= clearUs + 20000, "DATA read within 20 ms");
    }
}

void test_a_controller_off_the_bus_for_20_ms_after_pbmc()
{
    // A fault model of the stage 5 observation: for 20 ms after PBMc
    // completes, every read at the unique address returns 28 of the bytes
    // asked for. The documented sequence makes no read in that window. This
    // models the documents' requirement; it does not reproduce the hardware.
    Rig r;
    r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
        Fault f;
        f.kind = 1;
        f.addr = 0x20;
        f.fromUs = d.lastCompletedUs;
        f.toUs = d.lastCompletedUs + 20000;
        f.readBytes = 28;
        d.faults.push_back(f);
    };
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
}

void test_500_us_after_the_burst_before_the_next_transaction()
{
    // TRM Figure 5-1 (page 88): "Delay at least 500us" between the last burst
    // and PBMc; SLVAFV8A step 10 (page 10) likewise. No delay between chunks.
    for (int irq = 0; irq < 2; ++irq) {
        Rig r;
        if (irq) fake_arduino::g_pinLevel[5] = LOW;
        ASSERT_LOAD(PatchLoadStatus::Success, r.load(irq ? 5 : -1));
        std::vector<size_t> bursts;
        for (size_t i = 0; i < r.dev.log.size(); ++i)
            if (r.dev.log[i].addr == 0x30) bursts.push_back(i);
        TEST_ASSERT_EQUAL_size_t(4, bursts.size());
        for (size_t i = 1; i < bursts.size(); ++i)
            TEST_ASSERT_EQUAL_UINT64(r.dev.log[bursts[0]].timeUs, r.dev.log[bursts[i]].timeUs);
        const Txn &next = r.dev.log[bursts.back() + 1];
        TEST_ASSERT_TRUE(next.timeUs >= r.dev.log[bursts.back()].timeUs + PBM_PRE_COMMIT_DELAY_US);
        TEST_ASSERT_EQUAL_UINT32(500, PBM_PRE_COMMIT_DELAY_US);
        // The next transaction is INT_CLEAR1 in IRQ mode and PBMc otherwise.
        TEST_ASSERT_TRUE(r.dev.isRegWrite(next, irq ? FakeTps25751Target::kRegIntClear1 : REG_COMMAND));
    }
}

// --- DATA after PBMc -----------------------------------------------------------------

void test_data_read_is_retried_every_10_ms()
{
    Rig r;
    Fault f = shortReads(REG_DATA, 0, 1000000);
    f.remaining = 3;
    r.dev.tasks["PBMc"].onDone = [f](FakeTps25751Target &d) mutable {
        f.fromUs = d.lastCompletedUs;
        f.toUs = d.lastCompletedUs + 1000000000ull;
        d.faults.push_back(f);
    };
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const std::vector<size_t> dataReads = r.reads(REG_DATA, r.commandWrite("PBMc"));
    TEST_ASSERT_EQUAL_size_t(4, dataReads.size());
    for (size_t i = 1; i < dataReads.size(); ++i)
        TEST_ASSERT_EQUAL_UINT64(10000, r.dev.log[dataReads[i]].timeUs - r.dev.log[dataReads[i - 1]].timeUs);
}

void test_data_read_gives_up_at_its_cap()
{
    Rig r;
    r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
        Fault f;
        f.kind = 1;
        f.reg = REG_DATA;
        f.readBytes = 28;
        f.fromUs = d.lastCompletedUs;
        f.toUs = d.lastCompletedUs + 1000000000ull;
        d.faults.push_back(f);
    };
    ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
    const long pbmc = r.commandWrite("PBMc");
    const long clear = r.commandClear(pbmc);
    const std::vector<size_t> dataReads = r.reads(REG_DATA, pbmc);
    // Reads at 20, 30, ..., up to the cap after the first; no MODE read follows.
    const uint64_t last = r.dev.log[dataReads.back()].timeUs - r.dev.log[clear].timeUs;
    TEST_ASSERT_TRUE(last >= 20000 + PBM_DATA_READ_CAP_MS * 1000ull - 10000);
    TEST_ASSERT_TRUE(last <= 20000 + PBM_DATA_READ_CAP_MS * 1000ull);
    TEST_ASSERT_EQUAL_size_t(PBM_DATA_READ_CAP_MS / 10 + 1, dataReads.size());
    TEST_ASSERT_EQUAL_size_t(0, r.reads(REG_MODE, pbmc).size());
}

void test_nonzero_data_byte_0_is_patch_apply_failed()
{
    Rig r;
    r.dev.tasks["PBMc"].output = {0x01};
    ASSERT_LOAD(PatchLoadStatus::PatchApplyFailed, r.load());
    TEST_ASSERT_EQUAL_size_t(0, r.reads(REG_MODE, r.commandWrite("PBMc")).size());
}

// --- MODE after PBMc -----------------------------------------------------------------

void test_mode_poll_through_failed_reads_and_ptch_to_app()
{
    // MODE switches to APP 100 ms after PBMc completes, and MODE reads fail
    // between 20 and 60 ms after it: failed reads, then PTCH, then APP, every
    // 10 ms.
    Rig r;
    r.dev.modeAppDelayMs = 100;
    r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
        d.faults.push_back(shortReads(REG_MODE, d.lastCompletedUs / 1000 + 20, d.lastCompletedUs / 1000 + 60));
    };
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const long pbmc = r.commandWrite("PBMc");
    const uint64_t clearUs = r.dev.log[r.commandClear(pbmc)].timeUs;
    const std::vector<size_t> modeReads = r.reads(REG_MODE, pbmc);
    TEST_ASSERT_EQUAL_size_t(9, modeReads.size());  // 20, 30, ..., 100 ms
    for (size_t i = 1; i < modeReads.size(); ++i)
        TEST_ASSERT_EQUAL_UINT64(10000, r.dev.log[modeReads[i]].timeUs - r.dev.log[modeReads[i - 1]].timeUs);
    TEST_ASSERT_EQUAL_UINT64(clearUs + 100000, r.dev.log[modeReads.back()].timeUs);
}

void test_mode_poll_cap_outcomes()
{
    {
        // MODE keeps reading PTCH: NotInAppMode at the cap.
        Rig r;
        r.dev.modeAppDelayMs = 1000000;
        ASSERT_LOAD(PatchLoadStatus::NotInAppMode, r.load());
        const long pbmc = r.commandWrite("PBMc");
        const std::vector<size_t> modeReads = r.reads(REG_MODE, pbmc);
        TEST_ASSERT_EQUAL_size_t(PBM_MODE_POLL_CAP_MS / 10 + 1, modeReads.size());
    }
    {
        // No MODE read succeeds: I2cError at the cap.
        Rig r;
        r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
            d.faults.push_back(shortReads(REG_MODE, d.lastCompletedUs / 1000, d.lastCompletedUs / 1000 + 1000000));
        };
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
    }
    {
        // A PTCH read, then only failed reads: the last read that succeeded
        // was not APP, so NotInAppMode.
        Rig r;
        r.dev.modeAppDelayMs = 1000000;
        r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
            d.faults.push_back(shortReads(REG_MODE, d.lastCompletedUs / 1000 + 25, d.lastCompletedUs / 1000 + 1000000));
        };
        ASSERT_LOAD(PatchLoadStatus::NotInAppMode, r.load());
    }
}

void test_caps_across_a_millis_wrap()
{
    // millis() wraps at 2^32 ms, as on the Teensy 4. Both caps measure elapsed
    // time by unsigned subtraction, so each gives up after the same number of
    // reads when the wrap falls inside its window.
    const uint64_t startUs = (0x100000000ull - 2000) * 1000;  // 2 s before the wrap
    {
        Rig r;
        fake_arduino::g_nowUs = startUs;
        r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
            Fault f;
            f.kind = 1;
            f.reg = REG_DATA;
            f.fromUs = d.lastCompletedUs;
            f.toUs = d.lastCompletedUs + 1000000000ull;
            d.faults.push_back(f);
        };
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
        TEST_ASSERT_EQUAL_size_t(PBM_DATA_READ_CAP_MS / 10 + 1, r.reads(REG_DATA, r.commandWrite("PBMc")).size());
        TEST_ASSERT_TRUE(millis() < 10000);  // wrapped
    }
    {
        Rig r;
        fake_arduino::g_nowUs = startUs;
        r.dev.modeAppDelayMs = 1000000;
        ASSERT_LOAD(PatchLoadStatus::NotInAppMode, r.load());
        TEST_ASSERT_EQUAL_size_t(PBM_MODE_POLL_CAP_MS / 10 + 1, r.reads(REG_MODE, r.commandWrite("PBMc")).size());
    }
}

void test_pbmc_that_takes_longer_than_200_ms()
{
    // PBMc is polled for up to PBMC_CMD_TIMEOUT_MS (5000 ms), not the 200 ms
    // default of the task calls: a PBMc that completes after 300 ms of polls
    // succeeds.
    Rig r;
    r.dev.tasks["PBMc"].pollsBeforeDone = 60;  // 60 polls 5 ms apart
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const long pbmc = r.commandWrite("PBMc");
    const long clear = r.commandClear(pbmc);
    TEST_ASSERT_TRUE(r.dev.log[clear].timeUs - r.dev.log[pbmc].timeUs >= 300000);
}

// --- Every other status --------------------------------------------------------------

void test_not_in_patch_mode()
{
    {
        Rig r;
        r.dev.setMode("APP ");
        ASSERT_LOAD(PatchLoadStatus::NotInPatchMode, r.load());
        TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMs"));
    }
    {
        Rig r;
        r.dev.faults.push_back(shortReads(REG_MODE, 0, 1000000));
        ASSERT_LOAD(PatchLoadStatus::NotInPatchMode, r.load());
    }
}

void test_pbms_outcomes()
{
    const struct {
        uint8_t rc;
        PatchLoadStatus expected;
    } codes[] = {
        {0x04, PatchLoadStatus::BadBundleSize},
        {0x05, PatchLoadStatus::BadTargetAddress},
        {0x06, PatchLoadStatus::BadTimeout},
        {0x01, PatchLoadStatus::PbmsRejected},
    };
    for (const auto &c : codes) {
        Rig r;
        r.dev.tasks["PBMs"].output = {c.rc};
        ASSERT_LOAD(c.expected, r.load());
        TEST_ASSERT_EQUAL_size_t(0, r.dev.count([](const Txn &t) { return t.addr == 0x30; }));
    }
    {
        Rig r;
        r.dev.tasks["PBMs"].reject = true;
        ASSERT_LOAD(PatchLoadStatus::PbmsRejected, r.load());
    }
    {
        Rig r;
        r.dev.tasks["PBMs"].pollsBeforeDone = 100000;
        ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load());
    }
    {
        // The DATA write of the configuration is not acknowledged.
        Rig r;
        Fault f;
        f.index = 2;
        r.dev.faults.push_back(f);
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
    }
}

void test_burst_failure()
{
    Rig r;
    Fault f;
    f.addr = 0x30;
    f.writeError = 2;
    f.remaining = 1;
    r.dev.faults.push_back(f);
    ASSERT_LOAD(PatchLoadStatus::BurstFailed, r.load());
    TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMc"));
}

void test_pbmc_outcomes()
{
    {
        Rig r;
        r.dev.tasks["PBMc"].reject = true;
        ASSERT_LOAD(PatchLoadStatus::PbmcRejected, r.load());
    }
    {
        Rig r;
        r.dev.tasks["PBMc"].pollsBeforeDone = 100000;
        ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load());
    }
    {
        // The COMMAND write of PBMc is not acknowledged.
        Rig r;
        Fault f;
        f.reg = REG_COMMAND;
        f.kind = 0;
        f.writeError = 2;
        r.dev.tasks["PBMs"].onDone = [f](FakeTps25751Target &d) { d.faults.push_back(f); };
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
    }
}

void test_irq_mode()
{
    {
        // With the pin low at once: INT_MASK1 enables CMD1 complete (bit 30),
        // INT_CLEAR1 is written all ones before PBMs and again before PBMc.
        Rig r;
        fake_arduino::g_pinLevel[5] = LOW;
        ASSERT_LOAD(PatchLoadStatus::Success, r.load(5));
        std::vector<size_t> masks, clears;
        for (size_t i = 0; i < r.dev.log.size(); ++i) {
            if (r.dev.isRegWrite(r.dev.log[i], FakeTps25751Target::kRegIntMask1)) masks.push_back(i);
            if (r.dev.isRegWrite(r.dev.log[i], FakeTps25751Target::kRegIntClear1)) clears.push_back(i);
        }
        TEST_ASSERT_EQUAL_size_t(1, masks.size());
        TEST_ASSERT_EQUAL_HEX8(0x40, r.dev.log[masks[0]].bytes[2 + 3]);
        TEST_ASSERT_EQUAL_size_t(2, clears.size());
        TEST_ASSERT_TRUE(static_cast<long>(clears[1]) < r.commandWrite("PBMc"));
    }
    {
        // The pin never goes low: CommandTimeout after the 5 s wait, before
        // any transaction.
        Rig r;
        ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load(5));
        TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
        TEST_ASSERT_TRUE(millis() > 5000);
    }
}

void test_to_string_and_debug_print()
{
    const PatchLoadStatus all[] = {
        PatchLoadStatus::Success,          PatchLoadStatus::NotInPatchMode, PatchLoadStatus::PbmsRejected,
        PatchLoadStatus::BadBundleSize,    PatchLoadStatus::BadTargetAddress, PatchLoadStatus::BadTimeout,
        PatchLoadStatus::BurstFailed,      PatchLoadStatus::PbmcRejected,   PatchLoadStatus::PatchApplyFailed,
        PatchLoadStatus::NotInAppMode,     PatchLoadStatus::CommandTimeout, PatchLoadStatus::I2cError,
    };
    const char *names[] = {"Success",          "NotInPatchMode", "PbmsRejected", "BadBundleSize",
                           "BadTargetAddress", "BadTimeout",     "BurstFailed",  "PbmcRejected",
                           "PatchApplyFailed", "NotInAppMode",   "CommandTimeout", "I2cError"};
    for (size_t i = 0; i < 12; ++i) TEST_ASSERT_EQUAL_STRING(names[i], TPS25751PatchLoader::toString(all[i]));
    TEST_ASSERT_EQUAL_STRING("Unknown", TPS25751PatchLoader::toString(static_cast<PatchLoadStatus>(200)));

    // debugPrint() reports the result of the last load.
    Rig r;
    TPS25751PatchLoader loader(r.pd);
    r.dev.tasks["PBMc"].reject = true;
    ASSERT_LOAD(PatchLoadStatus::PbmcRejected, loader.loadPatchBundle(r.image.data(), r.image.size()));
    const std::string out = captureStdout([&] { loader.debugPrint(Serial); });
    TEST_ASSERT_TRUE(out.find("TPS25751PatchLoader") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("lastStatus : PbmcRejected") != std::string::npos);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_load_follows_the_documented_sequence);
    RUN_TEST(test_no_data_read_within_20_ms_of_pbmc_completing);
    RUN_TEST(test_a_controller_off_the_bus_for_20_ms_after_pbmc);
    RUN_TEST(test_500_us_after_the_burst_before_the_next_transaction);
    RUN_TEST(test_data_read_is_retried_every_10_ms);
    RUN_TEST(test_data_read_gives_up_at_its_cap);
    RUN_TEST(test_nonzero_data_byte_0_is_patch_apply_failed);
    RUN_TEST(test_mode_poll_through_failed_reads_and_ptch_to_app);
    RUN_TEST(test_mode_poll_cap_outcomes);
    RUN_TEST(test_caps_across_a_millis_wrap);
    RUN_TEST(test_pbmc_that_takes_longer_than_200_ms);
    RUN_TEST(test_not_in_patch_mode);
    RUN_TEST(test_pbms_outcomes);
    RUN_TEST(test_burst_failure);
    RUN_TEST(test_pbmc_outcomes);
    RUN_TEST(test_irq_mode);
    RUN_TEST(test_to_string_and_debug_print);
    return UNITY_END();
}
