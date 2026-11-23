#!/usr/bin/env python3
"""Generate C++ test fixtures from USB PD capture bundles.

A bundle is a directory under test/fixtures/pd-captures/ holding capture.jsonl
(km003c-capture version 1), registers.json (tps25751-registers version 1), and
provenance.json. For each bundle this script writes
test/support/native/fixtures/PdCapture_<name>.h with constexpr arrays of:

- the final contract's Request RDO and the Source_Capabilities PDOs before it,
- the latest Discover Identity ACK per SOP and sender (VDM header and VDOs),
- each register's raw data bytes, without the I2C length byte,

with the provenance as a comment. The native tests include the headers, so
`pio test -e native` needs no Python. The script parses the `wire` hex of the
capture's `pd` records itself (USB PD message header, data objects, and the
extended header); it does not use the meter server's decoder. The final contract
follows the same rules as the server's final_contract() (km003c fork,
src/km003c_mcp/capture_read.py): see select_contract().

Usage:
    python3 test/support/pd_capture_fixture.py            # write every header
    python3 test/support/pd_capture_fixture.py --check    # verify, write nothing
    python3 test/support/pd_capture_fixture.py BUNDLE ... # only these bundles

--check exits 1 when a header is missing or stale, when a PdCapture_*.h has no
bundle, or when README.md does not list a bundle. A malformed bundle exits 2
with a one-line message naming the file. Standard library only. The tests are
test/support/test_pd_capture_fixture.py.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUNDLES_DIR = ROOT / "test" / "fixtures" / "pd-captures"
HEADERS_DIR = ROOT / "test" / "support" / "native" / "fixtures"
SCRIPT_NAME = "test/support/pd_capture_fixture.py"

CAPTURE_FORMAT = ("km003c-capture", 1)
REGISTERS_FORMAT = ("tps25751-registers", 1)
INPUT_FILES = ("capture.jsonl", "registers.json", "provenance.json")

# USB PD R3.3 message types, bits 4:0 of the message header (Tables 6.5 to 6.7).
CTRL_GOODCRC, CTRL_ACCEPT, CTRL_REJECT, CTRL_PS_RDY, CTRL_WAIT, CTRL_SOFT_RESET = 1, 3, 4, 6, 12, 13
DATA_SOURCE_CAPABILITIES, DATA_REQUEST, DATA_EPR_REQUEST, DATA_VENDOR_DEFINED = 1, 2, 9, 15
EXT_EPR_SOURCE_CAPABILITIES = 17

MAX_CHUNK_BYTES = 26  # data bytes per chunk of a chunked extended message
PD_SID = 0xFF00
VDM_CMD_DISCOVER_IDENTITY = 1
VDM_CMD_TYPE_ACK = 1


class BundleError(Exception):
    """A bundle file is missing or does not follow its format."""


@dataclass
class Message:
    index: int  # record field `i`
    t_meter_ms: int
    sop: str
    wire: bytes
    header: int
    objects: list[int] = field(default_factory=list)  # data objects; empty for control and extended
    retry: bool = False  # a retransmission of the sender's previous message (RetryFilter)
    ext_data: bytes | None = None  # complete extended payload, once reassembled

    @property
    def msg_type(self) -> int:
        return self.header & 0x1F

    @property
    def num_objects(self) -> int:
        return (self.header >> 12) & 0x7

    @property
    def extended(self) -> bool:
        return bool(self.header >> 15)

    def is_control(self, msg_type: int) -> bool:
        return not self.extended and self.num_objects == 0 and self.msg_type == msg_type

    def is_data(self, msg_type: int) -> bool:
        return not self.extended and self.num_objects > 0 and self.msg_type == msg_type

    @property
    def goodcrc(self) -> bool:
        return self.is_control(CTRL_GOODCRC)

    def sender(self) -> str:
        """Sender from the header: power and data role on SOP, Cable Plug on SOP'/SOP''."""
        if self.sop == "SOP":
            power = "Source" if self.header & (1 << 8) else "Sink"
            data = "Dfp" if self.header & (1 << 5) else "Ufp"
            return power + data
        return "CablePlug" if self.header & (1 << 8) else "Port"

    def describe(self) -> str:
        return f"record i={self.index}, t_meter_ms={self.t_meter_ms}, {self.sop}, wire {self.wire.hex()}"


@dataclass
class Register:
    address: int
    name: str
    length: int
    data: bytes


