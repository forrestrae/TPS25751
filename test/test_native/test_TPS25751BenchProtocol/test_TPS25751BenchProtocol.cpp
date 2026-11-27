/**
 * Native unit tests for TPS25751BenchProtocol: the request parser, the task and
 * write allowlists, and the reply formatting of the bench console's line
 * protocol, version 1. Nothing here touches I2C; the console suite covers the
 * commands against a simulated controller.
 */

#include <cstdio>
#include <cstring>
#include <string>

#include <unity.h>

#include "FakeStream.h"
#include "TPS25751BenchProtocol.h"

namespace {

using Protocol = TPS25751BenchProtocol;
using Command = TPS25751BenchCommand;
using Error = TPS25751BenchError;
using Policy = TPS25751BenchTaskPolicy;

TPS25751BenchRequest request;

Error parse(const char *line)
{
    return Protocol::parse(line, request);
}

std::string taskOf(const TPS25751BenchRequest &r)
{
    return std::string(r.task.code, 4);
}

Policy policyOf(const char (&code)[5], bool allowSwaps = false)
{
    return Protocol::taskPolicy(TPS25751FourCC::of(code), allowSwaps);
}

std::string flashText(const __FlashStringHelper *text)
{
    return std::string(reinterpret_cast<const char *>(text));
}

}  // namespace

void setUp() { request = TPS25751BenchRequest(); }
void tearDown() {}

// ---------------------------------------------------------------------------
// Parsing: the seven commands
// ---------------------------------------------------------------------------

void test_parse_hello()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@1 HELLO"));
    TEST_ASSERT_EQUAL(Command::Hello, request.command);
    TEST_ASSERT_EQUAL_STRING("1", request.id);
}

void test_parse_snap()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@abc123 SNAP"));
    TEST_ASSERT_EQUAL(Command::Snapshot, request.command);
    TEST_ASSERT_EQUAL_STRING("abc123", request.id);
}

void test_parse_read_address_forms()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@2 RD 0x34"));
    TEST_ASSERT_EQUAL(Command::Read, request.command);
    TEST_ASSERT_EQUAL_HEX8(0x34, request.address);

    // The prefix is optional and the digits are always hex.
    TEST_ASSERT_EQUAL(Error::None, parse("@2 RD 34"));
    TEST_ASSERT_EQUAL_HEX8(0x34, request.address);
    TEST_ASSERT_EQUAL(Error::None, parse("@2 RD 0X6a"));
    TEST_ASSERT_EQUAL_HEX8(0x6A, request.address);
    TEST_ASSERT_EQUAL(Error::None, parse("@2 RD 3"));
    TEST_ASSERT_EQUAL_HEX8(0x03, request.address);
    TEST_ASSERT_EQUAL(Error::None, parse("@2 RD 0xFF"));
    TEST_ASSERT_EQUAL_HEX8(0xFF, request.address);
}

void test_parse_decode()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@3 DEC 0x1A"));
    TEST_ASSERT_EQUAL(Command::Decode, request.command);
    TEST_ASSERT_EQUAL_HEX8(0x1A, request.address);
}

void test_parse_task_without_payload()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@4 CMD GSrC"));
    TEST_ASSERT_EQUAL(Command::Task, request.command);
    TEST_ASSERT_EQUAL_STRING("GSrC", taskOf(request).c_str());
    TEST_ASSERT_EQUAL(0, request.payloadLength);
}

void test_parse_task_with_payload()
{
    // I2Cr input: target 0x6B, register 0x1B, 2 bytes.
    TEST_ASSERT_EQUAL(Error::None, parse("@4 CMD I2Cr 6b1B02"));
    TEST_ASSERT_EQUAL_STRING("I2Cr", taskOf(request).c_str());
    TEST_ASSERT_EQUAL(3, request.payloadLength);
    const uint8_t expected[] = {0x6B, 0x1B, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, request.payload, 3);
}

void test_parse_task_code_is_case_sensitive()
{
    // 'Gaid' is the warm restart and 'GAID' the cold one, so case must survive.
    TEST_ASSERT_EQUAL(Error::None, parse("@4 cmd Gaid"));
    TEST_ASSERT_EQUAL_STRING("Gaid", taskOf(request).c_str());
    TEST_ASSERT_EQUAL(Error::None, parse("@4 cmd GAID"));
    TEST_ASSERT_EQUAL_STRING("GAID", taskOf(request).c_str());
}

