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

// --- Typed VDO decode (USB PD R3.3 Tables 6.34, 6.38, 6.39) ---------------------------

using SOP = TPS25751ReceivedSOPIdentity;

// ID Header VDO: Host 0, Device 1 (bit 30 = 0x40000000), Product Type UFP 010b
// (2 << 27 = 0x10000000), Alternate Modes 1 (bit 26 = 0x04000000), Product Type DFP
// 000b, Connector 10b (2 << 21 = 0x00400000), VID 0x05AC.
// Sum: 0x40000000 + 0x10000000 + 0x04000000 + 0x00400000 + 0x05AC = 0x544005AC.
// Little-endian in bytes 1 to 4: AC 05 40 54.
void test_id_header_fixture_decodes()
{
    const uint32_t v[1] = {0x544005ACu};
    SOP reg = make((1 << 6) | 1, v, 1);

    TEST_ASSERT_EQUAL_HEX8(0xAC, reg.raw()[1]);
    TEST_ASSERT_EQUAL_HEX8(0x05, reg.raw()[2]);
    TEST_ASSERT_EQUAL_HEX8(0x40, reg.raw()[3]);
    TEST_ASSERT_EQUAL_HEX8(0x54, reg.raw()[4]);

    TEST_ASSERT_FALSE(reg.usbHostCapable());
    TEST_ASSERT_TRUE(reg.usbDeviceCapable());
    TEST_ASSERT_TRUE(reg.productTypeUfp() == SOP::ProductTypeUfp::PdusbPeripheral);
    TEST_ASSERT_EQUAL_UINT8(2, reg.rawProductTypeUfp());
    TEST_ASSERT_TRUE(reg.alternateModesSupported());
    TEST_ASSERT_TRUE(reg.productTypeDfp() == SOP::ProductTypeDfp::NotDfp);
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawProductTypeDfp());
    TEST_ASSERT_TRUE(reg.connectorType() == SOP::ConnectorType::TypeCReceptacle);
    TEST_ASSERT_EQUAL_UINT8(2, reg.rawConnectorType());
    TEST_ASSERT_EQUAL_HEX16(0x05AC, reg.usbVendorId());
}

// Host 1 (bit 31 = 0x80000000), Device 0, UFP 000b, Alt Modes 0, DFP 010b
// (2 << 23 = 0x01000000), Connector 11b (3 << 21 = 0x00600000), VID 0x1234.
// Sum: 0x80000000 + 0x01000000 + 0x00600000 + 0x1234 = 0x81601234.
void test_id_header_host_dfp_plug_fixture()
{
    const uint32_t v[1] = {0x81601234u};
    SOP reg = make((1 << 6) | 1, v, 1);

    TEST_ASSERT_TRUE(reg.usbHostCapable());
    TEST_ASSERT_FALSE(reg.usbDeviceCapable());
    TEST_ASSERT_TRUE(reg.productTypeUfp() == SOP::ProductTypeUfp::NotUfp);
    TEST_ASSERT_FALSE(reg.alternateModesSupported());
    TEST_ASSERT_TRUE(reg.productTypeDfp() == SOP::ProductTypeDfp::PdusbHost);
    TEST_ASSERT_TRUE(reg.connectorType() == SOP::ConnectorType::TypeCPlug);
    TEST_ASSERT_EQUAL_HEX16(0x1234, reg.usbVendorId());
}

// Cert Stat VDO is the XID: 0x12345678 in bytes 5 to 8 (78 56 34 12).
void test_cert_stat_xid_fixture()
{
    const uint32_t v[2] = {0, 0x12345678u};
    SOP reg = make((1 << 6) | 2, v, 2);

    TEST_ASSERT_EQUAL_HEX8(0x78, reg.raw()[5]);
    TEST_ASSERT_EQUAL_HEX8(0x12, reg.raw()[8]);
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, reg.xid());
}

