"""Compare a USB PD capture with a TPS25751 register snapshot, word for word.

Inputs are two files: a ``km003c-capture`` version 1 capture (JSON Lines, written by
the ``km003c`` MCP server) and a ``tps25751-registers`` version 1 snapshot (written by
``bench_snapshot(path=...)``). The capture is read from the ``wire`` hex of each ``pd``
record, not from the meter server's decode, so a decode error in either server cannot
hide a mismatch. The rules for retransmissions and the final contract are those of the
meter server's ``final_contract()`` and of ``test/support/pd_capture_fixture.py``, except
that Source_Capabilities count only when sent with the Source power role.

Each row compares one wire word with the register bits that should hold it:

- Source_Capabilities PDO count and words against 0x30 when the partner sent them, or
  0x32 when the board did (PDO 1 at byte 3). Against 0x32, PDO 1 bits 31:30 and 22:0
  must match and each differing flag in bits 29:23 is reported, not failed: the
  controller sets those flags at runtime.
- The board's power role comes from STATUS at the snapshot, and each accepted PR_Swap
  in the capture flips it for what came before. After a PR_Swap the final contract is
  compared as usual, and the other side's Source_Capabilities from before the swap are
  compared with their own register in rows marked "Before PR_Swap"; the earlier
  contract's Request is not compared, since 0x34 and 0x35 hold the final contract.
- The final contract's Request RDO against 0x35 bits 31:0 (sink: the RDO this device
  sent) or bits 127:96 (source: the partner's request).
- The PDO at the RDO's Object Position against 0x34 bits 31:0, and PDO 1 bits 29:20
  against 0x34 bits 41:32. The TRM does not state that mapping; two chargers confirmed
  it with the board sinking. With the board sourcing, one observation found 0x34 bits
  41:32 equal to 0x32 PDO 1 bits 29:20 instead, so that pair is reported, not compared.
  Without Source_Capabilities on the wire (``aneg`` sends none), the PDOs come from the
  snapshot's 0x30 or 0x32.
- The partner's latest Discover Identity ACK on SOP against 0x48: Response Type,
  Number Valid VDOs, and each VDO. An ACK is the board's own when its power role equals
  the board's role at that point of the capture.
- The meter's last VBUS reading against ADC_RESULTS (0x6A) byte 3, 98 mV per LSB,
  skipped when STATUS reports no plug (0x6A is not updated in Unconnected Sleep).

The board's own Discover Identity ACK and SOP'/SOP'' cable ACKs are reported, not
compared: no register in this library holds them.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

CAPTURE_FORMAT = ("km003c-capture", 1)
REGISTERS_FORMAT = ("tps25751-registers", 1)

# Message types (USB PD R3.3 Tables 6.5 and 6.6).
CTRL_GOODCRC, CTRL_ACCEPT, CTRL_REJECT, CTRL_PS_RDY, CTRL_PR_SWAP, CTRL_WAIT, CTRL_SOFT_RESET = 1, 3, 4, 6, 10, 12, 13
DATA_SOURCE_CAPABILITIES, DATA_REQUEST, DATA_EPR_REQUEST, DATA_VENDOR_DEFINED = 1, 2, 9, 15
EXT_EPR_SOURCE_CAPABILITIES = 17
PD_SID = 0xFF00
VDM_DISCOVER_IDENTITY, VDM_ACK = 1, 1

VBUS_FLAT_MV = 500          # the meter server's threshold for "VBUS flat" (capture_read.py)
ADC_VBUS_LSB_MV = 98        # ADC_RESULTS byte 3, SLVUCR8B p.51
# An allowance for the meter's and the controller's gain error on top of one ADC step.
# Chosen conservatively; neither datasheet figure is in this repository.
VBUS_GAIN_ALLOWANCE = 0.01

SPEC_REVISIONS = {0: "1.0", 1: "2.0", 2: "3.x", 3: "reserved"}
RESPONSE_TYPES = {0: "not sent or pending", 1: "ACK", 2: "NAK or timeout", 3: "BUSY"}

MATCH, MISMATCH, REPORT, MISSING, SKIPPED = "match", "MISMATCH", "report", "missing", "not compared"


class CrosscheckError(Exception):
    """An input file is missing or does not follow its format."""


@dataclass
class Message:
    line: int
    index: int | None
    t: int | None
    sop: str
    wire: bytes
    objects: list[int] = field(default_factory=list)
    retry: bool = False

    @property
    def header(self) -> int:
        return int.from_bytes(self.wire[0:2], "little")

    @property
    def msg_type(self) -> int:
        return self.header & 0x1F

    @property
    def num_objects(self) -> int:
        return (self.header >> 12) & 0x7

    @property
    def extended(self) -> bool:
        return bool(self.header >> 15)

    @property
    def spec_revision(self) -> str:
        return SPEC_REVISIONS[(self.header >> 6) & 0x3]

    def is_control(self, msg_type: int) -> bool:
        return not self.extended and self.num_objects == 0 and self.msg_type == msg_type

    def is_data(self, msg_type: int) -> bool:
        return not self.extended and self.num_objects > 0 and self.msg_type == msg_type

    @property
    def goodcrc(self) -> bool:
        return self.is_control(CTRL_GOODCRC)

    def sender(self) -> str:
        """``source/dfp`` style on SOP, ``cable`` or ``port`` on SOP' and SOP'', as the meter server names them."""
        if self.sop == "SOP":
            power = "source" if self.header & (1 << 8) else "sink"
            data = "dfp" if self.header & (1 << 5) else "ufp"
            return f"{power}/{data}"
        return "cable" if self.header & (1 << 8) else "port"

    def where(self) -> str:
        return f"t={self.t} line {self.line}"