void test_parse_task_accepts_full_data_register()
{
    std::string line = "@4 CMD GPPI ";
    for (int i = 0; i < 64; ++i) line += "a5";
    TEST_ASSERT_EQUAL(Error::None, parse(line.c_str()));
    TEST_ASSERT_EQUAL(64, request.payloadLength);
    TEST_ASSERT_EQUAL_HEX8(0xA5, request.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0xA5, request.payload[63]);
    // The longest valid line fits the console's buffer with an 8-character id.
    TEST_ASSERT_TRUE(line.size() + 7 <= Protocol::kMaxLineLength);
}

void test_parse_write()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@5 WR 0x42 1a000800"));
    TEST_ASSERT_EQUAL(Command::Write, request.command);
    TEST_ASSERT_EQUAL_HEX8(0x42, request.address);
    TEST_ASSERT_EQUAL(4, request.payloadLength);
    const uint8_t expected[] = {0x1A, 0x00, 0x08, 0x00};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, request.payload, 4);
}

void test_parse_restart()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@6 RESTART warm"));
    TEST_ASSERT_EQUAL(Command::Restart, request.command);
    TEST_ASSERT_FALSE(request.cold);

    TEST_ASSERT_EQUAL(Error::None, parse("@6 RESTART cold"));
    TEST_ASSERT_TRUE(request.cold);

    TEST_ASSERT_EQUAL(Error::None, parse("@6 restart COLD"));
    TEST_ASSERT_TRUE(request.cold);
}

// ---------------------------------------------------------------------------
// Parsing: tolerance
// ---------------------------------------------------------------------------

void test_parse_ignores_case_of_command_word()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@1 hello"));
    TEST_ASSERT_EQUAL(Command::Hello, request.command);
    TEST_ASSERT_EQUAL(Error::None, parse("@1 Snap"));
    TEST_ASSERT_EQUAL(Command::Snapshot, request.command);
    TEST_ASSERT_EQUAL(Error::None, parse("@1 rd 0x03"));
    TEST_ASSERT_EQUAL(Command::Read, request.command);
    TEST_ASSERT_EQUAL(Error::None, parse("@1 dec 0x03"));
    TEST_ASSERT_EQUAL(Command::Decode, request.command);
    TEST_ASSERT_EQUAL(Error::None, parse("@1 wr 0x42 00000000"));
    TEST_ASSERT_EQUAL(Command::Write, request.command);
}

void test_parse_ignores_surrounding_whitespace_and_carriage_return()
{
    TEST_ASSERT_EQUAL(Error::None, parse("  @7   RD   0x34  \r"));
    TEST_ASSERT_EQUAL(Command::Read, request.command);
    TEST_ASSERT_EQUAL_STRING("7", request.id);
    TEST_ASSERT_EQUAL_HEX8(0x34, request.address);

    TEST_ASSERT_EQUAL(Error::None, parse("@7\tHELLO\r"));
    TEST_ASSERT_EQUAL(Command::Hello, request.command);

    TEST_ASSERT_EQUAL(Error::None, parse("@7 CMD GSrC\r"));
    TEST_ASSERT_EQUAL(Error::None, parse("@7 CMD GSrC 00\r"));
    TEST_ASSERT_EQUAL(Error::None, parse("@7 WR 0x37 00 "));
}

void test_parse_accepts_longest_id()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@Ab345678 HELLO"));
    TEST_ASSERT_EQUAL_STRING("Ab345678", request.id);
}

// ---------------------------------------------------------------------------
// Parsing: syntax errors
// ---------------------------------------------------------------------------

void test_parse_rejects_lines_without_an_id()
{
    TEST_ASSERT_EQUAL(Error::Syntax, parse(nullptr));
    TEST_ASSERT_EQUAL(Error::Syntax, parse(""));
    TEST_ASSERT_EQUAL(Error::Syntax, parse("HELLO"));
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@ HELLO"));
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@"));
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@123456789 HELLO"));  // 9 characters
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@a-b HELLO"));
    TEST_ASSERT_EQUAL_STRING("", request.id);
}

void test_parse_rejects_missing_or_malformed_command_word()
{
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@1"));
    TEST_ASSERT_EQUAL_STRING("1", request.id);
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@1 "));
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@1 ?"));
    TEST_ASSERT_EQUAL(Error::Syntax, parse("@1 RD=0x34"));
    TEST_ASSERT_EQUAL_STRING("1", request.id);
}

