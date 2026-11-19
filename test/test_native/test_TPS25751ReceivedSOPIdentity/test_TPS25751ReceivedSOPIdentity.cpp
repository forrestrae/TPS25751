/**
 * @file test_TPS25751ReceivedSOPIdentity.cpp
 * @brief Native (host-run) unit tests for TPS25751ReceivedSOPIdentity.
 *
 * Run with: pio test -e native
 *
 * RECEIVED_SOP_IDENTITY (0x48) is a read-only 25-byte register. Byte 0 holds
 * Number Valid VDOs (bits 2:0), reserved bits 5:3, and Response Type (7:6).
 * VDO n (1-based) is a little-endian 32-bit word at bytes 4n-3 .. 4n.
 */

#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "TPS25751ReceivedSOPIdentity.h"
#include "TPS25751Register.h"
#include "TPS25751RegisterAddress.h"

namespace {

using RT = TPS25751ReceivedSOPIdentity::ResponseType;

constexpr size_t SOP_SIZE = TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY.size;  // 25

void putVdo(uint8_t *buf, unsigned index, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) buf[1 + 4 * index + i] = static_cast<uint8_t>(value >> (8 * i));
}

// Buffer with byte 0 and the first `n` VDOs set from `vdos`.
TPS25751ReceivedSOPIdentity make(uint8_t byte0, const uint32_t *vdos, unsigned n)
{
    uint8_t buf[SOP_SIZE] = {0};
    buf[0] = byte0;
    for (unsigned i = 0; i < n; i++) putVdo(buf, i, vdos[i]);
    return TPS25751ReceivedSOPIdentity(buf);
}

std::string captureDebugPrint(const TPS25751ReceivedSOPIdentity &reg)
{
    fflush(stdout);
    FILE *tmp = tmpfile();
    const int saved = dup(fileno(stdout));
    dup2(fileno(tmp), fileno(stdout));

    reg.debugPrint();

    fflush(stdout);
    dup2(saved, fileno(stdout));
    close(saved);

    std::string out;
    rewind(tmp);
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), tmp)) > 0) out.append(buf, n);
    fclose(tmp);
    return out;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Identity / construction -----------------------------------------------------

void test_register_info()
{
    TEST_ASSERT_EQUAL_UINT8(25, TPS25751Registers::getRegisterSize(TPS25751Registers::Address::RECEIVED_SOP_IDENTITY));
    TEST_ASSERT_EQUAL_UINT8(0x48, static_cast<uint8_t>(TPS25751Registers::Address::RECEIVED_SOP_IDENTITY));
    TEST_ASSERT_NOT_NULL(TPS25751Registers::getRegisterInfo(TPS25751Registers::Address::RECEIVED_SOP_IDENTITY));
    TEST_ASSERT_EQUAL_UINT8(6, TPS25751ReceivedSOPIdentity::kMaxVdos);
}

void test_default_constructor_allocates_zeroed_buffer()
{
    TPS25751ReceivedSOPIdentity reg;

    TEST_ASSERT_TRUE(reg.isValid());
    TEST_ASSERT_EQUAL_size_t(SOP_SIZE, reg.size());
    TEST_ASSERT_TRUE(reg.isValidSize(SOP_SIZE));
}

void test_data_constructor_copies_buffer()
{
    uint8_t buf[SOP_SIZE] = {0x43};
    TPS25751ReceivedSOPIdentity reg(buf);

    TEST_ASSERT_NOT_EQUAL(buf, reg.raw());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(buf, reg.raw(), SOP_SIZE);
}

void test_explicit_length_and_reginfo_constructors()
{
    uint8_t buf[SOP_SIZE] = {0x41};
    TPS25751ReceivedSOPIdentity a(buf, SOP_SIZE);
    TPS25751ReceivedSOPIdentity b(TPS25751Registers::Registers::RECEIVED_SOP_IDENTITY);

    TEST_ASSERT_TRUE(a.isValidSize(SOP_SIZE));
    TEST_ASSERT_EQUAL_UINT8(1, a.numberValidVdos());
    TEST_ASSERT_EQUAL_size_t(SOP_SIZE, b.size());
}