@dataclass
class Capture:
    path: Path
    header: dict
    footer: dict | None
    messages: list[Message]
    conn: int
    vbus: list[int]
    connects: list[int] = field(default_factory=list)  # meter times of connect records


# --- reading ------------------------------------------------------------------------

def _json_lines(path: Path):
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise CrosscheckError(f"{path}: {exc.strerror or exc}") from exc
    for number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue  # a line cut off by a crash; the meter server skips it too
        if isinstance(record, dict):
            yield number, record


def load_capture(path: str | Path) -> Capture:
    path = Path(path).expanduser()
    header = None
    footer = None
    messages: list[Message] = []
    last: dict[tuple[str, int], bytes] = {}
    conn = 0
    connects: list[int] = []
    vbus: list[int] = []
    for number, record in _json_lines(path):
        kind = record.get("type")
        if kind == "header":
            if (record.get("format"), record.get("version")) != CAPTURE_FORMAT:
                raise CrosscheckError(f"{path}: not a {CAPTURE_FORMAT[0]} version {CAPTURE_FORMAT[1]} file")
            header = record
        elif kind == "footer":
            footer = record
        elif kind == "conn":
            conn += 1
            if record.get("event") == "connect" and isinstance(record.get("t_meter_ms"), int):
                connects.append(record["t_meter_ms"])
        elif kind == "status" and isinstance(record.get("vbus_mv"), int):
            vbus.append(record["vbus_mv"])
        elif kind == "pd":
            try:
                wire = bytes.fromhex(record.get("wire", ""))
            except (TypeError, ValueError):
                continue
            if len(wire) < 2:
                continue
            msg = Message(number, record.get("i"), record.get("t_meter_ms"), str(record.get("sop", "SOP")), wire)
            if not msg.extended:
                expected = 2 + 4 * msg.num_objects
                if len(wire) != expected:
                    # The fixture generator refuses such a file too (BundleError).
                    raise CrosscheckError(
                        f"{path}:{number}: header 0x{msg.header:04X} declares {msg.num_objects} data "
                        f"objects ({expected} bytes), wire has {len(wire)}")
                msg.objects = [int.from_bytes(wire[o:o + 4], "little") for o in range(2, len(wire), 4)]
            # A retransmission: the same bytes from the same sender on the same SOP, with
            # no GoodCRC on that SOP in between.
            if msg.goodcrc:
                for key in [k for k in last if k[0] == msg.sop]:
                    del last[key]
            else:
                key = (msg.sop, msg.header & 0x0120)
                msg.retry = last.get(key) == wire
                last[key] = wire
            messages.append(msg)
    if header is None:
        raise CrosscheckError(f"{path}: no header record")
    return Capture(path, header, footer, messages, conn, vbus, connects)


def load_registers(path: str | Path) -> dict[int, bytes]:
    path = Path(path).expanduser()
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as exc:
        raise CrosscheckError(f"{path}: {exc.strerror or exc}") from exc
    except json.JSONDecodeError as exc:
        raise CrosscheckError(f"{path}: not JSON ({exc.msg})") from exc
    if not isinstance(document, dict) or (document.get("format"), document.get("version")) != REGISTERS_FORMAT:
        raise CrosscheckError(f"{path}: not a {REGISTERS_FORMAT[0]} version {REGISTERS_FORMAT[1]} document")
    out: dict[int, bytes] = {}
    for entry in document.get("registers", []):
        try:
            out[int(str(entry["address"]), 16)] = bytes.fromhex(entry["data"])
        except (KeyError, TypeError, ValueError) as exc:
            raise CrosscheckError(f"{path}: malformed register entry {entry!r}") from exc
    return out


# --- analysis -----------------------------------------------------------------------

@dataclass
class Contract:
    request: Message
    caps: Message | None
    epr: bool


OUTCOMES = {None: "no Accept, Reject, or Wait", CTRL_ACCEPT: "Accept but no PS_RDY",
            CTRL_REJECT: "Reject", CTRL_WAIT: "Wait", CTRL_SOFT_RESET: "abandoned by a Soft_Reset"}


def is_source_capabilities(m: Message) -> bool:
    """Source_Capabilities or EPR_Source_Capabilities sent with the Source power role (header bit 8)."""
    sent_as_source = bool(m.header & (1 << 8))
    return sent_as_source and (m.is_data(DATA_SOURCE_CAPABILITIES)
                               or (m.extended and m.msg_type == EXT_EPR_SOURCE_CAPABILITIES))


