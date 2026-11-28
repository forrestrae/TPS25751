#!/usr/bin/env python3
"""Show and edit the app config block of a TPS25751 patch bundle.

Usage:
  scripts/appconfig_image.py show <lowRegion.bin | fullFlash.bin>
  scripts/appconfig_image.py edit <lowRegion.bin> -o <out_lowRegion.bin>
                                  [--set REG=HEX]... [--remove REG]...
                                  [--full-flash <out_fullFlash.bin>]
  scripts/appconfig_image.py full-flash <lowRegion.bin> -o <out_fullFlash.bin>

The input is a low-region binary from TI's USBCPD Application Customization
Tool. Its app config block holds one record per register the controller loads
at boot. `edit` replaces a record's data (or adds the record) with --set and
drops it with --remove, then rewrites the block length and CRC; the firmware
patch is not touched. The data of --set is the register's bytes as an I2C read
returns them after the length byte, in hex, for example `--set 0x70=05`.

`full-flash` builds the image for a blank 32 KB EEPROM: region pointers at
0x000 and 0x400, and the low region at 0x800 and again at 0x4400, as the tool
exports it.

The format and the CRC follow the tool's own code (AppConfigService and
LowRegionService, tool version 2.0.2); TPS25751AppConfigImage.h documents them
and implements the same editor in the library. CONSTRAINTS.md, App config
block, has the background.

Exits 0 on success, 1 on a rejected input or argument.
"""

from __future__ import annotations

import argparse
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

HEADER_ID = 0xACE00001
CODE_PATCH_POINTER = 0x08
CODE_PATCH_SIZE = 0x0C
CODE_PATCH_CRC = 0x10
FIRMWARE_VERSION = 0x34
APP_CONFIG_SIZE = 0x44
APP_CONFIG_POINTER = 0x48
MIN_IMAGE_SIZE = 0x4C

FIRST_RECORD = 0x1A
RECORD_HEADER = 4
MAX_RECORD_LENGTH = 64
DEFAULT_PORTS = 0x0F

FULL_FLASH_LOW = 0x0800
FULL_FLASH_HIGH = 0x4400
FULL_FLASH_POINTERS = {0x000: FULL_FLASH_LOW, 0x3FC: 0, 0x400: FULL_FLASH_HIGH, 0x7FC: 0}


def word(data: bytes, at: int) -> int:
    return int.from_bytes(data[at:at + 4], "little")


def stored_form(crc: int) -> int:
    """The tool's stored form of a CRC-32: complemented, then bit-reversed."""
    return int(f"{~crc & 0xFFFFFFFF:032b}"[::-1], 2)


def app_config_crc(block: bytes) -> int:
    """CRC of an app config block as stored at block offset 4 (little-endian word)."""
    return stored_form(zlib.crc32(block[0:4] + block[12:]))


def code_patch_crc(patch: bytes) -> int:
    return stored_form(zlib.crc32(patch))


@dataclass
class Record:
    ports: int
    address: int
    offset: int
    flags: int
    data: bytes

    def encode(self) -> bytes:
        return bytes([self.ports, self.address, self.offset,
                      (self.flags << 6) | (len(self.data) - 1)]) + self.data


