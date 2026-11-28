import contextlib
import io
import re
import tempfile
import unittest
import zlib
from pathlib import Path

import appconfig_image as aci

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "test/test_native/test_TPS25751AppConfigImage/AppConfigFixture.h"

# Written by TI's tool into the fixture's block; see test_TPS25751AppConfigImage.cpp.
FIXTURE_CRC = 0x06961F6A
SLEEP_05_CRC = 0x7EEB66A8   # after 0x70 = 05; the native suite expects the same value
REMOVE_70_CRC = 0xDD222279  # after removing 0x70


def fixture_image() -> bytes:
    """The native suite's fixture, zero-filled to the end of its 0x600-byte app config area."""
    text = FIXTURE.read_text()
    body = re.search(r"kAppConfigFixture\[\] = \{(.*?)\};", text, re.S).group(1)
    data = bytes(int(t, 16) for t in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    return data + bytes(0x680 - len(data))


def run_main(*argv: str) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = aci.main(list(argv))
    return code, out.getvalue(), err.getvalue()


class CrcTests(unittest.TestCase):
    def test_stored_form_of_the_check_value(self):
        self.assertEqual(zlib.crc32(b"123456789"), 0xCBF43926)
        self.assertEqual(aci.stored_form(0xCBF43926), 0x9B63D02C)

    def test_fixture_crc_is_the_tools(self):
        img = aci.AppConfigImage(fixture_image())
        self.assertEqual(img.stored_crc, FIXTURE_CRC)
        self.assertTrue(img.crc_ok)

    def test_customer_version_word_is_not_covered(self):
        data = bytearray(fixture_image())
        data[0x88] = 0x55
        self.assertTrue(aci.AppConfigImage(bytes(data)).crc_ok)
        data[0x8C] ^= 1
        self.assertFalse(aci.AppConfigImage(bytes(data)).crc_ok)


class ParseAndBuildTests(unittest.TestCase):
    def test_records(self):
        img = aci.AppConfigImage(fixture_image())
        self.assertEqual(len(img.records), 22)
        self.assertEqual(img.records[0].address, 0x16)
        self.assertEqual(img.records[-1].address, 0x95)
        self.assertEqual(img.find(0x70).data, b"\x03")
        self.assertEqual(len(img.bin_table), 314)

    def test_build_without_changes_is_identical(self):
        data = fixture_image()
        self.assertEqual(aci.AppConfigImage(data).build(), data)

    def test_set_changes_data_and_crc(self):
        img = aci.AppConfigImage(fixture_image())
        img.set(0x70, b"\x05")
        out = aci.AppConfigImage(img.build())
        self.assertEqual(out.stored_crc, SLEEP_05_CRC)
        self.assertTrue(out.crc_ok)
        self.assertEqual(out.find(0x70).data, b"\x05")

    def test_remove_then_set_restores(self):
        data = fixture_image()
        img = aci.AppConfigImage(data)
        img.remove(0x70)
        removed = img.build()
        out = aci.AppConfigImage(removed)
        self.assertEqual(out.stored_crc, REMOVE_70_CRC)
        self.assertEqual(out.length, 887)
        self.assertEqual(removed[0x80 + 887:0x80 + 892], bytes(5))
        out.set(0x70, b"\x03")
        self.assertEqual(out.build(), data)

    def test_new_record_in_address_order(self):
        img = aci.AppConfigImage(fixture_image())
        img.set(0x06, bytes(range(1, 9)))
        out = aci.AppConfigImage(img.build())
        self.assertEqual(out.records[0].address, 0x06)
        self.assertEqual(out.records[0].ports, 0x0F)
        self.assertEqual(out.records[1].address, 0x16)

    def test_refusals(self):
        img = aci.AppConfigImage(fixture_image())
        with self.assertRaises(ValueError):
            img.set(0x70, b"")
        with self.assertRaises(ValueError):
            img.set(0x70, bytes(65))
        with self.assertRaises(ValueError):
            img.remove(0x06)
        img.capacity = img.length
        img.set(0x06, b"\x01")
        with self.assertRaises(ValueError):
            img.build()

    def test_bad_images(self):
        data = bytearray(fixture_image())
        with self.assertRaises(ValueError):
            aci.AppConfigImage(bytes(data[:0x40]))
        bad = bytearray(data); bad[0] = 0
        with self.assertRaisesRegex(ValueError, "Header_ID"):
            aci.AppConfigImage(bytes(bad))
        bad = bytearray(data); bad[0x80] = 2
        with self.assertRaisesRegex(ValueError, "version"):
            aci.AppConfigImage(bytes(bad))
        bad = bytearray(data); bad[0x99] |= 0x80
        with self.assertRaisesRegex(ValueError, "not supported"):
            aci.AppConfigImage(bytes(bad))
        bad = bytearray(data); bad[0x95] = 0x39
        with self.assertRaisesRegex(ValueError, "runs into the bin table"):
            aci.AppConfigImage(bytes(bad))


class FullFlashTests(unittest.TestCase):
    def test_layout(self):
        low = fixture_image()
        ff = aci.full_flash(low)
        self.assertEqual(len(ff), 0x4400 + len(low))
        self.assertEqual(ff[0:4], (0x800).to_bytes(4, "little"))
        self.assertEqual(ff[0x3FC:0x400], bytes(4))
        self.assertEqual(ff[0x400:0x404], (0x4400).to_bytes(4, "little"))
        self.assertEqual(ff[0x7FC:0x800], bytes(4))
        self.assertEqual(ff[4:0x3FC], b"\xFF" * (0x3FC - 4))
        self.assertEqual(ff[0x800:0x800 + len(low)], low)
        self.assertEqual(ff[0x800 + len(low):0x4400], b"\xFF" * (0x4400 - 0x800 - len(low)))
        self.assertEqual(ff[0x4400:], low)
        self.assertEqual([name for name, _ in aci.regions(ff)],
                         ["full flash, low copy at 0x800", "full flash, high copy at 0x4400"])


class CliTests(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.dir = Path(self._dir.name)
        self.low = self.dir / "low.bin"
        self.low.write_bytes(fixture_image())

    def tearDown(self):
        self._dir.cleanup()

    def test_show(self):
        code, out, _ = run_main("show", str(self.low))
        self.assertEqual(code, 0)
        self.assertIn("CRC 0x06961F6A ok", out)
        self.assertIn("0x70 [ 1] 03", out)

    def test_show_fails_on_a_bad_crc(self):
        data = bytearray(fixture_image()); data[0x100] ^= 1
        self.low.write_bytes(bytes(data))
        code, out, _ = run_main("show", str(self.low))
        self.assertEqual(code, 1)
        self.assertIn("MISMATCH", out)

    def test_edit_writes_low_region_and_full_flash(self):
        out, ff = self.dir / "out.bin", self.dir / "ff.bin"
        code, _, err = run_main("edit", str(self.low), "-o", str(out), "--set", "0x70=05",
                                "--remove", "0x95", "--full-flash", str(ff))
        self.assertEqual(code, 0, err)
        img = aci.AppConfigImage(out.read_bytes())
        self.assertEqual(img.find(0x70).data, b"\x05")
        self.assertIsNone(img.find(0x95))
        self.assertEqual(ff.read_bytes(), aci.full_flash(out.read_bytes()))

    def test_edit_refuses_a_damaged_image(self):
        data = bytearray(fixture_image()); data[0x100] ^= 1
        self.low.write_bytes(bytes(data))
        out = self.dir / "out.bin"
        code, _, err = run_main("edit", str(self.low), "-o", str(out), "--set", "0x70=05")
        self.assertEqual(code, 1)
        self.assertIn("refusing", err)
        self.assertFalse(out.exists())

    def test_edit_rejects_bad_arguments(self):
        out = self.dir / "out.bin"
        for arg in ("0x70", "0x70=zz", "0x100=00"):
            code, _, err = run_main("edit", str(self.low), "-o", str(out), "--set", arg)
            self.assertEqual(code, 1, arg)
        self.assertFalse(out.exists())

    def test_full_flash_command(self):
        out = self.dir / "ff.bin"
        code, _, _ = run_main("full-flash", str(self.low), "-o", str(out))
        self.assertEqual(code, 0)
        self.assertEqual(out.read_bytes(), aci.full_flash(fixture_image()))

    def test_missing_input(self):
        code, _, err = run_main("show", str(self.dir / "nope.bin"))
        self.assertEqual(code, 1)
        self.assertIn("cannot read", err)


if __name__ == "__main__":
    unittest.main()