void test_parse_keeps_the_id_when_a_later_part_fails()
{
    TEST_ASSERT_EQUAL(Error::UnknownCommand, parse("@42 BOGUS"));
    TEST_ASSERT_EQUAL_STRING("42", request.id);
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@43 RD"));
    TEST_ASSERT_EQUAL_STRING("43", request.id);
}

void test_parse_rejects_unknown_commands()
{
    TEST_ASSERT_EQUAL(Error::UnknownCommand, parse("@1 READ 0x34"));
    TEST_ASSERT_EQUAL(Error::UnknownCommand, parse("@1 HELLOO"));
    TEST_ASSERT_EQUAL(Error::UnknownCommand, parse("@1 R"));
    TEST_ASSERT_EQUAL(Error::UnknownCommand, parse("@1 RESTARTING"));  // over 8 characters
}

void test_parse_rejects_extra_arguments()
{
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 HELLO now"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 SNAP 0x30"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RD 0x34 0x35"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RESTART warm cold"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD GSrC 00 11"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 WR 0x37 00 11"));
}

void test_parse_rejects_bad_addresses()
{
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RD"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RD 0x"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RD 0x134"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RD 3g"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RD zz"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 DEC"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 WR"));
}

void test_parse_rejects_bad_task_codes()
{
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD GSr"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD GSrCx"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD G\x01rC"));
}

void test_parse_rejects_bad_hex()
{
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD I2Cr 6b1"));     // odd digits
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD I2Cr 6b1g"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 CMD I2Cr 0x6b"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 WR 0x37"));          // no data
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 WR 0x37 0"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 WR 0x37 zz"));

    std::string line = "@1 CMD GPPI ";
    for (int i = 0; i < 65; ++i) line += "00";  // one byte more than DATA holds
    TEST_ASSERT_EQUAL(Error::BadArgument, parse(line.c_str()));
}

void test_parse_rejects_bad_restart_modes()
{
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RESTART"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RESTART hot"));
    TEST_ASSERT_EQUAL(Error::BadArgument, parse("@1 RESTART warmer"));
}

void test_parse_resets_the_request_between_lines()
{
    TEST_ASSERT_EQUAL(Error::None, parse("@1 WR 0x42 01020304"));
    TEST_ASSERT_EQUAL(Error::None, parse("@2 HELLO"));
    TEST_ASSERT_EQUAL(0, request.payloadLength);
    TEST_ASSERT_EQUAL_HEX8(0, request.address);
    TEST_ASSERT_FALSE(request.cold);
}

// ---------------------------------------------------------------------------
// Task allowlist (SLVUCR8B section 4)
// ---------------------------------------------------------------------------