def select_contract(messages: list[Message]) -> tuple[Contract | None, Message | None, dict | None]:
    """The final contract, the Source_Capabilities it named, and the last unfinished Request.

    The same rules as ``select_contract()`` of ``test/support/pd_capture_fixture.py`` and
    the meter server's ``final_contract()``: only non-GoodCRC, non-retransmitted messages
    on SOP count. A Request starts a negotiation and names the latest Source_Capabilities
    before it; the first Accept, Reject, or Wait is its outcome; a PS_RDY after an Accept
    completes it; a Soft_Reset abandons it. The final contract is the last completed
    negotiation, or None. The capabilities are the final contract's, or the latest in
    the capture when there is no contract. Unlike the generator, Source_Capabilities
    count only when sent with the Source power role.

    The third value describes the last Request after the final contract (or the last
    Request, with no contract) that did not complete: ``{"request", "outcome"}``.
    """
    caps: Message | None = None
    current: dict | None = None
    final: Contract | None = None
    for m in messages:
        if m.sop != "SOP" or m.goodcrc or m.retry:
            continue
        if m.is_control(CTRL_SOFT_RESET):
            if current is not None and current["outcome"] != CTRL_PS_RDY:
                current["outcome"] = CTRL_SOFT_RESET
                current["closed"] = True
        elif is_source_capabilities(m):
            caps = m
        elif m.is_data(DATA_REQUEST) or m.is_data(DATA_EPR_REQUEST):
            current = {"contract": Contract(m, caps, m.msg_type == DATA_EPR_REQUEST), "outcome": None,
                       "closed": False}
        elif current is not None and not current["closed"] and current["outcome"] is None and (
                m.is_control(CTRL_ACCEPT) or m.is_control(CTRL_REJECT) or m.is_control(CTRL_WAIT)):
            current["outcome"] = m.msg_type
        elif current is not None and not current["closed"] and current["outcome"] == CTRL_ACCEPT \
                and m.is_control(CTRL_PS_RDY):
            final = current["contract"]
            current["outcome"] = CTRL_PS_RDY  # a second PS_RDY does not complete it again
    unfinished = None
    if current is not None and current["outcome"] != CTRL_PS_RDY:
        unfinished = {"request": current["contract"].request, "outcome": OUTCOMES[current["outcome"]]}
    if final is not None:
        return final, final.caps, unfinished
    return None, caps, unfinished


def is_identity_ack(m: Message) -> bool:
    if not m.is_data(DATA_VENDOR_DEFINED):
        return False
    vdm = m.objects[0]
    return ((vdm >> 16) == PD_SID and bool(vdm & (1 << 15)) and (vdm >> 6) & 0x3 == VDM_ACK
            and vdm & 0x1F == VDM_DISCOVER_IDENTITY)


def _bits(data: bytes, low: int, width: int) -> int:
    return (int.from_bytes(data, "little") >> low) & ((1 << width) - 1)


def _w(value: int | None) -> str | None:
    return None if value is None else f"{value:08x}"


def board_role(regs: dict[int, bytes], role: str | None) -> tuple[str, str, list[str]]:
    """The board's power role, where it came from, and any disagreement worth a note."""
    notes: list[str] = []
    status = regs.get(0x1A)
    if status:
        from_status = "source" if _bits(status, 5, 1) else "sink"
        if role and role != from_status:
            notes.append(f"role {role!r} was given but STATUS (0x1A) bit 5 reads {from_status}; "
                         "using STATUS")
        if not _bits(status, 0, 1):
            notes.append("STATUS (0x1A) bit 0 reads no plug: the snapshot was taken while detached")
        return from_status, "STATUS (0x1A) bit 5", notes
    if role in ("sink", "source"):
        return role, "given", notes
    if 0x30 in regs and 0x32 not in regs:
        return "sink", "assumed: 0x30 present, no STATUS", notes
    if 0x32 in regs and 0x30 not in regs:
        return "source", "assumed: 0x32 present, no STATUS", notes
    raise CrosscheckError("the snapshot has no STATUS (0x1A); give role sink or source")


def _row(check: str, wire, register, where: str, result: str, note: str = "") -> dict:
    return {"check": check, "wire": wire, "register": register, "where": where,
            "result": result, "note": note}


def _compare(check: str, wire: int | None, register: int | None, where: str, note: str = "",
             width: int = 8) -> dict:
    fmt = (lambda v: None if v is None else f"{v:0{width}x}")
    if register is None:
        return _row(check, fmt(wire), None, where, MISSING, note or "register not in the snapshot")
    return _row(check, fmt(wire), fmt(register), where, MATCH if wire == register else MISMATCH, note)


