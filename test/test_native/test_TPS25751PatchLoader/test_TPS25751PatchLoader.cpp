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
constexpr uint8_t REG_INT_EVENT1 = FakeTps25751Target::kRegIntEvent1;

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

    // Step 1, polling mode: INT_EVENT1 reads Ready for Patch (TRM Figure 5-1).
    TEST_ASSERT_TRUE(r.dev.isRead(log[1], REG_INT_EVENT1));
    TEST_ASSERT_EQUAL_size_t(12, log[1].requested);
    // Step 2: MODE reads PTCH.
    TEST_ASSERT_TRUE(r.dev.isRead(log[3], REG_MODE));
    // Step 4: the PBMs configuration in DATA, little-endian bundle size,
    // target 0x30, timeout 0x32 (TRM Table 4-15; hardware stage 7).
    const uint8_t config[] = {0x09, 6, 100, 0, 0, 0, 0x30, 0x32};
    TEST_ASSERT_TRUE(r.dev.isRegWrite(log[4], REG_DATA));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(config, log[4].bytes.data(), sizeof(config));
    // Step 5: DATA read back 500 us after the write (SLVAFV8A page 8).
    TEST_ASSERT_TRUE(r.dev.isRead(log[6], REG_DATA));
    TEST_ASSERT_EQUAL_UINT64(log[4].timeUs + 500, log[5].timeUs);
    // Steps 6 to 9: PBMs with no DATA write of its own, its COMMAND poll, and
    // DATA read at once (steps 8 and 9 read DATA right after the COMMAND clear).
    const long pbms = r.commandWrite("PBMs");
    TEST_ASSERT_EQUAL_INT(7, pbms);
    TEST_ASSERT_EQUAL_size_t(1, r.dev.count([&](const Txn &t) { return r.dev.isRegWrite(t, REG_DATA) && t.bytes.size() > 1; }));
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
    {
        // The Ready for Patch poll before the load.
        Rig r;
        fake_arduino::g_nowUs = startUs;
        r.dev.setReadyForPatch(false);
        ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load());
        TEST_ASSERT_EQUAL_size_t(PBM_READY_WAIT_CAP_MS / 10 + 1, r.reads(REG_INT_EVENT1).size());
        TEST_ASSERT_TRUE(millis() < 10000);  // wrapped
    }
    {
        // The PTCH poll before the load.
        Rig r;
        fake_arduino::g_nowUs = startUs;
        r.dev.setMode("BOOT");
        ASSERT_LOAD(PatchLoadStatus::NotInPatchMode, r.load());
        TEST_ASSERT_EQUAL_size_t(PBM_PTCH_POLL_CAP_MS / 10 + 1, r.reads(REG_MODE).size());
        TEST_ASSERT_TRUE(millis() < 10000);  // wrapped
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

void test_pbms_that_takes_longer_than_200_ms()
{
    // PBMs is polled for up to PBMS_CMD_TIMEOUT_MS (500 ms), not the 200 ms
    // default of the task calls: a PBMs that completes after 300 ms of polls
    // succeeds.
    Rig r;
    r.dev.tasks["PBMs"].pollsBeforeDone = 60;  // 60 polls 5 ms apart
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const long pbms = r.commandWrite("PBMs");
    const long clear = r.commandClear(pbms);
    TEST_ASSERT_TRUE(r.dev.log[clear].timeUs - r.dev.log[pbms].timeUs >= 300000);
}

void test_irq_ready_wait_across_a_millis_wrap()
{
    // The step 1 wait for I2Ct_IRQ low measures elapsed time by unsigned
    // subtraction, so a wait that starts 2 s before millis() wraps still lasts
    // the full 5 s before CommandTimeout.
    Rig r;
    const uint64_t startUs = (0x100000000ull - 2000) * 1000;
    fake_arduino::g_nowUs = startUs;
    ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load(5));
    TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    const uint64_t waitedMs = (fake_arduino::g_nowUs - startUs) / 1000;
    TEST_ASSERT_TRUE(waitedMs > 5000);
    TEST_ASSERT_TRUE(waitedMs <= 5002);
    TEST_ASSERT_TRUE(millis() < 10000);  // wrapped
}

// --- Before the load: Ready for Patch and MODE (steps 1 and 2) ----------------------