class AppConfigImage:
    """A low-region image whose app config block is parsed into records."""

    def __init__(self, image: bytes):
        if len(image) < MIN_IMAGE_SIZE:
            raise ValueError(f"{len(image)} bytes is shorter than the 0x{MIN_IMAGE_SIZE:X}-byte image header")
        if word(image, 0) != HEADER_ID:
            raise ValueError(f"first word is 0x{word(image, 0):08X}, not Header_ID 0x{HEADER_ID:08X}; "
                             "not a low-region image")
        self.image = bytearray(image)
        self.offset = word(image, APP_CONFIG_POINTER)
        self.capacity = word(image, APP_CONFIG_SIZE)
        if self.offset < MIN_IMAGE_SIZE or self.offset + self.capacity > len(image) or self.capacity < FIRST_RECORD:
            raise ValueError(f"app config at 0x{self.offset:X}, 0x{self.capacity:X} bytes, does not fit the image")
        b = self.image[self.offset:self.offset + self.capacity]
        if b[0] != 1:
            raise ValueError(f"app config header version is {b[0]}, not 1")
        length = int.from_bytes(b[2:4], "little")
        if not FIRST_RECORD <= length <= self.capacity:
            raise ValueError(f"app config length {length} is outside {FIRST_RECORD}..{self.capacity}")
        field = int.from_bytes(b[0x18:0x1A], "little")
        if field & 0x8000 or b[0x17] != 0 or field & 0x3FF != FIRST_RECORD:
            raise ValueError("app config layout not supported (four-byte ports or every-device records)")
        self.length = length
        self.stored_crc = word(b, 4)
        self.computed_crc = app_config_crc(bytes(b[:length]))
        bin_length = b[0x15] | (((b[0x19] & 0x7C) >> 2) << 8)
        if bin_length > length - FIRST_RECORD:
            raise ValueError("bin table longer than the block")
        self.header = bytes(b[:FIRST_RECORD])
        self.bin_table = bytes(b[length - bin_length:length])
        self.records: list[Record] = []
        at, end = FIRST_RECORD, length - bin_length
        while at < end:
            if end - at < RECORD_HEADER:
                raise ValueError(f"record header at block offset 0x{at:X} runs into the bin table")
            n = (b[at + 3] & 0x3F) + 1
            if end - at - RECORD_HEADER < n:
                raise ValueError(f"record 0x{b[at + 1]:02X} at block offset 0x{at:X} runs into the bin table")
            self.records.append(Record(b[at], b[at + 1], b[at + 2], b[at + 3] >> 6,
                                       bytes(b[at + RECORD_HEADER:at + RECORD_HEADER + n])))
            at += RECORD_HEADER + n

    @property
    def crc_ok(self) -> bool:
        return self.stored_crc == self.computed_crc

    def find(self, address: int) -> Record | None:
        return next((r for r in self.records if r.address == address), None)

    def set(self, address: int, data: bytes) -> None:
        if not 1 <= len(data) <= MAX_RECORD_LENGTH:
            raise ValueError(f"record 0x{address:02X}: {len(data)} data bytes, not 1 to {MAX_RECORD_LENGTH}")
        existing = self.find(address)
        if existing is not None:
            existing.data = data
            return
        ports = self.records[0].ports if self.records else DEFAULT_PORTS
        index = next((i for i, r in enumerate(self.records) if r.address > address), len(self.records))
        self.records.insert(index, Record(ports, address, 0, 0, data))

    def remove(self, address: int) -> None:
        existing = self.find(address)
        if existing is None:
            raise ValueError(f"no record for register 0x{address:02X}")
        self.records.remove(existing)

    def build(self) -> bytes:
        """The image with the block rebuilt from the records; length and CRC rewritten."""
        block = bytearray(self.header) + b"".join(r.encode() for r in self.records) + self.bin_table
        if len(block) > self.capacity:
            raise ValueError(f"app config would be {len(block)} bytes; the image reserves {self.capacity}")
        block[2:4] = len(block).to_bytes(2, "little")
        block[4:8] = app_config_crc(bytes(block)).to_bytes(4, "little")
        out = bytearray(self.image)
        out[self.offset:self.offset + self.capacity] = bytes(block) + bytes(self.capacity - len(block))
        return bytes(out)


def full_flash(low: bytes) -> bytes:
    """The full-flash image the tool exports for a low region."""
    if len(low) > FULL_FLASH_HIGH - FULL_FLASH_LOW:
        raise ValueError(f"{len(low)} bytes does not fit one 0x{FULL_FLASH_HIGH - FULL_FLASH_LOW:X}-byte region")
    out = bytearray(b"\xFF" * (FULL_FLASH_HIGH + len(low)))
    for at, value in FULL_FLASH_POINTERS.items():
        out[at:at + 4] = value.to_bytes(4, "little")
    out[FULL_FLASH_LOW:FULL_FLASH_LOW + len(low)] = low
    out[FULL_FLASH_HIGH:] = low
    return bytes(out)


def regions(data: bytes) -> list[tuple[str, bytes]]:
    """The low-region images in a file: the file itself, or both regions of a full-flash image."""
    if word(data, 0) == HEADER_ID:
        return [("low region", data)]
    if len(data) > FULL_FLASH_HIGH and word(data, 0) == FULL_FLASH_LOW and word(data, 0x400) == FULL_FLASH_HIGH:
        size = len(data) - FULL_FLASH_HIGH
        return [("full flash, low copy at 0x800", data[FULL_FLASH_LOW:FULL_FLASH_LOW + size]),
                ("full flash, high copy at 0x4400", data[FULL_FLASH_HIGH:])]
    raise ValueError("neither a low-region image (Header_ID at 0) nor a full-flash image (pointers 0x800, 0x4400)")