# PDO 1 bits 29:23 of a Fixed 5 V Source PDO (USB PD R3.3 Table 6.8). On the wire the
# controller can send other values than 0x32 holds: on 2026-11-25 the board sent PDO 1
# 0x2301912C with 0x32 PDO 1 at 0x2001912C. Each flag names the setting most likely to
# drive it at runtime; only bit 27's is TRM text (SLVUCR8B p.31).
PDO1_FLAGS = {
    29: ("Dual-Role Power", "inferred, not TRM text: the DRP Type-C state machine (0x28 bits 1:0) "
                            "and PR_Swap support (0x29 bits 7:4)"),
    28: ("USB Suspend Supported", "none identified"),
    27: ("Unconstrained Power", "TRM: PORT_CONTROL (0x29) bit 19 also sets this bit (SLVUCR8B p.31)"),
    26: ("USB Communications Capable", "none identified"),
    25: ("Dual-Role Data", "inferred, not TRM text: DR_Swap support (0x29 bits 15:12)"),
    24: ("Unchunked Extended Messages Supported", "inferred, not TRM text: PD3_CONFIGURATION (0x42) "
                                                  "bit 4, Unchunked Supported"),
    23: ("EPR Capable", "none identified"),
}
PDO1_FLAG_MASK = 0x3F800000  # bits 29:23


def _caps_rows(caps: Message, regs: dict[int, bytes], from_board: bool, prefix: str = "") -> list[dict]:
    """Rows for one Source_Capabilities: against 0x32 when the board sent it, else 0x30.

    Against 0x32, PDO 1 bits 31:30 and 22:0 must match, and each differing flag in bits
    29:23 is reported, not failed: the controller sets those flags at runtime.
    """
    address, first = (0x32, 3) if from_board else (0x30, 1)
    name = f"0x{address:02X}"
    data = regs.get(address)
    pdos = list(caps.objects)
    rows = []
    sender = f"from {caps.sender()} ({'the board' if from_board else 'the partner'}) at {caps.where()}"
    if data is None:
        return [_row(f"{prefix}Source_Capabilities", " ".join(_w(p) for p in pdos), None, name, MISSING,
                     f"{sender}; register not in the snapshot")]
    count = data[0] & 0x07
    rows.append(_compare(f"{prefix}Source_Capabilities PDO count", len(pdos), count, f"{name} bits 2:0",
                         sender, width=1))
    for position in range(1, max(len(pdos), count) + 1):
        offset = first + 4 * (position - 1)
        register = int.from_bytes(data[offset:offset + 4], "little") if len(data) >= offset + 4 else None
        wire = pdos[position - 1] if position <= len(pdos) else None
        where = f"{name} bytes {offset}..{offset + 3}"
        if not (from_board and position == 1 and wire is not None and register is not None):
            rows.append(_compare(f"{prefix}PDO {position}", wire, register, where))
            continue
        fixed = ~PDO1_FLAG_MASK & 0xFFFFFFFF
        same = (wire & fixed) == (register & fixed)
        differing = [bit for bit in PDO1_FLAGS if (wire ^ register) >> bit & 1]
        note = "bits 31:30 and 22:0 compared; flags in bits 29:23 "
        note += "equal" if not differing else "reported below"
        rows.append(_row(f"{prefix}PDO {position}", _w(wire), _w(register), where,
                         MATCH if same else MISMATCH, note))
        for bit in differing:
            flag, source = PDO1_FLAGS[bit]
            rows.append(_row(f"{prefix}PDO 1 bit {bit} {flag}", str(wire >> bit & 1), str(register >> bit & 1),
                             f"{name} bit {24 + bit}", REPORT,
                             f"the controller sets PDO 1 flags at runtime: reported, not failed. Likely "
                             f"source: {source}"))
    return rows


def _register_pdos(regs: dict[int, bytes], role: str) -> tuple[list[int], str] | None:
    """The PDO words of 0x30 (board sinking) or 0x32 (board sourcing), and the register's name."""
    address, first = (0x30, 1) if role == "sink" else (0x32, 3)
    data = regs.get(address)
    if not data:
        return None
    count = data[0] & 0x07
    pdos = [int.from_bytes(data[first + 4 * k:first + 4 * k + 4], "little") for k in range(count)
            if len(data) >= first + 4 * k + 4]
    return pdos, f"0x{address:02X}"