void test_ready_for_patch_is_polled_every_10_ms()
{
    // Polling mode, TRM Figure 5-1: INT_EVENT1 every 10 ms until bit 81 is
    // set, which here happens at 30 ms; then MODE.
    Rig r;
    r.dev.setReadyForPatch(false);
    r.dev.beforeRead = [](FakeTps25751Target &d) {
        if (d.now() >= 30000) d.setReadyForPatch(true);
    };
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    const std::vector<size_t> ev = r.reads(REG_INT_EVENT1);
    TEST_ASSERT_EQUAL_size_t(4, ev.size());
    for (size_t i = 0; i < ev.size(); ++i) TEST_ASSERT_EQUAL_UINT64(10000 * i, r.dev.log[ev[i]].timeUs);
    TEST_ASSERT_EQUAL_size_t(ev.back() + 2, r.reads(REG_MODE)[0]);
}

void test_ready_for_patch_poll_through_failed_reads()
{
    Rig r;
    r.dev.faults.push_back(shortReads(REG_INT_EVENT1, 0, 25));
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    TEST_ASSERT_EQUAL_size_t(4, r.reads(REG_INT_EVENT1).size());  // 0, 10, 20 fail; 30 succeeds
}

void test_ready_for_patch_never_set()
{
    // Bit 81 never set: CommandTimeout at PBM_READY_WAIT_CAP_MS, with nothing
    // but INT_EVENT1 reads on the bus. A controller in APP ends here too.
    Rig r;
    r.dev.setReadyForPatch(false);
    r.dev.setMode("APP ");
    ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load());
    TEST_ASSERT_EQUAL_UINT32(5000, PBM_READY_WAIT_CAP_MS);
    TEST_ASSERT_EQUAL_size_t(PBM_READY_WAIT_CAP_MS / 10 + 1, r.reads(REG_INT_EVENT1).size());
    TEST_ASSERT_EQUAL_size_t(2 * (PBM_READY_WAIT_CAP_MS / 10 + 1), r.dev.log.size());
    TEST_ASSERT_EQUAL_UINT32(PBM_READY_WAIT_CAP_MS, millis());
}

void test_irq_mode_does_not_read_int_event1()
{
    // In IRQ mode step 1 is the pin; INT_EVENT1 is not read, even with bit 81 clear.
    Rig r;
    r.dev.setReadyForPatch(false);
    fake_arduino::g_pinLevel[5] = LOW;
    ASSERT_LOAD(PatchLoadStatus::Success, r.load(5));
    TEST_ASSERT_EQUAL_size_t(0, r.reads(REG_INT_EVENT1).size());
}

void test_mode_is_polled_until_ptch()
{
    // TRM Figure 5-1: MODE every 10 ms until PTCH, through failed reads, in
    // both modes. MODE reads BOOT, then fails between 10 and 25 ms, then reads
    // PTCH from 30 ms.
    for (int irq = 0; irq < 2; ++irq) {
        Rig r;
        if (irq) fake_arduino::g_pinLevel[5] = LOW;
        r.dev.setMode("BOOT");
        r.dev.beforeRead = [](FakeTps25751Target &d) {
            if (d.now() >= 30000 && d.mode() == "BOOT") d.setMode("PTCH");
        };
        r.dev.faults.push_back(shortReads(REG_MODE, 10, 25));
        ASSERT_LOAD(PatchLoadStatus::Success, r.load(irq ? 5 : -1));
        const std::vector<size_t> modeReads = r.reads(REG_MODE, 0);
        const long pbms = r.commandWrite("PBMs");
        std::vector<size_t> before;
        for (size_t i : modeReads)
            if (static_cast<long>(i) < pbms) before.push_back(i);
        TEST_ASSERT_EQUAL_size_t(4, before.size());
        for (size_t i = 1; i < before.size(); ++i)
            TEST_ASSERT_EQUAL_UINT64(10000, r.dev.log[before[i]].timeUs - r.dev.log[before[i - 1]].timeUs);
    }
}