// Product VDO: PID 0x12A8 in bits 31:16, bcdDevice 0x0110 in bits 15:0, so the
// VDO is 0x12A80110, in bytes 9 to 12 (10 01 A8 12).
void test_product_vdo_fixture()
{
    const uint32_t v[3] = {0, 0, 0x12A80110u};
    SOP reg = make((1 << 6) | 3, v, 3);

    TEST_ASSERT_EQUAL_HEX8(0x10, reg.raw()[9]);
    TEST_ASSERT_EQUAL_HEX8(0x12, reg.raw()[12]);
    TEST_ASSERT_EQUAL_HEX16(0x12A8, reg.usbProductId());
    TEST_ASSERT_EQUAL_HEX16(0x0110, reg.bcdDevice());
}

void test_product_type_ufp_codes()
{
    // Product Type UFP sits in bits 29:27, so code n is n << 27.
    for (uint32_t code = 0; code < 8; code++)
    {
        const uint32_t v[1] = {code << 27};
        SOP reg = make((1 << 6) | 1, v, 1);
        TEST_ASSERT_EQUAL_UINT8(code, reg.rawProductTypeUfp());
        TEST_ASSERT_EQUAL_UINT8(code, static_cast<uint8_t>(reg.productTypeUfp()));
    }
    TEST_ASSERT_EQUAL_STRING("Not a UFP", toString(SOP::ProductTypeUfp::NotUfp));
    TEST_ASSERT_EQUAL_STRING("PDUSB Hub", toString(SOP::ProductTypeUfp::PdusbHub));
    TEST_ASSERT_EQUAL_STRING("PDUSB Peripheral", toString(SOP::ProductTypeUfp::PdusbPeripheral));
    TEST_ASSERT_EQUAL_STRING("PSD", toString(SOP::ProductTypeUfp::Psd));
    TEST_ASSERT_EQUAL_STRING("Alternate Mode Adapter (AMA, deprecated)", toString(SOP::ProductTypeUfp::Ama));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ProductTypeUfp>(4)));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ProductTypeUfp>(6)));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ProductTypeUfp>(7)));
}

void test_product_type_dfp_codes()
{
    // Product Type DFP sits in bits 25:23, so code n is n << 23.
    for (uint32_t code = 0; code < 8; code++)
    {
        const uint32_t v[1] = {code << 23};
        SOP reg = make((1 << 6) | 1, v, 1);
        TEST_ASSERT_EQUAL_UINT8(code, reg.rawProductTypeDfp());
        TEST_ASSERT_EQUAL_UINT8(code, static_cast<uint8_t>(reg.productTypeDfp()));
    }
    TEST_ASSERT_EQUAL_STRING("Not a DFP", toString(SOP::ProductTypeDfp::NotDfp));
    TEST_ASSERT_EQUAL_STRING("PDUSB Hub", toString(SOP::ProductTypeDfp::PdusbHub));
    TEST_ASSERT_EQUAL_STRING("PDUSB Host", toString(SOP::ProductTypeDfp::PdusbHost));
    TEST_ASSERT_EQUAL_STRING("Power Brick", toString(SOP::ProductTypeDfp::PowerBrick));
    TEST_ASSERT_EQUAL_STRING("Alternate Mode Controller (AMC)", toString(SOP::ProductTypeDfp::AlternateModeController));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ProductTypeDfp>(5)));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ProductTypeDfp>(6)));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ProductTypeDfp>(7)));
}