void test_policy_allows_the_listed_tasks()
{
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("GSrC"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("GSkC"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("ANeg"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SSrC"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("GPPI"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("MBRd"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SRDY"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SRYR"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("DBfg"));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("I2Cr"));
}

void test_policy_routes_restart_tasks_to_the_restart_sequence()
{
    TEST_ASSERT_EQUAL(Policy::Restart, policyOf("Gaid"));
    TEST_ASSERT_EQUAL(Policy::Restart, policyOf("GAID"));
    // The swap flag does not change them.
    TEST_ASSERT_EQUAL(Policy::Restart, policyOf("GAID", true));
}

void test_policy_refuses_swaps_without_the_build_flag()
{
    TEST_ASSERT_EQUAL(Policy::RefusedSwap, policyOf("SWSk", false));
    TEST_ASSERT_EQUAL(Policy::RefusedSwap, policyOf("SWSr", false));
    TEST_ASSERT_EQUAL(Policy::RefusedSwap, policyOf("SWDF", false));
    TEST_ASSERT_EQUAL(Policy::RefusedSwap, policyOf("SWUF", false));
}

void test_policy_allows_swaps_with_the_build_flag()
{
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SWSk", true));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SWSr", true));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SWDF", true));
    TEST_ASSERT_EQUAL(Policy::Allowed, policyOf("SWUF", true));
}

void test_policy_default_follows_the_build()
{
    // The native build does not define TPS25751_BENCH_ALLOW_SWAPS.
    TEST_ASSERT_FALSE(TPS25751_BENCH_SWAPS_ALLOWED);
    TEST_ASSERT_EQUAL(Policy::RefusedSwap, Protocol::taskPolicy(TPS25751FourCC::of("SWSk")));
}

void test_policy_refuses_patch_eeprom_and_downstream_write_tasks()
{
    TEST_ASSERT_EQUAL(Policy::RefusedPatch, policyOf("PBMs"));
    TEST_ASSERT_EQUAL(Policy::RefusedPatch, policyOf("PBMc"));
    TEST_ASSERT_EQUAL(Policy::RefusedPatch, policyOf("PBMe"));
    TEST_ASSERT_EQUAL(Policy::RefusedPatchMode, policyOf("GO2P"));
    TEST_ASSERT_EQUAL(Policy::RefusedFlash, policyOf("FLrd"));
    TEST_ASSERT_EQUAL(Policy::RefusedFlash, policyOf("FLad"));
    TEST_ASSERT_EQUAL(Policy::RefusedFlash, policyOf("FLwd"));
    TEST_ASSERT_EQUAL(Policy::RefusedFlash, policyOf("FLvy"));
    TEST_ASSERT_EQUAL(Policy::RefusedDownstreamWrite, policyOf("I2Cw"));
    // The flag that allows swaps allows nothing else.
    TEST_ASSERT_EQUAL(Policy::RefusedPatch, policyOf("PBMs", true));
    TEST_ASSERT_EQUAL(Policy::RefusedDownstreamWrite, policyOf("I2Cw", true));
}

void test_policy_refuses_everything_else()
{
    TEST_ASSERT_EQUAL(Policy::RefusedUnlisted, policyOf("GPsh"));
    TEST_ASSERT_EQUAL(Policy::RefusedUnlisted, policyOf("GPsl"));
    TEST_ASSERT_EQUAL(Policy::RefusedUnlisted, policyOf("!CMD"));
    TEST_ASSERT_EQUAL(Policy::RefusedUnlisted, policyOf("gsrc"));  // wrong case
    TEST_ASSERT_EQUAL(Policy::RefusedUnlisted, policyOf("GSRC"));
    TEST_ASSERT_EQUAL(Policy::RefusedUnlisted, Protocol::taskPolicy(TPS25751_CMD_CLEAR));
}

void test_refusal_text_names_each_reason()
{
    TEST_ASSERT_EQUAL_STRING("", flashText(Protocol::refusalText(Policy::Allowed)).c_str());
    TEST_ASSERT_EQUAL_STRING("", flashText(Protocol::refusalText(Policy::Restart)).c_str());

    const Policy refusals[] = {Policy::RefusedPatch, Policy::RefusedPatchMode, Policy::RefusedFlash,
                               Policy::RefusedDownstreamWrite, Policy::RefusedSwap,
                               Policy::RefusedUnlisted};
    for (size_t i = 0; i < sizeof(refusals) / sizeof(refusals[0]); ++i) {
        const std::string text = flashText(Protocol::refusalText(refusals[i]));
        TEST_ASSERT_TRUE(text.size() > 10);
        for (size_t j = 0; j < i; ++j) {
            TEST_ASSERT_TRUE(text != flashText(Protocol::refusalText(refusals[j])));
        }
    }
    TEST_ASSERT_NOT_NULL(strstr(flashText(Protocol::refusalText(Policy::RefusedSwap)).c_str(),
                                "TPS25751_BENCH_ALLOW_SWAPS"));
}

void test_task_timeouts()
{
    TEST_ASSERT_EQUAL_UINT32(2000, Protocol::taskTimeoutMs(TPS25751FourCC::of("GSrC")));
    TEST_ASSERT_EQUAL_UINT32(2000, Protocol::taskTimeoutMs(TPS25751FourCC::of("GSkC")));
    TEST_ASSERT_EQUAL_UINT32(2000, Protocol::taskTimeoutMs(TPS25751FourCC::of("ANeg")));
    TEST_ASSERT_EQUAL_UINT32(2000, Protocol::taskTimeoutMs(TPS25751FourCC::of("SSrC")));
    TEST_ASSERT_EQUAL_UINT32(2000, Protocol::taskTimeoutMs(TPS25751FourCC::of("SWSk")));
    TEST_ASSERT_EQUAL_UINT32(2000, Protocol::taskTimeoutMs(TPS25751FourCC::of("SWUF")));
    TEST_ASSERT_EQUAL_UINT32(5000, Protocol::taskTimeoutMs(TPS25751FourCC::of("GPPI")));
    TEST_ASSERT_EQUAL_UINT32(1000, Protocol::taskTimeoutMs(TPS25751FourCC::of("MBRd")));
    TEST_ASSERT_EQUAL_UINT32(1000, Protocol::taskTimeoutMs(TPS25751FourCC::of("SRDY")));
    TEST_ASSERT_EQUAL_UINT32(1000, Protocol::taskTimeoutMs(TPS25751FourCC::of("I2Cr")));
}

void test_same_task()
{
    TEST_ASSERT_TRUE(Protocol::sameTask(TPS25751FourCC::of("GAID"), TPS25751FourCC::of("GAID")));
    TEST_ASSERT_FALSE(Protocol::sameTask(TPS25751FourCC::of("GAID"), TPS25751FourCC::of("Gaid")));
}

// ---------------------------------------------------------------------------
// Write allowlist and snapshot list
// ---------------------------------------------------------------------------

// The sixteen registers WR may write, in address order (isWriteAllowed()).
const uint8_t kAllowed[] = {0x16, 0x18, 0x27, 0x28, 0x29, 0x32, 0x33, 0x37,
                            0x42, 0x70, 0x77, 0x7A, 0x7B, 0x7D, 0x7E, 0x98};

void test_write_allowlist_is_exactly_the_sixteen_registers()
{
    for (uint8_t address : kAllowed) {
        TEST_ASSERT_TRUE_MESSAGE(Protocol::isWriteAllowed(address), std::to_string(address).c_str());
    }
    unsigned allowed = 0;
    for (unsigned address = 0; address <= 0xFF; ++address) {
        if (Protocol::isWriteAllowed(static_cast<uint8_t>(address))) ++allowed;
    }
    TEST_ASSERT_EQUAL(sizeof(kAllowed), allowed);
    // 0x78 waits for a bench probe; the others are neighbors and the registers
    // a careless write would hurt most.
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x78));  // TRANSMIT_SOURCE_INFO
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x08));  // COMMAND
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x09));  // DATA
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x14));  // INT_EVENT1
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x2A));  // above PORT_CONTROL
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x2D));  // BOOT_FLAGS
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x5C));  // IO_CONFIG
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x79));
    TEST_ASSERT_FALSE(Protocol::isWriteAllowed(0x7C));
}

