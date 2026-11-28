/**
 * @file test_TPS25751AppConfigImage.cpp
 * @brief Native (host-run) unit tests for TPS25751AppConfigImage.
 *
 * Run with: pio test -e native -f "test_native/test_TPS25751AppConfigImage"
 *
 * The fixture (AppConfigFixture.h) is the header and app config block of a real
 * image from TI's USBCPD Application Customization Tool 2.0.2. Its stored CRC,
 * 0x06961F6A, was written by the tool. The CRCs expected after an edit were
 * computed independently of this class, in Python, by the tool's algorithm
 * (AppConfigService.generateAppConfig(), tool version 2.0.2):
 *
 *   c = zlib.crc32(block[0:4] + block[12:])
 *   stored = bytes(bitreverse8(~b & 0xFF) for b in c.to_bytes(4, "big"))
 *
 * read as a little-endian word.
 */

#include <unity.h>
#include <string.h>

#include "TPS25751AppConfigImage.h"
#include "TPS25751SleepControl.h"
#include "TPS25751TransmitSourceCaps.h"
#include "AppConfigFixture.h"

namespace {

constexpr size_t kImageSize = 0x680;          // header, then the 0x600-byte app config area
constexpr size_t kBlock = 0x80;               // app config pointer of the fixture
constexpr size_t kFixtureLength = 892;        // block offset 0x02 of the fixture
constexpr uint32_t kFixtureCrc = 0x06961F6AUL;
constexpr size_t kBinTableLength = 314;       // block 0x15 = 0x3A, block 0x19 bits 6:2 = 1 -> 0x13A
constexpr size_t kSleepRecord = 0x1A7;        // block offset of the 0x70 record

// Register addresses of the fixture's records, in block order.
constexpr uint8_t kAddresses[] = {
    0x16, 0x17, 0x27, 0x28, 0x29, 0x2B, 0x32, 0x33, 0x37, 0x42, 0x47,
    0x55, 0x56, 0x5C, 0x62, 0x64, 0x70, 0x77, 0x78, 0x7B, 0x7D, 0x95,
};

uint8_t image[kImageSize];
uint8_t original[kImageSize];

void loadFixture() {
    memset(image, 0, sizeof(image));
    memcpy(image, kAppConfigFixture, kAppConfigFixtureSize);
    memcpy(original, image, sizeof(image));
}

uint16_t blockLength() {
    return static_cast<uint16_t>(image[kBlock + 2] | (image[kBlock + 3] << 8));
}

void putWord(size_t at, uint32_t v) {
    image[at] = static_cast<uint8_t>(v);
    image[at + 1] = static_cast<uint8_t>(v >> 8);
    image[at + 2] = static_cast<uint8_t>(v >> 16);
    image[at + 3] = static_cast<uint8_t>(v >> 24);
}

}  // namespace

void setUp() { loadFixture(); }
void tearDown() {}

// ---------------------------------------------------------------------------
// CRC
// ---------------------------------------------------------------------------

void test_crc32_check_value() {
    // CRC-32 check value of "123456789" (the standard catalogue entry).
    const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, TPS25751AppConfigImage::crc32(text, sizeof(text)));
    // Chained in two parts gives the same value.
    const uint32_t part = TPS25751AppConfigImage::crc32(text, 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926UL, TPS25751AppConfigImage::crc32(text + 4, 5, part));
}

void test_stored_form_is_complement_bit_reversed() {
    // ~0xCBF43926 = 0x340BC6D9, bit-reversed = 0x9B63D02C.
    TEST_ASSERT_EQUAL_HEX32(0x9B63D02CUL, TPS25751AppConfigImage::storedForm(0xCBF43926UL));
}

void test_app_config_crc_matches_the_tool() {
    TEST_ASSERT_EQUAL_HEX32(kFixtureCrc, TPS25751AppConfigImage::appConfigCrc(image + kBlock, kFixtureLength));
}

