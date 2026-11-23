#!/usr/bin/env python3
"""Generate C++ test fixtures from USB PD capture bundles.

A bundle is a directory under test/fixtures/pd-captures/ holding capture.jsonl
(km003c-capture version 1), registers.json (tps25751-registers version 1), and
provenance.json. For each bundle this script writes
test/support/native/fixtures/PdCapture_<name>.h with constexpr arrays of:

- the Source_Capabilities PDO words of the final contract,
- the final Request RDO,
- the latest Discover Identity ACK per SOP and sender (VDM header and VDOs),
- each register's raw data bytes, without the I2C length byte,

with the provenance as a comment. The native tests include the headers, so
`pio test -e native` needs no Python. The script parses the `wire` hex of the
capture's `pd` records itself (USB PD message header and little-endian data
objects); it does not use the meter server's decoder.

Usage:
    python3 test/support/pd_capture_fixture.py            # write every header
    python3 test/support/pd_capture_fixture.py --check    # verify, write nothing
    python3 test/support/pd_capture_fixture.py BUNDLE ... # only these bundles

--check exits 1 when a header is missing or stale, when a PdCapture_*.h has no
bundle, or when README.md does not list a bundle. A malformed bundle exits 2.
Standard library only.
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

# USB PD R3.3 message header (16 bits, little-endian on the wire).
MSG_SOURCE_CAPABILITIES = 1  # data message types, bits 4:0
MSG_REQUEST = 2
MSG_VENDOR_DEFINED = 15
MSG_GOODCRC = 1  # control message types, bits 4:0
MSG_ACCEPT = 3

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
    wire: str
    header: int
    objects: list[int]

    @property
    def msg_type(self) -> int:
        return self.header & 0x1F

    @property
    def is_data(self) -> bool:
        return self.num_objects > 0 and not self.extended

    @property
    def num_objects(self) -> int:
        return (self.header >> 12) & 0x7

    @property
    def extended(self) -> bool:
        return bool(self.header >> 15)

    def sender(self) -> str:
        """Sender from the header: power and data role on SOP, Cable Plug on SOP'/SOP''."""
        if self.sop == "SOP":
            power = "Source" if self.header & (1 << 8) else "Sink"
            data = "Dfp" if self.header & (1 << 5) else "Ufp"
            return power + data
        return "CablePlug" if self.header & (1 << 8) else "Port"

    def describe(self) -> str:
        return f"record i={self.index}, t_meter_ms={self.t_meter_ms}, {self.sop}, wire {self.wire}"


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


# --- Parsing -----------------------------------------------------------------------


def _hex_bytes(text: object, where: str) -> bytes:
    if not isinstance(text, str) or not re.fullmatch(r"(?:[0-9a-fA-F]{2})*", text):
        raise BundleError(f"{where}: expected an even-length hex string, got {text!r}")
    return bytes.fromhex(text)