void test_write_refusal_text_lists_the_allowlist()
{
    // Built from isWriteAllowed(), so the text cannot drift from the table.
    std::string list;
    for (unsigned address = 0; address <= 0xFF; ++address) {
        if (!Protocol::isWriteAllowed(static_cast<uint8_t>(address))) continue;
        char hex[8];
        snprintf(hex, sizeof(hex), "0x%02x", address);
        if (!list.empty()) list += ", ";
        list += hex;
    }
    TEST_ASSERT_EQUAL_STRING(("register is not on the write allowlist (" + list + ")").c_str(),
                             flashText(Protocol::writeRefusalText()).c_str());
}

void test_pd3_enable_bit_of_each_coupled_register()
{
    // PD3_CONFIGURATION (0x42), SLVUCR8B Table 3-26, p.46.
    TEST_ASSERT_EQUAL(8, Protocol::pd3EnableBit(0x77));   // Support Source Extended Message
    TEST_ASSERT_EQUAL(20, Protocol::pd3EnableBit(0x7A));  // Support PPS Status
    TEST_ASSERT_EQUAL(11, Protocol::pd3EnableBit(0x7B));  // Support Battery Status Message
    TEST_ASSERT_EQUAL(10, Protocol::pd3EnableBit(0x7D));  // Support Battery Capabilities Message
    TEST_ASSERT_EQUAL(17, Protocol::pd3EnableBit(0x7E));  // Support Sink Cap Extended

    // 0x78 (bit 18) is not coupled until it is on the allowlist.
    TEST_ASSERT_EQUAL(Protocol::kNoPd3EnableBit, Protocol::pd3EnableBit(0x78));
    TEST_ASSERT_EQUAL(Protocol::kNoPd3EnableBit, Protocol::pd3EnableBit(0x42));
    TEST_ASSERT_EQUAL(Protocol::kNoPd3EnableBit, Protocol::pd3EnableBit(0x32));
    TEST_ASSERT_EQUAL(Protocol::kNoPd3EnableBit, Protocol::pd3EnableBit(0x70));
    TEST_ASSERT_EQUAL(Protocol::kNoPd3EnableBit, Protocol::pd3EnableBit(0x00));
    TEST_ASSERT_EQUAL(Protocol::kNoPd3EnableBit, Protocol::pd3EnableBit(0xFF));

    // Exactly five coupled registers, each one writable.
    unsigned coupled = 0;
    for (unsigned address = 0; address <= 0xFF; ++address) {
        if (Protocol::pd3EnableBit(static_cast<uint8_t>(address)) == Protocol::kNoPd3EnableBit) continue;
        ++coupled;
        TEST_ASSERT_TRUE(Protocol::isWriteAllowed(static_cast<uint8_t>(address)));
    }
    TEST_ASSERT_EQUAL(5, coupled);
}

