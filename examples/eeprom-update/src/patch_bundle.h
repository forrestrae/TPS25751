#pragma once

/**
 * @file patch_bundle.h
 * @brief PLACEHOLDER image for the eeprom-update example
 *
 * Replace this file with the patch bundle to write before using any command
 * that writes the EEPROM. The bundle is a Low Region binary. SLVAFL1 (page 3)
 * says that TI's USBCPD Application Customization Tool can generate one, for
 * the external host to use in the EEPROM update. Convert it with the script in
 * the library root:
 *
 *   scripts/bin_to_header.py <build>_lowRegion.bin -o examples/eeprom-update/src/patch_bundle.h
 *
 * The script checks that the file is a patch bundle and records its size,
 * SHA-256, and version word in the header it writes. It lives in the library's
 * repository; the package published to the PlatformIO Registry leaves scripts/
 * out. Without it, write the two definitions below by hand from the binary.
 *
 * While this placeholder is in place the example refuses every command that
 * writes the EEPROM: the bytes below are not a patch bundle, so
 * TPS25751EepromUpdater::checkImage() returns BadImageHeader for them. The
 * read-only report and the cold reset still work.
 */

#include <stddef.h>
#include <stdint.h>

/// Placeholder bytes: 64 erased-EEPROM bytes, not a patch bundle.
const uint8_t tps25751_eeprom_bundle[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

/// Number of bytes in tps25751_eeprom_bundle.
constexpr size_t tps25751_eeprom_bundle_size = sizeof(tps25751_eeprom_bundle);
