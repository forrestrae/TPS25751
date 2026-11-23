"""Tests for pd_capture_fixture.py: bundle validation and final-contract selection.

Run from the library root:
    python3 -m unittest discover -s test/support -p "test_*.py"
"""

from __future__ import annotations

import contextlib
import io
import json
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import pd_capture_fixture as gen

HEADER = {"type": "header", "format": "km003c-capture", "version": 1,
          "meter": {"serial": "000000", "fw_version": "2.0.0", "hw_version": "2.3"}}
REGISTERS = {"format": "tps25751-registers", "version": 1, "read_utc": None,
             "registers": [{"address": "0x34", "length": 6, "data": "d64106009800"}]}
PROVENANCE = {"format": "tps25751-pd-capture-provenance", "version": 1, "summary": "test bundle"}

# Message types (USB PD R3.3 Tables 6.5 to 6.7).
GOODCRC, ACCEPT, REJECT, PS_RDY, WAIT, SOFT_RESET = 1, 3, 4, 6, 12, 13
SRC_CAPS, REQUEST, EPR_REQUEST, VDM = 1, 2, 9, 15
EPR_SRC_CAPS = 17

PDOS_1 = [0x0001912C, 0x0002D12C]
PDOS_2 = [0x0001912C, 0x0004B12C]


def header(msg_type: int, objects: int = 0, source: bool = True, msg_id: int = 0,
           extended: bool = False, cable_plug: bool | None = None) -> int:
    """A USB PD message header (Table 6.1): Revision 3.x, DFP when sent by the Source."""
    h = msg_type | (2 << 6) | (msg_id & 7) << 9 | (objects & 7) << 12 | int(extended) << 15
    if cable_plug is None:
        h |= int(source) << 8 | int(source) << 5
    else:
        h |= int(cable_plug) << 8
    return h


def wire(h: int, words: list[int] = (), tail: bytes = b"") -> str:
    return (h.to_bytes(2, "little") + b"".join(w.to_bytes(4, "little") for w in words) + tail).hex()


class Capture:
    """Builds a km003c-capture v1 body, adding a GoodCRC after each message unless told not to."""

    def __init__(self) -> None:
        self.lines = [HEADER]
        self.ids = {}

    def pd(self, hex_wire: str, sop: str = "SOP", goodcrc: bool = True, source: bool = True) -> Capture:
        self.lines.append({"type": "pd", "i": len(self.lines) - 1, "t_meter_ms": len(self.lines),
                           "sop": sop, "wire": hex_wire})
        if goodcrc:
            self.lines.append({"type": "pd", "i": len(self.lines) - 1, "t_meter_ms": len(self.lines),
                               "sop": sop, "wire": wire(header(GOODCRC, source=not source))})
        return self

    def nid(self, source: bool) -> int:
        n = self.ids.get(source, 0)
        self.ids[source] = n + 1
        return n

    def caps(self, pdos: list[int]) -> Capture:
        return self.pd(wire(header(SRC_CAPS, len(pdos), True, self.nid(True)), pdos))

    def request(self, rdo: int) -> Capture:
        return self.pd(wire(header(REQUEST, 1, False, self.nid(False)), [rdo]), source=False)

    def control(self, msg_type: int, source: bool = True) -> Capture:
        return self.pd(wire(header(msg_type, 0, source, self.nid(source))), source=source)

    def text(self) -> str:
        return "".join(json.dumps(line) + "\n" for line in self.lines)