def parse_capture(path: Path) -> tuple[dict, list[Message]]:
    header = None
    messages: list[Message] = []
    with path.open(encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            if not line.strip():
                continue
            where = f"{path.name}:{lineno}"
            try:
                rec = json.loads(line)
            except json.JSONDecodeError as e:
                raise BundleError(f"{where}: not JSON ({e})") from e
            kind = rec.get("type")
            if kind == "header":
                if (rec.get("format"), rec.get("version")) != CAPTURE_FORMAT:
                    raise BundleError(
                        f"{where}: expected format {CAPTURE_FORMAT[0]} version {CAPTURE_FORMAT[1]}, "
                        f"got {rec.get('format')!r} version {rec.get('version')!r}"
                    )
                header = rec
            elif kind == "pd":
                if header is None:
                    raise BundleError(f"{where}: pd record before the header")
                wire = _hex_bytes(rec.get("wire"), where)
                if len(wire) < 2:
                    raise BundleError(f"{where}: wire shorter than a message header")
                msg_header = int.from_bytes(wire[0:2], "little")
                msg = Message(rec.get("i", -1), rec.get("t_meter_ms", 0), rec.get("sop", ""),
                              wire.hex(), msg_header, [])
                if not msg.extended:
                    expected = 2 + 4 * msg.num_objects
                    if len(wire) != expected:
                        raise BundleError(
                            f"{where}: header 0x{msg_header:04X} declares {msg.num_objects} data objects "
                            f"({expected} bytes), wire has {len(wire)}"
                        )
                    msg.objects = [int.from_bytes(wire[o:o + 4], "little") for o in range(2, len(wire), 4)]
                messages.append(msg)
    if header is None:
        raise BundleError(f"{path.name}: no header record")
    return header, messages


def parse_registers(path: Path) -> list[Register]:
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise BundleError(f"{path.name}: not JSON ({e})") from e
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
    for n, r in enumerate(regs):
        where = f"{path.name} registers[{n}]"
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
        data = _hex_bytes(r.get("data"), where)
        if len(data) != length:
            raise BundleError(f"{where}: length byte is {length}, data holds {len(data)} bytes")
        name = r.get("name", "")
        if not isinstance(name, str):
            raise BundleError(f"{where}: name must be a string")
        out.append(Register(address, name, length, data))
    return sorted(out, key=lambda reg: reg.address)


def load_bundle(path: Path) -> Bundle:
    for name in INPUT_FILES:
        if not (path / name).is_file():
            raise BundleError(f"{path.name}: missing {name}")
    capture_header, messages = parse_capture(path / "capture.jsonl")
    registers = parse_registers(path / "registers.json")
    try:
        provenance = json.loads((path / "provenance.json").read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise BundleError(f"{path.name}/provenance.json: not JSON ({e})") from e
    hashes = {name: hashlib.sha256((path / name).read_bytes()).hexdigest() for name in INPUT_FILES}
    return Bundle(path.name, path, capture_header, messages, registers, provenance, hashes)


# --- Selection ---------------------------------------------------------------------


def is_identity_ack(msg: Message) -> bool:
    if not msg.is_data or msg.msg_type != MSG_VENDOR_DEFINED:
        return False
    vdm = msg.objects[0]
    return ((vdm >> 16) == PD_SID and vdm & (1 << 15) and (vdm >> 6) & 0x3 == VDM_CMD_TYPE_ACK
            and vdm & 0x1F == VDM_CMD_DISCOVER_IDENTITY)


def select_contract(messages: list[Message]) -> tuple[Message | None, bool, Message | None]:
    """The final Request on SOP and the Source_Capabilities before it.

    The final Request is the last one the Source accepted; with none accepted, the
    last Request. Returns (request, accepted, source_capabilities).
    """
    sop = [m for m in messages if m.sop == "SOP"]
    request, accepted = None, False
    for n, m in enumerate(sop):
        if m.is_data and m.msg_type == MSG_REQUEST:
            # The next non-GoodCRC message on SOP must be an Accept sent by the Source.
            following = next((x for x in sop[n + 1:] if x.is_data or x.msg_type != MSG_GOODCRC), None)
            ok = (following is not None and not following.is_data and not following.extended
                  and following.msg_type == MSG_ACCEPT and following.header & (1 << 8))
            if ok or not accepted:
                request, accepted = m, bool(ok)
    limit = sop.index(request) if request else len(sop)
    caps = None
    for m in sop[:limit]:
        if m.is_data and m.msg_type == MSG_SOURCE_CAPABILITIES:
            caps = m
    return request, accepted, caps


def select_identity_acks(messages: list[Message]) -> list[tuple[str, str, Message]]:
    """Latest Discover Identity ACK per (SOP, sender), in SOP then sender order."""
    latest: dict[tuple[str, str], Message] = {}
    for m in messages:
        if is_identity_ack(m):
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
    omitted = [o.get("address", "?") for o in regs.get("omitted") or []]
    if omitted:
        lines += _wrap_comment("registers omitted: " + ", ".join(omitted))
    return lines


def render(bundle: Bundle) -> str:
    ns = identifier(bundle.name)
    rel = bundle.path.relative_to(ROOT).as_posix()
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
        "",
    ]

    request, accepted, caps = select_contract(bundle.messages)
    if caps:
        out += [
            f"// Source_Capabilities: {caps.describe()}",
            "constexpr bool kHasSourceCapabilities = true;",
            f"constexpr uint16_t kSourceCapabilitiesHeader = 0x{caps.header:04X};",
            f"constexpr size_t kSourceCapabilitiesPdoCount = {len(caps.objects)};",
            f"constexpr uint32_t kSourceCapabilitiesPdos[{len(caps.objects)}] = {{{_words(caps.objects)}}};",
            "",
        ]
    else:
        out += ["constexpr bool kHasSourceCapabilities = false;", ""]
    if request:
        out += [
            f"// Request: {request.describe()}",
            "constexpr bool kHasRequest = true;",
            f"constexpr bool kRequestAccepted = {'true' if accepted else 'false'};",
            f"constexpr uint16_t kRequestHeader = 0x{request.header:04X};",
            f"constexpr uint32_t kRequestRdo = 0x{request.objects[0]:08X};",
            "",
        ]
    else:
        out += ["constexpr bool kHasRequest = false;", ""]

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
        current = target.read_text(encoding="utf-8") if target.is_file() else None
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
        index = readme.read_text(encoding="utf-8") if readme.is_file() else ""
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
