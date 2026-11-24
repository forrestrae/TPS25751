/**
 * @file test_TPS25751Core.cpp
 * @brief Native tests of the I2C paths of TPS25751 (src/TPS25751.cpp): the
 *        register read and write with the length byte, the 4CC task calls, and
 *        the raw PBM burst. Driven through the TwoWire fake (Wire.h) and the
 *        scripted target (FakeTps25751Target.h).
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751Core"
 *
 * The typed read and write wrappers and the factory read functions of
 * TPS25751.cpp are tested in test_TPS25751Typed.
 */

#include <unity.h>
#include <cstring>
#include <string>
#include <vector>

#include "TPS25751.h"
#include "FakeTps25751Target.h"

namespace {

using Txn = FakeTps25751Target::Txn;
using Kind = FakeTps25751Target::Kind;
using Fault = FakeTps25751Target::Fault;

struct Rig
{
    FakeTps25751Target dev;
    TPS25751 pd;

    Rig() : pd(Wire, 0x20)
    {
        fake_arduino::reset();
        Wire = TwoWire();
        Wire.target = &dev;
    }
    ~Rig() { Wire.target = nullptr; }
};

std::vector<uint8_t> bytes(std::initializer_list<int> v)
{
    std::vector<uint8_t> out;
    for (int b : v) out.push_back(static_cast<uint8_t>(b));
    return out;
}

void assertWrite(const Txn &t, uint8_t addr, const std::vector<uint8_t> &payload, bool stop, const char *msg)
{
    TEST_ASSERT_TRUE_MESSAGE(t.kind == Kind::Write, msg);
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(addr, t.addr, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(payload.size(), t.bytes.size(), msg);
    TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(payload.data(), t.bytes.data(), payload.size(), msg);
    TEST_ASSERT_EQUAL_MESSAGE(stop, t.stop, msg);
}

void assertRead(const Txn &t, uint8_t reg, size_t quantity, const char *msg)
{
    TEST_ASSERT_TRUE_MESSAGE(t.kind == Kind::Read, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(reg, t.reg, msg);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(quantity, t.requested, msg);
}

// A register read is two transactions: the pointer write without STOP, then
// the read of size + 1 bytes.
void assertRegisterRead(const std::vector<Txn> &log, size_t i, uint8_t reg, size_t size, const char *msg)
{
    assertWrite(log[i], 0x20, {reg}, false, msg);
    assertRead(log[i + 1], reg, size + 1, msg);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Register read and write ---------------------------------------------------------

void test_read_register_skips_the_length_byte()
{
    Rig r;
    r.dev.setMode("APP ");
    uint8_t buf[4] = {};
    TEST_ASSERT_TRUE(r.pd.readRegister(TPS25751Registers::Address::MODE, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_MEMORY("APP ", buf, 4);
    TEST_ASSERT_EQUAL_size_t(2, r.dev.log.size());
    assertRegisterRead(r.dev.log, 0, 0x03, 4, "MODE read");
}

void test_read_register_failures()
{
    uint8_t buf[4] = {};
    {
        // The register reports another length than the caller expects.
        Rig r;
        r.dev.setReg(0x03, bytes({'A', 'P', 'P', ' ', 0}));
        TEST_ASSERT_FALSE(r.pd.readRegister(TPS25751Registers::Address::MODE, buf, sizeof(buf)));
    }
    {
        // A short read: fewer bytes than length + 1.
        Rig r;
        Fault f;
        f.kind = 1;
        f.readBytes = 3;
        r.dev.faults.push_back(f);
        TEST_ASSERT_FALSE(r.pd.readRegister(TPS25751Registers::Address::MODE, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_size_t(2, r.dev.log.size());
    }
    {
        // The pointer write is not acknowledged: no read follows.
        Rig r;
        Fault f;
        f.kind = 0;
        f.writeError = 2;
        r.dev.faults.push_back(f);
        TEST_ASSERT_FALSE(r.pd.readRegister(TPS25751Registers::Address::MODE, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL_size_t(1, r.dev.log.size());
    }
    {
        // A length that is not the register's size: refused without a transaction.
        Rig r;
        uint8_t big[5] = {};
        TEST_ASSERT_FALSE(r.pd.readRegister(TPS25751Registers::Address::MODE, big, sizeof(big)));
        TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    }
}

void test_write_register_sends_the_length_byte()
{
    Rig r;
    const uint8_t clear[11] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    TEST_ASSERT_TRUE(r.pd.writeRegister(TPS25751Registers::Address::INT_CLEAR1, clear, sizeof(clear)));
    TEST_ASSERT_EQUAL_size_t(1, r.dev.log.size());
    std::vector<uint8_t> expected = {0x18, 11};
    expected.insert(expected.end(), clear, clear + 11);
    assertWrite(r.dev.log[0], 0x20, expected, true, "INT_CLEAR1 write");
}

void test_write_register_failures()
{
    const uint8_t data[4] = {1, 2, 3, 4};
    {
        // Failed with code 4 (lost arbitration, a pin held low, or a timeout in
        // the Teensy driver).
        Rig r;
        Fault f;
        f.writeError = 4;
        r.dev.faults.push_back(f);
        TEST_ASSERT_FALSE(r.pd.writeRegister(TPS25751Registers::Address::COMMAND, data, sizeof(data)));
    }
    {
        // The Wire buffer takes fewer bytes than given: refused before the
        // transaction is sent.
        Rig r;
        Wire.txCapacity = 4;
        TEST_ASSERT_FALSE(r.pd.writeRegister(TPS25751Registers::Address::COMMAND, data, sizeof(data)));
        TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    }
    {
        // A length that is not the register's size.
        Rig r;
        TEST_ASSERT_FALSE(r.pd.writeRegister(TPS25751Registers::Address::COMMAND, data, 3));
        TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    }
}

// --- 4CC tasks -----------------------------------------------------------------------

void test_execute_command_reads_data_right_after_the_clear()
{
    // The full handshake of a task with input and output: DATA write, COMMAND
    // write, COMMAND polled every 5 ms until it clears, then DATA read at once.
    Rig r;
    r.dev.tasks["TEST"] = FakeTps25751Target::TaskScript{2, false, {0x00, 0xAA, 0xBB}, nullptr};
    const uint8_t in[3] = {1, 2, 3};
    uint8_t out[8] = {};

    const TPS25751TaskResult res = r.pd.executeCommand(TPS25751FourCC::of("TEST"), in, sizeof(in), out, sizeof(out), 200);

    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success, res.status);
    TEST_ASSERT_EQUAL_HEX8(0x00, res.returnCode);
    TEST_ASSERT_EQUAL_HEX8(0xAA, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xBB, out[2]);

    const std::vector<Txn> &log = r.dev.log;
    TEST_ASSERT_EQUAL_size_t(2 + 3 * 2 + 2, log.size());
    assertWrite(log[0], 0x20, bytes({0x09, 3, 1, 2, 3}), true, "DATA write");
    assertWrite(log[1], 0x20, bytes({0x08, 4, 'T', 'E', 'S', 'T'}), true, "COMMAND write");
    for (size_t p = 0; p < 3; ++p) assertRegisterRead(log, 2 + 2 * p, 0x08, 4, "COMMAND poll");
    TEST_ASSERT_EQUAL_UINT64(5000, log[4].timeUs - log[2].timeUs);
    assertRegisterRead(log, 8, 0x09, 64, "DATA read");
    // No time passes between the poll that saw COMMAND clear and the DATA read.
    TEST_ASSERT_EQUAL_UINT64(log[7].timeUs, log[9].timeUs);
}

void test_execute_command_without_input_or_output()
{
    Rig r;
    r.dev.tasks["NOIN"] = FakeTps25751Target::TaskScript{0, false, {0x05}, nullptr};
    const TPS25751TaskResult res = r.pd.executeCommand(TPS25751FourCC::of("NOIN"));
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success, res.status);
    TEST_ASSERT_EQUAL_HEX8(0x05, res.returnCode);
    TEST_ASSERT_TRUE(r.dev.isRegWrite(r.dev.log[0], 0x08));
    TEST_ASSERT_EQUAL_size_t(1 + 2 + 2, r.dev.log.size());
}

void test_execute_command_rejected_and_timeout()
{
    {
        // An unknown code completes as "!CMD": Rejected, and DATA is not read.
        Rig r;
        const TPS25751TaskResult res = r.pd.executeCommand(TPS25751FourCC::of("XXXX"));
        TEST_ASSERT_EQUAL(TPS25751TaskStatus::Rejected, res.status);
        TEST_ASSERT_EQUAL_size_t(0, r.dev.count([&](const Txn &t) { return r.dev.isRead(t, 0x09); }));
    }
    {
        // COMMAND never clears: Timeout once more than 200 ms have passed.
        Rig r;
        r.dev.tasks["SLOW"] = FakeTps25751Target::TaskScript{1000, false, {}, nullptr};
        const TPS25751TaskResult res = r.pd.executeCommand(TPS25751FourCC::of("SLOW"), nullptr, 0, nullptr, 0, 200);
        TEST_ASSERT_EQUAL(TPS25751TaskStatus::Timeout, res.status);
        TEST_ASSERT_TRUE(millis() > 200);
        TEST_ASSERT_TRUE(millis() <= 210);
        TEST_ASSERT_EQUAL_size_t(0, r.dev.count([&](const Txn &t) { return r.dev.isRead(t, 0x09); }));
    }
}

void test_execute_command_i2c_failure_at_each_transaction()
{
    // The handshake above has 10 transactions. A failure at any of them ends
    // the call with I2CError and no further transaction.
    for (long i = 0; i < 10; ++i) {
        Rig r;
        r.dev.tasks["TEST"] = FakeTps25751Target::TaskScript{2, false, {0x00}, nullptr};
        Fault f;
        f.index = i;
        f.writeError = 2;
        f.readBytes = 2;  // a short read for the read transactions
        r.dev.faults.push_back(f);
        const uint8_t in[3] = {1, 2, 3};
        const TPS25751TaskResult res = r.pd.executeCommand(TPS25751FourCC::of("TEST"), in, sizeof(in), nullptr, 0, 200);
        char msg[32];
        snprintf(msg, sizeof(msg), "transaction %ld", i);
        TEST_ASSERT_EQUAL_MESSAGE(TPS25751TaskStatus::I2CError, res.status, msg);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(static_cast<size_t>(i + 1), r.dev.log.size(), msg);
    }
}

void test_issue_command_does_not_read_data()
{
    // The same DATA write, COMMAND write, and polls as executeCommand(), and
    // no DATA read: the caller reads DATA when the task requires.
    Rig r;
    r.dev.tasks["TEST"] = FakeTps25751Target::TaskScript{2, false, {0x00}, nullptr};
    const uint8_t in[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success,
                      r.pd.issueCommand(TPS25751FourCC::of("TEST"), in, sizeof(in), 200));
    TEST_ASSERT_EQUAL_size_t(2 + 3 * 2, r.dev.log.size());
    TEST_ASSERT_EQUAL_size_t(0, r.dev.count([&](const Txn &t) { return r.dev.isRead(t, 0x09); }));
    assertWrite(r.dev.log[0], 0x20, bytes({0x09, 3, 1, 2, 3}), true, "DATA write");

    // Without input there is no DATA write; the other outcomes match executeCommand().
    Rig q;
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::Rejected, q.pd.issueCommand(TPS25751FourCC::of("XXXX")));
    TEST_ASSERT_TRUE(q.dev.isRegWrite(q.dev.log[0], 0x08));
    Rig t;
    Fault f;
    f.index = 0;
    t.dev.faults.push_back(f);
    TEST_ASSERT_EQUAL(TPS25751TaskStatus::I2CError, t.pd.issueCommand(TPS25751FourCC::of("TEST"), in, sizeof(in)));
}

void test_write_command_data_at_its_own_length()
{
    {
        // One write of [0x09, length, data...], and DATA reads it back.
        Rig r;
        const uint8_t cfg[6] = {0x80, 0x2C, 0x00, 0x00, 0x30, 0x32};
        TEST_ASSERT_TRUE(r.pd.writeCommandData(cfg, sizeof(cfg)));
        TEST_ASSERT_EQUAL_size_t(1, r.dev.log.size());
        assertWrite(r.dev.log[0], 0x20, bytes({0x09, 6, 0x80, 0x2C, 0x00, 0x00, 0x30, 0x32}), true, "DATA write");
        TEST_ASSERT_EQUAL_HEX8_ARRAY(cfg, r.dev.regs[0x09].data(), sizeof(cfg));
    }
    {
        // The full 64 bytes are accepted.
        Rig r;
        std::vector<uint8_t> full(64, 0xA5);
        TEST_ASSERT_TRUE(r.pd.writeCommandData(full.data(), full.size()));
        TEST_ASSERT_EQUAL_size_t(66, r.dev.log[0].bytes.size());
    }
    {
        // Null data, length 0, and length 65 are refused without a transaction.
        Rig r;
        std::vector<uint8_t> big(65, 0);
        TEST_ASSERT_FALSE(r.pd.writeCommandData(nullptr, 6));
        TEST_ASSERT_FALSE(r.pd.writeCommandData(big.data(), 0));
        TEST_ASSERT_FALSE(r.pd.writeCommandData(big.data(), big.size()));
        TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    }
    {
        // A write that is not acknowledged.
        Rig r;
        Fault f;
        f.writeError = 2;
        r.dev.faults.push_back(f);
        const uint8_t one[1] = {0x01};
        TEST_ASSERT_FALSE(r.pd.writeCommandData(one, sizeof(one)));
    }
}

void test_command_poll_across_a_millis_wrap()
{
    // millis() is 32-bit and wraps, as on the Teensy 4. The COMMAND poll
    // measures its timeout by unsigned subtraction, so it ends after the same
    // elapsed time when the wrap falls inside it.
    const uint64_t startUs = (0x100000000ull - 100) * 1000;  // 100 ms before the wrap
    {
        Rig r;
        fake_arduino::g_nowUs = startUs;
        r.dev.tasks["SLOW"] = FakeTps25751Target::TaskScript{1000, false, {}, nullptr};
        TEST_ASSERT_EQUAL(TPS25751TaskStatus::Timeout,
                          r.pd.issueCommand(TPS25751FourCC::of("SLOW"), nullptr, 0, 200));
        TEST_ASSERT_EQUAL_UINT64(startUs + 205000, fake_arduino::g_nowUs);
        TEST_ASSERT_TRUE(millis() < 1000);  // the 32-bit clock has wrapped
    }
    {
        Rig r;
        fake_arduino::g_nowUs = startUs;
        r.dev.tasks["SLOW"] = FakeTps25751Target::TaskScript{30, false, {0x00}, nullptr};
        TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success,
                          r.pd.issueCommand(TPS25751FourCC::of("SLOW"), nullptr, 0, 200));
    }
}

void test_issue_command_honours_its_timeout()
{
    // With a 100 ms timeout, COMMAND is polled at 0, 5, ..., 105 ms: a task that
    // clears on the poll at 105 ms succeeds and one that would clear at 110 ms
    // times out. 100 is not the default, so a call that ignored its argument
    // would let both succeed.
    {
        Rig r;
        r.dev.tasks["TEST"] = FakeTps25751Target::TaskScript{21, false, {0x00}, nullptr};
        TEST_ASSERT_EQUAL(TPS25751TaskStatus::Success, r.pd.issueCommand(TPS25751FourCC::of("TEST"), nullptr, 0, 100));
        TEST_ASSERT_EQUAL_UINT32(105, millis());
    }
    {
        Rig r;
        r.dev.tasks["TEST"] = FakeTps25751Target::TaskScript{22, false, {0x00}, nullptr};
        TEST_ASSERT_EQUAL(TPS25751TaskStatus::Timeout, r.pd.issueCommand(TPS25751FourCC::of("TEST"), nullptr, 0, 100));
        TEST_ASSERT_EQUAL_UINT32(105, millis());
    }
}

void test_wire_fake_matches_the_teensy_driver()
{
    // The limits of WireIMXRT.cpp the fake reproduces.
    Rig r;
    TwoWire w;
    w.target = &r.dev;
    // No transmission begun: write() takes nothing and endTransmission() returns 4.
    TEST_ASSERT_EQUAL_size_t(0, w.write(static_cast<uint8_t>(0x03)));
    TEST_ASSERT_EQUAL_UINT8(4, w.endTransmission());
    TEST_ASSERT_EQUAL_size_t(0, r.dev.log.size());
    // 136 data bytes fit after the address; the 137th does not.
    w.beginTransmission(0x20);
    std::vector<uint8_t> big(140, 0);
    TEST_ASSERT_EQUAL_size_t(136, w.write(big.data(), big.size()));
    // A read is at most 136 bytes, and asking for 0 asks for 1.
    TEST_ASSERT_EQUAL_UINT8(136, w.requestFrom(0x20, 200));
    TEST_ASSERT_EQUAL_UINT8(1, w.requestFrom(0x20, 0));
}

// --- Raw burst -----------------------------------------------------------------------

void test_burst_write_chunks_without_register_or_length_byte()
{
    Rig r;
    r.dev.burstOpen = true;
    std::vector<uint8_t> image(100);
    for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i * 3 + 1);

    TEST_ASSERT_TRUE(r.pd.burstWrite(0x30, image.data(), image.size(), 32, 500));

    TEST_ASSERT_EQUAL_size_t(4, r.dev.log.size());
    const size_t sizes[] = {32, 32, 32, 4};
    for (size_t i = 0; i < 4; ++i) {
        assertWrite(r.dev.log[i], 0x30, std::vector<uint8_t>(image.begin() + 32 * i, image.begin() + 32 * i + sizes[i]),
                    true, "chunk");
        if (i > 0) TEST_ASSERT_EQUAL_UINT64(500, r.dev.log[i].timeUs - r.dev.log[i - 1].timeUs);
    }
    // No delay after the last chunk.
    TEST_ASSERT_EQUAL_UINT64(1500, fake_arduino::g_nowUs);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(image.data(), r.dev.burst.data(), image.size());
}

void test_burst_write_failures()
{
    std::vector<uint8_t> image(100, 0x5A);
    {
        // A chunk that is not acknowledged ends the burst there.
        Rig r;
        r.dev.burstOpen = true;
        Fault f;
        f.index = 1;
        f.writeError = 2;
        r.dev.faults.push_back(f);
        TEST_ASSERT_FALSE(r.pd.burstWrite(0x30, image.data(), image.size()));
        TEST_ASSERT_EQUAL_size_t(2, r.dev.log.size());
    }
    {
        // The Wire buffer takes fewer bytes than the chunk: the transaction is
        // still ended, to release the bus, and the burst fails.
        Rig r;
        r.dev.burstOpen = true;
        Wire.txCapacity = 16;
        TEST_ASSERT_FALSE(r.pd.burstWrite(0x30, image.data(), image.size()));
        TEST_ASSERT_EQUAL_size_t(1, r.dev.log.size());
        TEST_ASSERT_EQUAL_size_t(16, r.dev.log[0].bytes.size());
    }
    {
        // The port is not open: the first chunk is not acknowledged.
        Rig r;
        TEST_ASSERT_FALSE(r.pd.burstWrite(0x30, image.data(), image.size()));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_read_register_skips_the_length_byte);
    RUN_TEST(test_read_register_failures);
    RUN_TEST(test_write_register_sends_the_length_byte);
    RUN_TEST(test_write_register_failures);
    RUN_TEST(test_execute_command_reads_data_right_after_the_clear);
    RUN_TEST(test_execute_command_without_input_or_output);
    RUN_TEST(test_execute_command_rejected_and_timeout);
    RUN_TEST(test_execute_command_i2c_failure_at_each_transaction);
    RUN_TEST(test_issue_command_does_not_read_data);
    RUN_TEST(test_write_command_data_at_its_own_length);
    RUN_TEST(test_command_poll_across_a_millis_wrap);
    RUN_TEST(test_issue_command_honours_its_timeout);
    RUN_TEST(test_wire_fake_matches_the_teensy_driver);
    RUN_TEST(test_burst_write_chunks_without_register_or_length_byte);
    RUN_TEST(test_burst_write_failures);
    return UNITY_END();
}