def _contract_rows(contract: Contract, regs: dict[int, bytes], role: str, caps_expected: bool) -> list[dict]:
    rows = []
    rdo = contract.request.objects[0]
    position = (rdo >> 28) & 0xF
    pdo35 = regs.get(0x35)
    own = _bits(pdo35, 0, 32) if pdo35 and len(pdo35) >= 4 else None
    requested = _bits(pdo35, 96, 32) if pdo35 and len(pdo35) >= 16 else None
    note = f"from {contract.request.sender()} at {contract.request.where()}, Object Position {position}"
    if role == "sink":
        rows.append(_compare("Request RDO (own)", rdo, own, "0x35 bits 31:0", note))
        if requested is not None:
            rows.append(_row("0x35 bits 127:96", None, _w(requested), "0x35 bits 127:96", REPORT,
                             "a partner's request only while STATUS reports Source; equal to bits 31:0 "
                             "on the sink bench" if requested == own else
                             "a partner's request only while STATUS reports Source; differs from bits 31:0"))
    else:
        rows.append(_compare("Request RDO (partner)", rdo, requested, "0x35 bits 127:96", note))

    pdo34 = regs.get(0x34)
    if contract.caps is not None:
        pdos, origin, label, label_note = contract.caps.objects, "the wire", "", ""
    else:
        # No Source_Capabilities before the Request: aneg sends none, and a late start
        # misses them. The board's own capability register stands in, which compares
        # register with register rather than with the wire.
        from_register = _register_pdos(regs, role)
        why = "aneg sends none" if not caps_expected else "the capture started late"
        if from_register is None or not from_register[0]:
            rows.append(_row("Requested PDO", None, _w(_bits(pdo34, 0, 32)) if pdo34 else None, "0x34 bits 31:0",
                             SKIPPED, f"no Source_Capabilities on the wire ({why}) and no PDOs in the snapshot's "
                             f"{'0x30' if role == 'sink' else '0x32'}"))
            return rows
        pdos, name = from_register
        origin, label = name, f", from {name}"
        label_note = f"PDO from {name}, not from the wire: no Source_Capabilities before the Request ({why})"
    wire_pdo = pdos[position - 1] if 1 <= position <= len(pdos) else None
    if wire_pdo is None:
        rows.append(_row("Requested PDO", None, _w(_bits(pdo34, 0, 32)) if pdo34 else None, "0x34 bits 31:0",
                         MISMATCH, f"Object Position {position} names no PDO of the {len(pdos)} in {origin}"))
    else:
        rows.append(_compare(f"Requested PDO (position {position}{label})", wire_pdo,
                             _bits(pdo34, 0, 32) if pdo34 else None, "0x34 bits 31:0",
                             label_note if label else ""))
    if pdos:
        control = (pdos[0] >> 20) & 0x3FF
        check = f"PDO 1 bits 29:20{label}"
        mapping = "the TRM does not state this mapping; two chargers confirmed it on hardware with the board sinking"
        if label:
            mapping = f"{label_note}; {mapping}"
        if role == "sink":
            rows.append(_compare(check, control, _bits(pdo34, 32, 10) if pdo34 else None, "0x34 bits 41:32",
                                 mapping, width=3))
        else:
            # With the board sourcing, one observation (2026-11-25, bundle
            # 2026-11-25-tps25751-iphone) found bits 41:32 equal to 0x32 PDO 1 bits 29:20
            # rather than to the PDO 1 sent on the wire, whose runtime flags differ.
            register = _bits(pdo34, 32, 10) if pdo34 else None
            tx = _register_pdos(regs, "source")
            tx_control = (tx[0][0] >> 20) & 0x3FF if tx and tx[0] else None
            reference = tx_control if tx_control is not None else control
            if tx_control is None:
                agreement = "0x32 is not in the snapshot, so the wire's PDO 1 bits 29:20 are shown"
            else:
                agreement = "equal" if register == tx_control else "differ"
                agreement = f"0x32 PDO 1 bits 29:20 and 0x34 bits 41:32 {agreement} here"
            rows.append(_row("0x32 PDO 1 bits 29:20" if tx_control is not None else check,
                             f"{reference:03x}", None if register is None else f"{register:03x}",
                             "0x34 bits 41:32", REPORT,
                             f"reported, not compared: with the board sourcing, one hardware observation "
                             f"(2026-11-25) found 0x34 bits 41:32 equal to 0x32 PDO 1 bits 29:20, not to the "
                             f"PDO 1 sent on the wire (bits 29:20 = {control:03x}); {agreement}"))
    return rows