void test_crc_skips_the_customer_version_word() {
    image[kBlock + 0x08] = 0x55;  // customer version
    image[kBlock + 0x0B] = 0xAA;  // reserved
    TEST_ASSERT_EQUAL_HEX32(kFixtureCrc, TPS25751AppConfigImage::appConfigCrc(image + kBlock, kFixtureLength));
    image[kBlock + 0x0C] ^= 0x01;  // XID is covered
    TEST_ASSERT_NOT_EQUAL(kFixtureCrc, TPS25751AppConfigImage::appConfigCrc(image + kBlock, kFixtureLength));
}

// ---------------------------------------------------------------------------
// Reading the fixture
// ---------------------------------------------------------------------------

void test_fixture_checks_ok() {
    TPS25751AppConfigImage img(image, sizeof(image));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
    TEST_ASSERT_EQUAL_UINT32(kBlock, img.configOffset());
    TEST_ASSERT_EQUAL_UINT32(0x600, img.configCapacity());
    TEST_ASSERT_EQUAL_UINT32(kFixtureLength, img.configLength());
    TEST_ASSERT_EQUAL_HEX32(kFixtureCrc, img.storedCrc());
    TEST_ASSERT_EQUAL_HEX32(kFixtureCrc, img.computedCrc());
    TEST_ASSERT_EQUAL_HEX32(0xF8091166UL, img.firmwareVersion());
}

void test_fixture_records_in_order() {
    TPS25751AppConfigImage img(image, sizeof(image));
    TEST_ASSERT_EQUAL_UINT32(sizeof(kAddresses), img.recordCount());
    TPS25751AppConfigImage::Record r;
    for (size_t i = 0; i < sizeof(kAddresses); ++i) {
        TEST_ASSERT_TRUE(img.record(i, r));
        TEST_ASSERT_EQUAL_HEX8(kAddresses[i], r.address);
        TEST_ASSERT_EQUAL_HEX8(0x0F, r.ports);
        TEST_ASSERT_EQUAL_UINT8(0, r.offset);
        TEST_ASSERT_EQUAL_UINT8(0, r.flags);
    }
    TEST_ASSERT_FALSE(img.record(sizeof(kAddresses), r));
    // The last record ends where the bin table starts.
    TEST_ASSERT_TRUE(img.record(sizeof(kAddresses) - 1, r));
    TEST_ASSERT_EQUAL_UINT32(kFixtureLength - kBinTableLength, r.blockOffset + 4 + r.length);
}

void test_find_record() {
    TPS25751AppConfigImage img(image, sizeof(image));
    TPS25751AppConfigImage::Record r;
    TEST_ASSERT_TRUE(img.findRecord(0x70, r));
    TEST_ASSERT_EQUAL_UINT32(kSleepRecord, r.blockOffset);
    TEST_ASSERT_EQUAL_UINT8(1, r.length);
    TEST_ASSERT_EQUAL_HEX8(0x03, r.data[0]);
    TEST_ASSERT_TRUE(img.findRecord(0x32, r));
    TEST_ASSERT_EQUAL_UINT8(63, r.length);
    TEST_ASSERT_FALSE(img.findRecord(0x06, r));  // all zero in the tool's project, so not written
}

void test_record_as_sleep_control() {
    TPS25751AppConfigImage img(image, sizeof(image));
    auto sleep = img.recordAs<TPS25751SleepControl>(TPS25751Registers::Address::SLEEP_CONTROL);
    TEST_ASSERT_NOT_NULL(sleep.get());
    // 0x03: bit 0 Sleep Mode Allowed = 1, bits 2:1 Sleep Time = 1h (100 ms), SLVUCR8B p.52.
    TEST_ASSERT_TRUE(sleep->sleepModeAllowed());
    TEST_ASSERT_EQUAL(SleepTime::Time100ms, sleep->sleepTime());
}

void test_record_as_transmit_source_caps() {
    TPS25751AppConfigImage img(image, sizeof(image));
    auto caps = img.recordAs<TPS25751TransmitSourceCaps>(TPS25751Registers::Address::TRANSMIT_SOURCE_CAPABILITIES);
    TEST_ASSERT_NOT_NULL(caps.get());
    // Byte 0 = 0x04: bits 2:0 PDO count 4.
    TEST_ASSERT_EQUAL_UINT8(4, caps->pdoCount());
    // PDO 1 at bytes 3..6 = 2C 91 01 20 -> 0x2001912C. Fixed (bits 31:30 = 0, Table 6.8):
    // bits 19:10 = 0x064 = 100 x 50 mV = 5000 mV; bits 9:0 = 0x12C = 300 x 10 mA = 3000 mA.
    const TPS25751Pdo pdo = caps->pdo(0);
    TEST_ASSERT_EQUAL_UINT16(5000, pdo.fixedVoltage_mV());
    TEST_ASSERT_EQUAL_UINT16(3000, pdo.fixedCurrent_mA());
}