void test_not_in_patch_mode()
{
    {
        // APP ends the wait at the first read.
        Rig r;
        r.dev.setMode("APP ");
        ASSERT_LOAD(PatchLoadStatus::NotInPatchMode, r.load());
        TEST_ASSERT_EQUAL_size_t(1, r.reads(REG_MODE).size());
        TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMs"));
    }
    {
        // MODE stays BOOT: NotInPatchMode at PBM_PTCH_POLL_CAP_MS, no DATA write.
        Rig r;
        r.dev.setMode("BOOT");
        ASSERT_LOAD(PatchLoadStatus::NotInPatchMode, r.load());
        TEST_ASSERT_EQUAL_UINT32(5000, PBM_PTCH_POLL_CAP_MS);
        TEST_ASSERT_EQUAL_size_t(PBM_PTCH_POLL_CAP_MS / 10 + 1, r.reads(REG_MODE).size());
        TEST_ASSERT_EQUAL_size_t(0, r.dev.count([&](const Txn &t) { return r.dev.isRegWrite(t, REG_DATA) && t.bytes.size() > 1; }));
    }
    {
        // No MODE read succeeds: NotInPatchMode at the cap.
        Rig r;
        r.dev.faults.push_back(shortReads(REG_MODE, 0, 1000000));
        ASSERT_LOAD(PatchLoadStatus::NotInPatchMode, r.load());
        TEST_ASSERT_EQUAL_size_t(PBM_PTCH_POLL_CAP_MS / 10 + 1, r.reads(REG_MODE).size());
    }
}

// --- Every other status --------------------------------------------------------------

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
}

// --- The PBMs configuration in DATA (steps 4 and 5) ----------------------------------

namespace {

// A fault on writes to DATA of at least 2 bytes, which skips the 1-byte
// pointer write of a DATA read.
Fault dataWrites(uint8_t writeError, int remaining)
{
    Fault f;
    f.kind = 0;
    f.reg = REG_DATA;
    f.minBytes = 2;
    f.writeError = writeError;
    f.remaining = remaining;
    return f;
}

size_t configWrites(const Rig &r)
{
    return r.dev.count([&](const Txn &t) { return r.dev.isRegWrite(t, REG_DATA) && t.bytes.size() == 8; });
}

}  // namespace

void test_pbms_config_rewritten_after_a_dropped_write()
{
    // SLVAFV8A step 5 (page 8): the controller acknowledged the write, DATA
    // read back zeros, and the host wrote it again.
    Rig r;
    r.dev.faults.push_back(dataWrites(0, 1));
    ASSERT_LOAD(PatchLoadStatus::Success, r.load());
    TEST_ASSERT_EQUAL_size_t(2, configWrites(r));
    const long pbms = r.commandWrite("PBMs");
    TEST_ASSERT_EQUAL_size_t(2, r.reads(REG_DATA).size() - r.reads(REG_DATA, pbms).size());
}

void test_pbms_config_never_reads_back()
{
    // Every write dropped: three attempts, three read-backs, PbmsConfigMismatch,
    // and no COMMAND write at all.
    Rig r;
    r.dev.faults.push_back(dataWrites(0, -1));
    ASSERT_LOAD(PatchLoadStatus::PbmsConfigMismatch, r.load());
    TEST_ASSERT_EQUAL_UINT8(3, PBMS_DATA_ATTEMPTS);
    TEST_ASSERT_EQUAL_size_t(3, configWrites(r));
    TEST_ASSERT_EQUAL_size_t(3, r.reads(REG_DATA).size());
    TEST_ASSERT_EQUAL_size_t(0, r.dev.count([&](const Txn &t) { return r.dev.isRegWrite(t, REG_COMMAND); }));
}

void test_pbms_config_write_or_read_back_fails()
{
    {
        // The first write is not acknowledged: no read-back follows it, and
        // the second attempt succeeds.
        Rig r;
        r.dev.faults.push_back(dataWrites(2, 1));
        ASSERT_LOAD(PatchLoadStatus::Success, r.load());
        TEST_ASSERT_EQUAL_size_t(2, configWrites(r));
        const long pbms = r.commandWrite("PBMs");
        TEST_ASSERT_EQUAL_size_t(1, r.reads(REG_DATA).size() - r.reads(REG_DATA, pbms).size());
    }
    {
        // Every write fails: I2cError after three attempts, no read-back, no PBMs.
        Rig r;
        r.dev.faults.push_back(dataWrites(2, -1));
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
        TEST_ASSERT_EQUAL_size_t(3, configWrites(r));
        TEST_ASSERT_EQUAL_size_t(0, r.reads(REG_DATA).size());
        TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMs"));
    }
    {
        // The first read-back is short: the configuration is written again.
        Rig r;
        Fault f = shortReads(REG_DATA, 0, 1000000, 10);
        f.remaining = 1;
        r.dev.faults.push_back(f);
        ASSERT_LOAD(PatchLoadStatus::Success, r.load());
        TEST_ASSERT_EQUAL_size_t(2, configWrites(r));
    }
    {
        // No read-back succeeds: I2cError, not PbmsConfigMismatch, because
        // nothing showed what DATA holds.
        Rig r;
        r.dev.faults.push_back(shortReads(REG_DATA, 0, 1000000, 10));
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
        TEST_ASSERT_EQUAL_size_t(3, configWrites(r));
        TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMs"));
    }
    {
        // One mismatch, one failed read-back, one failed write: some read-back
        // succeeded, so PbmsConfigMismatch.
        Rig r;
        r.dev.faults.push_back(dataWrites(0, 1));
        Fault rd = shortReads(REG_DATA, 0, 1000000, 10);
        rd.index = 9;  // the second read-back: INT_EVENT1 0-1, MODE 2-3, write 4, read 5-6, write 7, read 8-9
        r.dev.faults.push_back(rd);
        Fault wr = dataWrites(2, -1);
        wr.index = 10;  // the third write
        r.dev.faults.push_back(wr);
        ASSERT_LOAD(PatchLoadStatus::PbmsConfigMismatch, r.load());
        TEST_ASSERT_EQUAL_size_t(3, configWrites(r));
    }
}