void test_parse_write_of_the_longest_register_with_the_longest_id()
{
    // TRANSMIT_SOURCE_CAPABILITIES (0x32) is 63 bytes: "@12345678 WR 0x32 "
    // (18 characters) and 126 hex digits make 144, under kMaxLineLength.
    std::string hex;
    for (unsigned i = 0; i < 63; ++i) {
        char pair[3];
        snprintf(pair, sizeof(pair), "%02x", i);
        hex += pair;
    }
    const std::string line = "@12345678 WR 0x32 " + hex;
    TEST_ASSERT_EQUAL(144, line.size());
    TEST_ASSERT_TRUE(line.size() <= Protocol::kMaxLineLength);
    TEST_ASSERT_EQUAL(Error::None, parse(line.c_str()));
    TEST_ASSERT_EQUAL(Command::Write, request.command);
    TEST_ASSERT_EQUAL_HEX8(0x32, request.address);
    TEST_ASSERT_EQUAL(63, request.payloadLength);
    TEST_ASSERT_EQUAL_HEX8(0x00, request.payload[0]);
    TEST_ASSERT_EQUAL_HEX8(0x3E, request.payload[62]);
}

void test_snapshot_register_list()
{
    const uint8_t expected[] = {0x03, 0x14, 0x1A, 0x26, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
                                0x37, 0x3F, 0x40, 0x42, 0x48, 0x69, 0x6A};
    TEST_ASSERT_EQUAL(sizeof(expected), Protocol::kSnapshotRegisterCount);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, Protocol::kSnapshotRegisters, sizeof(expected));
}

void test_error_codes()
{
    TEST_ASSERT_EQUAL_STRING("NONE", Protocol::errorCode(Error::None));
    TEST_ASSERT_EQUAL_STRING("SYNTAX", Protocol::errorCode(Error::Syntax));
    TEST_ASSERT_EQUAL_STRING("TOOLONG", Protocol::errorCode(Error::TooLong));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", Protocol::errorCode(Error::UnknownCommand));
    TEST_ASSERT_EQUAL_STRING("BADARG", Protocol::errorCode(Error::BadArgument));
    TEST_ASSERT_EQUAL_STRING("REFUSED", Protocol::errorCode(Error::Refused));
    TEST_ASSERT_EQUAL_STRING("I2C", Protocol::errorCode(Error::I2C));
    TEST_ASSERT_EQUAL_STRING("TIMEOUT", Protocol::errorCode(Error::Timeout));
    TEST_ASSERT_EQUAL_STRING("REJECTED", Protocol::errorCode(Error::Rejected));
    TEST_ASSERT_EQUAL_STRING("NOBUNDLE", Protocol::errorCode(Error::NoBundle));
    TEST_ASSERT_EQUAL_STRING("PATCH", Protocol::errorCode(Error::Patch));
    TEST_ASSERT_EQUAL_STRING("NODECODER", Protocol::errorCode(Error::NoDecoder));
}

// ---------------------------------------------------------------------------
// Reply formatting
// ---------------------------------------------------------------------------

void test_format_ok_and_line_start()
{
    FakeStream out;
    Protocol::writeOk(out, "17");
    TEST_ASSERT_EQUAL_STRING("@17 OK\n", out.take().c_str());

    Protocol::beginLine(out, "17");
    TEST_ASSERT_EQUAL_STRING("@17 ", out.take().c_str());
}

void test_format_missing_id_prints_a_placeholder()
{
    FakeStream out;
    Protocol::beginLine(out, "");
    Protocol::beginLine(out, nullptr);
    TEST_ASSERT_EQUAL_STRING("@? @? ", out.take().c_str());
}

