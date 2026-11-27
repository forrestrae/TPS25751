/**
 * Native unit tests for TPS25751BenchConsole: each protocol command against a
 * simulated controller (FakeBenchTarget, behind the native TwoWire fake) and a
 * FakeStream for the serial port. The real TPS25751 class and the real patch
 * loader run in between, so the tests also cover the I2C framing the console
 * depends on. Time is the stub clock, which only delay() advances.
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "FakeStream.h"
#include "FakeBenchTarget.h"
#include "TPS25751.h"
#include "TPS25751BenchConsole.h"
#include "TPS25751Debug.h"
#include "TPS25751PatchLoader.h"
#include "fixtures/PdCapture_2026_11_23_apple_140w_tps25751.h"

namespace {

using Verdict = FakeBenchTarget::Verdict;
using Lines = std::vector<std::string>;
namespace bundle = PdCapture_2026_11_23_apple_140w_tps25751;

constexpr unsigned long kStartMillis = 5000;

// A stand-in patch bundle. The size is not a multiple of the 32-byte burst
// chunk, so the last chunk is short.
uint8_t patchImage[100];

// STATUS (0x1A, SLVUCR8B p.25): bit 0 Plug Present, bits 3:1 Connection State,
// bit 5 Port Role.
//   sink:   1 | (6 << 1)            = 0x0D
//   source: 1 | (6 << 1) | (1 << 5) = 0x2D
const uint8_t kStatusSink[5] = {0x0D, 0x00, 0x00, 0x00, 0x00};
const uint8_t kStatusSource[5] = {0x2D, 0x00, 0x00, 0x00, 0x00};
const uint8_t kStatusUnplugged[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

// BOOT_FLAGS (0x2D, SLVUCR8B p.33): Patch Config Source is bits 31:29, the top
// three bits of byte 3. 5 = EEPROM gives 0xA0, 6 = I2C gives 0xC0.
const uint8_t kBootFlagsEeprom[5] = {0x08, 0x00, 0x00, 0xA0, 0xC1};
const uint8_t kBootFlagsI2c[5] = {0x00, 0x00, 0x00, 0xC0, 0xC1};
const uint8_t kBootFlagsNone[5] = {0x00, 0x00, 0x00, 0x00, 0xC1};
const uint8_t kBootFlagsReserved[5] = {0x00, 0x00, 0x00, 0x20, 0xC1};

// INT_EVENT1 (0x14): ReadyForPatch is bit 81, bit 1 of byte 10.
constexpr size_t kReadyForPatchByte = 10;
constexpr uint8_t kReadyForPatchMask = 0x02;

struct Bench
{
    FakeBenchTarget fake;
    FakeStream io;
    TPS25751 pd;
    TPS25751BenchConsole console;

    Bench() : pd(Wire, 0x20), console(pd, io)
    {
        Wire = TwoWire();
        Wire.target = &fake;
        populate();
    }
    ~Bench() { Wire.target = nullptr; }

    // Every register the library knows, zeroed at its RegisterInfo size, then
    // the ones the console reads for HELLO.
    void populate()
    {
        for (unsigned address = 0; address <= 0xFF; ++address) {
            const uint8_t size = TPS25751Registers::getRegisterSize(
                static_cast<TPS25751Registers::Address>(address));
            if (size != 0) fake.registers[static_cast<uint8_t>(address)] = std::vector<uint8_t>(size, 0);
        }
        fake.setMode("APP ");
        fake.setRegister(0x0F, {0x66, 0x11, 0x09, 0xF8});  // VERSION 0xF8091166, little-endian
        fake.setRegister(0x1A, kStatusSink, sizeof(kStatusSink));
        fake.setRegister(0x2D, kBootFlagsEeprom, sizeof(kBootFlagsEeprom));
    }

    Lines run(const char *line)
    {
        console.handleLine(line);
        return io.takeLines();
    }
};

void setMillis(unsigned long ms)
{
    fake_arduino::g_nowUs = static_cast<uint64_t>(ms) * 1000;
}

bool hasLine(const Lines &lines, const char *expected)
{
    for (const std::string &line : lines)
        if (line == expected) return true;
    return false;
}

bool hasLineStarting(const Lines &lines, const char *prefix)
{
    for (const std::string &line : lines)
        if (line.compare(0, strlen(prefix), prefix) == 0) return true;
    return false;
}

size_t countContaining(const Lines &lines, const char *text)
{
    size_t n = 0;
    for (const std::string &line : lines)
        if (line.find(text) != std::string::npos) ++n;
    return n;
}

// True if the line is one complete reply line of request `id`: "OK", an "ERR"
// line, or nothing but key=value tokens. Debug text spliced into a reply line
// fails this, because it brings a '[' and words without '='.
bool isWholeReplyLine(const std::string &line, const char *id)
{
    const std::string prefix = std::string("@") + id + " ";
    if (line.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string text = line.substr(prefix.size());
    if (text.find('[') != std::string::npos) return false;
    if (text == "OK" || text.compare(0, 4, "ERR ") == 0) return true;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        const std::string token = text.substr(start, end - start);
        if (token.find('=') == std::string::npos || token[0] == '=') return false;
        start = end + 1;
    }
    return !text.empty();
}

std::string hexOf(const std::vector<uint8_t> &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (uint8_t b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}

// Plays the controller's side of a restart: COMMAND keeps the task code while
// the controller sits in Type-C Error Recovery, the bus goes quiet, and the
// controller comes back with COMMAND and DATA cleared and MODE as given.
void scriptRestart(FakeBenchTarget &fake, const char *modeAfter, bool clearData = true)
{
    const unsigned long now = millis();
    fake.schedule(now + 1000, [](FakeBenchTarget &f) { f.silent = true; });
    fake.schedule(now + 1300, [modeAfter, clearData](FakeBenchTarget &f) {
        f.silent = false;
        f.registers[FakeBenchTarget::kCommand] = std::vector<uint8_t>(4, 0);
        if (clearData) f.registers[FakeBenchTarget::kData] = std::vector<uint8_t>(64, 0);
        f.setMode(modeAfter);
    });
}

// A task handler that accepts a patch bundle the way the controller does:
// PBMs and PBMc report PatchStartStatus 0 in DATA byte 0, and PBMc ends in APP.
Verdict acceptPatch(FakeBenchTarget &fake, const std::string &code, std::vector<uint8_t> &data)
{
    if (code == "PBMs") {
        data[0] = 0;
    } else if (code == "PBMc") {
        data[0] = 0;
        fake.setMode("APP ");
    }
    return Verdict::Complete;
}

void setReadyForPatch(FakeBenchTarget &fake)
{
    fake.registers[0x14][kReadyForPatchByte] |= kReadyForPatchMask;
}

}  // namespace

void setUp()
{
    setMillis(kStartMillis);
    for (size_t i = 0; i < sizeof(patchImage); ++i) patchImage[i] = static_cast<uint8_t>(i * 7 + 3);
}
void tearDown()
{
    // A test may point the library's debug output at its own stream.
    TPS25751::setDebugLevel(DEBUG_LEVEL_NONE);
    TPS25751::setDebugStream(&Serial);
}

// ---------------------------------------------------------------------------
// HELLO
// ---------------------------------------------------------------------------

void test_hello_reports_the_console_and_the_controller()
{
    Bench b;
    const Lines lines = b.run("@1 HELLO");

    TEST_ASSERT_EQUAL(4, lines.size());
    // proto, lib, build, and uptime share the first line, which needs no I2C.
    const std::string head = "@1 proto=1 lib=unknown build=";
    const std::string tail = " uptime_ms=5000";
    TEST_ASSERT_TRUE(lines[0].compare(0, head.size(), head) == 0);
    TEST_ASSERT_TRUE(lines[0].size() > head.size() + tail.size());
    TEST_ASSERT_TRUE(lines[0].compare(lines[0].size() - tail.size(), tail.size(), tail) == 0);
    // The build token is the compile date and time with spaces as underscores,
    // so the line has exactly the four spaces that separate its tokens.
    size_t spaces = 0;
    for (char c : lines[0])
        if (c == ' ') ++spaces;
    TEST_ASSERT_EQUAL(4, spaces);
    TEST_ASSERT_EQUAL_STRING("@1 mode=APP version=f8091166 role=sink plug=1", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 patch_bundle=0 patch_pending=0 patch_source=eeprom swaps=0",
                             lines[2].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 OK", lines[3].c_str());
}

void test_hello_reports_the_source_role_and_an_empty_port()
{
    Bench b;
    b.fake.setRegister(0x1A, kStatusSource, sizeof(kStatusSource));
    TEST_ASSERT_TRUE(hasLine(b.run("@1 HELLO"), "@1 mode=APP version=f8091166 role=source plug=1"));

    b.fake.setRegister(0x1A, kStatusUnplugged, sizeof(kStatusUnplugged));
    TEST_ASSERT_TRUE(hasLine(b.run("@2 HELLO"), "@2 mode=APP version=f8091166 role=sink plug=0"));
}

void test_hello_reports_the_patch_source_and_the_bundle()
{
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setRegister(0x2D, kBootFlagsI2c, sizeof(kBootFlagsI2c));
    TEST_ASSERT_TRUE(hasLine(b.run("@1 HELLO"), "@1 patch_bundle=1 patch_pending=0 patch_source=i2c swaps=0"));

    b.console.setPatchBundle(nullptr, 0);
    b.fake.setRegister(0x2D, kBootFlagsNone, sizeof(kBootFlagsNone));
    TEST_ASSERT_TRUE(hasLine(b.run("@2 HELLO"), "@2 patch_bundle=0 patch_pending=0 patch_source=none swaps=0"));

    b.fake.setRegister(0x2D, kBootFlagsReserved, sizeof(kBootFlagsReserved));
    TEST_ASSERT_TRUE(hasLine(b.run("@3 HELLO"), "@3 patch_bundle=0 patch_pending=0 patch_source=other swaps=0"));
}

void test_hello_answers_when_the_controller_does_not()
{
    // The console is alive even when I2C is not, and says what it could not read.
    Bench b;
    b.fake.silent = true;
    const Lines lines = b.run("@1 HELLO");
    TEST_ASSERT_TRUE(hasLine(lines, "@1 mode=? version=? role=? plug=?"));
    TEST_ASSERT_TRUE(hasLine(lines, "@1 patch_bundle=0 patch_pending=0 patch_source=? swaps=0"));
    TEST_ASSERT_EQUAL_STRING("@1 OK", lines.back().c_str());
}

void test_hello_reports_a_mode_word_it_cannot_print()
{
    // MODE reads as four zero bytes, which is no mode at all.
    Bench b;
    b.fake.setRegister(0x03, {0x00, 0x00, 0x00, 0x00});
    const Lines lines = b.run("@1 HELLO");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@1 mode=? "));

    // A wrong length for MODE counts as unreadable.
    b.fake.setRegister(0x03, {'A', 'P', 'P'});
    TEST_ASSERT_TRUE(hasLineStarting(b.run("@2 HELLO"), "@2 mode=? "));
}

void test_hello_retries_mode_before_reporting_it_unreadable()
{
    // Right after a restart MODE can miss a read or two. HELLO tries three
    // times, 100 ms apart, so it does not report "?" for a controller that is
    // only still coming up.
    Bench b;
    int modeReads = 0;
    b.fake.beforeRead = [&modeReads](FakeBenchTarget &f, uint8_t reg, size_t) {
        if (reg != 0x03) return;
        if (++modeReads < 3) {
            f.failRead[0x03] = true;
        } else {
            f.failRead.erase(0x03);
        }
    };
    Lines lines = b.run("@1 HELLO");
    TEST_ASSERT_EQUAL_STRING("@1 mode=APP version=f8091166 role=sink plug=1", lines[1].c_str());
    TEST_ASSERT_EQUAL(3, modeReads);
    TEST_ASSERT_EQUAL(kStartMillis + 200, millis());
    // The uptime in the reply is the time the request arrived.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, lines[0].find(" uptime_ms=5000"));

    // A MODE that stays unreadable is reported as "?" after the three tries.
    b.fake.beforeRead = nullptr;
    b.fake.failRead[0x03] = true;
    modeReads = 0;
    setMillis(kStartMillis);
    lines = b.run("@2 HELLO");
    TEST_ASSERT_EQUAL_STRING("@2 mode=? version=f8091166 role=sink plug=1", lines[1].c_str());
    TEST_ASSERT_EQUAL(kStartMillis + 200, millis());
}

void test_hello_in_ptch_mode_reads_only()
{
    // HELLO never loads the patch, even with a bundle at hand and the
    // controller asking for one. It reports the mode and that a load is pending.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("PTCH");
    setReadyForPatch(b.fake);

    const Lines lines = b.run("@1 HELLO");
    TEST_ASSERT_EQUAL(4, lines.size());
    TEST_ASSERT_EQUAL_STRING("@1 mode=PTCH version=f8091166 role=sink plug=1", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 patch_bundle=1 patch_pending=1 patch_source=eeprom swaps=0",
                             lines[2].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 OK", lines[3].c_str());

    // No Patch Burst Mode traffic, no task, no write of any kind, and no waiting.
    TEST_ASSERT_EQUAL(0, b.fake.pbmBytes.size());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
    TEST_ASSERT_EQUAL(0, b.fake.writes.size());
    TEST_ASSERT_EQUAL(kStartMillis, millis());
    // Four reads: MODE, VERSION, STATUS, BOOT_FLAGS.
    TEST_ASSERT_EQUAL(4, b.fake.reads.size());
}

void test_hello_reports_ptch_without_a_bundle()
{
    Bench b;
    b.fake.setMode("PTCH");
    setReadyForPatch(b.fake);
    const Lines lines = b.run("@1 HELLO");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@1 mode=PTCH "));
    // Nothing is pending without a bundle to load.
    TEST_ASSERT_TRUE(hasLine(lines, "@1 patch_bundle=0 patch_pending=0 patch_source=eeprom swaps=0"));
    TEST_ASSERT_EQUAL_STRING("@1 OK", lines.back().c_str());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
}

// ---------------------------------------------------------------------------
// RD
// ---------------------------------------------------------------------------

void test_read_returns_the_length_byte_and_the_data()
{
    Bench b;
    b.fake.setRegister(0x34, bundle::kRegister0x34, sizeof(bundle::kRegister0x34));
    const Lines lines = b.run("@2 RD 0x34");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@2 reg=0x34 len=6 data=d64106009800", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@2 OK", lines[1].c_str());

    // One transaction of the library's usual shape: the length byte plus six.
    TEST_ASSERT_EQUAL(1, b.fake.reads.size());
    TEST_ASSERT_EQUAL_HEX8(0x34, b.fake.reads[0].first);
    TEST_ASSERT_EQUAL(7, b.fake.reads[0].second);
}

void test_read_reports_a_length_byte_longer_than_the_library_expects()
{
    // GLOBAL_SYSTEM_CONFIGURATION is 14 bytes in RegisterInfo. If a controller
    // reports 15, the console reads again and returns all 15 with len=15.
    Bench b;
    std::vector<uint8_t> data(15);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(0x10 + i);
    b.fake.registers[0x27] = data;

    const Lines lines = b.run("@2 RD 0x27");
    TEST_ASSERT_EQUAL_STRING(("@2 reg=0x27 len=15 data=" + hexOf(data)).c_str(), lines[0].c_str());
    TEST_ASSERT_EQUAL(2, b.fake.reads.size());
    TEST_ASSERT_EQUAL(15, b.fake.reads[0].second);  // RegisterInfo size 14, plus the length byte
    TEST_ASSERT_EQUAL(16, b.fake.reads[1].second);
}

void test_read_reports_a_length_byte_shorter_than_the_library_expects()
{
    // The length byte is reported as read, and only that many bytes follow.
    Bench b;
    b.fake.setRegister(0x1A, {0x0D, 0x00, 0x00, 0x00});  // 4 bytes, RegisterInfo says 5
    const Lines lines = b.run("@2 RD 0x1A");
    TEST_ASSERT_EQUAL_STRING("@2 reg=0x1a len=4 data=0d000000", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@2 OK", lines[1].c_str());
}

void test_read_of_a_register_the_library_does_not_know()
{
    // 0x73 (TX_MIDB_SOP) has no RegisterInfo: the console asks for one byte to
    // learn the length byte, then reads that many, and never more.
    Bench b;
    b.fake.setRegister(0x73, {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02});
    const Lines lines = b.run("@2 RD 73");
    TEST_ASSERT_EQUAL_STRING("@2 reg=0x73 len=6 data=deadbeef0102", lines[0].c_str());
    TEST_ASSERT_EQUAL(2, b.fake.reads.size());
    TEST_ASSERT_EQUAL(2, b.fake.reads[0].second);
    TEST_ASSERT_EQUAL(7, b.fake.reads[1].second);
}

void test_read_of_an_unimplemented_address_reports_length_zero()
{
    Bench b;
    const Lines lines = b.run("@2 RD 0xEE");
    TEST_ASSERT_EQUAL_STRING("@2 reg=0xee len=0 data=", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@2 OK", lines[1].c_str());
}

void test_read_fails_when_the_controller_does_not_answer()
{
    Bench b;
    b.fake.silent = true;
    Lines lines = b.run("@2 RD 0x34");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@2 ERR I2C register read failed", lines[0].c_str());

    b.fake.silent = false;
    b.fake.failRead[0x34] = true;
    lines = b.run("@3 RD 0x34");
    TEST_ASSERT_EQUAL_STRING("@3 ERR I2C register read failed", lines[0].c_str());
}

void test_read_rejects_an_impossible_length_byte()
{
    // 0xFF is what a floating bus reads; no register is longer than 64 bytes.
    Bench b;
    b.fake.lengthByte[0x34] = 0xFF;
    TEST_ASSERT_EQUAL_STRING("@2 ERR I2C register read failed", b.run("@2 RD 0x34")[0].c_str());

    b.fake.lengthByte[0x73] = 65;
    TEST_ASSERT_EQUAL_STRING("@3 ERR I2C register read failed", b.run("@3 RD 0x73")[0].c_str());
}

void test_read_fails_when_the_second_read_fails()
{
    // The one-byte probe of an unknown register succeeds and the full read
    // that follows does not.
    Bench b;
    b.fake.setRegister(0x73, {0x01, 0x02, 0x03});
    b.fake.beforeRead = [](FakeBenchTarget &f, uint8_t reg, size_t quantity) {
        if (reg == 0x73 && quantity != 2) f.failRead[0x73] = true;
    };
    TEST_ASSERT_EQUAL_STRING("@2 ERR I2C register read failed", b.run("@2 RD 0x73")[0].c_str());
}

void test_read_rejects_a_length_byte_that_turns_impossible_on_the_second_read()
{
    Bench b;
    b.fake.setRegister(0x73, {0x01, 0x02, 0x03});
    b.fake.beforeRead = [](FakeBenchTarget &f, uint8_t reg, size_t quantity) {
        if (reg == 0x73 && quantity != 2) f.lengthByte[0x73] = 0xFF;
    };
    TEST_ASSERT_EQUAL_STRING("@2 ERR I2C register read failed", b.run("@2 RD 0x73")[0].c_str());
}

// ---------------------------------------------------------------------------
// DEC
// ---------------------------------------------------------------------------

void test_decode_returns_the_raw_line_and_the_debug_print()
{
    Bench b;
    const Lines lines = b.run("@3 DEC 0x1A");
    TEST_ASSERT_EQUAL_STRING("@3 reg=0x1a len=5 data=0d00000000", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@3 | === Status Register (0x1A) ===", lines[1].c_str());
    TEST_ASSERT_TRUE(hasLine(lines, "@3 |   Plug Present: Yes"));
    TEST_ASSERT_EQUAL_STRING("@3 OK", lines.back().c_str());
    // Every line between the raw line and OK is decode text.
    for (size_t i = 1; i + 1 < lines.size(); ++i) {
        TEST_ASSERT_TRUE(lines[i].compare(0, 4, "@3 |") == 0);
    }
}

void test_decode_without_a_register_class()
{
    Bench b;
    b.fake.setRegister(0x73, {0x01, 0x02});
    const Lines lines = b.run("@3 DEC 0x73");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@3 reg=0x73 len=2 data=0102", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING(
        "@3 ERR NODECODER the library has no register class for this address", lines[1].c_str());
}

void test_decode_fails_when_the_controller_does_not_answer()
{
    Bench b;
    b.fake.silent = true;
    const Lines lines = b.run("@3 DEC 0x1A");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@3 ERR I2C register read failed", lines[0].c_str());
}

void test_decode_of_the_rdo_contract_takes_the_kind_from_the_active_pdo()
{
    // The Apple 140 W bundle: 0x35 holds Request 0x4184B12C in both words and
    // 0x34 the 20 V Fixed PDO, so the own RDO decodes as Fixed/Variable. The
    // board is Sink, so the requested RDO stays undecoded.
    Bench b;
    b.fake.setRegister(0x34, bundle::kRegister0x34, sizeof(bundle::kRegister0x34));
    b.fake.setRegister(0x35, bundle::kRegister0x35, sizeof(bundle::kRegister0x35));

    const Lines lines = b.run("@3 DEC 0x35");
    TEST_ASSERT_EQUAL_STRING("@3 reg=0x35 len=16 data=2cb1844100000000000000002cb18441",
                             lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@3 | === Active RDO Contract Register (0x35) ===", lines[1].c_str());
    TEST_ASSERT_EQUAL(1, countContaining(lines, "Operating Current: "));
    TEST_ASSERT_EQUAL_STRING("@3 OK", lines.back().c_str());
}

void test_decode_of_the_rdo_contract_as_source_uses_the_advertised_pdo()
{
    // As Source, the requested RDO takes its kind from the PDO this device
    // advertises in 0x32 at the RDO's Object Position (4). PDO 1 of 0x32 starts
    // at byte 3, so PDO 4 is bytes 15 to 18: 0x000641D6, a Fixed PDO.
    Bench b;
    b.fake.setRegister(0x1A, kStatusSource, sizeof(kStatusSource));
    b.fake.setRegister(0x34, bundle::kRegister0x34, sizeof(bundle::kRegister0x34));
    b.fake.setRegister(0x35, bundle::kRegister0x35, sizeof(bundle::kRegister0x35));
    std::vector<uint8_t> sourceCaps(63, 0);
    sourceCaps[0] = 4;
    sourceCaps[15] = 0xD6;
    sourceCaps[16] = 0x41;
    sourceCaps[17] = 0x06;
    sourceCaps[18] = 0x00;
    b.fake.registers[0x32] = sourceCaps;

    const Lines lines = b.run("@3 DEC 0x35");
    TEST_ASSERT_EQUAL(2, countContaining(lines, "Operating Current: "));
}

void test_decode_of_an_empty_rdo_contract()
{
    Bench b;
    const Lines lines = b.run("@3 DEC 0x35");
    TEST_ASSERT_TRUE(hasLine(lines, "@3 |   No active RDO contract"));
    // debugPrint() ends with a blank line, which stays a reply line.
    TEST_ASSERT_TRUE(hasLine(lines, "@3 |"));
    TEST_ASSERT_EQUAL_STRING("@3 OK", lines.back().c_str());
}

// ---------------------------------------------------------------------------
// SNAP
// ---------------------------------------------------------------------------

void test_snapshot_reads_every_register_in_order()
{
    Bench b;
    b.fake.setRegister(0x30, bundle::kRegister0x30, sizeof(bundle::kRegister0x30));
    b.fake.setRegister(0x34, bundle::kRegister0x34, sizeof(bundle::kRegister0x34));
    b.fake.setRegister(0x35, bundle::kRegister0x35, sizeof(bundle::kRegister0x35));

    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_EQUAL(1 + 17 + 1 + 1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@4 uptime_ms=5000", lines[0].c_str());

    const char *expectedPrefix[17] = {
        "@4 reg=0x03 len=4 ",  "@4 reg=0x14 len=11 ", "@4 reg=0x1a len=5 ",  "@4 reg=0x26 len=5 ",
        "@4 reg=0x30 len=53 ", "@4 reg=0x31 len=53 ", "@4 reg=0x32 len=63 ", "@4 reg=0x33 len=53 ",
        "@4 reg=0x34 len=6 ",  "@4 reg=0x35 len=16 ", "@4 reg=0x37 len=24 ", "@4 reg=0x3f len=2 ",
        "@4 reg=0x40 len=4 ",  "@4 reg=0x42 len=4 ",  "@4 reg=0x48 len=25 ", "@4 reg=0x69 len=4 ",
        "@4 reg=0x6a len=13 "};
    for (size_t i = 0; i < 17; ++i) {
        TEST_ASSERT_EQUAL_STRING_LEN(expectedPrefix[i], lines[1 + i].c_str(), strlen(expectedPrefix[i]));
    }
    TEST_ASSERT_EQUAL_STRING("@4 reg=0x03 len=4 data=41505020", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@4 reg=0x34 len=6 data=d64106009800", lines[9].c_str());
    TEST_ASSERT_EQUAL_STRING("@4 reg=0x35 len=16 data=2cb1844100000000000000002cb18441",
                             lines[10].c_str());
    TEST_ASSERT_EQUAL_STRING("@4 int_clear=ok", lines[18].c_str());
    TEST_ASSERT_EQUAL_STRING("@4 OK", lines[19].c_str());
}

void test_snapshot_clears_only_the_events_it_reported()
{
    // New Contract as Consumer (bit 12) and Source Cap Message Received
    // (bit 14) are set when the snapshot reads INT_EVENT1.
    Bench b;
    b.fake.registers[0x14][1] = 0x50;

    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_TRUE(hasLine(lines, "@4 reg=0x14 len=11 data=0050000000000000000000"));
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=ok"));

    // INT_CLEAR1 received exactly the bytes that were read.
    const FakeBenchTarget::Write *clear = b.fake.lastWrite(0x18);
    TEST_ASSERT_NOT_NULL(clear);
    TEST_ASSERT_EQUAL(11, clear->data.size());
    TEST_ASSERT_EQUAL_HEX8(0x00, clear->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x50, clear->data[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, b.fake.registers[0x14][1]);
    // It is the only register the snapshot wrote.
    TEST_ASSERT_EQUAL(1, b.fake.writes.size());
}

void test_snapshot_keeps_an_event_that_arrives_after_the_read()
{
    // New Contract as Consumer (bit 12) is set when INT_EVENT1 is read. Plug
    // Insert or Removal (bit 3) arrives while the snapshot reads STATUS, which
    // is after INT_EVENT1 and before the clear, so the clear must leave it.
    Bench b;
    b.fake.registers[0x14][1] = 0x10;
    b.fake.beforeRead = [](FakeBenchTarget &f, uint8_t reg, size_t) {
        if (reg == 0x1A) f.registers[0x14][0] |= 0x08;
    };

    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_TRUE(hasLine(lines, "@4 reg=0x14 len=11 data=0010000000000000000000"));
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=ok"));

    const FakeBenchTarget::Write *clear = b.fake.lastWrite(0x18);
    TEST_ASSERT_NOT_NULL(clear);
    TEST_ASSERT_EQUAL_HEX8(0x00, clear->data[0]);
    TEST_ASSERT_EQUAL_HEX8(0x10, clear->data[1]);
    // The reported event is cleared and the late one is still pending.
    TEST_ASSERT_EQUAL_HEX8(0x08, b.fake.registers[0x14][0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, b.fake.registers[0x14][1]);
}

void test_snapshot_reports_a_register_that_does_not_answer()
{
    Bench b;
    b.fake.failRead[0x30] = true;
    b.fake.failRead[0x6A] = true;
    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_TRUE(hasLine(lines, "@4 reg=0x30 error=I2C"));
    TEST_ASSERT_TRUE(hasLine(lines, "@4 reg=0x6a error=I2C"));
    // The others are still reported, and INT_EVENT1 is still cleared.
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@4 reg=0x34 len=6 "));
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=ok"));
    TEST_ASSERT_EQUAL_STRING("@4 ERR I2C 2 of 17 registers did not answer", lines.back().c_str());
}

void test_snapshot_skips_the_clear_without_an_event_read()
{
    Bench b;
    b.fake.failRead[0x14] = true;
    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=skipped"));
    TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x18));
    TEST_ASSERT_EQUAL_STRING("@4 ERR I2C 1 of 17 registers did not answer", lines.back().c_str());
}

void test_snapshot_reports_a_failed_clear()
{
    Bench b;
    b.fake.failWrite[0x18] = true;
    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=failed"));
    TEST_ASSERT_EQUAL_STRING("@4 OK", lines.back().c_str());
}

void test_snapshot_with_a_silent_controller()
{
    Bench b;
    b.fake.silent = true;
    const Lines lines = b.run("@4 SNAP");
    TEST_ASSERT_EQUAL(17, countContaining(lines, "error=I2C"));
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=skipped"));
    TEST_ASSERT_EQUAL_STRING("@4 ERR I2C 17 of 17 registers did not answer", lines.back().c_str());
}

// ---------------------------------------------------------------------------
// WR
// ---------------------------------------------------------------------------

void test_write_refuses_registers_off_the_allowlist()
{
    Bench b;
    const char *refused[] = {"@5 WR 0x79 00", "@5 WR 0x08 47414944",
                             "@5 WR 0x09 00", "@5 WR 0x2A 00000000", "@5 WR 0x2D 00",
                             "@5 WR 0x14 00", "@5 WR 0x5C 00"};
    for (const char *line : refused) {
        const Lines lines = b.run(line);
        TEST_ASSERT_EQUAL(1, lines.size());
        TEST_ASSERT_EQUAL_STRING(
            "@5 ERR REFUSED register is not on the write allowlist (0x16, 0x18, 0x27, 0x28, "
            "0x29, 0x32, 0x33, 0x37, 0x42, 0x70, 0x77, 0x78, 0x7a, 0x7b, 0x7d, 0x7e, 0x98)",
            lines[0].c_str());
    }
    // A refusal costs no I2C traffic at all.
    TEST_ASSERT_EQUAL(0, b.fake.transactions);
}

void test_write_returns_the_readback()
{
    Bench b;
    const Lines lines = b.run("@5 WR 0x42 1a000800");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x42 len=4 data=1a000800 match=1", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[1].c_str());

    // The write carried the register address, the length byte, and the data.
    TEST_ASSERT_EQUAL(1, b.fake.writes.size());
    TEST_ASSERT_EQUAL_HEX8(0x42, b.fake.writes[0].reg);
    const uint8_t expected[] = {0x1A, 0x00, 0x08, 0x00};
    TEST_ASSERT_EQUAL(4, b.fake.writes[0].data.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, b.fake.writes[0].data.data(), 4);
}

void test_write_accepts_every_allowed_register()
{
    // Each register written whole at the size the fake reports, which is its
    // RegisterInfo size. For a PD 3.x data block 0x42 is written too.
    const uint8_t allowed[] = {0x16, 0x18, 0x27, 0x28, 0x29, 0x32, 0x33, 0x37,
                               0x42, 0x70, 0x77, 0x78, 0x7A, 0x7B, 0x7D, 0x7E, 0x98};
    for (uint8_t address : allowed) {
        Bench b;
        const size_t size = b.fake.registers[address].size();
        char line[TPS25751BenchProtocol::kMaxLineLength + 1];
        snprintf(line, sizeof(line), "@5 WR 0x%02x %s", address,
                 std::string(size * 2, '0').c_str());
        const Lines lines = b.run(line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("@5 OK", lines.back().c_str(), line);
        TEST_ASSERT_EQUAL_MESSAGE(1, b.fake.writeCount(address), line);
        const bool coupled =
            TPS25751BenchProtocol::pd3EnableBit(address) != TPS25751BenchProtocol::kNoPd3EnableBit;
        if (address != 0x42) {
            TEST_ASSERT_EQUAL_MESSAGE(coupled ? 1 : 0, b.fake.writeCount(0x42), line);
        }
    }
}

void test_write_port_control_returns_the_readback()
{
    // PORT_CONTROL as the bench board read it on 2026-11-25 (0xDA815072), with
    // bits 5 (Initiate Swap to Sink) and 4 (Process Swap to Sink) cleared, so a
    // sourcing board neither sends nor accepts a PR_Swap to sink.
    Bench b;
    b.fake.setRegister(0x29, {0x72, 0x50, 0x81, 0xDA});
    const Lines lines = b.run("@5 WR 0x29 425081da");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x29 len=4 data=425081da match=1", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[1].c_str());
    TEST_ASSERT_EQUAL(1, b.fake.writes.size());
    TEST_ASSERT_EQUAL_HEX8(0x29, b.fake.writes[0].reg);
    const uint8_t expected[] = {0x42, 0x50, 0x81, 0xDA};
    TEST_ASSERT_EQUAL(4, b.fake.writes[0].data.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, b.fake.writes[0].data.data(), 4);

    // Whole register only, as for the others.
    const Lines partial = b.run("@6 WR 0x29 4250");
    TEST_ASSERT_EQUAL_STRING("@6 ERR BADARG the register is 4 bytes, got 2", partial[0].c_str());
    TEST_ASSERT_EQUAL(1, b.fake.writes.size());
}

void test_write_reports_a_readback_that_differs()
{
    // The controller ACKs the write and keeps its value, as it does for bits
    // the firmware owns.
    Bench b;
    b.fake.setRegister(0x42, {0x10, 0x00, 0x08, 0x00});
    b.fake.ignoreWrite[0x42] = true;
    const Lines lines = b.run("@5 WR 0x42 1a000800");
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x42 len=4 data=10000800 match=0", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[1].c_str());
}

void test_write_requires_the_whole_register()
{
    Bench b;
    Lines lines = b.run("@5 WR 0x42 1a0008");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 ERR BADARG the register is 4 bytes, got 3", lines[0].c_str());

    lines = b.run("@5 WR 0x42 1a00080000");
    TEST_ASSERT_EQUAL_STRING("@5 ERR BADARG the register is 4 bytes, got 5", lines[0].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.writes.size());
}

void test_write_reports_i2c_failures()
{
    Bench b;
    b.fake.silent = true;
    TEST_ASSERT_EQUAL_STRING("@5 ERR I2C register read failed before the write",
                             b.run("@5 WR 0x42 1a000800")[0].c_str());

    b.fake.silent = false;
    b.fake.failWrite[0x42] = true;
    TEST_ASSERT_EQUAL_STRING("@5 ERR I2C register write failed",
                             b.run("@5 WR 0x42 1a000800")[0].c_str());

    b.fake.failWrite.clear();
    b.fake.afterWrite = [](FakeBenchTarget &f, uint8_t reg) {
        if (reg == 0x42) f.failRead[0x42] = true;
    };
    TEST_ASSERT_EQUAL_STRING("@5 ERR I2C register read failed after the write",
                             b.run("@5 WR 0x42 1a000800")[0].c_str());
}

// PD3_CONFIGURATION as the bench board read it on 2026-11-18, 0x0008001A
// (TPS25751PD3Config.h): Support Get Revision (bit 19), Unchunked Supported
// (bit 4), and the firmware's reserved bits 3 and 1.
const uint8_t kPd3Bench[4] = {0x1A, 0x00, 0x08, 0x00};

// TRANSMIT_PPS_STATUS (0x7A, SLVUCR8B p.56; USB PD R3.3 Table 6.67): Output
// Voltage 9000 mV / 20 mV = 450 = 0x01C2 in bits 15:0, Output Current
// 2000 mA / 50 mA = 40 = 0x28 in bits 23:16, PTF 01b (normal) in bits 26:25
// = 0x02000000. Word 0x022801C2, little-endian c2012802.
const char kWritePps[] = "@5 WR 0x7A c2012802";

void test_write_coupled_block_sets_its_pd3_bit()
{
    // 0x7A is gated by Support PPS Status, bit 20 = byte 2, bit 4: the bench
    // value 0x0008001A becomes 0x0018001A, buffer {0x1A, 0x00, 0x18, 0x00}.
    Bench b;
    b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
    const Lines lines = b.run(kWritePps);
    TEST_ASSERT_EQUAL(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x7a len=4 data=c2012802 match=1", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x42 len=4 data=1a001800 match=1", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[2].c_str());

    // The data register first, then 0x42 with only bit 20 added.
    TEST_ASSERT_EQUAL(2, b.fake.writes.size());
    TEST_ASSERT_EQUAL_HEX8(0x7A, b.fake.writes[0].reg);
    TEST_ASSERT_EQUAL_HEX8(0x42, b.fake.writes[1].reg);
    const uint8_t expected[] = {0x1A, 0x00, 0x18, 0x00};
    TEST_ASSERT_EQUAL(4, b.fake.writes[1].data.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, b.fake.writes[1].data.data(), 4);
}

void test_write_sets_the_right_pd3_bit_for_each_block()
{
    // Bit n is byte n / 8, bit n % 8 of the 0x42 buffer (SLVUCR8B p.46):
    //   0x77 bit 8  -> byte 1, 0x01    0x7B bit 11 -> byte 1, 0x08
    //   0x7D bit 10 -> byte 1, 0x04    0x7E bit 17 -> byte 2, 0x02
    //   0x7A bit 20 -> byte 2, 0x10    0x78 bit 18 -> byte 2, 0x04
    struct Case {
        uint8_t address;
        uint8_t byte;
        uint8_t mask;
    };
    const Case cases[] = {{0x77, 1, 0x01}, {0x7A, 2, 0x10}, {0x7B, 1, 0x08},
                          {0x7D, 1, 0x04}, {0x7E, 2, 0x02}, {0x78, 2, 0x04}};
    for (const Case &c : cases) {
        Bench b;
        const size_t size = b.fake.registers[c.address].size();
        char line[TPS25751BenchProtocol::kMaxLineLength + 1];
        snprintf(line, sizeof(line), "@5 WR 0x%02x %s", c.address,
                 std::string(size * 2, '0').c_str());
        const Lines lines = b.run(line);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("@5 OK", lines.back().c_str(), line);
        const FakeBenchTarget::Write *pd3 = b.fake.lastWrite(0x42);
        TEST_ASSERT_NOT_NULL_MESSAGE(pd3, line);
        uint8_t expected[4] = {0, 0, 0, 0};
        expected[c.byte] = c.mask;
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected, pd3->data.data(), 4, line);
    }
}

void test_write_coupled_block_with_its_bit_already_set()
{
    // 0x0018001A already has bit 20, so 0x42 is read and not written.
    Bench b;
    b.fake.setRegister(0x42, {0x1A, 0x00, 0x18, 0x00});
    const Lines lines = b.run(kWritePps);
    TEST_ASSERT_EQUAL(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x7a len=4 data=c2012802 match=1", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 pd3=already", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[2].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x42));
    TEST_ASSERT_EQUAL(1, b.fake.writes.size());
}

void test_write_reports_pd3_failures_after_the_data_write()
{
    {
        // 0x42 does not answer: the data write stands, 0x42 is not written.
        Bench b;
        b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
        b.fake.failRead[0x42] = true;
        const Lines lines = b.run(kWritePps);
        TEST_ASSERT_EQUAL(2, lines.size());
        TEST_ASSERT_EQUAL_STRING("@5 reg=0x7a len=4 data=c2012802 match=1", lines[0].c_str());
        TEST_ASSERT_EQUAL_STRING("@5 ERR I2C the data register was written; the 0x42 enable bit "
                                 "was not set: PD3_CONFIGURATION read failed",
                                 lines[1].c_str());
        TEST_ASSERT_EQUAL(1, b.fake.writeCount(0x7A));
        TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x42));
    }
    {
        // 0x42 reports a length other than its 4 bytes: named as a length mismatch.
        Bench b;
        b.fake.setRegister(0x42, {0x1A, 0x00});
        const Lines lines = b.run(kWritePps);
        TEST_ASSERT_EQUAL_STRING("@5 ERR I2C the data register was written; the 0x42 enable bit "
                                 "was not set: PD3_CONFIGURATION is 2 bytes, expected 4",
                                 lines.back().c_str());
        TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x42));
    }
    {
        // The 0x42 write is not acknowledged.
        Bench b;
        b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
        b.fake.failWrite[0x42] = true;
        const Lines lines = b.run(kWritePps);
        TEST_ASSERT_EQUAL(2, lines.size());
        TEST_ASSERT_EQUAL_STRING("@5 reg=0x7a len=4 data=c2012802 match=1", lines[0].c_str());
        TEST_ASSERT_EQUAL_STRING("@5 ERR I2C the data register was written; the 0x42 enable bit "
                                 "was not set: PD3_CONFIGURATION write failed",
                                 lines[1].c_str());
    }
    {
        // The 0x42 write goes through and its readback does not answer.
        Bench b;
        b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
        b.fake.afterWrite = [](FakeBenchTarget &f, uint8_t reg) {
            if (reg == 0x42) f.failRead[0x42] = true;
        };
        const Lines lines = b.run(kWritePps);
        TEST_ASSERT_EQUAL(2, lines.size());
        TEST_ASSERT_EQUAL_STRING("@5 reg=0x7a len=4 data=c2012802 match=1", lines[0].c_str());
        TEST_ASSERT_EQUAL_STRING("@5 ERR I2C the data register was written; the 0x42 enable bit "
                                 "was not confirmed: PD3_CONFIGURATION read failed after its write",
                                 lines[1].c_str());
        TEST_ASSERT_EQUAL(1, b.fake.writeCount(0x42));
    }
    {
        // The controller keeps its old 0x42: the line says match=0.
        Bench b;
        b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
        b.fake.ignoreWrite[0x42] = true;
        const Lines lines = b.run(kWritePps);
        TEST_ASSERT_EQUAL_STRING("@5 reg=0x42 len=4 data=1a000800 match=0", lines[1].c_str());
        TEST_ASSERT_EQUAL_STRING("@5 OK", lines[2].c_str());
    }
}

void test_write_failure_of_a_coupled_block_leaves_pd3_alone()
{
    Bench b;
    b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
    b.fake.failWrite[0x7A] = true;
    const Lines lines = b.run(kWritePps);
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 ERR I2C register write failed", lines[0].c_str());
    for (const auto &read : b.fake.reads) TEST_ASSERT_NOT_EQUAL(0x42, read.first);
    TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x42));
}

void test_write_uncoupled_register_leaves_pd3_alone()
{
    Bench b;
    b.fake.setRegister(0x42, kPd3Bench, sizeof(kPd3Bench));
    const Lines lines = b.run(("@5 WR 0x70 03"));
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@5 reg=0x70 len=1 data=03 match=1", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[1].c_str());
    for (const auto &read : b.fake.reads) TEST_ASSERT_NOT_EQUAL(0x42, read.first);
    TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x42));
}

void test_write_full_transmit_source_caps_with_the_longest_id()
{
    // TRANSMIT_SOURCE_CAPABILITIES (0x32) is 63 bytes, the longest register on
    // the allowlist: "@12345678 WR 0x32 " is 18 characters and the data 126,
    // 144 in all, under kMaxLineLength (160). Fed through poll() so the line
    // buffer is exercised too. Byte i is i, so a dropped or shifted byte shows.
    Bench b;
    std::vector<uint8_t> payload(63);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<uint8_t>(i);
    const std::string line = "@12345678 WR 0x32 " + hexOf(payload);
    TEST_ASSERT_EQUAL(144, line.size());
    TEST_ASSERT_TRUE(line.size() <= TPS25751BenchProtocol::kMaxLineLength);

    b.io.feed(line + "\n");
    b.console.poll();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING(("@12345678 reg=0x32 len=63 data=" + hexOf(payload) + " match=1").c_str(),
                             lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@12345678 OK", lines[1].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.writeCount(0x42));
}

void test_write_int_clear_reads_back_as_a_clear_request()
{
    // INT_CLEAR1 (0x18): a 1 clears that INT_EVENT1 bit (SLVUCR8B p.23). This
    // target keeps 0x18 at zero and clears the event, the readback the
    // isWriteAllowed() documentation tells the host to expect. Plug Insert or
    // Removal is bit 3, byte 0 mask 0x08.
    Bench b;
    b.fake.registers[0x14][0] = 0x08;
    std::string clear = "08" + std::string(20, '0');
    const Lines lines = b.run(("@5 WR 0x18 " + clear).c_str());
    TEST_ASSERT_EQUAL_STRING(("@5 reg=0x18 len=11 data=" + std::string(22, '0') + " match=0").c_str(),
                             lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@5 OK", lines[1].c_str());
    TEST_ASSERT_EQUAL_HEX8(0x00, b.fake.registers[0x14][0]);
}

// ---------------------------------------------------------------------------
// CMD
// ---------------------------------------------------------------------------

void test_task_runs_and_returns_data()
{
    Bench b;
    b.fake.taskHandler = [](const std::string &code, std::vector<uint8_t> &data) {
        TEST_ASSERT_EQUAL_STRING("GSrC", code.c_str());
        data.assign(64, 0);
        data[0] = 0x00;  // Standard Task Return Code: success
        data[1] = 0xAB;
        data[63] = 0xCD;
        return Verdict::Complete;
    };

    const Lines lines = b.run("@6 CMD GSrC");
    TEST_ASSERT_EQUAL(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("@6 task=GSrC status=success rc=0x00", lines[0].c_str());
    std::string expected = "@6 data=00ab";
    expected += std::string(61 * 2, '0');
    expected += "cd";
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 OK", lines[2].c_str());

    // No input: DATA is not written, only COMMAND.
    TEST_ASSERT_EQUAL(1, b.fake.writes.size());
    TEST_ASSERT_EQUAL_HEX8(0x08, b.fake.writes[0].reg);
}

void test_task_passes_the_payload_through_data()
{
    Bench b;
    std::vector<uint8_t> seen;
    b.fake.taskHandler = [&seen](const std::string &, std::vector<uint8_t> &data) {
        seen.assign(data.begin(), data.begin() + 3);
        data[0] = 0x00;
        data[1] = 0x12;
        data[2] = 0x34;
        return Verdict::Complete;
    };
    const Lines lines = b.run("@6 CMD I2Cr 6b1b02");
    const uint8_t expected[] = {0x6B, 0x1B, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, seen.data(), 3);
    TEST_ASSERT_EQUAL_STRING("@6 task=I2Cr status=success rc=0x00", lines[0].c_str());
    TEST_ASSERT_TRUE(lines[1].compare(0, 14, "@6 data=001234") == 0);

    // DATA was written with exactly the three payload bytes, then COMMAND.
    TEST_ASSERT_EQUAL(2, b.fake.writes.size());
    TEST_ASSERT_EQUAL_HEX8(0x09, b.fake.writes[0].reg);
    TEST_ASSERT_EQUAL(3, b.fake.writes[0].data.size());
}

void test_task_reports_a_task_level_return_code()
{
    // Return code 3 is "Task rejected" (SLVUCR8B Table 4-1): the executor
    // succeeded, so the reply is OK and carries the code for the host.
    Bench b;
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &data) {
        data[0] = 0x03;
        return Verdict::Complete;
    };
    const Lines lines = b.run("@6 CMD SSrC");
    TEST_ASSERT_EQUAL_STRING("@6 task=SSrC status=success rc=0x03", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 OK", lines.back().c_str());
}

void test_task_reports_an_unknown_code_answer()
{
    Bench b;
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &) { return Verdict::Reject; };
    const Lines lines = b.run("@6 CMD ANeg");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REJECTED the controller answered !CMD", lines[0].c_str());
}

void test_task_times_out_when_command_never_clears()
{
    Bench b;
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &) { return Verdict::Pending; };
    Lines lines = b.run("@6 CMD GSrC");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@6 ERR TIMEOUT COMMAND did not clear within 2000 ms", lines[0].c_str());
    TEST_ASSERT_TRUE(millis() - kStartMillis > 2000);

    // GPPI gets the longer wait (SLVUCR8B p.67).
    lines = b.run("@7 CMD GPPI 0000");
    TEST_ASSERT_EQUAL_STRING("@7 ERR TIMEOUT COMMAND did not clear within 5000 ms", lines[0].c_str());
}

void test_task_reports_an_i2c_failure()
{
    Bench b;
    b.fake.silent = true;
    const Lines lines = b.run("@6 CMD GSkC");
    TEST_ASSERT_EQUAL_STRING("@6 ERR I2C the controller did not answer during the task",
                             lines[0].c_str());
}

void test_task_refuses_what_the_allowlist_refuses()
{
    Bench b;
    TEST_ASSERT_EQUAL_STRING(
        "@6 ERR REFUSED patch tasks are run by the console's own patch loader",
        b.run("@6 CMD PBMs 640000003031")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED patch tasks are run by the console's own patch loader",
                             b.run("@6 CMD PBMc")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED patch tasks are run by the console's own patch loader",
                             b.run("@6 CMD PBMe")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED GO2P forces the controller back to PTCH mode",
                             b.run("@6 CMD GO2P")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED external EEPROM tasks are not allowed",
                             b.run("@6 CMD FLwd 00")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED I2Cw writes a device on the I2Cc bus",
                             b.run("@6 CMD I2Cw 6b020100")[0].c_str());
    TEST_ASSERT_EQUAL_STRING(
        "@6 ERR REFUSED role swaps need a build with -D TPS25751_BENCH_ALLOW_SWAPS",
        b.run("@6 CMD SWSr")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED task is not on the allowlist",
                             b.run("@6 CMD GPsh 00")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@6 ERR REFUSED task is not on the allowlist",
                             b.run("@6 CMD gsrc")[0].c_str());

    // None of them reached the controller.
    TEST_ASSERT_EQUAL(0, b.fake.transactions);
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
}

// ---------------------------------------------------------------------------
// RESTART
// ---------------------------------------------------------------------------

void test_restart_warm_reports_each_step()
{
    Bench b;
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &) {
        TEST_ASSERT_EQUAL_STRING("Gaid", code.c_str());
        scriptRestart(b.fake, "APP ");
        return Verdict::Pending;
    };

    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL(6, lines.size());
    // COMMAND kept the code for the 100 ms the console watched it.
    TEST_ASSERT_EQUAL_STRING("@7 step=issue task=Gaid status=timeout t_ms=5105", lines[0].c_str());
    // The controller went quiet 1000 ms after the task and returned at 1300 ms;
    // the console polls every 20 ms.
    TEST_ASSERT_EQUAL_STRING("@7 step=nak t_ms=6005", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 step=back evidence=data_cleared t_ms=6305", lines[2].c_str());
    // MODE read APP at the first try here, so the wait was 0 ms.
    TEST_ASSERT_EQUAL_STRING("@7 step=mode t_ms=6305 mode=APP waited_ms=0", lines[3].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 step=done mode=APP t_ms=6305", lines[4].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines[5].c_str());

    // DATA carried the marker before the task was issued.
    TEST_ASSERT_EQUAL_HEX8(0x09, b.fake.writes[0].reg);
    TEST_ASSERT_EQUAL(64, b.fake.writes[0].data.size());
    const uint8_t marker[] = {0xA5, 0x5A, 0xC3, 0x3C};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(marker, b.fake.writes[0].data.data(), 4);
    TEST_ASSERT_EQUAL(1, b.fake.tasks.size());
}

void test_restart_of_a_controller_that_reboots_at_once()
{
    // The controller stops answering as soon as it has the task code, so the
    // console's first poll of COMMAND already fails.
    Bench b;
    b.fake.afterWrite = [](FakeBenchTarget &f, uint8_t reg) {
        if (reg != FakeBenchTarget::kCommand) return;
        f.silent = true;
        f.schedule(millis() + 400, [](FakeBenchTarget &g) {
            g.silent = false;
            g.registers[FakeBenchTarget::kCommand] = std::vector<uint8_t>(4, 0);
            g.registers[FakeBenchTarget::kData] = std::vector<uint8_t>(64, 0);
        });
    };
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &) { return Verdict::Pending; };

    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING("@7 step=issue task=Gaid status=i2c_error t_ms=5000", lines[0].c_str());
    // The console probes at once after the failed poll, so the quiet
    // controller is noticed without a delay.
    TEST_ASSERT_EQUAL_STRING("@7 step=nak t_ms=5000", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 step=back evidence=data_cleared t_ms=5400", lines[2].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
}

void test_restart_cold_reloads_the_patch()
{
    // An EEPROM-less board: after 'GAID' the controller comes back in BOOT,
    // moves to PTCH, and asserts ReadyForPatch.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setRegister(0x2D, kBootFlagsI2c, sizeof(kBootFlagsI2c));
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &data) {
        if (code == "GAID") {
            scriptRestart(b.fake, "BOOT");
            b.fake.schedule(millis() + 1500, [](FakeBenchTarget &f) {
                f.setMode("PTCH");
                setReadyForPatch(f);
            });
            return Verdict::Pending;
        }
        return acceptPatch(b.fake, code, data);
    };

    const Lines lines = b.run("@7 RESTART cold");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=issue task=GAID status=timeout t_ms=5105"));
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=back evidence=data_cleared t_ms=6305"));
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=patch result=Success t_ms="));
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=done mode=APP t_ms="));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());

    TEST_ASSERT_EQUAL(3, b.fake.tasks.size());
    TEST_ASSERT_EQUAL_STRING("GAID", b.fake.tasks[0].c_str());
    TEST_ASSERT_EQUAL_STRING("PBMs", b.fake.tasks[1].c_str());
    TEST_ASSERT_EQUAL_STRING("PBMc", b.fake.tasks[2].c_str());
    TEST_ASSERT_EQUAL(sizeof(patchImage), b.fake.pbmBytes.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(patchImage, b.fake.pbmBytes.data(), sizeof(patchImage));
}

void test_restart_cold_without_a_bundle_is_refused_on_an_eeprom_less_board()
{
    Bench b;
    b.fake.setRegister(0x2D, kBootFlagsI2c, sizeof(kBootFlagsI2c));
    const Lines lines = b.run("@7 RESTART cold");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR NOBUNDLE the configuration did not come from an EEPROM and no patch bundle is "
        "compiled in",
        lines[0].c_str());
    // Nothing was written: no marker, no task.
    TEST_ASSERT_EQUAL(0, b.fake.writes.size());
}

void test_restart_cold_without_a_bundle_needs_boot_flags()
{
    Bench b;
    b.fake.failRead[0x2D] = true;
    const Lines lines = b.run("@7 RESTART cold");
    TEST_ASSERT_EQUAL_STRING("@7 ERR I2C BOOT_FLAGS read failed", lines[0].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.writes.size());
}

void test_restart_cold_without_a_bundle_runs_on_an_eeprom_board()
{
    // BOOT_FLAGS says the configuration came from the EEPROM, so the controller
    // loads it again on its own: BOOT, PTCH, then APP.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "BOOT");
        b.fake.schedule(millis() + 1400, [](FakeBenchTarget &f) { f.setMode("PTCH"); });
        b.fake.schedule(millis() + 1800, [](FakeBenchTarget &f) { f.setMode("APP "); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART cold");
    TEST_ASSERT_FALSE(hasLineStarting(lines, "@7 step=patch"));
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=done mode=APP t_ms="));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
    TEST_ASSERT_EQUAL(1, b.fake.tasks.size());
}

void test_restart_through_cmd_runs_the_same_sequence()
{
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        return Verdict::Pending;
    };
    Lines lines = b.run("@7 CMD Gaid");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=issue task=Gaid "));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());

    // 'GAID' through CMD obeys the bundle rule of RESTART cold.
    b.fake.setRegister(0x2D, kBootFlagsI2c, sizeof(kBootFlagsI2c));
    lines = b.run("@8 CMD GAID");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@8 ERR NOBUNDLE "));
}

void test_restart_reports_a_rejected_task()
{
    Bench b;
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &) { return Verdict::Reject; };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL(1, lines.size());
    TEST_ASSERT_EQUAL_STRING("@7 ERR REJECTED the controller answered !CMD", lines[0].c_str());
}

void test_restart_fails_when_the_marker_cannot_be_written()
{
    Bench b;
    b.fake.failWrite[0x09] = true;
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING("@7 ERR I2C DATA write failed before the restart", lines[0].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
}

void test_restart_without_evidence_times_out()
{
    // COMMAND reads clear at once, DATA keeps the marker, and the controller
    // never stops answering: nothing shows a restart.
    Bench b;
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@7 step=issue task=Gaid status=success t_ms=5000", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 ERR TIMEOUT no restart observed within 10000 ms", lines[1].c_str());
    TEST_ASSERT_TRUE(millis() - kStartMillis >= TPS25751BenchConsole::kRestartTimeoutMs);
}

void test_restart_accepts_a_nak_window_as_evidence()
{
    // A controller that keeps DATA across the restart still shows it by going
    // quiet for a while.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ", false);
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=back evidence=nak t_ms=6305"));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
}

void test_restart_accepts_a_cleared_command_as_evidence()
{
    // No quiet window and DATA kept: COMMAND held the task code and then
    // cleared, which only a restart does (SLVUCR8B p.62).
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        b.fake.schedule(millis() + 1000, [](FakeBenchTarget &f) {
            f.registers[FakeBenchTarget::kCommand] = std::vector<uint8_t>(4, 0);
        });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_FALSE(hasLineStarting(lines, "@7 step=nak"));
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=back evidence=command_cleared t_ms=6005"));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
}

void test_restart_fails_when_the_controller_stays_silent()
{
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        b.fake.schedule(millis() + 1000, [](FakeBenchTarget &f) { f.silent = true; });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=nak t_ms=6005"));
    TEST_ASSERT_EQUAL_STRING("@7 ERR I2C the controller did not answer again within 10000 ms",
                             lines.back().c_str());
}

void test_restart_keeps_waiting_while_command_holds_another_value()
{
    // COMMAND reads as some other code for a while after the restart begins;
    // the console waits for it to clear.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        b.fake.schedule(millis() + 500, [](FakeBenchTarget &f) {
            f.registers[FakeBenchTarget::kCommand] = {'B', 'O', 'O', 'T'};
        });
        b.fake.schedule(millis() + 900, [](FakeBenchTarget &f) {
            f.registers[FakeBenchTarget::kCommand] = std::vector<uint8_t>(4, 0);
            f.registers[FakeBenchTarget::kData] = std::vector<uint8_t>(64, 0);
        });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=back evidence=data_cleared t_ms=5905"));
}

void test_restart_fails_fast_when_the_command_write_fails()
{
    // The COMMAND write is not acknowledged. The controller still answers,
    // holds no task, and DATA keeps the marker, so the console reports the
    // failed write at once instead of waiting out the restart timeout.
    Bench b;
    b.fake.failWrite[FakeBenchTarget::kCommand] = true;

    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@7 step=issue task=Gaid status=i2c_error t_ms=5000", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 ERR I2C the COMMAND write failed and the task was not issued",
                             lines[1].c_str());
    TEST_ASSERT_EQUAL(kStartMillis, millis());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
}

void test_restart_fails_fast_on_a_late_rejection()
{
    // The controller refuses the task only after the 100 ms issue window.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        b.fake.schedule(millis() + 500, [](FakeBenchTarget &f) {
            f.registers[FakeBenchTarget::kCommand] = {'!', 'C', 'M', 'D'};
        });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@7 step=issue task=Gaid status=timeout t_ms=5105", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR REJECTED the controller answered !CMD while the restart was pending",
        lines[1].c_str());
    // It did not wait for the 10 s restart timeout.
    TEST_ASSERT_TRUE(millis() - kStartMillis < 600);
}

void test_restart_survives_one_failed_poll_after_the_write()
{
    // The write went through and one poll of COMMAND failed. COMMAND then reads
    // as the task code, so the task was issued and the restart proceeds.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        return Verdict::Pending;
    };
    int commandReads = 0;
    b.fake.beforeRead = [&commandReads](FakeBenchTarget &f, uint8_t reg, size_t) {
        if (reg != FakeBenchTarget::kCommand) return;
        if (++commandReads == 1) {
            f.failRead[FakeBenchTarget::kCommand] = true;
        } else {
            f.failRead.erase(FakeBenchTarget::kCommand);
        }
    };

    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING("@7 step=issue task=Gaid status=i2c_error t_ms=5000", lines[0].c_str());
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=back evidence=data_cleared t_ms=6300"));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
}

void test_restart_treats_an_unreadable_data_register_as_no_evidence()
{
    // COMMAND reads clear after an I2C error at issue, and DATA cannot be read:
    // nothing shows a restart and nothing shows a failed write, so the console
    // waits, and reports the timeout.
    Bench b;
    b.fake.failWrite[FakeBenchTarget::kCommand] = true;
    b.fake.beforeRead = [](FakeBenchTarget &f, uint8_t reg, size_t) {
        if (reg == FakeBenchTarget::kCommand) f.failRead[FakeBenchTarget::kData] = true;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING("@7 ERR TIMEOUT no restart observed within 10000 ms", lines.back().c_str());
}

void test_restart_into_ptch_without_a_bundle()
{
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "PTCH");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { setReadyForPatch(f); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=back "));
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR NOBUNDLE the controller is in PTCH mode and no patch bundle is compiled in",
        lines.back().c_str());
}

void test_restart_reports_a_failed_patch_load()
{
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &data) {
        if (code == "Gaid") {
            scriptRestart(b.fake, "PTCH");
            b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { setReadyForPatch(f); });
            return Verdict::Pending;
        }
        data[0] = 0x04;  // PatchStartStatus: bad bundle size
        return Verdict::Complete;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=patch result=BadBundleSize t_ms="));
    TEST_ASSERT_EQUAL(1, countContaining(lines, " sent=1"));
    TEST_ASSERT_EQUAL_STRING("@7 ERR PATCH patch load ended with BadBundleSize, MODE PTCH",
                             lines.back().c_str());
}

void test_restart_succeeds_when_the_controller_reaches_app_while_the_loader_waits()
{
    // A board with an EEPROM that stays in PTCH past kPatchReadyTimeoutMs
    // without ReadyForPatch and then reaches APP by itself. The loader polls
    // INT_EVENT1 only, runs to PBM_READY_WAIT_CAP_MS, and returns
    // CommandTimeout with nothing written. MODE reads APP, so the restart
    // succeeded; the loader's status is reported with sent=0.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "PTCH");
        // PTCH from 1300 ms; the console's wait ends near 4300 ms.
        b.fake.schedule(millis() + 5000, [](FakeBenchTarget &f) { f.setMode("APP "); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=mode t_ms=6305 mode=PTCH waited_ms=0"));
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=patch result=CommandTimeout t_ms=14305 sent=0"));
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=done mode=APP t_ms=14305"));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
    for (const std::string &line : lines) TEST_ASSERT_TRUE(isWholeReplyLine(line, "7"));

    // The loader ran its whole ReadyForPatch wait and wrote nothing: the only
    // writes are the restart's marker in DATA and its task in COMMAND.
    TEST_ASSERT_TRUE(millis() >= kStartMillis + 1300 + TPS25751BenchConsole::kPatchReadyTimeoutMs +
                                     PBM_READY_WAIT_CAP_MS);
    TEST_ASSERT_EQUAL(1, b.fake.tasks.size());
    TEST_ASSERT_EQUAL(1, b.fake.writeCount(FakeBenchTarget::kData));
    TEST_ASSERT_EQUAL(2, b.fake.writes.size());
    TEST_ASSERT_EQUAL(0, b.fake.pbmBytes.size());
}

void test_restart_succeeds_when_the_loader_finds_app_in_its_mode_check()
{
    // PTCH with ReadyForPatch sends the console to the loader at once. The
    // controller reaches APP before the loader's MODE check (step 2), which
    // ends the load as NotInPatchMode with nothing written.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "PTCH");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { setReadyForPatch(f); });
        return Verdict::Pending;
    };
    // The console reads INT_EVENT1 once, the loader reads it next.
    size_t eventReads = 0;
    b.fake.beforeRead = [&eventReads](FakeBenchTarget &f, uint8_t reg, size_t) {
        if (reg == FakeBenchTarget::kIntEvent && ++eventReads == 2) f.setMode("APP ");
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=patch result=NotInPatchMode t_ms="));
    TEST_ASSERT_EQUAL(1, countContaining(lines, " sent=0"));
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=done mode=APP t_ms="));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
    TEST_ASSERT_EQUAL(1, b.fake.tasks.size());
    TEST_ASSERT_EQUAL(2, b.fake.writes.size());
}

void test_restart_fails_when_a_load_that_wrote_ends_with_mode_app()
{
    // The same status, CommandTimeout, from a load that did write: PBMs never
    // completes. MODE reads APP afterwards, and the restart still fails,
    // because the controller took a PBMs the loader could not finish.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &) {
        if (code == "Gaid") {
            scriptRestart(b.fake, "PTCH");
            b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { setReadyForPatch(f); });
            return Verdict::Pending;
        }
        b.fake.setMode("APP ");
        return Verdict::Pending;  // PBMs stays in COMMAND
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=patch result=CommandTimeout t_ms="));
    TEST_ASSERT_EQUAL(1, countContaining(lines, " sent=1"));
    TEST_ASSERT_FALSE(hasLineStarting(lines, "@7 step=done"));
    TEST_ASSERT_EQUAL_STRING("@7 ERR PATCH patch load ended with CommandTimeout, MODE APP",
                             lines.back().c_str());
    TEST_ASSERT_EQUAL(2, b.fake.tasks.size());
    TEST_ASSERT_EQUAL_STRING("PBMs", b.fake.tasks[1].c_str());
}

void test_restart_fails_when_a_rejected_load_ends_with_mode_app()
{
    // PatchStartStatus 0x04 with MODE APP afterwards: the load failed after
    // PBMs, so APP does not make it a success.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &data) {
        if (code == "Gaid") {
            scriptRestart(b.fake, "PTCH");
            b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { setReadyForPatch(f); });
            return Verdict::Pending;
        }
        data[0] = 0x04;
        b.fake.setMode("APP ");
        return Verdict::Complete;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=patch result=BadBundleSize t_ms="));
    TEST_ASSERT_EQUAL(1, countContaining(lines, " sent=1"));
    TEST_ASSERT_EQUAL_STRING("@7 ERR PATCH patch load ended with BadBundleSize, MODE APP",
                             lines.back().c_str());
}

void test_restart_reports_a_controller_stuck_in_boot()
{
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "BOOT");
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR TIMEOUT MODE did not read APP or PTCH within 10000 ms after the restart; "
        "last read: BOOT",
        lines.back().c_str());
}

void test_restart_waits_for_mode_after_the_controller_answers()
{
    // What the bench board did: COMMAND and DATA answer right after the warm
    // restart, and then MODE cannot be read for a while because the controller
    // is still booting. The console polls MODE every 50 ms instead of failing.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { f.failRead[0x03] = true; });
        b.fake.schedule(millis() + 3000, [](FakeBenchTarget &f) { f.failRead.erase(0x03); });
        return Verdict::Pending;
    };

    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL(6, lines.size());
    TEST_ASSERT_EQUAL_STRING("@7 step=back evidence=data_cleared t_ms=6305", lines[2].c_str());
    // MODE became readable 3000 ms after the task, at time 8000; the poll that
    // started at 6305 reaches it at 8005, after 1700 ms.
    TEST_ASSERT_EQUAL_STRING("@7 step=mode t_ms=8005 mode=APP waited_ms=1700", lines[3].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 step=done mode=APP t_ms=8005", lines[4].c_str());
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines[5].c_str());
}

void test_restart_waits_through_an_empty_mode_and_boot()
{
    // MODE answers with four zero bytes, then BOOT, then APP. Neither of the
    // first two is a running mode.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { f.setRegister(0x03, {0, 0, 0, 0}); });
        b.fake.schedule(millis() + 2000, [](FakeBenchTarget &f) { f.setMode("BOOT"); });
        b.fake.schedule(millis() + 2500, [](FakeBenchTarget &f) { f.setMode("APP "); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=mode t_ms=7505 mode=APP waited_ms=1200"));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
}

void test_restart_cold_waits_for_mode_too()
{
    // 'GAID' on a board with an EEPROM: MODE is unreadable for a while, then
    // PTCH while the controller loads its configuration, then APP.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &) {
        TEST_ASSERT_EQUAL_STRING("GAID", code.c_str());
        scriptRestart(b.fake, "PTCH");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { f.failRead[0x03] = true; });
        b.fake.schedule(millis() + 2000, [](FakeBenchTarget &f) { f.failRead.erase(0x03); });
        b.fake.schedule(millis() + 2600, [](FakeBenchTarget &f) { f.setMode("APP "); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART cold");
    // PTCH is a running mode for the wait; the patch path then waits for APP.
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=mode t_ms=7005 mode=PTCH waited_ms=700"));
    TEST_ASSERT_FALSE(hasLineStarting(lines, "@7 step=patch"));
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=done mode=APP t_ms="));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
}

void test_restart_reloads_the_patch_after_waiting_for_mode()
{
    // An EEPROM-less board: MODE is unreadable for a while and then reads PTCH
    // with ReadyForPatch, and the console loads the bundle.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setRegister(0x2D, kBootFlagsI2c, sizeof(kBootFlagsI2c));
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &data) {
        if (code == "GAID") {
            scriptRestart(b.fake, "PTCH");
            b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) {
                f.failRead[0x03] = true;
                setReadyForPatch(f);
            });
            b.fake.schedule(millis() + 2000, [](FakeBenchTarget &f) { f.failRead.erase(0x03); });
            return Verdict::Pending;
        }
        return acceptPatch(b.fake, code, data);
    };
    const Lines lines = b.run("@7 RESTART cold");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=mode t_ms=7005 mode=PTCH waited_ms=700"));
    TEST_ASSERT_TRUE(hasLineStarting(lines, "@7 step=patch result=Success t_ms="));
    TEST_ASSERT_EQUAL_STRING("@7 OK", lines.back().c_str());
    TEST_ASSERT_EQUAL(sizeof(patchImage), b.fake.pbmBytes.size());
}

void test_restart_times_out_when_mode_never_comes_back()
{
    // MODE cannot be read at all after the restart. The console gives up after
    // kRestartModeTimeoutMs and names the last read.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { f.failRead[0x03] = true; });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=back evidence=data_cleared t_ms=6305"));
    TEST_ASSERT_FALSE(hasLineStarting(lines, "@7 step=mode"));
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR TIMEOUT MODE did not read APP or PTCH within 10000 ms after the restart; "
        "last read: no answer",
        lines.back().c_str());
    // It waited the whole window, counted from step=back, and no longer.
    TEST_ASSERT_EQUAL(6305 + TPS25751BenchConsole::kRestartModeTimeoutMs, millis());
}

void test_restart_times_out_on_a_mode_that_stays_empty()
{
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        b.fake.schedule(millis() + 1300, [](FakeBenchTarget &f) { f.setRegister(0x03, {0, 0, 0, 0}); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR TIMEOUT MODE did not read APP or PTCH within 10000 ms after the restart; "
        "last read: an empty MODE",
        lines.back().c_str());
}

void test_restart_times_out_when_mode_stops_answering_in_boot()
{
    // BOOT for a while, then no answer: the last read decides the message.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "BOOT");
        b.fake.schedule(millis() + 1500, [](FakeBenchTarget &f) { f.failRead[0x03] = true; });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_EQUAL_STRING(
        "@7 ERR TIMEOUT MODE did not read APP or PTCH within 10000 ms after the restart; "
        "last read: no answer",
        lines.back().c_str());
}

void test_restart_reports_a_mode_that_fails_after_it_was_running()
{
    // MODE reads APP for the wait and then fails all three tries of the check
    // that follows: that is an I2C failure, not a timeout.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "APP ");
        return Verdict::Pending;
    };
    int modeReads = 0;
    b.fake.beforeRead = [&modeReads](FakeBenchTarget &f, uint8_t reg, size_t) {
        if (reg == 0x03 && ++modeReads == 2) f.failRead[0x03] = true;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=mode t_ms=6305 mode=APP waited_ms=0"));
    TEST_ASSERT_EQUAL_STRING("@7 ERR I2C MODE read failed after the restart", lines.back().c_str());
}

void test_restart_reports_a_controller_that_falls_back_to_boot()
{
    // MODE reads PTCH, which ends the wait, and then the controller restarts
    // once more and stays in BOOT. The patch path gives it 3 s and reports it.
    Bench b;
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        scriptRestart(b.fake, "PTCH");
        b.fake.schedule(millis() + 1400, [](FakeBenchTarget &f) { f.setMode("BOOT"); });
        return Verdict::Pending;
    };
    const Lines lines = b.run("@7 RESTART warm");
    TEST_ASSERT_TRUE(hasLine(lines, "@7 step=mode t_ms=6305 mode=PTCH waited_ms=0"));
    TEST_ASSERT_EQUAL_STRING("@7 ERR TIMEOUT MODE stayed BOOT", lines.back().c_str());
}

void test_patch_wait_tolerates_a_failed_mode_read()
{
    // While the console waits for a booting controller to reach APP, single
    // failed MODE reads are skipped, not treated as the end.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("BOOT");
    b.fake.schedule(kStartMillis + 100, [](FakeBenchTarget &f) { f.failRead[0x03] = true; });
    b.fake.schedule(kStartMillis + 400, [](FakeBenchTarget &f) {
        f.failRead.erase(0x03);
        f.setMode("APP ");
    });
    b.console.begin();
    TEST_ASSERT_EQUAL_STRING("! READY proto=1\n", b.io.take().c_str());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
    TEST_ASSERT_TRUE(millis() >= kStartMillis + 400);
}

// ---------------------------------------------------------------------------
// Input handling: poll(), malformed lines, events
// ---------------------------------------------------------------------------

void test_poll_answers_a_line_once_it_is_complete()
{
    Bench b;
    b.io.feed("@1 RD ");
    b.console.poll();
    TEST_ASSERT_EQUAL_STRING("", b.io.take().c_str());

    b.io.feed("0x03\n");
    b.console.poll();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("@1 reg=0x03 len=4 data=41505020", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 OK", lines[1].c_str());
}

void test_poll_answers_several_lines_and_ignores_blank_ones()
{
    Bench b;
    b.io.feed("@1 RD 0x03\r\n\r\n   \n@2 RD 0x0F\n");
    b.console.poll();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(4, lines.size());
    TEST_ASSERT_EQUAL_STRING("@1 OK", lines[1].c_str());
    TEST_ASSERT_EQUAL_STRING("@2 reg=0x0f len=4 data=661109f8", lines[2].c_str());
    TEST_ASSERT_EQUAL_STRING("@2 OK", lines[3].c_str());
}

void test_poll_with_no_input_does_nothing()
{
    Bench b;
    b.console.poll();
    TEST_ASSERT_EQUAL_STRING("", b.io.take().c_str());
    TEST_ASSERT_EQUAL(0, b.fake.transactions);
}

void test_poll_rejects_a_line_that_is_too_long()
{
    Bench b;
    std::string line = "@9 CMD GPPI ";
    line += std::string(TPS25751BenchProtocol::kMaxLineLength, '0');
    b.io.feed(line + "\n@10 RD 0x03\n");
    b.console.poll();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("@9 ERR TOOLONG request line is too long", lines[0].c_str());
    // The next line is handled normally.
    TEST_ASSERT_EQUAL_STRING("@10 OK", lines[2].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
}

void test_poll_reports_a_long_line_without_an_id_as_an_event()
{
    Bench b;
    b.io.feed(std::string(TPS25751BenchProtocol::kMaxLineLength + 10, 'x') + "\n");
    b.console.poll();
    TEST_ASSERT_EQUAL_STRING("! ERR TOOLONG request line is too long\n", b.io.take().c_str());
}

void test_poll_rejects_a_line_with_a_nul_byte()
{
    // A NUL would end the C string early: "RD 0x34" would run and the rest of
    // the line would vanish. The whole line is refused instead.
    Bench b;
    b.io.feed(std::string("@1 RD 0x34\0 trailing\n@2 RD 0x03\n", 32));
    b.console.poll();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(3, lines.size());
    TEST_ASSERT_EQUAL_STRING("@1 ERR SYNTAX request line contains a NUL byte", lines[0].c_str());
    // The next line is handled normally, and only it reached the controller.
    TEST_ASSERT_EQUAL_STRING("@2 reg=0x03 len=4 data=41505020", lines[1].c_str());
    TEST_ASSERT_EQUAL(1, b.fake.reads.size());

    // A NUL in front of the id leaves no id to answer, so it is an event.
    b.io.feed(std::string("\0@3 RD 0x03\n", 12));
    b.console.poll();
    TEST_ASSERT_EQUAL_STRING("! ERR SYNTAX request line contains a NUL byte\n", b.io.take().c_str());
    TEST_ASSERT_EQUAL(1, b.fake.reads.size());
}

void test_refused_lines_reach_the_log_stream()
{
    // A line that is too long or holds a NUL is logged like any other request.
    Bench b;
    FakeStream log;
    b.console.setLogStream(&log);

    std::string line = "@9 CMD GPPI ";
    line += std::string(TPS25751BenchProtocol::kMaxLineLength, '0');
    b.io.feed(line + "\n");
    b.console.poll();
    std::string logged = log.take();
    TEST_ASSERT_TRUE(logged.compare(0, 20, "[bench] @9 CMD GPPI ") == 0);
    const std::string tooLong = " -> ERR TOOLONG (0 ms)\n";
    TEST_ASSERT_TRUE(logged.size() > tooLong.size() &&
                     logged.compare(logged.size() - tooLong.size(), tooLong.size(), tooLong) == 0);

    b.io.feed(std::string(TPS25751BenchProtocol::kMaxLineLength + 10, 'x') + "\n");
    b.console.poll();
    logged = log.take();
    TEST_ASSERT_TRUE(logged.compare(logged.size() - tooLong.size(), tooLong.size(), tooLong) == 0);

    b.io.feed(std::string("@1 RD 0x34\0\n", 12));
    b.console.poll();
    TEST_ASSERT_EQUAL_STRING("[bench] @1 RD 0x34 -> ERR SYNTAX (0 ms)\n", log.take().c_str());

    // A refusal does not leak into the next request's summary.
    b.run("@2 RD 0x03");
    TEST_ASSERT_EQUAL_STRING("[bench] @2 RD 0x03 -> OK (0 ms)\n", log.take().c_str());
}

void test_debug_output_on_the_console_stream_never_splits_a_reply_line()
{
    // Firmware may send the library's debug output to the console's own stream.
    // An I2C error report must then land between reply lines, never inside one,
    // so every reply line still parses.
    Bench b;
    TPS25751::setDebugStream(&b.io);
    TPS25751::setDebugCategories(DEBUG_CAT_ALL);
    TPS25751::setDebugLevel(DEBUG_LEVEL_ERROR);
    b.fake.failRead[0x30] = true;
    b.fake.failRead[0x6A] = true;
    b.fake.failWrite[0x18] = true;

    Lines lines = b.run("@4 SNAP");
    size_t replies = 0;
    size_t debug = 0;
    for (const std::string &line : lines) {
        if (line.compare(0, 1, "[") == 0) {
            ++debug;
            TEST_ASSERT_NOT_EQUAL(std::string::npos, line.find("ERROR I2C: "));
        } else {
            ++replies;
            TEST_ASSERT_TRUE_MESSAGE(isWholeReplyLine(line, "4"), line.c_str());
        }
    }
    TEST_ASSERT_EQUAL(1 + 17 + 1 + 1, replies);   // uptime, registers, int_clear, final
    TEST_ASSERT_TRUE(debug >= 3);                 // two failed reads and the failed clear
    TEST_ASSERT_TRUE(hasLine(lines, "@4 reg=0x30 error=I2C"));
    TEST_ASSERT_TRUE(hasLine(lines, "@4 int_clear=failed"));

    // HELLO with every one of its reads failing.
    b.fake.failRead[0x03] = true;
    b.fake.failRead[0x0F] = true;
    b.fake.failRead[0x1A] = true;
    b.fake.failRead[0x2D] = true;
    lines = b.run("@5 HELLO");
    replies = 0;
    debug = 0;
    for (const std::string &line : lines) {
        if (line.compare(0, 1, "[") == 0) {
            ++debug;
        } else {
            ++replies;
            TEST_ASSERT_TRUE_MESSAGE(isWholeReplyLine(line, "5"), line.c_str());
        }
    }
    TEST_ASSERT_EQUAL(4, replies);
    TEST_ASSERT_EQUAL(6, debug);   // MODE is tried three times, the other three registers once
    TEST_ASSERT_TRUE(hasLine(lines, "@5 mode=? version=? role=? plug=?"));

    // The other commands print after their reads as well.
    b.fake.failRead[0x34] = true;
    for (const char *request : {"@6 RD 0x34", "@6 DEC 0x34", "@6 WR 0x42 1a000800", "@6 RESTART cold"}) {
        b.fake.failRead[0x42] = true;
        for (const std::string &line : b.run(request)) {
            if (line.compare(0, 1, "[") != 0) {
                TEST_ASSERT_TRUE_MESSAGE(isWholeReplyLine(line, "6"), line.c_str());
            }
        }
    }
}

void test_line_without_an_id_is_reported_as_an_event()
{
    Bench b;
    TEST_ASSERT_EQUAL_STRING("! ERR SYNTAX expected @<id> <COMMAND>", b.run("HELLO")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("! ERR SYNTAX expected @<id> <COMMAND>", b.run("@ RD 0x03")[0].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.transactions);
}

void test_malformed_requests_with_an_id_get_an_error_reply()
{
    Bench b;
    TEST_ASSERT_EQUAL_STRING("@1 ERR UNKNOWN unknown command", b.run("@1 READ 0x03")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 ERR BADARG missing, extra, or malformed argument",
                             b.run("@1 RD")[0].c_str());
    TEST_ASSERT_EQUAL_STRING("@1 ERR SYNTAX expected @<id> <COMMAND>", b.run("@1")[0].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.transactions);
}

void test_handle_line_ignores_null_and_blank()
{
    Bench b;
    b.console.handleLine(nullptr);
    b.console.handleLine("");
    b.console.handleLine("  \r");
    TEST_ASSERT_EQUAL_STRING("", b.io.take().c_str());
}

void test_begin_announces_the_console()
{
    Bench b;
    b.console.begin();
    TEST_ASSERT_EQUAL_STRING("! READY proto=1\n", b.io.take().c_str());
}

void test_begin_loads_the_patch_and_reports_it()
{
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("PTCH");
    setReadyForPatch(b.fake);
    b.fake.taskHandler = [&b](const std::string &code, std::vector<uint8_t> &data) {
        return acceptPatch(b.fake, code, data);
    };
    b.console.begin();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("! READY proto=1", lines[0].c_str());
    TEST_ASSERT_EQUAL_STRING("! PATCH result=Success mode=APP sent=1", lines[1].c_str());
}

void test_begin_reports_ptch_without_a_bundle()
{
    Bench b;
    b.fake.setMode("PTCH");
    setReadyForPatch(b.fake);
    b.console.begin();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL_STRING("! PATCH result=NoBundle mode=PTCH", lines[1].c_str());
}

void test_begin_reports_a_failed_patch_load()
{
    // PatchStartStatus 0x04 is a bad bundle size (SLVAFV8A Table 4-2).
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("PTCH");
    setReadyForPatch(b.fake);
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &data) {
        data[0] = 0x04;
        return Verdict::Complete;
    };
    b.console.begin();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL_STRING("! PATCH result=BadBundleSize mode=PTCH sent=1", lines[1].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.pbmBytes.size());
}

void test_begin_waits_for_a_controller_that_boots_from_eeprom()
{
    // BOOT, then PTCH without ReadyForPatch, then APP: the controller loads its
    // own configuration, so the console must not start a patch load.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("BOOT");
    b.fake.schedule(kStartMillis + 200, [](FakeBenchTarget &f) { f.setMode("PTCH"); });
    b.fake.schedule(kStartMillis + 600, [](FakeBenchTarget &f) { f.setMode("APP "); });

    b.console.begin();
    TEST_ASSERT_EQUAL_STRING("! READY proto=1\n", b.io.take().c_str());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
    TEST_ASSERT_TRUE(millis() >= kStartMillis + 600);
}

void test_begin_leaves_a_ptch_without_ready_for_patch_to_the_loader()
{
    // PTCH without ReadyForPatch: after kPatchReadyTimeoutMs the console hands
    // over to the patch loader, which polls ReadyForPatch for its own
    // PBM_READY_WAIT_CAP_MS and sends nothing without it.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("PTCH");
    b.console.begin();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL_STRING("! PATCH result=CommandTimeout mode=PTCH sent=0", lines[1].c_str());
    TEST_ASSERT_TRUE(millis() >= kStartMillis + TPS25751BenchConsole::kPatchReadyTimeoutMs +
                                     PBM_READY_WAIT_CAP_MS);
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
    TEST_ASSERT_EQUAL(0, b.fake.writes.size());
    TEST_ASSERT_EQUAL(0, b.fake.pbmBytes.size());
}

void test_begin_reports_a_controller_that_reaches_app_while_the_loader_waits()
{
    // PTCH without ReadyForPatch past kPatchReadyTimeoutMs, then APP while the
    // loader polls INT_EVENT1. Nothing was written, and the event says so.
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("PTCH");
    b.fake.schedule(kStartMillis + 4000, [](FakeBenchTarget &f) { f.setMode("APP "); });
    b.console.begin();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL(2, lines.size());
    TEST_ASSERT_EQUAL_STRING("! PATCH result=CommandTimeout mode=APP sent=0", lines[1].c_str());
    TEST_ASSERT_EQUAL(0, b.fake.tasks.size());
    TEST_ASSERT_EQUAL(0, b.fake.writes.size());
    TEST_ASSERT_EQUAL(0, b.fake.pbmBytes.size());
}

void test_begin_reports_a_load_that_wrote_and_ended_with_mode_app()
{
    Bench b;
    b.console.setPatchBundle(patchImage, sizeof(patchImage));
    b.fake.setMode("PTCH");
    setReadyForPatch(b.fake);
    b.fake.taskHandler = [&b](const std::string &, std::vector<uint8_t> &) {
        b.fake.setMode("APP ");
        return Verdict::Pending;  // PBMs stays in COMMAND
    };
    b.console.begin();
    const Lines lines = b.io.takeLines();
    TEST_ASSERT_EQUAL_STRING("! PATCH result=CommandTimeout mode=APP sent=1", lines[1].c_str());
}

void test_patch_bundle_can_be_given_to_the_constructor()
{
    FakeStream io;
    TPS25751 pd(Wire, 0x20);
    TPS25751BenchConsole withBundle(pd, io, patchImage, sizeof(patchImage));
    TPS25751BenchConsole without(pd, io);
    TEST_ASSERT_TRUE(withBundle.hasPatchBundle());
    TEST_ASSERT_FALSE(without.hasPatchBundle());

    withBundle.setPatchBundle(patchImage, 0);
    TEST_ASSERT_FALSE(withBundle.hasPatchBundle());
    withBundle.setPatchBundle(nullptr, 100);
    TEST_ASSERT_FALSE(withBundle.hasPatchBundle());
}

void test_log_stream_gets_one_line_per_request()
{
    Bench b;
    FakeStream log;
    b.console.setLogStream(&log);

    b.run("@1 RD 0x03");
    TEST_ASSERT_EQUAL_STRING("[bench] @1 RD 0x03 -> OK (0 ms)\n", log.take().c_str());

    b.run("@2 CMD GO2P");
    TEST_ASSERT_EQUAL_STRING("[bench] @2 CMD GO2P -> ERR REFUSED (0 ms)\n", log.take().c_str());

    b.run("@3 BOGUS");
    TEST_ASSERT_EQUAL_STRING("[bench] @3 BOGUS -> ERR UNKNOWN (0 ms)\n", log.take().c_str());

    b.run("bogus");
    TEST_ASSERT_EQUAL_STRING("[bench] bogus -> ERR SYNTAX (0 ms)\n", log.take().c_str());

    // The duration is the time the command took on the console's clock.
    b.fake.taskHandler = [](const std::string &, std::vector<uint8_t> &) { return Verdict::Pending; };
    b.run("@4 CMD GSrC");
    TEST_ASSERT_EQUAL_STRING("[bench] @4 CMD GSrC -> ERR TIMEOUT (2005 ms)\n", log.take().c_str());

    // Nothing of the log reaches the protocol stream, and the reverse.
    b.console.setLogStream(nullptr);
    b.run("@5 RD 0x03");
    TEST_ASSERT_EQUAL_STRING("", log.take().c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_hello_reports_the_console_and_the_controller);
    RUN_TEST(test_hello_reports_the_source_role_and_an_empty_port);
    RUN_TEST(test_hello_reports_the_patch_source_and_the_bundle);
    RUN_TEST(test_hello_answers_when_the_controller_does_not);
    RUN_TEST(test_hello_reports_a_mode_word_it_cannot_print);
    RUN_TEST(test_hello_retries_mode_before_reporting_it_unreadable);
    RUN_TEST(test_hello_in_ptch_mode_reads_only);
    RUN_TEST(test_hello_reports_ptch_without_a_bundle);

    RUN_TEST(test_read_returns_the_length_byte_and_the_data);
    RUN_TEST(test_read_reports_a_length_byte_longer_than_the_library_expects);
    RUN_TEST(test_read_reports_a_length_byte_shorter_than_the_library_expects);
    RUN_TEST(test_read_of_a_register_the_library_does_not_know);
    RUN_TEST(test_read_of_an_unimplemented_address_reports_length_zero);
    RUN_TEST(test_read_fails_when_the_controller_does_not_answer);
    RUN_TEST(test_read_rejects_an_impossible_length_byte);
    RUN_TEST(test_read_fails_when_the_second_read_fails);
    RUN_TEST(test_read_rejects_a_length_byte_that_turns_impossible_on_the_second_read);

    RUN_TEST(test_decode_returns_the_raw_line_and_the_debug_print);
    RUN_TEST(test_decode_without_a_register_class);
    RUN_TEST(test_decode_fails_when_the_controller_does_not_answer);
    RUN_TEST(test_decode_of_the_rdo_contract_takes_the_kind_from_the_active_pdo);
    RUN_TEST(test_decode_of_the_rdo_contract_as_source_uses_the_advertised_pdo);
    RUN_TEST(test_decode_of_an_empty_rdo_contract);

    RUN_TEST(test_snapshot_reads_every_register_in_order);
    RUN_TEST(test_snapshot_clears_only_the_events_it_reported);
    RUN_TEST(test_snapshot_keeps_an_event_that_arrives_after_the_read);
    RUN_TEST(test_snapshot_reports_a_register_that_does_not_answer);
    RUN_TEST(test_snapshot_skips_the_clear_without_an_event_read);
    RUN_TEST(test_snapshot_reports_a_failed_clear);
    RUN_TEST(test_snapshot_with_a_silent_controller);

    RUN_TEST(test_write_refuses_registers_off_the_allowlist);
    RUN_TEST(test_write_returns_the_readback);
    RUN_TEST(test_write_accepts_every_allowed_register);
    RUN_TEST(test_write_port_control_returns_the_readback);
    RUN_TEST(test_write_reports_a_readback_that_differs);
    RUN_TEST(test_write_requires_the_whole_register);
    RUN_TEST(test_write_reports_i2c_failures);
    RUN_TEST(test_write_coupled_block_sets_its_pd3_bit);
    RUN_TEST(test_write_sets_the_right_pd3_bit_for_each_block);
    RUN_TEST(test_write_coupled_block_with_its_bit_already_set);
    RUN_TEST(test_write_reports_pd3_failures_after_the_data_write);
    RUN_TEST(test_write_failure_of_a_coupled_block_leaves_pd3_alone);
    RUN_TEST(test_write_uncoupled_register_leaves_pd3_alone);
    RUN_TEST(test_write_full_transmit_source_caps_with_the_longest_id);
    RUN_TEST(test_write_int_clear_reads_back_as_a_clear_request);

    RUN_TEST(test_task_runs_and_returns_data);
    RUN_TEST(test_task_passes_the_payload_through_data);
    RUN_TEST(test_task_reports_a_task_level_return_code);
    RUN_TEST(test_task_reports_an_unknown_code_answer);
    RUN_TEST(test_task_times_out_when_command_never_clears);
    RUN_TEST(test_task_reports_an_i2c_failure);
    RUN_TEST(test_task_refuses_what_the_allowlist_refuses);

    RUN_TEST(test_restart_warm_reports_each_step);
    RUN_TEST(test_restart_of_a_controller_that_reboots_at_once);
    RUN_TEST(test_restart_cold_reloads_the_patch);
    RUN_TEST(test_restart_cold_without_a_bundle_is_refused_on_an_eeprom_less_board);
    RUN_TEST(test_restart_cold_without_a_bundle_needs_boot_flags);
    RUN_TEST(test_restart_cold_without_a_bundle_runs_on_an_eeprom_board);
    RUN_TEST(test_restart_through_cmd_runs_the_same_sequence);
    RUN_TEST(test_restart_reports_a_rejected_task);
    RUN_TEST(test_restart_fails_when_the_marker_cannot_be_written);
    RUN_TEST(test_restart_without_evidence_times_out);
    RUN_TEST(test_restart_accepts_a_nak_window_as_evidence);
    RUN_TEST(test_restart_accepts_a_cleared_command_as_evidence);
    RUN_TEST(test_restart_fails_when_the_controller_stays_silent);
    RUN_TEST(test_restart_keeps_waiting_while_command_holds_another_value);
    RUN_TEST(test_restart_fails_fast_when_the_command_write_fails);
    RUN_TEST(test_restart_fails_fast_on_a_late_rejection);
    RUN_TEST(test_restart_survives_one_failed_poll_after_the_write);
    RUN_TEST(test_restart_treats_an_unreadable_data_register_as_no_evidence);
    RUN_TEST(test_restart_into_ptch_without_a_bundle);
    RUN_TEST(test_restart_reports_a_failed_patch_load);
    RUN_TEST(test_restart_succeeds_when_the_controller_reaches_app_while_the_loader_waits);
    RUN_TEST(test_restart_succeeds_when_the_loader_finds_app_in_its_mode_check);
    RUN_TEST(test_restart_fails_when_a_load_that_wrote_ends_with_mode_app);
    RUN_TEST(test_restart_fails_when_a_rejected_load_ends_with_mode_app);
    RUN_TEST(test_restart_reports_a_controller_stuck_in_boot);
    RUN_TEST(test_restart_waits_for_mode_after_the_controller_answers);
    RUN_TEST(test_restart_waits_through_an_empty_mode_and_boot);
    RUN_TEST(test_restart_cold_waits_for_mode_too);
    RUN_TEST(test_restart_reloads_the_patch_after_waiting_for_mode);
    RUN_TEST(test_restart_times_out_when_mode_never_comes_back);
    RUN_TEST(test_restart_times_out_on_a_mode_that_stays_empty);
    RUN_TEST(test_restart_times_out_when_mode_stops_answering_in_boot);
    RUN_TEST(test_restart_reports_a_mode_that_fails_after_it_was_running);
    RUN_TEST(test_restart_reports_a_controller_that_falls_back_to_boot);
    RUN_TEST(test_patch_wait_tolerates_a_failed_mode_read);

    RUN_TEST(test_poll_answers_a_line_once_it_is_complete);
    RUN_TEST(test_poll_answers_several_lines_and_ignores_blank_ones);
    RUN_TEST(test_poll_with_no_input_does_nothing);
    RUN_TEST(test_poll_rejects_a_line_that_is_too_long);
    RUN_TEST(test_poll_reports_a_long_line_without_an_id_as_an_event);
    RUN_TEST(test_poll_rejects_a_line_with_a_nul_byte);
    RUN_TEST(test_refused_lines_reach_the_log_stream);
    RUN_TEST(test_debug_output_on_the_console_stream_never_splits_a_reply_line);
    RUN_TEST(test_line_without_an_id_is_reported_as_an_event);
    RUN_TEST(test_malformed_requests_with_an_id_get_an_error_reply);
    RUN_TEST(test_handle_line_ignores_null_and_blank);
    RUN_TEST(test_begin_announces_the_console);
    RUN_TEST(test_begin_loads_the_patch_and_reports_it);
    RUN_TEST(test_begin_reports_ptch_without_a_bundle);
    RUN_TEST(test_begin_reports_a_failed_patch_load);
    RUN_TEST(test_begin_waits_for_a_controller_that_boots_from_eeprom);
    RUN_TEST(test_begin_leaves_a_ptch_without_ready_for_patch_to_the_loader);
    RUN_TEST(test_begin_reports_a_controller_that_reaches_app_while_the_loader_waits);
    RUN_TEST(test_begin_reports_a_load_that_wrote_and_ended_with_mode_app);
    RUN_TEST(test_patch_bundle_can_be_given_to_the_constructor);
    RUN_TEST(test_log_stream_gets_one_line_per_request);

    return UNITY_END();
}