def _identity_rows(messages: list[Message], regs: dict[int, bytes], role_at) -> list[dict]:
    """0x48 against the partner's latest Discover Identity ACK on SOP; the others reported.

    An ACK on SOP is the board's own when its power role bit equals the board's role at
    that point of the capture (``role_at``, which follows accepted PR_Swaps), else the
    partner's.
    """
    rows = []
    acks = [m for m in messages if not m.retry and is_identity_ack(m)]

    def power(m: Message) -> str:
        return "source" if m.header & (1 << 8) else "sink"

    partner_acks = [m for m in acks if m.sop == "SOP" and power(m) != role_at(m.line)]
    data = regs.get(0x48)
    if partner_acks:
        ack = partner_acks[-1]
        vdos = ack.objects[1:]
        note = (f"from {ack.sender()} (the partner) at {ack.where()}, PD {ack.spec_revision} header, "
                f"VDM header {ack.objects[0]:08x}")
        if data is None:
            rows.append(_row("Partner Discover Identity ACK", " ".join(_w(v) for v in vdos), None, "0x48",
                             MISSING, "register not in the snapshot"))
        else:
            rows.append(_row("0x48 Response Type", "ACK", RESPONSE_TYPES[_bits(data, 6, 2)], "0x48 bits 7:6",
                             MATCH if _bits(data, 6, 2) == VDM_ACK else MISMATCH, note))
            rows.append(_compare("0x48 Number Valid VDOs", len(vdos), _bits(data, 0, 3), "0x48 bits 2:0", width=1))
            for k in range(len(vdos)):
                low = 8 + 32 * k
                register = _bits(data, low, 32) if len(data) * 8 >= low + 32 else None
                rows.append(_compare(f"Identity VDO {k + 1}", vdos[k], register, f"0x48 bits {low + 31}:{low}"))
    elif data is not None:
        rows.append(_row("0x48", None, data.hex(), "0x48", REPORT,
                         f"no Discover Identity ACK from the partner on the wire; 0x48 reads Response Type "
                         f"{RESPONSE_TYPES[_bits(data, 6, 2)]} with {_bits(data, 0, 3)} VDOs, which may be "
                         "from an earlier exchange. Not compared"))
    else:
        rows.append(_row("Partner Discover Identity ACK", None, None, "0x48", SKIPPED,
                         "no Discover Identity ACK from the partner on SOP, and 0x48 is not in the snapshot"))
    others: dict[tuple[str, str], Message] = {}
    for m in acks:
        if m.sop != "SOP" or m not in partner_acks:
            others[(m.sop, m.sender())] = m
    for (sop, sender), m in sorted(others.items(), key=lambda kv: kv[1].line):
        whose = "board's own" if sop == "SOP" else "cable"
        note = f"{whose} ACK at {m.where()}, PD {m.spec_revision} header, SVDM version " \
               f"{'1.0' if not (m.objects[0] >> 13) & 0x3 else '2.x'}"
        if sop != "SOP":
            note += f"; connector type {(m.objects[1] >> 21) & 0x3:02b}b" if len(m.objects) > 1 else ""
        rows.append(_row(f"{sop} Discover Identity ACK ({sender})", " ".join(_w(v) for v in m.objects[1:]),
                         None, "no register", REPORT, note + "; not compared"))
    return rows


def _vbus_row(capture: Capture, regs: dict[int, bytes]) -> dict | None:
    adc = regs.get(0x6A)
    status = regs.get(0x1A)
    if not capture.vbus or (status and not _bits(status, 0, 1)):
        return None  # 0x6A is not updated in Unconnected Sleep
    meter = capture.vbus[-1]
    if adc is None or len(adc) < 4:
        return _row("VBUS (meter, last status)", f"{meter} mV", None, "0x6A byte 3", MISSING,
                    "register not in the snapshot")
    controller = adc[3] * ADC_VBUS_LSB_MV
    allowance = ADC_VBUS_LSB_MV + round(VBUS_GAIN_ALLOWANCE * meter)
    ok = abs(meter - controller) <= allowance
    return _row("VBUS (meter, last status)", f"{meter} mV", f"{controller} mV (0x{adc[3]:02x})", "0x6A byte 3",
                MATCH if ok else MISMATCH, f"allowed difference {allowance} mV")


NO_CAPABILITIES_STIMULI = {"aneg"}  # bench_renegotiate methods that send a Request but no Source_Capabilities


def sanity(capture: Capture, contract: Contract | None, caps: Message | None, unfinished: dict | None,
           caps_expected: bool) -> tuple[list[str], bool]:
    """The sanity notes, and whether they rule out a comparison."""
    notes: list[str] = []
    sent = [m for m in capture.messages if not m.goodcrc]
    if not sent:
        monitor = json.dumps(capture.header.get("pd_monitor"), separators=(",", ":"))
        if capture.conn:
            notes.append("only connection records, no PD messages: no PD traffic (for example a non-PD "
                         f"source); header pd_monitor {monitor}")
        else:
            notes.append(f"no PD messages and no connection records; header pd_monitor {monitor}")
        return notes, True
    flat = not capture.vbus or max(capture.vbus) - min(capture.vbus) < VBUS_FLAT_MV
    if caps is None and contract is None and unfinished is None and flat:
        notes.append("no Source_Capabilities, no Request, VBUS flat: no negotiation happened in this "
                     "window; check the stimulus, not the protocol")
        return notes, True
    if contract is not None and contract.caps is None:
        if caps_expected:
            notes.append(f"Request at {contract.request.where()} has no Source_Capabilities before it: the "
                         "capture started late")
        else:
            notes.append("aneg sends no Source_Capabilities: the requested PDO is taken from the snapshot")
    if unfinished is not None:
        where = unfinished["request"].where()
        if contract is None:
            notes.append(f"no completed contract: the last Request, at {where}, ended with {unfinished['outcome']}")
        else:
            notes.append(f"a Request after the final contract, at {where}, ended with {unfinished['outcome']}; "
                         "the comparison uses the earlier, completed contract")
    elif contract is None:
        notes.append("no Request: only the capabilities can be compared")
    if contract is not None and contract.epr:
        notes.append("the contract is an EPR_Request; EPR objects are not compared by this tool")
    retries = [m for m in capture.messages if m.retry]
    if retries:
        caps_retries = sum(1 for m in retries if m.is_data(DATA_SOURCE_CAPABILITIES))
        text = f"{len(retries)} retransmissions (counted once)"
        if caps_retries:
            text += (f", {caps_retries} of Source_Capabilities: expected while the controller sits in "
                     "PTCH during a cold restart")
        notes.append(text)
    if capture.footer is None:
        notes.append("no footer: the capture is still running or was cut off")
    return notes, False