void test_format_error()
{
    FakeStream out;
    Protocol::writeError(out, "9", Error::Refused, F("task is not on the allowlist"));
    TEST_ASSERT_EQUAL_STRING("@9 ERR REFUSED task is not on the allowlist\n", out.take().c_str());

    Protocol::beginError(out, "9", Error::Timeout);
    out.print(2000);
    out.println(F(" ms"));
    TEST_ASSERT_EQUAL_STRING("@9 ERR TIMEOUT 2000 ms\n", out.take().c_str());
}

void test_format_event()
{
    FakeStream out;
    Protocol::writeEvent(out, F("READY proto=1"));
    TEST_ASSERT_EQUAL_STRING("! READY proto=1\n", out.take().c_str());
}

void test_format_hex_is_lowercase_and_zero_padded()
{
    FakeStream out;
    const uint8_t bytes[] = {0x00, 0x0A, 0xD6, 0xFF};
    Protocol::printHex(out, bytes, sizeof(bytes));
    TEST_ASSERT_EQUAL_STRING("000ad6ff", out.take().c_str());

    Protocol::printHex(out, bytes, 0);
    TEST_ASSERT_EQUAL_STRING("", out.take().c_str());
}

void test_format_address()
{
    FakeStream out;
    Protocol::printAddress(out, 0x03);
    Protocol::printAddress(out, 0x6A);
    TEST_ASSERT_EQUAL_STRING("0x030x6a", out.take().c_str());
}

void test_format_task_replaces_unprintable_characters()
{
    FakeStream out;
    Protocol::printTask(out, TPS25751FourCC::of("GSrC"));
    TEST_ASSERT_EQUAL_STRING("GSrC", out.take().c_str());

    Protocol::printTask(out, TPS25751_CMD_CLEAR);
    TEST_ASSERT_EQUAL_STRING("????", out.take().c_str());

    Protocol::printTask(out, TPS25751FourCC{{'A', ' ', '\x7F', 'z'}});
    TEST_ASSERT_EQUAL_STRING("A??z", out.take().c_str());
}

void test_format_register_line()
{
    // ACTIVE_PDO_CONTRACT from the Apple 140 W bundle: 20 V, 4.7 A.
    FakeStream out;
    const uint8_t data[] = {0xD6, 0x41, 0x06, 0x00, 0x98, 0x00};
    Protocol::printRegister(out, 0x34, 6, data, sizeof(data));
    TEST_ASSERT_EQUAL_STRING("reg=0x34 len=6 data=d64106009800", out.take().c_str());
}

void test_format_register_line_keeps_the_length_byte_as_read()
{
    // A length byte that differs from the bytes returned is printed as read.
    FakeStream out;
    const uint8_t data[] = {0x01, 0x02};
    Protocol::printRegister(out, 0x27, 14, data, sizeof(data));
    TEST_ASSERT_EQUAL_STRING("reg=0x27 len=14 data=0102", out.take().c_str());

    Protocol::printRegister(out, 0x73, 0, data, 0);
    TEST_ASSERT_EQUAL_STRING("reg=0x73 len=0 data=", out.take().c_str());
}

// ---------------------------------------------------------------------------
// Line prefixer
// ---------------------------------------------------------------------------

void test_prefixer_prefixes_every_line()
{
    FakeStream out;
    TPS25751BenchLinePrefixer lines(out, "5");
    lines.println(F("========"));
    lines.print(F("Mode: "));
    lines.println(F("APP"));
    lines.finish();
    TEST_ASSERT_EQUAL_STRING("@5 | ========\n@5 | Mode: APP\n", out.take().c_str());
}

void test_prefixer_keeps_blank_lines_and_drops_carriage_returns()
{
    FakeStream out;
    TPS25751BenchLinePrefixer lines(out, "5");
    lines.print("one\r\n\r\ntwo\r\n");
    TEST_ASSERT_EQUAL_STRING("@5 | one\n@5 |\n@5 | two\n", out.take().c_str());
}

void test_prefixer_finish_ends_an_open_line_once()
{
    FakeStream out;
    TPS25751BenchLinePrefixer lines(out, "5");
    lines.print("no newline");
    lines.finish();
    lines.finish();
    TEST_ASSERT_EQUAL_STRING("@5 | no newline\n", out.take().c_str());
}