void test_burst_failure()
{
    // TRM Figure 5-1 (page 88): a burst transaction that is not acknowledged
    // goes to PBMe. PBMe closes the burst port, then its DATA is read at once,
    // and nothing follows; PBMc is never written.
    Rig r;
    // The second chunk fails. onDone runs inside the COMMAND read that
    // completes PBMs, before that read is logged at log.size(); the DATA read
    // takes the next two entries, then chunk 1, then chunk 2.
    r.dev.tasks["PBMs"].onDone = [](FakeTps25751Target &d) {
        Fault f;
        f.addr = 0x30;
        f.writeError = 2;
        f.index = static_cast<long>(d.log.size()) + 4;
        d.faults.push_back(f);
    };
    ASSERT_LOAD(PatchLoadStatus::BurstFailed, r.load());
    TEST_ASSERT_EQUAL_size_t(2, r.dev.count([](const Txn &t) { return t.addr == 0x30; }));
    TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMc"));
    const long pbme = r.commandWrite("PBMe");
    TEST_ASSERT_TRUE(pbme > 0);
    TEST_ASSERT_EQUAL_HEX8(0x30, r.dev.log[pbme - 1].addr);
    TEST_ASSERT_FALSE(r.dev.burstOpen);
    TEST_ASSERT_EQUAL_STRING("PBMe", r.dev.lastCompleted.c_str());
    const long clear = r.commandClear(pbme);
    TEST_ASSERT_TRUE(r.dev.isRead(r.dev.log[clear + 2], REG_DATA));
    TEST_ASSERT_EQUAL_size_t(static_cast<size_t>(clear) + 3, r.dev.log.size());
}

void test_pbme_after_pbmc_rejected()
{
    // TRM Figure 5-1: "!CMD" on PBMc goes to PBMe.
    Rig r;
    r.dev.tasks["PBMc"].reject = true;
    ASSERT_LOAD(PatchLoadStatus::PbmcRejected, r.load());
    const long pbmc = r.commandWrite("PBMc");
    const long pbme = r.commandWrite("PBMe");
    TEST_ASSERT_TRUE(pbme > pbmc);
    TEST_ASSERT_FALSE(r.dev.burstOpen);
    // No DATA or MODE read between PBMc and PBMe: PBMe follows the "!CMD" poll.
    TEST_ASSERT_TRUE(r.dev.isRead(r.dev.log[pbme - 1], REG_COMMAND));
}

void test_pbme_failure_keeps_the_load_status()
{
    {
        // PBMe rejected.
        Rig r;
        r.dev.tasks["PBMc"].reject = true;
        r.dev.tasks["PBMe"].reject = true;
        ASSERT_LOAD(PatchLoadStatus::PbmcRejected, r.load());
        TEST_ASSERT_TRUE(r.commandWrite("PBMe") > 0);
    }
    {
        // The PBMe COMMAND write is not acknowledged.
        Rig r;
        r.dev.tasks["PBMc"].reject = true;
        r.dev.tasks["PBMc"].onDone = [](FakeTps25751Target &d) {
            Fault f;
            f.kind = 0;
            f.reg = REG_COMMAND;
            f.minBytes = 2;
            d.faults.push_back(f);
        };
        ASSERT_LOAD(PatchLoadStatus::PbmcRejected, r.load());
        TEST_ASSERT_EQUAL_UINT8(2, r.dev.log.back().result);
    }
    {
        // PBMe never completes: the poll ends after PBME_CMD_TIMEOUT_MS.
        Rig r;
        r.dev.tasks["PBMc"].reject = true;
        r.dev.tasks["PBMe"].pollsBeforeDone = 100000;
        ASSERT_LOAD(PatchLoadStatus::PbmcRejected, r.load());
        const long pbme = r.commandWrite("PBMe");
        const uint64_t polledUs = r.dev.log.back().timeUs - r.dev.log[pbme].timeUs;
        TEST_ASSERT_EQUAL_UINT32(500, PBME_CMD_TIMEOUT_MS);
        TEST_ASSERT_TRUE(polledUs > PBME_CMD_TIMEOUT_MS * 1000ull);
        TEST_ASSERT_TRUE(polledUs <= PBME_CMD_TIMEOUT_MS * 1000ull + 5000);
    }
}

