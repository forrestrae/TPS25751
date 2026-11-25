import contextlib
import hashlib
import io
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

import bin_to_header as b2h


def bundle(length: int = 0x3800, version: int = 0xF8091166) -> bytes:
    """A synthetic patch bundle: Header_ID, a version word, and a non-repeating fill."""
    data = bytearray((i * 7 + 3) & 0xFF ^ (i >> 8) & 0xFF for i in range(length))
    data[0:4] = (0xACE00001).to_bytes(4, "little")
    if length >= 0x38:
        data[0x34:0x38] = version.to_bytes(4, "little")
    return bytes(data)


def array_bytes(text: str, name: str = b2h.DEFAULT_NAME) -> bytes:
    """The bytes of `const uint8_t <name>[] = {...};` parsed back out of a header."""
    match = re.search(rf"const uint8_t {re.escape(name)}\[\] = \{{(.*?)\}};", text, re.S)
    if match is None:
        raise AssertionError(f"no array named {name} in the header")
    return bytes(int(token, 16) for token in re.findall(r"0x[0-9A-Fa-f]{2}", match.group(1)))


def run_main(*argv: str) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = b2h.main(list(argv))
    return code, out.getvalue(), err.getvalue()


class RenderTests(unittest.TestCase):
    def test_round_trip(self):
        data = bundle()
        self.assertEqual(array_bytes(b2h.render(data)), data)

    def test_round_trip_of_a_length_that_is_not_a_multiple_of_16(self):
        data = bundle(0x38 + 5)
        text = b2h.render(data)
        self.assertEqual(array_bytes(text), data)
        self.assertNotIn(",\n};", text)  # no trailing comma after the last byte

    def test_sixteen_bytes_per_line(self):
        text = b2h.render(bundle(0x40))
        rows = [line for line in text.splitlines() if line.startswith("    0x")]
        self.assertEqual(len(rows), 4)
        for row in rows:
            self.assertEqual(len(re.findall(r"0x[0-9A-F]{2}", row)), 16)
        self.assertEqual(rows[0], "    0x01, 0x00, 0xE0, 0xAC, 0x1F, 0x26, 0x2D, 0x34, "
                                  "0x3B, 0x42, 0x49, 0x50, 0x57, 0x5E, 0x65, 0x6C,")

    def test_declares_a_const_uint8_t_array_and_its_length(self):
        text = b2h.render(bundle())
        self.assertIn("#pragma once", text)
        self.assertIn("#include <stdint.h>", text)
        self.assertIn("#include <stddef.h>", text)
        self.assertIn("const uint8_t tps25751_eeprom_bundle[] = {", text)
        self.assertIn("constexpr size_t tps25751_eeprom_bundle_size = sizeof(tps25751_eeprom_bundle);", text)
        self.assertNotIn("const char", text)

    def test_custom_name(self):
        text = b2h.render(bundle(), name="my_bundle")
        self.assertEqual(array_bytes(text, "my_bundle"), bundle())
        self.assertIn("constexpr size_t my_bundle_size = sizeof(my_bundle);", text)

    def test_rejects_a_name_that_is_not_an_identifier(self):
        for name in ("1bundle", "my-bundle", "a b", "", "x;int y"):
            with self.assertRaises(ValueError, msg=name):
                b2h.render(bundle(), name=name)

    def test_comment_records_where_the_bytes_came_from(self):
        data = bundle(version=0xF8091164)
        text = b2h.render(data, source="build_lowRegion.bin", file_name="patch_bundle.h")
        self.assertIn("@file patch_bundle.h", text)
        self.assertIn("Source file:  build_lowRegion.bin", text)
        self.assertIn(f"Size:         {len(data)} bytes", text)
        self.assertIn(f"SHA-256:      {hashlib.sha256(data).hexdigest()}", text)
        self.assertIn("Version word: 0xF8091164", text)

    def test_output_is_reproducible(self):
        self.assertEqual(b2h.render(bundle(), source="a.bin"), b2h.render(bundle(), source="a.bin"))

    @unittest.skipUnless(shutil.which("c++"), "no C++ compiler on PATH")
    def test_header_compiles_as_cxx17(self):
        data = bundle(0x38 + 9)
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "patch_bundle.h").write_text(b2h.render(data))
            (Path(tmp) / "check.cpp").write_text(
                '#include "patch_bundle.h"\n'
                f"static_assert(tps25751_eeprom_bundle_size == {len(data)}, \"size\");\n"
                "static_assert(sizeof(tps25751_eeprom_bundle[0]) == 1, \"element\");\n"
                "int main() { return tps25751_eeprom_bundle[0] == 0x01 ? 0 : 1; }\n")
            result = subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", "check.cpp"],
                cwd=tmp, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