class TempTree(unittest.TestCase):
    """A scratch library root with one bundle directory; the generator's paths point into it."""

    def setUp(self) -> None:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve()
        bundles = self.root / "test" / "fixtures" / "pd-captures"
        self.bundle = bundles / "2026-01-01-test"
        self.bundle.mkdir(parents=True)
        (bundles / "README.md").write_text("| `2026-01-01-test` |\n")
        for name, value in (("ROOT", self.root), ("BUNDLES_DIR", bundles),
                            ("HEADERS_DIR", self.root / "test" / "support" / "native" / "fixtures")):
            patcher = mock.patch.object(gen, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.write(Capture().caps(PDOS_1).request(0x1001912C).control(ACCEPT).control(PS_RDY).text(),
                   REGISTERS, PROVENANCE)

    def write(self, capture: str | bytes | None = None, registers: object = None,
              provenance: object = None) -> None:
        for name, value in (("capture.jsonl", capture), ("registers.json", registers),
                            ("provenance.json", provenance)):
            if value is None:
                continue
            path = self.bundle / name
            if isinstance(value, bytes):
                path.write_bytes(value)
            else:
                path.write_text(value if isinstance(value, str) else json.dumps(value))

    def run_main(self, *args: str) -> tuple[int, str, str]:
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = gen.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def contract(self, capture: Capture):
        self.write(capture.text())
        _, messages = gen.parse_capture(self.bundle / "capture.jsonl")
        return gen.select_contract(messages)


class MalformedBundleTests(TempTree):
    """Every malformed input exits 2 with one line that names the file; none raises."""

    def assert_rejected(self, file: str, *args: str) -> None:
        code, _, err = self.run_main(*(args or ("--check",)))
        self.assertEqual(code, 2, err)
        lines = err.strip().splitlines()
        self.assertEqual(len(lines), 1, err)
        self.assertIn(file, lines[0])
        self.assertNotIn("Traceback", err)

    def test_valid_bundle_writes_then_checks_clean(self):
        self.assertEqual(self.run_main()[0], 0)
        self.assertEqual(self.run_main("--check")[0], 0)

    def test_capture_line_that_is_an_array(self):
        self.write(json.dumps(HEADER) + "\n[1, 2]\n")
        self.assert_rejected("capture.jsonl:2")

    def test_capture_header_meter_that_is_not_an_object(self):
        self.write(json.dumps({**HEADER, "meter": ["053893"]}) + "\n")
        self.assert_rejected("capture.jsonl:1")

    def test_capture_sop_that_is_not_a_string(self):
        self.write(json.dumps(HEADER) + "\n" + json.dumps({"type": "pd", "sop": 0, "wire": "a303"}) + "\n")
        self.assert_rejected("capture.jsonl:2")

    def test_capture_wire_shorter_than_its_header_declares(self):
        self.write(json.dumps(HEADER) + "\n" + json.dumps({"type": "pd", "sop": "SOP", "wire": "a141"}) + "\n")
        self.assert_rejected("capture.jsonl:2")

    def test_capture_with_invalid_utf8(self):
        self.write(json.dumps(HEADER).encode() + b"\n\xff\xfe\n")
        self.assert_rejected("capture.jsonl")

    def test_registers_that_are_an_array(self):
        self.write(registers=[REGISTERS])
        self.assert_rejected("registers.json")

    def test_register_entry_that_is_not_an_object(self):
        self.write(registers={**REGISTERS, "registers": ["0x34"]})
        self.assert_rejected("registers.json registers[0]")

    def test_registers_with_invalid_utf8(self):
        self.write(registers=b'{"format": "\xc3\x28"}')
        self.assert_rejected("registers.json")

    def test_register_data_in_uppercase(self):
        self.write(registers={**REGISTERS, "registers": [{"address": "0x34", "length": 6,
                                                          "data": "D64106009800"}]})
        self.assert_rejected("registers.json registers[0]: data")

    def test_register_length_that_disagrees_with_the_data(self):
        self.write(registers={**REGISTERS, "registers": [{"address": "0x34", "length": 7,
                                                          "data": "d64106009800"}]})
        self.assert_rejected("registers.json registers[0]")

    def test_provenance_that_is_a_list(self):
        self.write(provenance=[PROVENANCE])
        self.assert_rejected("provenance.json")

    def test_provenance_setup_that_is_not_an_object(self):
        self.write(provenance={**PROVENANCE, "setup": "bench"})
        self.assert_rejected("provenance.json")

    def test_provenance_omitted_entry_that_is_not_an_object(self):
        self.write(provenance={**PROVENANCE, "registers": {"omitted": ["0x1A"]}})
        self.assert_rejected("provenance.json")

    def test_bundle_outside_the_bundles_directory(self):
        outside = self.root / "elsewhere"
        shutil.copytree(self.bundle, outside)
        self.assert_rejected(str(outside), str(outside))


class ContractSelectionTests(TempTree):
    """select_contract() follows the meter server's final_contract()."""

    def test_unfinished_second_negotiation_leaves_the_first_contract(self):
        # The reviewer's case: the capture ends after the second Accept, before its PS_RDY.
        cap = (Capture().caps(PDOS_1).request(0x1001912C).control(ACCEPT).control(PS_RDY)
               .caps(PDOS_2).request(0x2004B12C).control(ACCEPT))
        contract, caps = self.contract(cap)
        self.assertEqual(contract.rdo, 0x1001912C)
        self.assertEqual(caps.pdos, PDOS_1)

    def test_completed_second_negotiation_wins(self):
        cap = (Capture().caps(PDOS_1).request(0x1001912C).control(ACCEPT).control(PS_RDY)
               .caps(PDOS_2).request(0x2004B12C).control(ACCEPT).control(PS_RDY))
        contract, caps = self.contract(cap)
        self.assertEqual(contract.rdo, 0x2004B12C)
        self.assertEqual(caps.pdos, PDOS_2)

    def test_accept_without_ps_rdy_is_no_contract(self):
        contract, caps = self.contract(Capture().caps(PDOS_1).request(0x1001912C).control(ACCEPT))
        self.assertIsNone(contract)
        self.assertEqual(caps.pdos, PDOS_1)  # the latest capabilities, for the header

    def test_reject_and_wait_are_not_contracts(self):
        for outcome in (REJECT, WAIT):
            cap = Capture().caps(PDOS_1).request(0x1001912C).control(outcome).control(PS_RDY)
            self.assertIsNone(self.contract(cap)[0], outcome)

    def test_soft_reset_abandons_the_negotiation(self):
        cap = (Capture().caps(PDOS_1).request(0x1001912C).control(ACCEPT)
               .control(SOFT_RESET, source=False).control(PS_RDY))
        self.assertIsNone(self.contract(cap)[0])

    def test_extended_message_between_request_and_accept_does_not_break_it(self):
        # An unchunked extended message (Status, type 2) sits between the Request and the
        # Accept; the old next-message filter treated it as the reply.
        status = wire(header(2, 2, True, 7, extended=True), [], (6).to_bytes(2, "little") + bytes(6))
        cap = (Capture().caps(PDOS_1).request(0x1001912C).pd(status).control(ACCEPT).control(PS_RDY))
        self.assertEqual(self.contract(cap)[0].rdo, 0x1001912C)

    def test_retransmissions_count_once(self):
        cap = Capture().caps(PDOS_1)
        req = wire(header(REQUEST, 1, False, 0), [0x1001912C])
        cap.pd(req, goodcrc=False, source=False).pd(req, source=False)  # retry, then acknowledged
        cap.control(ACCEPT).control(PS_RDY)
        self.write(cap.text())
        _, messages = gen.parse_capture(self.bundle / "capture.jsonl")
        self.assertEqual([m.retry for m in messages if m.is_data(REQUEST)], [False, True])
        self.assertEqual(gen.select_contract(messages)[0].request.index, 2)

    def test_same_bytes_after_a_goodcrc_are_a_new_message(self):
        req = wire(header(REQUEST, 1, False, 0), [0x1001912C])
        cap = Capture().caps(PDOS_1).pd(req, source=False).pd(req, source=False)
        self.write(cap.text())
        _, messages = gen.parse_capture(self.bundle / "capture.jsonl")
        self.assertEqual([m.retry for m in messages if m.is_data(REQUEST)], [False, False])

    def test_cable_messages_are_ignored(self):
        cap = (Capture().caps(PDOS_1).request(0x1001912C).control(ACCEPT)
               .pd(wire(header(PS_RDY, 0, cable_plug=True)), sop="SOP'"))
        self.assertIsNone(self.contract(cap)[0])

    def test_epr_capabilities_reassembled_and_epr_request(self):
        pdos = [0x0A01912C + i for i in range(11)]  # 44 bytes: two chunks
        data = b"".join(p.to_bytes(4, "little") for p in pdos)
        chunk0 = (1 << 15) | (0 << 11) | len(data)
        chunk1 = (1 << 15) | (1 << 11) | len(data)
        ask = (1 << 15) | (1 << 11) | (1 << 10)
        cap = Capture()
        cap.pd(wire(header(EPR_SRC_CAPS, 7, True, 0, extended=True), [], chunk0.to_bytes(2, "little") + data[:26]))
        cap.pd(wire(header(EPR_SRC_CAPS, 1, False, 0, extended=True), [], ask.to_bytes(2, "little") + bytes(2)),
               source=False)
        cap.pd(wire(header(EPR_SRC_CAPS, 5, True, 1, extended=True), [], chunk1.to_bytes(2, "little") + data[26:]))
        cap.pd(wire(header(EPR_REQUEST, 2, False, 1), [0x8004B12C, pdos[7]]), source=False)
        cap.control(ACCEPT).control(PS_RDY)
        contract, caps = self.contract(cap)
        self.assertTrue(contract.epr)
        self.assertEqual(contract.rdo, 0x8004B12C)
        self.assertEqual(contract.pdo_copy, pdos[7])
        self.assertTrue(caps.epr)
        self.assertEqual(caps.pdos, pdos)

    def test_identity_ack_sender_from_header_bits(self):
        ack = wire(header(VDM, 4, False, 0), [0xFF00A041, 0x00400451, 0, 0x911])
        cable = wire(header(VDM, 2, msg_id=0, cable_plug=True), [0xFF008041, 0x1C0020C2])
        self.write(Capture().pd(ack, source=False).pd(cable, sop="SOP'").text())
        _, messages = gen.parse_capture(self.bundle / "capture.jsonl")
        found = [(sop, who) for sop, who, _ in gen.select_identity_acks(messages)]
        self.assertEqual(found, [("SOP", "SinkUfp"), ("SOP'", "CablePlug")])


class CommittedBundleTests(unittest.TestCase):
    def test_committed_headers_are_current(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(gen.main(["--check"]), 0, err.getvalue())


if __name__ == "__main__":
    unittest.main()