void test_prefixer_prints_nothing_without_output()
{
    FakeStream out;
    TPS25751BenchLinePrefixer lines(out, "5");
    lines.finish();
    TEST_ASSERT_EQUAL_STRING("", out.take().c_str());
    // It is an output-only stream.
    TEST_ASSERT_EQUAL(0, lines.available());
    TEST_ASSERT_EQUAL(-1, lines.read());
    TEST_ASSERT_EQUAL(-1, lines.peek());
}

void test_prefixer_formats_numbers_through_print()
{
    FakeStream out;
    TPS25751BenchLinePrefixer lines(out, "5");
    lines.print(F("Voltage: "));
    lines.print(20000);
    lines.print(F(" mV, raw 0x"));
    lines.println(0x190, HEX);
    TEST_ASSERT_EQUAL_STRING("@5 | Voltage: 20000 mV, raw 0x190\n", out.take().c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_parse_hello);
    RUN_TEST(test_parse_snap);
    RUN_TEST(test_parse_read_address_forms);
    RUN_TEST(test_parse_decode);
    RUN_TEST(test_parse_task_without_payload);
    RUN_TEST(test_parse_task_with_payload);
    RUN_TEST(test_parse_task_code_is_case_sensitive);
    RUN_TEST(test_parse_task_accepts_full_data_register);
    RUN_TEST(test_parse_write);
    RUN_TEST(test_parse_restart);

    RUN_TEST(test_parse_ignores_case_of_command_word);
    RUN_TEST(test_parse_ignores_surrounding_whitespace_and_carriage_return);
    RUN_TEST(test_parse_accepts_longest_id);

    RUN_TEST(test_parse_rejects_lines_without_an_id);
    RUN_TEST(test_parse_rejects_missing_or_malformed_command_word);
    RUN_TEST(test_parse_keeps_the_id_when_a_later_part_fails);
    RUN_TEST(test_parse_rejects_unknown_commands);
    RUN_TEST(test_parse_rejects_extra_arguments);
    RUN_TEST(test_parse_rejects_bad_addresses);
    RUN_TEST(test_parse_rejects_bad_task_codes);
    RUN_TEST(test_parse_rejects_bad_hex);
    RUN_TEST(test_parse_rejects_bad_restart_modes);
    RUN_TEST(test_parse_resets_the_request_between_lines);

    RUN_TEST(test_policy_allows_the_listed_tasks);
    RUN_TEST(test_policy_routes_restart_tasks_to_the_restart_sequence);
    RUN_TEST(test_policy_refuses_swaps_without_the_build_flag);
    RUN_TEST(test_policy_allows_swaps_with_the_build_flag);
    RUN_TEST(test_policy_default_follows_the_build);
    RUN_TEST(test_policy_refuses_patch_eeprom_and_downstream_write_tasks);
    RUN_TEST(test_policy_refuses_everything_else);
    RUN_TEST(test_refusal_text_names_each_reason);
    RUN_TEST(test_task_timeouts);
    RUN_TEST(test_same_task);

    RUN_TEST(test_write_allowlist_is_exactly_the_sixteen_registers);
    RUN_TEST(test_write_refusal_text_lists_the_allowlist);
    RUN_TEST(test_pd3_enable_bit_of_each_coupled_register);
    RUN_TEST(test_parse_write_of_the_longest_register_with_the_longest_id);
    RUN_TEST(test_snapshot_register_list);
    RUN_TEST(test_error_codes);

    RUN_TEST(test_format_ok_and_line_start);
    RUN_TEST(test_format_missing_id_prints_a_placeholder);
    RUN_TEST(test_format_error);
    RUN_TEST(test_format_event);
    RUN_TEST(test_format_hex_is_lowercase_and_zero_padded);
    RUN_TEST(test_format_address);
    RUN_TEST(test_format_task_replaces_unprintable_characters);
    RUN_TEST(test_format_register_line);
    RUN_TEST(test_format_register_line_keeps_the_length_byte_as_read);

    RUN_TEST(test_prefixer_prefixes_every_line);
    RUN_TEST(test_prefixer_keeps_blank_lines_and_drops_carriage_returns);
    RUN_TEST(test_prefixer_finish_ends_an_open_line_once);
    RUN_TEST(test_prefixer_prints_nothing_without_output);
    RUN_TEST(test_prefixer_formats_numbers_through_print);

    return UNITY_END();
}