void test_connector_type_codes()
{
    // Connector Type sits in bits 22:21, so code n is n << 21.
    for (uint32_t code = 0; code < 4; code++)
    {
        const uint32_t v[1] = {code << 21};
        SOP reg = make((1 << 6) | 1, v, 1);
        TEST_ASSERT_EQUAL_UINT8(code, reg.rawConnectorType());
        TEST_ASSERT_EQUAL_UINT8(code, static_cast<uint8_t>(reg.connectorType()));
    }
    TEST_ASSERT_EQUAL_STRING("Unknown (deprecated)", toString(SOP::ConnectorType::Unknown));
    TEST_ASSERT_EQUAL_STRING("USB Type-C Receptacle", toString(SOP::ConnectorType::TypeCReceptacle));
    TEST_ASSERT_EQUAL_STRING("USB Type-C Plug", toString(SOP::ConnectorType::TypeCPlug));
    TEST_ASSERT_EQUAL_STRING("Invalid", toString(static_cast<SOP::ConnectorType>(1)));
}

void test_typed_fields_do_not_leak_across_fields()
{
    // Only Connector set to 11b: no other ID Header field may change.
    const uint32_t v[1] = {3u << 21};
    SOP reg = make((1 << 6) | 1, v, 1);
    TEST_ASSERT_FALSE(reg.usbHostCapable());
    TEST_ASSERT_FALSE(reg.usbDeviceCapable());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawProductTypeUfp());
    TEST_ASSERT_FALSE(reg.alternateModesSupported());
    TEST_ASSERT_EQUAL_UINT8(0, reg.rawProductTypeDfp());
    TEST_ASSERT_EQUAL_HEX16(0, reg.usbVendorId());
}

void test_debug_print_typed_decode_lines()
{
    const uint32_t v[3] = {0x544005ACu, 0x12345678u, 0x12A80110u};
    SOP reg = make((1 << 6) | 3, v, 3);
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(out.find("ID Header VDO: 0x544005AC") != std::string::npos);  // existing line kept
    TEST_ASSERT_TRUE(out.find("ID Header: VID 0x05AC, Host: No, Device: Yes, UFP: PDUSB Peripheral, DFP: Not a DFP, Alt Modes: Yes, Connector: USB Type-C Receptacle") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("Cert Stat: XID 0x12345678") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("Product: PID 0x12A8, bcdDevice 0x0110") != std::string::npos);
}

void test_debug_print_typed_decode_respects_count()
{
    const uint32_t v[3] = {0x544005ACu, 0x12345678u, 0x12A80110u};
    SOP reg = make((1 << 6) | 1, v, 3);  // count 1: only the ID Header is valid
    const std::string out = captureDebugPrint(reg);

    TEST_ASSERT_TRUE(out.find("ID Header: VID 0x05AC") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("Cert Stat:") == std::string::npos);
    TEST_ASSERT_TRUE(out.find("Product:") == std::string::npos);
}

void test_debug_print_typed_decode_skipped_without_ack()
{
    const uint32_t v[1] = {0x544005ACu};
    SOP nak = make((2 << 6) | 1, v, 1);  // NAK/timeout with a stale VDO
    const std::string out = captureDebugPrint(nak);
    TEST_ASSERT_TRUE(out.find("Identity not valid") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("ID Header: VID") == std::string::npos);

    SOP none = make((1 << 6) | 0, v, 1);  // Ack but zero valid VDOs
    const std::string out2 = captureDebugPrint(none);
    TEST_ASSERT_TRUE(out2.find("Identity not valid") != std::string::npos);
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

    RUN_TEST(test_id_header_fixture_decodes);
    RUN_TEST(test_id_header_host_dfp_plug_fixture);
    RUN_TEST(test_cert_stat_xid_fixture);
    RUN_TEST(test_product_vdo_fixture);
    RUN_TEST(test_product_type_ufp_codes);
    RUN_TEST(test_product_type_dfp_codes);
    RUN_TEST(test_connector_type_codes);
    RUN_TEST(test_typed_fields_do_not_leak_across_fields);
    RUN_TEST(test_debug_print_typed_decode_lines);
    RUN_TEST(test_debug_print_typed_decode_respects_count);
    RUN_TEST(test_debug_print_typed_decode_skipped_without_ack);

    RUN_TEST(test_copy_and_move);

    return UNITY_END();
}