class CheckTests(unittest.TestCase):
    def test_accepts_a_bundle(self):
        b2h.check_bundle(bundle())
        b2h.check_bundle(bundle(0x38))
        b2h.check_bundle(bundle(0x3C00))

    def test_rejects_a_wrong_header(self):
        full_flash = (0x00000800).to_bytes(4, "little") + b"\xff" * 0x100  # starts with a region pointer
        erased = b"\xff" * 0x100
        big_endian = (0xACE00001).to_bytes(4, "big") + bundle()[4:]
        for data in (full_flash, erased, big_endian):
            with self.assertRaisesRegex(ValueError, "not a patch bundle"):
                b2h.check_bundle(data)

    def test_rejects_a_short_file(self):
        for length in (0, 3, 0x37):
            with self.assertRaisesRegex(ValueError, "shorter"):
                b2h.check_bundle(bundle(0x38)[:length])

    def test_rejects_a_file_longer_than_a_region(self):
        with self.assertRaisesRegex(ValueError, "longer"):
            b2h.check_bundle(bundle(0x3C01))
        b2h.check_bundle(bundle(0x3C01), max_size=0xBC00)

    def test_version_word_is_little_endian_at_0x34(self):
        self.assertEqual(b2h.version_word(bundle(version=0xF8091166)), 0xF8091166)
        self.assertEqual(b2h.version_word(bundle(version=0x12345678)), 0x12345678)


class MainTests(unittest.TestCase):
    def setUp(self):
        self._dir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._dir.name)
        self.addCleanup(self._dir.cleanup)

    def write(self, name: str, data: bytes) -> Path:
        path = self.tmp / name
        path.write_bytes(data)
        return path

    def test_writes_the_header_to_the_output_file(self):
        data = bundle()
        source = self.write("build_lowRegion.bin", data)
        output = self.tmp / "patch_bundle.h"
        code, out, err = run_main(str(source), "-o", str(output))
        self.assertEqual(code, 0, err)
        self.assertEqual(out, "")
        text = output.read_text()
        self.assertEqual(array_bytes(text), data)
        self.assertIn("Source file:  build_lowRegion.bin", text)
        self.assertIn("@file patch_bundle.h", text)
        # The header records the file name only, never the directory it came from.
        self.assertNotIn(str(self.tmp), text)
        self.assertIn(hashlib.sha256(data).hexdigest(), err)

    def test_writes_to_standard_output_without_an_output_file(self):
        data = bundle(0x100)
        code, out, err = run_main(str(self.write("b.bin", data)))
        self.assertEqual(code, 0, err)
        self.assertEqual(array_bytes(out), data)
        self.assertEqual(err, "")

    def test_name_option(self):
        code, out, _ = run_main(str(self.write("b.bin", bundle(0x100))), "--name", "other")
        self.assertEqual(code, 0)
        self.assertEqual(array_bytes(out, "other"), bundle(0x100))

    def test_rejected_input_writes_no_header(self):
        output = self.tmp / "patch_bundle.h"
        output.write_text("previous contents\n")
        cases = {
            "full_flash.bin": (0x00000800).to_bytes(4, "little") + b"\xff" * 0x7BFC,
            "short.bin": bundle(0x38)[:0x20],
            "too_long.bin": bundle(0x3C01),
        }
        for name, data in cases.items():
            code, out, err = run_main(str(self.write(name, data)), "-o", str(output))
            self.assertEqual(code, 1, name)
            self.assertEqual(out, "", name)
            self.assertIn(name, err)
            self.assertEqual(output.read_text(), "previous contents\n", name)

    def test_max_size_option_accepts_hex(self):
        source = self.write("big.bin", bundle(0x3C01))
        self.assertEqual(run_main(str(source))[0], 1)
        code, out, _ = run_main(str(source), "--max-size", "0xBC00")
        self.assertEqual(code, 0)
        self.assertEqual(len(array_bytes(out)), 0x3C01)

    def test_bad_name_is_reported(self):
        code, out, err = run_main(str(self.write("b.bin", bundle(0x100))), "--name", "9lives")
        self.assertEqual(code, 1)
        self.assertEqual(out, "")
        self.assertIn("not a C++ identifier", err)

    def test_missing_input_is_reported(self):
        code, out, err = run_main(str(self.tmp / "absent.bin"))
        self.assertEqual(code, 1)
        self.assertEqual(out, "")
        self.assertIn("cannot read", err)


if __name__ == "__main__":
    unittest.main()
