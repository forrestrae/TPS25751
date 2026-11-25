"""Compare a USB PD capture with a TPS25751 register snapshot, word for word.

Inputs are two files: a ``km003c-capture`` version 1 capture (JSON Lines, written by
the ``km003c`` MCP server) and a ``tps25751-registers`` version 1 snapshot (written by
``bench_snapshot(path=...)``). The capture is read from the ``wire`` hex of each ``pd``
record, not from the meter server's decode, so a decode error in either server cannot
hide a mismatch. The rules for retransmissions and the final contract are those of the
meter server's ``final_contract()`` and of ``test/support/pd_capture_fixture.py``.

Each row compares one wire word with the register bits that should hold it:

- Source_Capabilities PDO count and words against 0x30 (board is sink) or 0x32 (board is
  source; PDO 1 at byte 3).
- The final contract's Request RDO against 0x35 bits 31:0 (sink: the RDO this device
  sent) or bits 127:96 (source: the partner's request).
- The PDO at the RDO's Object Position against 0x34 bits 31:0, and PDO 1 bits 29:20
  against 0x34 bits 41:32 (a mapping the TRM does not state, confirmed on hardware).
- The partner's latest Discover Identity ACK on SOP against 0x48: Response Type,
  Number Valid VDOs, and each VDO.
- The meter's last VBUS reading against ADC_RESULTS (0x6A) byte 3, 98 mV per LSB.

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
CTRL_GOODCRC, CTRL_ACCEPT, CTRL_REJECT, CTRL_PS_RDY, CTRL_WAIT, CTRL_SOFT_RESET = 1, 3, 4, 6, 12, 13
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
                msg.objects = [int.from_bytes(wire[o:o + 4], "little") for o in range(2, len(wire) - 3, 4)]
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
    return Capture(path, header, footer, messages, conn, vbus)


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
    complete: bool


def select_contract(messages: list[Message]) -> tuple[Contract | None, Message | None]:
    """The final contract and the Source_Capabilities its Request named.

    Only non-GoodCRC, non-retransmitted messages on SOP count. A Request starts a
    negotiation and names the latest capabilities before it; the first Accept, Reject,
    or Wait is its outcome; a PS_RDY after an Accept completes it; a Soft_Reset abandons
    it. Returns the last completed negotiation, else the last Request that was not
    completed (``complete`` False), and the latest capabilities when there is no Request.
    """
    caps: Message | None = None
    current: dict | None = None
    final: Contract | None = None
    pending: Contract | None = None
    for m in messages:
        if m.sop != "SOP" or m.goodcrc or m.retry:
            continue
        if m.is_control(CTRL_SOFT_RESET):
            current = None
        elif m.is_data(DATA_SOURCE_CAPABILITIES) or (m.extended and m.msg_type == EXT_EPR_SOURCE_CAPABILITIES):
            caps = m
        elif m.is_data(DATA_REQUEST) or m.is_data(DATA_EPR_REQUEST):
            pending = Contract(m, caps, m.msg_type == DATA_EPR_REQUEST, False)
            current = {"contract": pending, "outcome": None}
        elif current is not None and current["outcome"] is None and (
                m.is_control(CTRL_ACCEPT) or m.is_control(CTRL_REJECT) or m.is_control(CTRL_WAIT)):
            current["outcome"] = m.msg_type
        elif current is not None and current["outcome"] == CTRL_ACCEPT and m.is_control(CTRL_PS_RDY):
            final = Contract(current["contract"].request, current["contract"].caps,
                             current["contract"].epr, True)
            current["outcome"] = CTRL_PS_RDY
    if final is not None:
        return final, final.caps
    if pending is not None:
        return pending, pending.caps
    return None, caps


def is_identity_ack(m: Message) -> bool:
    if not m.is_data(DATA_VENDOR_DEFINED):
        return False
    vdm = m.objects[0]
    return ((vdm >> 16) == PD_SID and bool(vdm & (1 << 15)) and (vdm >> 6) & 0x3 == VDM_ACK
            and vdm & 0x1F == VDM_DISCOVER_IDENTITY)


def identity_acks(messages: list[Message]) -> dict[tuple[str, str], Message]:
    """The latest Discover Identity ACK per SOP and sender."""
    latest: dict[tuple[str, str], Message] = {}
    for m in messages:
        if not m.retry and is_identity_ack(m):
            latest[(m.sop, m.sender())] = m
    return latest


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


def _caps_rows(caps: Message, regs: dict[int, bytes], role: str) -> list[dict]:
    address, first = (0x30, 1) if role == "sink" else (0x32, 3)
    name = f"0x{address:02X}"
    data = regs.get(address)
    pdos = list(caps.objects)
    rows = []
    if data is None:
        return [_row("Source_Capabilities", " ".join(_w(p) for p in pdos), None, name, MISSING,
                     "register not in the snapshot")]
    count = data[0] & 0x07
    rows.append(_compare("Source_Capabilities PDO count", len(pdos), count, f"{name} bits 2:0",
                         f"from {caps.sender()} at {caps.where()}", width=1))
    for position in range(1, max(len(pdos), count) + 1):
        offset = first + 4 * (position - 1)
        register = int.from_bytes(data[offset:offset + 4], "little") if len(data) >= offset + 4 else None
        wire = pdos[position - 1] if position <= len(pdos) else None
        rows.append(_compare(f"PDO {position}", wire, register,
                             f"{name} bytes {offset}..{offset + 3}"))
    return rows


def _contract_rows(contract: Contract, regs: dict[int, bytes], role: str) -> list[dict]:
    rows = []
    rdo = contract.request.objects[0]
    position = (rdo >> 28) & 0xF
    pdo35 = regs.get(0x35)
    own = _bits(pdo35, 0, 32) if pdo35 and len(pdo35) >= 4 else None
    requested = _bits(pdo35, 96, 32) if pdo35 and len(pdo35) >= 16 else None
    note = f"from {contract.request.sender()} at {contract.request.where()}, Object Position {position}"
    if not contract.complete:
        note += "; no PS_RDY after an Accept, so this is not a completed contract"
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
    caps = contract.caps
    if caps is None:
        rows.append(_row("Requested PDO", None, _w(_bits(pdo34, 0, 32)) if pdo34 else None, "0x34 bits 31:0",
                         SKIPPED, "no Source_Capabilities before the Request: the capture started late"))
        return rows
    pdos = caps.objects
    wire_pdo = pdos[position - 1] if 1 <= position <= len(pdos) else None
    if wire_pdo is None:
        rows.append(_row("Requested PDO", None, None, "0x34 bits 31:0", MISMATCH,
                         f"Object Position {position} names no PDO of the {len(pdos)} on the wire"))
    else:
        rows.append(_compare(f"Requested PDO (position {position})", wire_pdo,
                             _bits(pdo34, 0, 32) if pdo34 else None, "0x34 bits 31:0"))
    if pdos:
        control = (pdos[0] >> 20) & 0x3FF
        rows.append(_compare("PDO 1 bits 29:20", control, _bits(pdo34, 32, 10) if pdo34 else None,
                             "0x34 bits 41:32", "mapping confirmed on hardware, not stated by the TRM",
                             width=3))
    return rows


def _identity_rows(acks: dict[tuple[str, str], Message], regs: dict[int, bytes], role: str) -> list[dict]:
    rows = []
    partner_power = "source" if role == "sink" else "sink"
    partner = [m for (sop, sender), m in acks.items() if sop == "SOP" and sender.startswith(partner_power)]
    data = regs.get(0x48)
    if partner:
        ack = max(partner, key=lambda m: m.line)
        vdos = ack.objects[1:]
        note = (f"from {ack.sender()} at {ack.where()}, PD {ack.spec_revision} header, "
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
                         f"no Discover Identity ACK from the {partner_power} on SOP, and 0x48 is not in the "
                         "snapshot"))
    for (sop, sender), m in sorted(acks.items(), key=lambda kv: kv[1].line):
        if m in partner:
            continue
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
    if not capture.vbus:
        return None
    meter = capture.vbus[-1]
    if adc is None or len(adc) < 4:
        return _row("VBUS (meter, last status)", f"{meter} mV", None, "0x6A byte 3", MISSING,
                    "register not in the snapshot")
    controller = adc[3] * ADC_VBUS_LSB_MV
    allowance = ADC_VBUS_LSB_MV + round(VBUS_GAIN_ALLOWANCE * meter)
    ok = abs(meter - controller) <= allowance
    return _row("VBUS (meter, last status)", f"{meter} mV", f"{controller} mV (0x{adc[3]:02x})", "0x6A byte 3",
                MATCH if ok else MISMATCH, f"allowed difference {allowance} mV")


def sanity(capture: Capture, contract: Contract | None, caps: Message | None) -> tuple[list[str], bool]:
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
    if caps is None and contract is None and flat:
        notes.append("no Source_Capabilities, no Request, VBUS flat: no negotiation happened in this "
                     "window; check the stimulus, not the protocol")
        return notes, True
    if contract is not None and contract.caps is None:
        notes.append(f"Request at {contract.request.where()} has no Source_Capabilities before it: the "
                     "capture started late")
    if contract is None:
        notes.append("no Request: only the capabilities can be compared")
    elif not contract.complete:
        notes.append("the last Request has no PS_RDY after an Accept: no completed contract")
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


def crosscheck(capture_path: str | Path, registers_path: str | Path, role: str | None = None) -> dict:
    """The comparison table for one capture and one snapshot, with a verdict."""
    capture = load_capture(capture_path)
    regs = load_registers(registers_path)
    board, role_from, role_notes = board_role(regs, role)
    contract, caps = select_contract(capture.messages)
    notes, blocked = sanity(capture, contract, caps)
    notes = role_notes + notes
    rows: list[dict] = []
    if not blocked:
        if caps is not None and caps.is_data(DATA_SOURCE_CAPABILITIES):
            rows += _caps_rows(caps, regs, board)
        elif caps is not None:
            rows.append(_row("EPR_Source_Capabilities", caps.wire.hex(), None, "", SKIPPED,
                             "EPR capabilities are not compared by this tool"))
        if contract is not None and not contract.epr:
            rows += _contract_rows(contract, regs, board)
        rows += _identity_rows(identity_acks(capture.messages), regs, board)
    vbus = _vbus_row(capture, regs)
    if vbus is not None:
        rows.append(vbus)
    compared = [r for r in rows if r["result"] in (MATCH, MISMATCH)]
    if blocked:
        verdict = "no negotiation"
    elif any(r["result"] == MISMATCH for r in rows):
        verdict = "fail"
    elif contract is None or not contract.complete or contract.caps is None or contract.epr \
            or any(r["result"] == MISSING for r in rows) or not compared:
        verdict = "inconclusive"
    else:
        verdict = "pass"
    return {
        "capture": str(capture.path.resolve()),
        "registers": str(Path(registers_path).expanduser().resolve()),
        "role": board,
        "role_from": role_from,
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
    parser.add_argument("--json", action="store_true", help="print the result as JSON")
    args = parser.parse_args(argv)
    try:
        result = crosscheck(args.capture, args.registers, args.role)
    except CrosscheckError as exc:
        print(f"tps25751-pd-crosscheck: {exc}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=1) if args.json else render_table(result))
    return 1 if result["verdict"] == "fail" else 0


if __name__ == "__main__":
    sys.exit(main())