void test_wrong_size_fails_size_validation()
{
    const uint8_t undersized[5] = {0};
    TPS25751ReceivedSOPIdentity reg(undersized, sizeof(undersized));

    TEST_ASSERT_FALSE(reg.isValidSize(SOP_SIZE));
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

// --- Validation ------------------------------------------------------------------

void test_all_ones_is_invalid()
{
    uint8_t buf[SOP_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    TPS25751ReceivedSOPIdentity reg(buf);

    TEST_ASSERT_FALSE(reg.hasValidData());
    TEST_ASSERT_FALSE(reg.isSemanticallyValid());
}

void test_all_zero_reset_is_valid()
{
    TPS25751ReceivedSOPIdentity reg;

    TEST_ASSERT_TRUE(reg.hasValidData());
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
    TEST_ASSERT_TRUE(reg.responseType() == RT::NotSentOrPending);
    TEST_ASSERT_EQUAL_UINT8(0, reg.numberValidVdos());
    TEST_ASSERT_EQUAL_UINT32(0, reg.idHeaderVdo());
}

void test_count_seven_invalid_count_six_valid()
{
    TEST_ASSERT_FALSE(make(0x07, nullptr, 0).isSemanticallyValid());
    TEST_ASSERT_TRUE(make(0x06, nullptr, 0).isSemanticallyValid());
}

void test_reserved_bits_invalid()
{
    TEST_ASSERT_FALSE(make(0x08, nullptr, 0).isSemanticallyValid());  // bit 3
    TEST_ASSERT_FALSE(make(0x10, nullptr, 0).isSemanticallyValid());  // bit 4
    TEST_ASSERT_FALSE(make(0x20, nullptr, 0).isSemanticallyValid());  // bit 5
}

void test_vdos_past_count_not_checked()
{
    const uint32_t v[1] = {0xDEADBEEF};
    uint8_t buf[SOP_SIZE] = {0};
    buf[0] = 0x00;  // count 0, NotSentOrPending
    putVdo(buf, 0, v[0]);
    TPS25751ReceivedSOPIdentity reg(buf);

    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

// --- Decode ----------------------------------------------------------------------

void test_ack_fixture_decodes()
{
    const uint32_t v[3] = {0x12345678u, 0x9ABCDEF0u, 0x0BADF00Du};
    TPS25751ReceivedSOPIdentity reg = make((1 << 6) | 3, v, 3);

    TEST_ASSERT_EQUAL_HEX8(0x43, reg.raw()[0]);
    TEST_ASSERT_TRUE(reg.responseType() == RT::Ack);
    TEST_ASSERT_EQUAL_UINT8(3, reg.numberValidVdos());
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, reg.vdo(0));
    TEST_ASSERT_EQUAL_HEX32(0x9ABCDEF0u, reg.vdo(1));
    TEST_ASSERT_EQUAL_HEX32(0x0BADF00Du, reg.vdo(2));
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, reg.idHeaderVdo());
    TEST_ASSERT_EQUAL_HEX32(0x9ABCDEF0u, reg.certStatVdo());
    TEST_ASSERT_EQUAL_HEX32(0x0BADF00Du, reg.productVdo());
    TEST_ASSERT_EQUAL_HEX32(0, reg.vdo(3));
    TEST_ASSERT_EQUAL_HEX32(0, reg.vdo(4));
    TEST_ASSERT_EQUAL_HEX32(0, reg.vdo(5));
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_six_vdos_decode_including_last()
{
    const uint32_t v[6] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44A44B44u, 0x55C55D55u, 0x66E66F66u};
    TPS25751ReceivedSOPIdentity reg = make((1 << 6) | 6, v, 6);

    for (uint8_t i = 0; i < 6; i++) TEST_ASSERT_EQUAL_HEX32(v[i], reg.vdo(i));
    TEST_ASSERT_EQUAL_HEX8(0x66, reg.raw()[24]);  // top byte of VDO 6 is the last buffer byte
    TEST_ASSERT_TRUE(reg.isSemanticallyValid());
}

void test_each_response_type_decodes()
{
    TEST_ASSERT_TRUE(make(0x00, nullptr, 0).responseType() == RT::NotSentOrPending);
    TEST_ASSERT_TRUE(make(0x40, nullptr, 0).responseType() == RT::Ack);
    TEST_ASSERT_TRUE(make(0x80, nullptr, 0).responseType() == RT::NakOrTimeout);
    TEST_ASSERT_TRUE(make(0xC0, nullptr, 0).responseType() == RT::Busy);

    TEST_ASSERT_EQUAL_STRING("SOP Discover Identity REQ not sent or pending", toString(RT::NotSentOrPending));
    TEST_ASSERT_EQUAL_STRING("Responder ACK received", toString(RT::Ack));
    TEST_ASSERT_EQUAL_STRING("Responder NAK received or response timeout", toString(RT::NakOrTimeout));
    TEST_ASSERT_EQUAL_STRING("Responder BUSY received", toString(RT::Busy));
}

void test_vdo_out_of_range_returns_zero()
{
    const uint32_t v[6] = {1, 2, 3, 4, 5, 6};
    TPS25751ReceivedSOPIdentity reg = make((1 << 6) | 6, v, 6);

    TEST_ASSERT_EQUAL_UINT32(0, reg.vdo(6));
    TEST_ASSERT_EQUAL_UINT32(0, reg.vdo(255));
}

// --- debugPrint ------------------------------------------------------------------

void test_debug_print_shows_only_valid_vdos_zero_padded()
{
    const uint32_t v[3] = {0x00001234u, 0x9ABCDEF0u, 0x0BADF00Du};
    TPS25751ReceivedSOPIdentity reg = make((1 << 6) | 2, v, 3);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(out.find("Responder ACK received") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("ID Header VDO: 0x00001234") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("Cert Stat VDO: 0x9ABCDEF0") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("Product VDO") == std::string::npos);
}

// --- Rule of Five ----------------------------------------------------------------

void test_copy_and_move()
{
    const uint32_t v[1] = {0xCAFEBABEu};
    TPS25751ReceivedSOPIdentity original = make((1 << 6) | 1, v, 1);

    TPS25751ReceivedSOPIdentity copy(original);
    TEST_ASSERT_NOT_EQUAL(original.raw(), copy.raw());
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABEu, copy.vdo(0));

    TPS25751ReceivedSOPIdentity target;
    target = original;
    TEST_ASSERT_NOT_EQUAL(original.raw(), target.raw());
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABEu, target.vdo(0));

    const uint8_t *buf = original.raw();
    TPS25751ReceivedSOPIdentity moved(static_cast<TPS25751ReceivedSOPIdentity &&>(original));
    TEST_ASSERT_EQUAL(buf, moved.raw());
    TEST_ASSERT_FALSE(original.isValid());

    TPS25751ReceivedSOPIdentity target2;
    const uint8_t *buf2 = moved.raw();
    target2 = static_cast<TPS25751ReceivedSOPIdentity &&>(moved);
    TEST_ASSERT_EQUAL(buf2, target2.raw());
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABEu, target2.vdo(0));
}

// --- Runner ----------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_register_info);
    RUN_TEST(test_default_constructor_allocates_zeroed_buffer);
    RUN_TEST(test_data_constructor_copies_buffer);
    RUN_TEST(test_explicit_length_and_reginfo_constructors);
    RUN_TEST(test_wrong_size_fails_size_validation);

    RUN_TEST(test_all_ones_is_invalid);
    RUN_TEST(test_all_zero_reset_is_valid);
    RUN_TEST(test_count_seven_invalid_count_six_valid);
    RUN_TEST(test_reserved_bits_invalid);
    RUN_TEST(test_vdos_past_count_not_checked);

    RUN_TEST(test_ack_fixture_decodes);
    RUN_TEST(test_six_vdos_decode_including_last);
    RUN_TEST(test_each_response_type_decodes);
    RUN_TEST(test_vdo_out_of_range_returns_zero);

    RUN_TEST(test_debug_print_shows_only_valid_vdos_zero_padded);

    RUN_TEST(test_copy_and_move);

    return UNITY_END();
}