void test_record_register_null_for_unknown_address() {
    TPS25751AppConfigImage img(image, sizeof(image));
    // 0x17 has a record but no register class in the library.
    TEST_ASSERT_NULL(img.recordRegister(static_cast<TPS25751Registers::Address>(0x17)).get());
    // 0x06 has a class but no record.
    TEST_ASSERT_NULL(img.recordRegister(TPS25751Registers::Address::CUSTOMER_USE).get());
}

void test_record_register_null_when_length_differs_from_register_size() {
    TPS25751AppConfigImage img(image, sizeof(image));
    const uint8_t two[] = {0x03, 0x00};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x70, two, sizeof(two)));
    TEST_ASSERT_NULL(img.recordRegister(TPS25751Registers::Address::SLEEP_CONTROL).get());
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

void test_set_same_bytes_leaves_image_identical() {
    TPS25751AppConfigImage img(image, sizeof(image));
    const uint8_t same[] = {0x03};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x70, same, sizeof(same)));
    TEST_ASSERT_EQUAL_MEMORY(original, image, sizeof(image));
}

void test_set_record_from_register_class() {
    TPS25751AppConfigImage img(image, sizeof(image));
    auto sleep = img.recordAs<TPS25751SleepControl>(TPS25751Registers::Address::SLEEP_CONTROL);
    TEST_ASSERT_NOT_NULL(sleep.get());
    sleep->setSleepTime(SleepTime::Time1000ms);  // bits 2:1 = 2h -> 0x05
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(*sleep));

    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
    TEST_ASSERT_EQUAL_HEX8(0x05, image[kBlock + kSleepRecord + 4]);
    TEST_ASSERT_EQUAL_UINT16(kFixtureLength, blockLength());
    TEST_ASSERT_EQUAL_HEX32(0x7EEB66A8UL, img.storedCrc());  // the tool's algorithm, see the file comment
    // Only the data byte and the CRC changed.
    for (size_t i = 0; i < sizeof(image); ++i) {
        if (i == kBlock + kSleepRecord + 4 || (i >= kBlock + 4 && i < kBlock + 8)) continue;
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(original[i], image[i], "unexpected change");
    }
}

void test_remove_then_add_restores_the_image() {
    TPS25751AppConfigImage img(image, sizeof(image));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.removeRecord(0x70));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
    TEST_ASSERT_EQUAL_UINT16(kFixtureLength - 5, blockLength());
    TEST_ASSERT_EQUAL_HEX32(0xDD222279UL, img.storedCrc());  // the tool's algorithm, see the file comment
    TEST_ASSERT_EQUAL_UINT32(sizeof(kAddresses) - 1, img.recordCount());
    // The bin table moved down by the record's 5 bytes, unchanged.
    TEST_ASSERT_EQUAL_MEMORY(original + kBlock + kFixtureLength - kBinTableLength,
                             image + kBlock + kFixtureLength - 5 - kBinTableLength, kBinTableLength);
    // The freed bytes are zero, as the tool leaves the unused area.
    for (size_t i = kFixtureLength - 5; i < kFixtureLength; ++i) TEST_ASSERT_EQUAL_HEX8(0, image[kBlock + i]);

    const uint8_t sleep[] = {0x03};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x70, sleep, sizeof(sleep)));
    TEST_ASSERT_EQUAL_MEMORY(original, image, sizeof(image));
}