def accepted_power_swaps(messages: list[Message]) -> list[tuple[Message, Message]]:
    """Each PR_Swap on SOP that the other port accepted, with that Accept."""
    swaps = []
    pending: Message | None = None
    for m in messages:
        if m.sop != "SOP" or m.goodcrc or m.retry:
            continue
        if m.is_control(CTRL_PR_SWAP):
            pending = m
        elif pending is not None and (m.is_control(CTRL_ACCEPT) or m.is_control(CTRL_REJECT)
                                      or m.is_control(CTRL_WAIT)):
            if m.is_control(CTRL_ACCEPT):
                swaps.append((pending, m))
            pending = None
    return swaps


def identity_after_attach(capture: Capture, role_at) -> tuple[Message | None, Message | None]:
    """The partner's Discover Identity ACK that the skill's identity wait accepts.

    The wait ends on the first Discover Identity ACK on SOP from the partner that comes
    after the first completed contract of the latest attach: the first PS_RDY that
    completes a Request after the last connect record, or after the start of the capture
    when it has none. An ACK between that contract and a later one counts; an iPhone sends
    its ACK between its two contracts. Returns that PS_RDY and the ACK, either None.
    """
    attach = max(capture.connects) if capture.connects else None
    messages = [m for m in capture.messages
                if m.sop == "SOP" and not m.goodcrc and not m.retry
                and (attach is None or (m.t is not None and m.t >= attach))]
    first_ps_rdy = None
    state = None  # None, "requested", "accepted"
    for m in messages:
        if m.is_data(DATA_REQUEST) or m.is_data(DATA_EPR_REQUEST):
            state = "requested"
        elif state == "requested" and m.is_control(CTRL_ACCEPT):
            state = "accepted"
        elif state == "requested" and (m.is_control(CTRL_REJECT) or m.is_control(CTRL_WAIT)):
            state = None
        elif m.is_control(CTRL_SOFT_RESET):
            state = None
        elif state == "accepted" and m.is_control(CTRL_PS_RDY):
            first_ps_rdy = m
            break
    if first_ps_rdy is None:
        return None, None
    for m in messages:
        if m.line > first_ps_rdy.line and is_identity_ack(m):
            sent_as = "source" if m.header & (1 << 8) else "sink"
            if sent_as != role_at(m.line):
                return first_ps_rdy, m
    return first_ps_rdy, None


def capture_stimulus(capture: Capture) -> str | None:
    """The stimulus the skill recorded in the header's provenance, if any."""
    provenance = capture.header.get("provenance")
    stimulus = provenance.get("stimulus") if isinstance(provenance, dict) else None
    return stimulus if isinstance(stimulus, str) else None