void test_no_pbme_on_other_failures()
{
    // Figure 5-1 has no PBMe after a PBMs failure, a PBMc DATA result other
    // than 0, or a PBMc that did not complete.
    const auto none = [](Rig &r) { TEST_ASSERT_EQUAL_INT(-1, r.commandWrite("PBMe")); };
    {
        Rig r;
        r.dev.tasks["PBMs"].reject = true;
        ASSERT_LOAD(PatchLoadStatus::PbmsRejected, r.load());
        none(r);
    }
    {
        Rig r;
        r.dev.tasks["PBMs"].output = {0x04};
        ASSERT_LOAD(PatchLoadStatus::BadBundleSize, r.load());
        none(r);
    }
    {
        Rig r;
        r.dev.tasks["PBMc"].pollsBeforeDone = 100000;
        ASSERT_LOAD(PatchLoadStatus::CommandTimeout, r.load());
        none(r);
    }
    {
        Rig r;
        r.dev.tasks["PBMc"].output = {0x01};
        ASSERT_LOAD(PatchLoadStatus::PatchApplyFailed, r.load());
        none(r);
    }
    {
        // A failed COMMAND poll after PBMc: I2cError, and no PBMe.
        Rig r;
        r.dev.tasks["PBMc"].pollsBeforeDone = 3;
        r.dev.tasks["PBMs"].onDone = [](FakeTps25751Target &d) {
            Fault f = shortReads(REG_COMMAND, d.lastCompletedUs / 1000 + 1, d.lastCompletedUs / 1000 + 1000000);
            d.faults.push_back(f);
        };
        ASSERT_LOAD(PatchLoadStatus::I2cError, r.load());
        TEST_ASSERT_TRUE(r.commandWrite("PBMc") > 0);
        none(r);
    }
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
        PatchLoadStatus::PbmsConfigMismatch,
    };
    const char *names[] = {"Success",          "NotInPatchMode", "PbmsRejected", "BadBundleSize",
                           "BadTargetAddress", "BadTimeout",     "BurstFailed",  "PbmcRejected",
                           "PatchApplyFailed", "NotInAppMode",   "CommandTimeout", "I2cError",
                           "PbmsConfigMismatch"};
    for (size_t i = 0; i < 13; ++i) TEST_ASSERT_EQUAL_STRING(names[i], TPS25751PatchLoader::toString(all[i]));
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
    RUN_TEST(test_pbms_that_takes_longer_than_200_ms);
    RUN_TEST(test_irq_ready_wait_across_a_millis_wrap);
    RUN_TEST(test_ready_for_patch_is_polled_every_10_ms);
    RUN_TEST(test_ready_for_patch_poll_through_failed_reads);
    RUN_TEST(test_ready_for_patch_never_set);
    RUN_TEST(test_irq_mode_does_not_read_int_event1);
    RUN_TEST(test_mode_is_polled_until_ptch);
    RUN_TEST(test_not_in_patch_mode);
    RUN_TEST(test_pbms_outcomes);
    RUN_TEST(test_pbms_config_rewritten_after_a_dropped_write);
    RUN_TEST(test_pbms_config_never_reads_back);
    RUN_TEST(test_pbms_config_write_or_read_back_fails);
    RUN_TEST(test_burst_failure);
    RUN_TEST(test_pbme_after_pbmc_rejected);
    RUN_TEST(test_pbme_failure_keeps_the_load_status);
    RUN_TEST(test_no_pbme_on_other_failures);
    RUN_TEST(test_pbmc_outcomes);
    RUN_TEST(test_irq_mode);
    RUN_TEST(test_to_string_and_debug_print);
    return UNITY_END();
}