void test_new_record_goes_in_address_order() {
    TPS25751AppConfigImage img(image, sizeof(image));
    const uint8_t customer[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x06, customer, sizeof(customer)));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
    TEST_ASSERT_EQUAL_UINT16(kFixtureLength + 12, blockLength());
    TPS25751AppConfigImage::Record r;
    TEST_ASSERT_TRUE(img.record(0, r));
    TEST_ASSERT_EQUAL_HEX8(0x06, r.address);
    TEST_ASSERT_EQUAL_HEX8(0x0F, r.ports);  // copied from the first record
    TEST_ASSERT_EQUAL_UINT32(0x1A, r.blockOffset);
    TEST_ASSERT_EQUAL_HEX8(0x07, image[kBlock + 0x1A + 3]);  // flags 0, length - 1 = 7
    TEST_ASSERT_TRUE(img.record(1, r));
    TEST_ASSERT_EQUAL_HEX8(0x16, r.address);
    // An address past every record goes after the last one, ahead of the bin table.
    const uint8_t one[] = {0x5A};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0xF0, one, sizeof(one)));
    TEST_ASSERT_TRUE(img.record(img.recordCount() - 1, r));
    TEST_ASSERT_EQUAL_HEX8(0xF0, r.address);
    TEST_ASSERT_EQUAL_MEMORY(original + kBlock + kFixtureLength - kBinTableLength,
                             image + kBlock + blockLength() - kBinTableLength, kBinTableLength);
}

void test_replace_with_a_different_length_keeps_ports_offset_flags() {
    image[kBlock + kSleepRecord + 3] = 0x80;  // flags = 2, length 1
    image[kBlock + kSleepRecord + 2] = 0x00;
    TPS25751AppConfigImage fixup(image, sizeof(image));
    putWord(kBlock + 4, fixup.computedCrc());  // re-seal the hand edit

    TPS25751AppConfigImage img(image, sizeof(image));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
    const uint8_t three[] = {0x01, 0x02, 0x03};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x70, three, sizeof(three)));
    TPS25751AppConfigImage::Record r;
    TEST_ASSERT_TRUE(img.findRecord(0x70, r));
    TEST_ASSERT_EQUAL_UINT8(2, r.flags);
    TEST_ASSERT_EQUAL_UINT8(3, r.length);
    TEST_ASSERT_EQUAL_HEX8(0x0F, r.ports);
    TEST_ASSERT_EQUAL_UINT16(kFixtureLength + 2, blockLength());
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

void test_corrupt_image_is_not_edited() {
    image[kBlock + 0x100] ^= 0x01;
    memcpy(original, image, sizeof(image));
    TPS25751AppConfigImage img(image, sizeof(image));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::CrcMismatch, img.check());
    const uint8_t sleep[] = {0x05};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::CrcMismatch, img.setRecord(0x70, sleep, sizeof(sleep)));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::CrcMismatch, img.removeRecord(0x70));
    TEST_ASSERT_NULL(img.recordRegister(TPS25751Registers::Address::SLEEP_CONTROL).get());
    TEST_ASSERT_EQUAL_MEMORY(original, image, sizeof(image));
}

void test_no_room() {
    putWord(TPS25751AppConfigImage::kAppConfigSizeOffset, kFixtureLength + 4);  // not covered by the CRC
    memcpy(original, image, sizeof(image));
    TPS25751AppConfigImage img(image, sizeof(image));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.check());
    const uint8_t one[] = {0x01};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::NoRoom, img.setRecord(0x06, one, sizeof(one)));  // needs 5
    TEST_ASSERT_EQUAL_MEMORY(original, image, sizeof(image));
    const uint8_t two[] = {0x01, 0x02};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x70, two, sizeof(two)));  // needs 1 more
}

void test_bad_arguments() {
    TPS25751AppConfigImage img(image, sizeof(image));
    uint8_t big[65] = {0};
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadArgument, img.setRecord(0x70, nullptr, 1));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadArgument, img.setRecord(0x70, big, 0));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadArgument, img.setRecord(0x70, big, 65));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::Ok, img.setRecord(0x70, big, 64));
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::NotFound, img.removeRecord(0x06));
    TPS25751SleepControl empty(nullptr, 0);
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadArgument,
                      img.setRecord(TPS25751Registers::Address::SLEEP_CONTROL, empty));
}