@dataclass
class Bundle:
    name: str
    path: Path
    capture_header: dict
    messages: list[Message]
    registers: list[Register]
    provenance: dict
    hashes: dict[str, str] = field(default_factory=dict)


# --- Reading and validation -------------------------------------------------------


def _read_text(path: Path) -> str:
    try:
        return path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as e:
        raise BundleError(f"{path.name}: not UTF-8 (byte {e.start})") from e
    except OSError as e:
        raise BundleError(f"{path.name}: cannot read ({e.strerror})") from e


def _load_json(text: str, where: str) -> object:
    try:
        return json.loads(text)
    except json.JSONDecodeError as e:
        raise BundleError(f"{where}: not JSON ({e.msg} at line {e.lineno} column {e.colno})") from e


def _obj(value: object, where: str) -> dict:
    if not isinstance(value, dict):
        raise BundleError(f"{where}: expected a JSON object, got {type(value).__name__}")
    return value


def _optional_obj(parent: dict, key: str, where: str) -> dict:
    value = parent.get(key)
    return {} if value is None else _obj(value, f"{where}: {key}")


def _int(value: object, where: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise BundleError(f"{where}: expected an integer, got {value!r}")
    return value


def _hex_bytes(text: object, where: str, lowercase: bool = False) -> bytes:
    digits = "0-9a-f" if lowercase else "0-9a-fA-F"
    if not isinstance(text, str) or not re.fullmatch(f"(?:[{digits}]{{2}})*", text):
        case = "lowercase " if lowercase else ""
        raise BundleError(f"{where}: expected an even-length {case}hex string, got {text!r}")
    return bytes.fromhex(text)


class RetryFilter:
    """Retransmissions, as the meter server's RetryFilter (km003c fork, capture.py) sees them.

    A port without a GoodCRC sends the same bytes again from the same sender (power and data
    role bits, or Cable Plug) on the same SOP. A GoodCRC on that SOP ends the wait.
    """

    def __init__(self) -> None:
        self._last: dict[tuple[str, int], bytes] = {}

    def is_retry(self, msg: Message) -> bool:
        if msg.goodcrc:
            for key in [k for k in self._last if k[0] == msg.sop]:
                del self._last[key]
            return False
        key = (msg.sop, msg.header & 0x0120)
        retry = self._last.get(key) == msg.wire
        self._last[key] = msg.wire
        return retry


def _extended_data(msg: Message, chunks: dict[tuple[str, int], bytearray]) -> bytes | None:
    """The complete payload of an extended message, reassembling chunks; None until complete."""
    if len(msg.wire) < 4:
        return None  # header only: the meter did not forward the payload
    ext = int.from_bytes(msg.wire[2:4], "little")
    chunked, number, request = bool(ext >> 15), (ext >> 11) & 0xF, bool(ext >> 10 & 1)
    size = ext & 0x1FF
    if request:
        return None
    if not chunked or size <= MAX_CHUNK_BYTES:
        return msg.wire[4:4 + size]
    key = (msg.sop, msg.msg_type)
    if number == 0:
        chunks[key] = bytearray()
    buf = chunks.get(key)
    if buf is None:
        return None
    buf += msg.wire[4:4 + max(0, min(MAX_CHUNK_BYTES, size - MAX_CHUNK_BYTES * number))]
    if len(buf) < size:
        return None
    del chunks[key]
    return bytes(buf[:size])


def parse_capture(path: Path) -> tuple[dict, list[Message]]:
    header = None
    messages: list[Message] = []
    retries = RetryFilter()
    chunks: dict[tuple[str, int], bytearray] = {}
    for lineno, line in enumerate(_read_text(path).splitlines(), 1):
        if not line.strip():
            continue
        where = f"{path.name}:{lineno}"
        rec = _obj(_load_json(line, where), where)
        kind = rec.get("type")
        if kind == "header":
            if (rec.get("format"), rec.get("version")) != CAPTURE_FORMAT:
                raise BundleError(
                    f"{where}: expected format {CAPTURE_FORMAT[0]} version {CAPTURE_FORMAT[1]}, "
                    f"got {rec.get('format')!r} version {rec.get('version')!r}"
                )
            _optional_obj(rec, "meter", where)
            header = rec
        elif kind == "pd":
            if header is None:
                raise BundleError(f"{where}: pd record before the header")
            wire = _hex_bytes(rec.get("wire"), f"{where}: wire")
            if len(wire) < 2:
                raise BundleError(f"{where}: wire shorter than a message header")
            sop = rec.get("sop")
            if not isinstance(sop, str):
                raise BundleError(f"{where}: sop must be a string, got {sop!r}")
            msg = Message(_int(rec.get("i", -1), f"{where}: i"),
                          _int(rec.get("t_meter_ms", 0), f"{where}: t_meter_ms"),
                          sop, wire, int.from_bytes(wire[0:2], "little"))
            if not msg.extended:
                expected = 2 + 4 * msg.num_objects
                if len(wire) != expected:
                    raise BundleError(
                        f"{where}: header 0x{msg.header:04X} declares {msg.num_objects} data objects "
                        f"({expected} bytes), wire has {len(wire)}"
                    )
                msg.objects = [int.from_bytes(wire[o:o + 4], "little") for o in range(2, len(wire), 4)]
            msg.retry = retries.is_retry(msg)
            if msg.extended and not msg.retry:
                msg.ext_data = _extended_data(msg, chunks)
            messages.append(msg)
    if header is None:
        raise BundleError(f"{path.name}: no header record")
    return header, messages


def parse_registers(path: Path) -> list[Register]:
    doc = _obj(_load_json(_read_text(path), path.name), path.name)
    if (doc.get("format"), doc.get("version")) != REGISTERS_FORMAT:
        raise BundleError(
            f"{path.name}: expected format {REGISTERS_FORMAT[0]} version {REGISTERS_FORMAT[1]}, "
            f"got {doc.get('format')!r} version {doc.get('version')!r}"
        )
    regs = doc.get("registers")
    if not isinstance(regs, list):
        raise BundleError(f"{path.name}: 'registers' must be a list")
    out: list[Register] = []
    seen: set[int] = set()
    for n, entry in enumerate(regs):
        where = f"{path.name} registers[{n}]"
        r = _obj(entry, where)
        addr_text = r.get("address")
        if not isinstance(addr_text, str) or not re.fullmatch(r"0[xX][0-9a-fA-F]{1,2}", addr_text):
            raise BundleError(f"{where}: address must be a hex string such as \"0x30\", got {addr_text!r}")
        address = int(addr_text, 16)
        if address in seen:
            raise BundleError(f"{where}: address 0x{address:02X} listed twice")
        seen.add(address)
        length = r.get("length")
        if not isinstance(length, int) or isinstance(length, bool) or not 1 <= length <= 255:
            raise BundleError(f"{where}: length must be the I2C length byte, 1 to 255, got {length!r}")
        data = _hex_bytes(r.get("data"), f"{where}: data", lowercase=True)
        if len(data) != length:
            raise BundleError(f"{where}: length byte is {length}, data holds {len(data)} bytes")
        name = r.get("name", "")
        if not isinstance(name, str):
            raise BundleError(f"{where}: name must be a string")
        out.append(Register(address, name, length, data))
    return sorted(out, key=lambda reg: reg.address)


def parse_provenance(path: Path) -> dict:
    doc = _obj(_load_json(_read_text(path), path.name), path.name)
    for key in ("setup", "capture"):
        _optional_obj(doc, key, path.name)
    regs = _optional_obj(doc, "registers", path.name)
    sources = regs.get("registers_synthesized_from", [])
    if not isinstance(sources, list) or not all(isinstance(s, str) for s in sources):
        raise BundleError(f"{path.name}: registers.registers_synthesized_from must be a list of strings")
    omitted = regs.get("omitted", [])
    if not isinstance(omitted, list):
        raise BundleError(f"{path.name}: registers.omitted must be a list")
    for n, o in enumerate(omitted):
        _obj(o, f"{path.name}: registers.omitted[{n}]")
    return doc


def load_bundle(path: Path) -> Bundle:
    path = path.resolve()
    if path.parent != BUNDLES_DIR.resolve():
        raise BundleError(f"{path}: a bundle must be a directory directly under "
                          f"{BUNDLES_DIR.relative_to(ROOT).as_posix()}/")
    for name in INPUT_FILES:
        if not (path / name).is_file():
            raise BundleError(f"{path.name}: missing {name}")
    capture_header, messages = parse_capture(path / "capture.jsonl")
    registers = parse_registers(path / "registers.json")
    provenance = parse_provenance(path / "provenance.json")
    hashes = {name: hashlib.sha256((path / name).read_bytes()).hexdigest() for name in INPUT_FILES}
    return Bundle(path.name, path, capture_header, messages, registers, provenance, hashes)


# --- Selection ---------------------------------------------------------------------


@dataclass
class Capabilities:
    message: Message
    epr: bool
    pdos: list[int]


@dataclass
class Contract:
    request: Message
    epr: bool
    rdo: int
    pdo_copy: int | None  # the second object of an EPR_Request
    caps: Capabilities | None


def capabilities_of(msg: Message) -> Capabilities | None:
    """Source_Capabilities, or a complete EPR_Source_Capabilities, with its PDO words."""
    if msg.is_data(DATA_SOURCE_CAPABILITIES):
        return Capabilities(msg, False, list(msg.objects))
    if msg.extended and msg.msg_type == EXT_EPR_SOURCE_CAPABILITIES and msg.ext_data is not None:
        data = msg.ext_data
        return Capabilities(msg, True, [int.from_bytes(data[o:o + 4], "little")
                                        for o in range(0, len(data) - 3, 4)])
    return None


def select_contract(messages: list[Message]) -> tuple[Contract | None, Capabilities | None]:
    """The final contract, by the rules of the meter server's final_contract().

    Only non-GoodCRC, non-retransmitted messages on SOP count. A Request or EPR_Request
    starts a negotiation, naming the latest Source_Capabilities or EPR_Source_Capabilities
    before it. The first Accept, Reject, or Wait after it is its outcome, and a PS_RDY
    after an Accept completes it. A Soft_Reset abandons the negotiation in progress. The
    final contract is the last completed negotiation.

    Returns (contract, capabilities): the contract or None, and the capabilities it names,
    or the latest capabilities in the capture when there is no contract.
    """
    caps: Capabilities | None = None
    current: dict | None = None
    final: Contract | None = None
    for m in messages:
        if m.sop != "SOP" or m.goodcrc or m.retry:
            continue
        found = capabilities_of(m)
        if m.is_control(CTRL_SOFT_RESET):
            current = None
        elif found is not None:
            caps = found
        elif m.is_data(DATA_REQUEST) or m.is_data(DATA_EPR_REQUEST):
            epr = m.msg_type == DATA_EPR_REQUEST
            current = {"contract": Contract(m, epr, m.objects[0],
                                            m.objects[1] if epr and len(m.objects) > 1 else None, caps),
                       "outcome": None}
        elif current is not None and current["outcome"] is None and (
                m.is_control(CTRL_ACCEPT) or m.is_control(CTRL_REJECT) or m.is_control(CTRL_WAIT)):
            current["outcome"] = m.msg_type
        elif current is not None and current["outcome"] == CTRL_ACCEPT and m.is_control(CTRL_PS_RDY):
            final = current["contract"]
            current["outcome"] = CTRL_PS_RDY  # a second PS_RDY does not complete it again
    if final is not None:
        return final, final.caps
    return None, caps


def is_identity_ack(msg: Message) -> bool:
    if not msg.is_data(DATA_VENDOR_DEFINED):
        return False
    vdm = msg.objects[0]
    return ((vdm >> 16) == PD_SID and bool(vdm & (1 << 15)) and (vdm >> 6) & 0x3 == VDM_CMD_TYPE_ACK
            and vdm & 0x1F == VDM_CMD_DISCOVER_IDENTITY)


def select_identity_acks(messages: list[Message]) -> list[tuple[str, str, Message]]:
    """Latest Discover Identity ACK per (SOP, sender), in SOP then sender order."""
    latest: dict[tuple[str, str], Message] = {}
    for m in messages:
        if not m.retry and is_identity_ack(m):
            latest[(m.sop, m.sender())] = m
    order = {"SOP": 0, "SOP'": 1, "SOP''": 2}
    keys = sorted(latest, key=lambda k: (order.get(k[0], 9), k[0], k[1]))
    return [(sop, sender, latest[(sop, sender)]) for sop, sender in keys]


# --- Rendering ---------------------------------------------------------------------


def identifier(bundle_name: str) -> str:
    return "PdCapture_" + re.sub(r"[^0-9A-Za-z]", "_", bundle_name)


def header_path(bundle_name: str) -> Path:
    return HEADERS_DIR / f"{identifier(bundle_name)}.h"


def sop_ident(sop: str) -> str:
    return {"SOP": "Sop", "SOP'": "SopPrime", "SOP''": "SopDoublePrime"}.get(
        sop, re.sub(r"[^0-9A-Za-z]", "", sop.title()))


def _wrap_comment(text: str, indent: str = "//   ", width: int = 88) -> list[str]:
    words, lines, cur = str(text).split(), [], ""
    for w in words:
        if cur and len(indent) + len(cur) + 1 + len(w) > width:
            lines.append(indent + cur)
            cur = w
        else:
            cur = f"{cur} {w}" if cur else w
    if cur:
        lines.append(indent + cur)
    return lines


def _words(values: list[int]) -> str:
    return ", ".join(f"0x{v:08X}" for v in values)


def _byte_rows(data: bytes) -> list[str]:
    return ["    " + ", ".join(f"0x{b:02X}" for b in data[o:o + 12]) + ","
            for o in range(0, len(data), 12)]


def provenance_comment(bundle: Bundle) -> list[str]:
    p, cap = bundle.provenance, bundle.capture_header
    meter = cap.get("meter") or {}
    lines = ["// Provenance (provenance.json and the capture header):"]
    if p.get("summary"):
        lines += _wrap_comment(p["summary"])
    for key, value in (p.get("setup") or {}).items():
        lines += _wrap_comment(f"{key}: {value}")
    capture = p.get("capture") or {}
    real = "real" if capture.get("real") else "synthesized"
    lines += _wrap_comment(
        f"capture: {real}, {cap.get('format')} v{cap.get('version')}, meter {meter.get('model', 'KM003C')} "
        f"serial {meter.get('serial')}, firmware {meter.get('fw_version')}, hardware {meter.get('hw_version')}, "
        f"started {cap.get('started_utc')}"
    )
    regs = p.get("registers") or {}
    if regs.get("synthesized"):
        sources = ", ".join(regs.get("registers_synthesized_from") or []) or "unstated"
        lines += _wrap_comment(f"registers: synthesized, not read from the board at the capture; from {sources}")
    else:
        lines += _wrap_comment("registers: read from the board")
    omitted = [str(o.get("address", "?")) for o in regs.get("omitted") or []]
    if omitted:
        lines += _wrap_comment("registers omitted: " + ", ".join(omitted))
    return lines


def render(bundle: Bundle) -> str:
    ns = identifier(bundle.name)
    rel = f"{BUNDLES_DIR.relative_to(ROOT).as_posix()}/{bundle.name}"
    out = [
        f"// Generated by {SCRIPT_NAME} from {rel}/.",
        "// Do not edit. Change the bundle and rerun the generator; --check fails while this is stale.",
        "//",
    ]
    out += provenance_comment(bundle)
    out += ["//", "// Inputs (SHA-256):"]
    out += [f"//   {name:<16} {digest}" for name, digest in bundle.hashes.items()]
    out += [
        "",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"namespace {ns} {{",
        "",
        "// --- Wire: final contract (USB PD message header and data objects) ---------------",
        "// The last Request or EPR_Request on SOP that an Accept and then a PS_RDY completed,",
        "// and the capabilities before it, as the meter server's final_contract() finds them.",
        "",
    ]

    contract, caps = select_contract(bundle.messages)
    if caps:
        kind = "EPR_Source_Capabilities" if caps.epr else "Source_Capabilities"
        note = "" if contract else " (latest; no completed contract)"
        out += [
            f"// {kind}{note}: {caps.message.describe()}",
            "constexpr bool kHasSourceCapabilities = true;",
            f"constexpr bool kSourceCapabilitiesEpr = {'true' if caps.epr else 'false'};",
            f"constexpr uint16_t kSourceCapabilitiesHeader = 0x{caps.message.header:04X};",
            f"constexpr size_t kSourceCapabilitiesPdoCount = {len(caps.pdos)};",
        ]
        if caps.pdos:
            out.append(f"constexpr uint32_t kSourceCapabilitiesPdos[{len(caps.pdos)}] = {{{_words(caps.pdos)}}};")
        out.append("")
    else:
        out += ["constexpr bool kHasSourceCapabilities = false;", ""]
    if contract:
        kind = "EPR_Request" if contract.epr else "Request"
        out += [
            f"// {kind}: {contract.request.describe()}",
            "constexpr bool kHasContract = true;",
            f"constexpr bool kRequestEpr = {'true' if contract.epr else 'false'};",
            f"constexpr uint16_t kRequestHeader = 0x{contract.request.header:04X};",
            f"constexpr uint32_t kRequestRdo = 0x{contract.rdo:08X};",
        ]
        if contract.pdo_copy is not None:
            out.append(f"constexpr uint32_t kRequestPdoCopy = 0x{contract.pdo_copy:08X};")
        out.append("")
    else:
        out += ["constexpr bool kHasContract = false;", ""]

    out += [
        "// --- Wire: latest Discover Identity ACK per SOP and sender -----------------------",
        "// Vdos excludes the VDM header; Vdos[0] is the ID Header VDO.",
        "",
    ]
    acks = select_identity_acks(bundle.messages)
    for sop, sender, m in acks:
        base = f"kIdentityAck{sop_ident(sop)}{sender}"
        vdos = m.objects[1:]
        out += [f"// Discover Identity ACK on {sop} from {sender}: {m.describe()}",
                f"constexpr uint16_t {base}Header = 0x{m.header:04X};",
                f"constexpr uint32_t {base}VdmHeader = 0x{m.objects[0]:08X};",
                f"constexpr size_t {base}VdoCount = {len(vdos)};"]
        if vdos:
            out.append(f"constexpr uint32_t {base}Vdos[{len(vdos)}] = {{{_words(vdos)}}};")
        out.append("")
    if not acks:
        out += ["// None in this capture.", ""]

    out += [
        "// --- Registers (registers.json): data bytes in read order, without the length byte --",
        "",
    ]
    for reg in bundle.registers:
        base = f"kRegister0x{reg.address:02X}"
        label = f" {reg.name}" if reg.name else ""
        out += [f"// 0x{reg.address:02X}{label}, length byte {reg.length}",
                f"constexpr size_t {base}Length = {reg.length};",
                f"constexpr uint8_t {base}[{reg.length}] = {{"]
        out += _byte_rows(reg.data)
        out += ["};", ""]
    if not bundle.registers:
        out += ["// None in this bundle.", ""]

    out += [f"}}  // namespace {ns}", ""]
    return "\n".join(out)


# --- Driver ------------------------------------------------------------------------


def find_bundles(names: list[str]) -> list[Path]:
    if names:
        paths = [Path(n) if Path(n).is_dir() else BUNDLES_DIR / n for n in names]
        for p in paths:
            if not p.is_dir():
                raise BundleError(f"no bundle directory {p}")
        return [p.resolve() for p in paths]
    if not BUNDLES_DIR.is_dir():
        return []
    return sorted(p for p in BUNDLES_DIR.iterdir() if p.is_dir())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("bundles", nargs="*", help="bundle directories or names (default: all)")
    parser.add_argument("--check", action="store_true",
                        help="exit 1 if any header is missing or stale; write nothing")
    args = parser.parse_args(argv)

    try:
        paths = find_bundles(args.bundles)
        rendered = {p.name: render(load_bundle(p)) for p in paths}
    except BundleError as e:
        print(f"pd_capture_fixture: {e}", file=sys.stderr)
        return 2

    problems: list[str] = []
    for name, text in rendered.items():
        target = header_path(name)
        current = target.read_text(encoding="utf-8", errors="replace") if target.is_file() else None
        rel = target.relative_to(ROOT).as_posix()
        if current == text:
            continue
        if args.check:
            problems.append(f"{rel}: {'missing' if current is None else 'stale'} (bundle {name})")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text, encoding="utf-8")
            print(f"wrote {rel}")

    if not args.bundles:
        expected = {header_path(n).name for n in rendered}
        if HEADERS_DIR.is_dir():
            for h in sorted(HEADERS_DIR.glob("PdCapture_*.h")):
                if h.name not in expected:
                    problems.append(f"{h.relative_to(ROOT).as_posix()}: no bundle generates this header")
        readme = BUNDLES_DIR / "README.md"
        index = readme.read_text(encoding="utf-8", errors="replace") if readme.is_file() else ""
        for name in rendered:
            if f"`{name}`" not in index:
                problems.append(f"{readme.relative_to(ROOT).as_posix()}: bundle {name} is not in the index")

    for p in problems:
        print(f"pd_capture_fixture: {p}", file=sys.stderr)
    if problems:
        if args.check:
            print(f"pd_capture_fixture: run python3 {SCRIPT_NAME} to regenerate", file=sys.stderr)
        return 1
    if args.check:
        print(f"pd_capture_fixture: {len(rendered)} header(s) up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