def crosscheck(capture_path: str | Path, registers_path: str | Path, role: str | None = None,
               stimulus: str | None = None) -> dict:
    """The comparison table for one capture and one snapshot, with a verdict.

    ``stimulus`` is the ``bench_renegotiate`` method that started the negotiation; it
    defaults to ``provenance.stimulus`` in the capture's header. With ``aneg``, which
    sends no Source_Capabilities, the requested PDO comes from the snapshot and the
    verdict can still be pass.
    """
    capture = load_capture(capture_path)
    regs = load_registers(registers_path)
    board, role_from, role_notes = board_role(regs, role)
    stimulus = stimulus or capture_stimulus(capture)
    caps_expected = stimulus not in NO_CAPABILITIES_STIMULI
    contract, caps, unfinished = select_contract(capture.messages)
    notes, blocked = sanity(capture, contract, caps, unfinished, caps_expected)
    notes = role_notes + notes

    # The board's power role at each point: STATUS gives it at the snapshot, and each
    # accepted PR_Swap before then flips it.
    swaps = accepted_power_swaps(capture.messages)
    other_role = "sink" if board == "source" else "source"

    def role_at(line: int) -> str:
        return board if sum(1 for _, accept in swaps if accept.line > line) % 2 == 0 else other_role

    rows: list[dict] = []
    if not blocked:
        if caps is not None and caps.is_data(DATA_SOURCE_CAPABILITIES):
            caps_from_board = role_at(caps.line) == "source"
            rows += _caps_rows(caps, regs, caps_from_board)
            if swaps:
                # The other side's capabilities from before the swap: the board's own
                # (against 0x32) when the final contract has the partner as Source.
                earlier = [m for m in capture.messages
                           if m.sop == "SOP" and not m.retry and m.line < caps.line
                           and m.is_data(DATA_SOURCE_CAPABILITIES) and is_source_capabilities(m)
                           and (role_at(m.line) == "source") != caps_from_board]
                for pr_swap, accept in swaps:
                    initiator = "the board" if (pr_swap.header >> 8 & 1) == (role_at(pr_swap.line) == "source") \
                        else "the partner"
                    notes.append(f"PR_Swap at {pr_swap.where()} from {initiator}, accepted at {accept.where()}: "
                                 f"the board was {role_at(pr_swap.line)} before it and {role_at(accept.line)} after it")
                if earlier:
                    first = earlier[-1]
                    whose = "the board's own" if not caps_from_board else "the partner's"
                    notes.append(f"the final contract's Source_Capabilities at {caps.where()} are "
                                 f"{'the board' if caps_from_board else 'the partner'}'s and are compared with "
                                 f"{'0x32' if caps_from_board else '0x30'}; {whose} Source_Capabilities at "
                                 f"{first.where()}, from before the PR_Swap, are compared with "
                                 f"{'0x30' if caps_from_board else '0x32'} in the rows marked 'Before PR_Swap'. "
                                 "The Request of that earlier contract is not compared: 0x34 and 0x35 hold the "
                                 "final contract")
                    rows += _caps_rows(first, regs, not caps_from_board, prefix="Before PR_Swap: ")
        elif caps is not None:
            rows.append(_row("EPR_Source_Capabilities", caps.wire.hex(), None, "", SKIPPED,
                             "EPR capabilities are not compared by this tool"))
        if contract is not None and not contract.epr:
            rows += _contract_rows(contract, regs, board, caps_expected)
        rows += _identity_rows(capture.messages, regs, role_at)
        provenance = capture.header.get("provenance")
        if isinstance(provenance, dict) and provenance.get("stop") == "identity":
            first_ps_rdy, ack = identity_after_attach(capture, role_at)
            if ack is not None:
                notes.append(f"stop identity: the partner's Discover Identity ACK at {ack.where()} came after "
                             f"the attach's first contract (PS_RDY at {first_ps_rdy.where()}), as the wait accepts")
            elif first_ps_rdy is not None:
                notes.append(f"stop identity: no Discover Identity ACK from the partner after the attach's "
                             f"first contract (PS_RDY at {first_ps_rdy.where()})")
            else:
                notes.append("stop identity: no completed contract after the last attach")
    vbus = _vbus_row(capture, regs)
    if vbus is not None:
        rows.append(vbus)
    compared = [r for r in rows if r["result"] in (MATCH, MISMATCH)]
    if blocked:
        verdict = "no negotiation"
    elif any(r["result"] == MISMATCH for r in rows):
        verdict = "fail"
    elif contract is None or contract.epr or (contract.caps is None and caps_expected) \
            or any(r["result"] == MISSING for r in rows) or not compared:
        verdict = "inconclusive"
    else:
        verdict = "pass"
    return {
        "capture": str(capture.path.resolve()),
        "registers": str(Path(registers_path).expanduser().resolve()),
        "role": board,
        "role_from": role_from,
        "stimulus": stimulus,
        "meter_fw": (capture.header.get("meter") or {}).get("fw_version"),
        "stop_reason": (capture.footer or {}).get("reason"),
        "sanity": notes,
        "rows": rows,
        "verdict": verdict,
    }


def render_table(result: dict) -> str:
    """The result as a Markdown table with the sanity notes and the verdict."""
    out = [f"Board role: {result['role']} ({result['role_from']})", ""]
    out.append("| Check | Wire | Register | Where | Result | Note |")
    out.append("|---|---|---|---|---|---|")
    for r in result["rows"]:
        cells = [r["check"], r["wire"] or "", r["register"] or "", r["where"], r["result"], r["note"]]
        out.append("| " + " | ".join(str(c).replace("|", "\\|") for c in cells) + " |")
    out.append("")
    for note in result["sanity"]:
        out.append(f"- {note}")
    if result["sanity"]:
        out.append("")
    out.append(f"Verdict: {result['verdict']}")
    return "\n".join(out)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="tps25751-pd-crosscheck", description=__doc__.split("\n\n")[0])
    parser.add_argument("capture", help="km003c-capture version 1 file (.jsonl)")
    parser.add_argument("registers", help="tps25751-registers version 1 file (registers.json)")
    parser.add_argument("--role", choices=("sink", "source"),
                        help="the board's power role, when the snapshot has no STATUS (0x1A)")
    parser.add_argument("--stimulus", help="the bench_renegotiate method, when the capture's header does "
                        "not record it; aneg sends no Source_Capabilities")
    parser.add_argument("--json", action="store_true", help="print the result as JSON")
    args = parser.parse_args(argv)
    try:
        result = crosscheck(args.capture, args.registers, args.role, args.stimulus)
    except CrosscheckError as exc:
        print(f"tps25751-pd-crosscheck: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=1) if args.json else render_table(result))
    return 1 if result["verdict"] == "fail" else 0


if __name__ == "__main__":
    sys.exit(main())