void test_structure_errors() {
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::NullImage, TPS25751AppConfigImage(nullptr, 0).check());
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::NullImage, TPS25751AppConfigImage(image, 0x4B).check());
    // A truncated image: the 0x600-byte app config area at 0x80 does not fit in 0x100 bytes.
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadConfigLocation, TPS25751AppConfigImage(image, 0x100).check());

    image[0] = 0x00;
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadHeaderId, TPS25751AppConfigImage(image, sizeof(image)).check());
    loadFixture();

    putWord(TPS25751AppConfigImage::kAppConfigPointerOffset, kImageSize);
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadConfigLocation, TPS25751AppConfigImage(image, sizeof(image)).check());
    loadFixture();

    image[kBlock] = 2;
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadConfigVersion, TPS25751AppConfigImage(image, sizeof(image)).check());
    loadFixture();

    image[kBlock + 2] = 0x10;  // length 0x0710, more than the 0x600-byte app config size
    image[kBlock + 3] = 0x07;
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadConfigLength, TPS25751AppConfigImage(image, sizeof(image)).check());
    loadFixture();

    image[kBlock + 0x19] |= 0x80;  // four-byte port fields
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::UnsupportedLayout, TPS25751AppConfigImage(image, sizeof(image)).check());
    loadFixture();

    image[kBlock + 0x17] = 1;  // every-device records
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::UnsupportedLayout, TPS25751AppConfigImage(image, sizeof(image)).check());
    loadFixture();

    image[kBlock + 0x15] = 0x39;  // bin table one byte shorter: the last record overruns the record area
    TEST_ASSERT_EQUAL(TPS25751AppConfigStatus::BadRecords, TPS25751AppConfigImage(image, sizeof(image)).check());
    TEST_ASSERT_EQUAL_UINT32(0, TPS25751AppConfigImage(image, sizeof(image)).recordCount());
}

void test_status_strings() {
    const TPS25751AppConfigStatus all[] = {
        TPS25751AppConfigStatus::Ok, TPS25751AppConfigStatus::NullImage, TPS25751AppConfigStatus::BadHeaderId,
        TPS25751AppConfigStatus::BadConfigLocation, TPS25751AppConfigStatus::BadConfigVersion,
        TPS25751AppConfigStatus::BadConfigLength, TPS25751AppConfigStatus::UnsupportedLayout,
        TPS25751AppConfigStatus::BadRecords, TPS25751AppConfigStatus::CrcMismatch, TPS25751AppConfigStatus::NotFound,
        TPS25751AppConfigStatus::BadArgument, TPS25751AppConfigStatus::NoRoom,
    };
    for (auto s : all) {
        const char* text = reinterpret_cast<const char*>(toString(s));
        TEST_ASSERT_NOT_EQUAL(0, strcmp(text, "unknown"));
    }
    TEST_ASSERT_EQUAL_STRING("unknown",
                             reinterpret_cast<const char*>(toString(static_cast<TPS25751AppConfigStatus>(0xFF))));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_stored_form_is_complement_bit_reversed);
    RUN_TEST(test_app_config_crc_matches_the_tool);
    RUN_TEST(test_crc_skips_the_customer_version_word);

    RUN_TEST(test_fixture_checks_ok);
    RUN_TEST(test_fixture_records_in_order);
    RUN_TEST(test_find_record);
    RUN_TEST(test_record_as_sleep_control);
    RUN_TEST(test_record_as_transmit_source_caps);
    RUN_TEST(test_record_register_null_for_unknown_address);
    RUN_TEST(test_record_register_null_when_length_differs_from_register_size);

    RUN_TEST(test_set_same_bytes_leaves_image_identical);
    RUN_TEST(test_set_record_from_register_class);
    RUN_TEST(test_remove_then_add_restores_the_image);
    RUN_TEST(test_new_record_goes_in_address_order);
    RUN_TEST(test_replace_with_a_different_length_keeps_ports_offset_flags);

    RUN_TEST(test_corrupt_image_is_not_edited);
    RUN_TEST(test_no_room);
    RUN_TEST(test_bad_arguments);
    RUN_TEST(test_structure_errors);
    RUN_TEST(test_status_strings);

    return UNITY_END();
}