def describe(name: str, data: bytes) -> list[str]:
    img = AppConfigImage(data)
    lines = [f"{name}: firmware 0x{word(data, FIRMWARE_VERSION):08X}"]
    ptr, size, crc = word(data, CODE_PATCH_POINTER), word(data, CODE_PATCH_SIZE), word(data, CODE_PATCH_CRC)
    if ptr + size <= len(data):
        ok = "ok" if code_patch_crc(data[ptr:ptr + size]) == crc else "MISMATCH"
        lines.append(f"  code patch 0x{ptr:X}+0x{size:X}, CRC 0x{crc:08X} {ok}")
    else:
        lines.append(f"  code patch 0x{ptr:X}+0x{size:X} is outside the file")
    crc = "ok" if img.crc_ok else f"MISMATCH (computed 0x{img.computed_crc:08X})"
    lines.append(f"  app config 0x{img.offset:X}, {img.length} of 0x{img.capacity:X} bytes, "
                 f"CRC 0x{img.stored_crc:08X} {crc}")
    lines.append(f"  {len(img.records)} records, bin table {len(img.bin_table)} bytes")
    for r in img.records:
        extra = "" if (r.ports, r.offset, r.flags) == (DEFAULT_PORTS, 0, 0) else \
            f"  ports 0x{r.ports:02X} offset {r.offset} flags {r.flags}"
        lines.append(f"    0x{r.address:02X} [{len(r.data):2}] {r.data.hex(' ')}{extra}")
    return lines


def parse_address(text: str) -> int:
    value = int(text, 0)
    if not 0 <= value <= 0xFF:
        raise ValueError(f"register {text} is not 0x00 to 0xFF")
    return value


def parse_set(text: str) -> tuple[int, bytes]:
    if "=" not in text:
        raise ValueError(f"--set {text}: expected REG=HEX")
    reg, hexdata = text.split("=", 1)
    try:
        data = bytes.fromhex(hexdata.replace(":", " "))
    except ValueError:
        raise ValueError(f"--set {text}: data is not hex") from None
    return parse_address(reg), data


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Show and edit the app config block of a TPS25751 patch bundle.")
    sub = parser.add_subparsers(dest="command", required=True)
    show = sub.add_parser("show", help="print the header, the CRCs, and every record")
    show.add_argument("input", type=Path)
    edit = sub.add_parser("edit", help="change records and write a new low region")
    edit.add_argument("input", type=Path)
    edit.add_argument("-o", "--output", type=Path, required=True)
    edit.add_argument("--set", action="append", default=[], metavar="REG=HEX")
    edit.add_argument("--remove", action="append", default=[], metavar="REG")
    edit.add_argument("--full-flash", type=Path)
    ff = sub.add_parser("full-flash", help="build the full-flash image of a low region")
    ff.add_argument("input", type=Path)
    ff.add_argument("-o", "--output", type=Path, required=True)
    args = parser.parse_args(argv)

    try:
        data = args.input.read_bytes()
    except OSError as error:
        print(f"appconfig_image: cannot read {args.input}: {error.strerror}", file=sys.stderr)
        return 1

    try:
        if args.command == "show":
            ok = True
            for name, region in regions(data):
                print("\n".join(describe(name, region)))
                ok = ok and AppConfigImage(region).crc_ok
            return 0 if ok else 1

        img = AppConfigImage(data)
        if not img.crc_ok:
            raise ValueError(f"stored CRC 0x{img.stored_crc:08X} is not the block's CRC 0x{img.computed_crc:08X}; "
                             "refusing to re-seal a damaged image")
        if args.command == "edit":
            for text in args.remove:
                img.remove(parse_address(text))
            for text in args.set:
                img.set(*parse_set(text))
        out = img.build()
        outputs = [(args.output, out if args.command == "edit" else full_flash(out))]
        if args.command == "edit" and args.full_flash is not None:
            outputs.append((args.full_flash, full_flash(out)))
    except ValueError as error:
        print(f"appconfig_image: {args.input.name}: {error}", file=sys.stderr)
        return 1

    for path, content in outputs:
        try:
            path.write_bytes(content)
        except OSError as error:
            print(f"appconfig_image: cannot write {path}: {error.strerror}", file=sys.stderr)
            return 1
        print(f"appconfig_image: wrote {path} ({len(content)} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
