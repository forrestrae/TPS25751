"""The capture-to-snapshot comparison behind bench_crosscheck and tps25751-pd-crosscheck."""

import json
from pathlib import Path

import pytest

from tps25751_pd_bench import crosscheck
from tps25751_pd_bench.crosscheck import MATCH, MISMATCH, MISSING, REPORT, SKIPPED, CrosscheckError

REPO_ROOT = Path(__file__).resolve().parents[3]
APPLE_BUNDLE = REPO_ROOT / "test" / "fixtures" / "pd-captures" / "2026-11-23-apple-140w-tps25751"

# The Apple 140 W charger's Source_Capabilities and the board's Request, from the real
# bundle (and test_TPS25751Pdo.cpp, kRx140W).
APPLE_PDOS = [0x0981912C, 0x0002D12C, 0x0004B12C, 0x000641D6]
APPLE_RDO = 0x4184B12C
# A partner's Discover Identity ACK: ID Header (Table 6.34, VID 0x05AC), Cert Stat, Product.
PARTNER_VDOS = [0x6C0005AC, 0x00000000, 0x12340100]
IDENTITY_ACK_VDM = 0xFF008041  # SVID PD SID, structured, SVDM 2.x, ACK, Discover Identity

CTRL = {"GoodCRC": 1, "Accept": 3, "Reject": 4, "PS_RDY": 6, "Soft_Reset": 13}


def header(msg_type, objects=0, source=False, dfp=False, msg_id=0, rev=2, cable=None):
    """A USB PD message header (Table 6.1): bit 8 is Power Role or Cable Plug, bit 5 Data Role."""
    role_bit = (cable if cable is not None else source)
    return (objects << 12) | (msg_id << 9) | (int(role_bit) << 8) | (rev << 6) | (int(dfp) << 5) | msg_type


def wire(head, objects=()):
    return (head.to_bytes(2, "little") + b"".join(o.to_bytes(4, "little") for o in objects)).hex()


class CaptureFile:
    """A km003c-capture v1 file built record by record."""

    def __init__(self):
        self.records = [{"type": "header", "format": "km003c-capture", "version": 1,
                         "meter": {"serial": "000000", "fw_version": "2.0.0"},
                         "pd_monitor": {"enabled": True, "outcome": "accepted"}}]
        self.t = 1000

    def status(self, vbus_mv):
        self.records.append({"type": "status", "t_meter_ms": self.t, "vbus_mv": vbus_mv})
        return self

    def conn(self, event="connect"):
        self.records.append({"type": "conn", "t_meter_ms": self.t, "code": 17, "event": event})
        return self

    def pd(self, head, objects=(), sop="SOP", goodcrc=True):
        self.t += 2
        self.records.append({"type": "pd", "i": len(self.records), "t_meter_ms": self.t, "sop": sop,
                             "wire": wire(head, objects)})
        if goodcrc:
            # The receiver's GoodCRC: the opposite power and data role on SOP.
            sender_source = bool(head & (1 << 8))
            self.records.append({"type": "pd", "i": len(self.records), "t_meter_ms": self.t, "sop": sop,
                                 "wire": wire(header(1, source=not sender_source, dfp=not sender_source,
                                                     rev=1))})
        return self

    def source_caps(self, pdos=APPLE_PDOS, **kw):
        return self.pd(header(1, len(pdos), source=True, dfp=True), pdos, **kw)

    def request(self, rdo=APPLE_RDO, from_source=False):
        return self.pd(header(2, 1, source=from_source, dfp=from_source), [rdo])

    def control(self, name, from_source=True):
        return self.pd(header(CTRL[name], source=from_source, dfp=from_source))

    def contract(self, pdos=APPLE_PDOS, rdo=APPLE_RDO):
        """Source_Capabilities from the Source, whichever side the board is, then the Sink's Request."""
        self.source_caps(pdos)
        self.request(rdo)
        self.control("Accept", from_source=True)
        return self.control("PS_RDY", from_source=True)

    def identity_ack(self, vdos=PARTNER_VDOS, source=True, sop="SOP", cable=None):
        return self.pd(header(15, 1 + len(vdos), source=source, dfp=source, cable=cable),
                       [IDENTITY_ACK_VDM, *vdos], sop=sop)

    def footer(self):
        self.records.append({"type": "footer", "reason": "contract"})
        return self

    def write(self, path):
        path.write_text("\n".join(json.dumps(r) for r in self.records) + "\n", encoding="utf-8")
        return path


def le(*words):
    return b"".join(w.to_bytes(4, "little") for w in words)


def sink_registers(pdos=APPLE_PDOS, rdo=APPLE_RDO, vdos=PARTNER_VDOS, vbus_raw=209, status_role=0):
    """A snapshot of the board sinking from the Apple charger, packed from the TRM layouts."""
    r30 = bytes([len(pdos)]) + le(*pdos) + bytes(53 - 1 - 4 * len(pdos))
    r34 = le(pdos[3]) + ((pdos[0] >> 20) & 0x3FF).to_bytes(2, "little")
    r35 = le(rdo) + bytes(8) + le(rdo)
    r48 = bytes([(1 << 6) | len(vdos)]) + le(*vdos) + bytes(24 - 4 * len(vdos))
    r1a = bytes([0x01 | (status_role << 5), 0, 0, 0, 0])  # bit 0 plug present, bit 5 port role
    r6a = bytes([0, 0, 0, vbus_raw]) + bytes(9)
    return {0x1A: r1a, 0x30: r30, 0x34: r34, 0x35: r35, 0x48: r48, 0x6A: r6a}


def write_registers(path, regs):
    document = {"format": "tps25751-registers", "version": 1, "read_utc": None,
                "registers": [{"address": f"0x{a:02X}", "length": len(d), "data": d.hex()}
                              for a, d in regs.items()]}
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


def run(tmp_path, capture, regs, role=None):
    return crosscheck.crosscheck(capture.write(tmp_path / "capture.jsonl"),
                                 write_registers(tmp_path / "registers.json", regs), role)


def rows_by_check(result):
    return {row["check"]: row for row in result["rows"]}


def sink_capture(vbus_mv=20480):
    return (CaptureFile().status(5).conn().contract().identity_ack().status(vbus_mv).footer())


# ----- the real bundle -----

@pytest.mark.skipif(not APPLE_BUNDLE.is_dir(), reason="needs the library checkout's fixture bundle")
def test_the_apple_bundle_matches_word_for_word():
    result = crosscheck.crosscheck(APPLE_BUNDLE / "capture.jsonl", APPLE_BUNDLE / "registers.json")
    rows = rows_by_check(result)
    assert result["role"] == "sink" and result["role_from"].startswith("assumed")
    assert rows["Source_Capabilities PDO count"]["result"] == MATCH
    assert [rows[f"PDO {p}"]["wire"] for p in range(1, 5)] == ["0981912c", "0002d12c", "0004b12c", "000641d6"]
    assert all(rows[f"PDO {p}"]["result"] == MATCH for p in range(1, 5))
    assert rows["Request RDO (own)"]["wire"] == "4184b12c" and rows["Request RDO (own)"]["result"] == MATCH
    assert rows["Requested PDO (position 4)"]["result"] == MATCH
    assert rows["PDO 1 bits 29:20"] == {**rows["PDO 1 bits 29:20"], "wire": "098", "register": "098",
                                         "result": MATCH}
    # The cable's and the board's own ACKs are reported, not compared.
    assert rows["SOP' Discover Identity ACK (cable)"]["result"] == REPORT
    assert "connector type 00b" in rows["SOP' Discover Identity ACK (cable)"]["note"]
    assert "PD 2.0 header, SVDM version 1.0" in rows["SOP' Discover Identity ACK (cable)"]["note"]
    assert rows["SOP Discover Identity ACK (sink/ufp)"]["result"] == REPORT
    assert rows["Partner Discover Identity ACK"]["result"] == SKIPPED
    # The synthesized registers.json has no 0x6A, so the verdict cannot be pass.
    assert rows["VBUS (meter, last status)"]["result"] == MISSING
    assert result["verdict"] == "inconclusive"


# ----- comparisons -----

def test_a_full_sink_snapshot_passes(tmp_path):
    result = run(tmp_path, sink_capture(), sink_registers())
    rows = rows_by_check(result)
    assert result["role"] == "sink" and result["role_from"] == "STATUS (0x1A) bit 5"
    assert rows["0x48 Response Type"]["result"] == MATCH
    assert rows["0x48 Number Valid VDOs"]["result"] == MATCH
    assert [rows[f"Identity VDO {k}"]["wire"] for k in (1, 2, 3)] == ["6c0005ac", "00000000", "12340100"]
    assert rows["Identity VDO 1"]["where"] == "0x48 bits 39:8"
    assert rows["0x35 bits 127:96"]["result"] == REPORT
    assert rows["VBUS (meter, last status)"]["result"] == MATCH  # 209 x 98 = 20482 mV
    assert not [r for r in result["rows"] if r["result"] == MISMATCH]
    assert result["verdict"] == "pass"


def test_a_broken_first_pdo_control_mapping_fails(tmp_path):
    # The Phase 7 check: a library that put PDO 1 bits 29:20 anywhere else in 0x34.
    regs = sink_registers()
    regs[0x34] = regs[0x34][:4] + (0x098 << 1).to_bytes(2, "little")
    result = run(tmp_path, sink_capture(), regs)
    row = rows_by_check(result)["PDO 1 bits 29:20"]
    assert (row["wire"], row["register"], row["result"]) == ("098", "130", MISMATCH)
    assert result["verdict"] == "fail"


def test_each_wrong_word_is_named(tmp_path):
    regs = sink_registers()
    regs[0x30] = regs[0x30][:5] + le(0x0002D12D) + regs[0x30][9:]
    regs[0x35] = le(APPLE_RDO ^ 0x400) + regs[0x35][4:]
    regs[0x48] = regs[0x48][:5] + le(0x1) + regs[0x48][9:]
    rows = rows_by_check(run(tmp_path, sink_capture(), regs))
    assert rows["PDO 2"]["result"] == MISMATCH and rows["PDO 2"]["where"] == "0x30 bytes 5..8"
    assert rows["Request RDO (own)"]["result"] == MISMATCH
    assert rows["Identity VDO 2"]["result"] == MISMATCH
    assert rows["PDO 1"]["result"] == MATCH


def test_a_count_mismatch_lists_the_extra_pdo(tmp_path):
    regs = sink_registers()
    regs[0x30] = bytes([5]) + regs[0x30][1:17] + le(0x0004B12C) + regs[0x30][21:]
    rows = rows_by_check(run(tmp_path, sink_capture(), regs))
    assert rows["Source_Capabilities PDO count"]["result"] == MISMATCH
    assert (rows["PDO 5"]["wire"], rows["PDO 5"]["result"]) == (None, MISMATCH)


def test_the_source_role_uses_0x32_and_the_partner_request(tmp_path):
    board_pdos = [0x0A01912C, 0x0002D12C]
    partner_rdo = 0x2205812C  # Object Position 2
    capture = CaptureFile().conn().contract(board_pdos, partner_rdo) \
        .identity_ack(source=False).status(9000).footer()
    r32 = bytes([2, 0, 0]) + le(*board_pdos) + bytes(63 - 3 - 8)
    r34 = le(board_pdos[1]) + ((board_pdos[0] >> 20) & 0x3FF).to_bytes(2, "little")
    r35 = le(0x11111111) + bytes(8) + le(partner_rdo)  # bits 31:0 are not this exchange's
    regs = {0x1A: bytes([0x21, 0, 0, 0, 0]), 0x32: r32, 0x34: r34, 0x35: r35,
            0x48: sink_registers()[0x48], 0x6A: bytes([0, 0, 0, 92]) + bytes(9)}
    result = run(tmp_path, capture, regs)
    rows = rows_by_check(result)
    assert result["role"] == "source"
    assert rows["PDO 1"]["where"] == "0x32 bytes 3..6" and rows["PDO 1"]["result"] == MATCH
    assert rows["Request RDO (partner)"]["where"] == "0x35 bits 127:96"
    assert rows["Request RDO (partner)"]["result"] == MATCH
    assert "Request RDO (own)" not in rows
    assert rows["Requested PDO (position 2)"]["result"] == MATCH
    assert rows["Identity VDO 1"]["result"] == MATCH  # the iPhone answers as sink/ufp
    assert result["verdict"] == "pass"


# ----- the final contract and retransmissions -----

def test_retransmitted_source_capabilities_count_once(tmp_path):
    capture = CaptureFile().conn()
    for _ in range(25):
        capture.source_caps(goodcrc=False)
    capture.contract().identity_ack().status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    assert result["verdict"] == "pass"
    note = next(n for n in result["sanity"] if "retransmissions" in n)
    # 24 repeats, and the contract's own Source_Capabilities repeats the last one too.
    assert note.startswith("25 retransmissions") and "PTCH" in note


def test_a_soft_reset_abandons_the_negotiation_in_progress(tmp_path):
    other = 0x3184B12C  # Object Position 3
    capture = CaptureFile().conn().contract().source_caps().request(other).control("Soft_Reset") \
        .identity_ack().status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    rows = rows_by_check(result)
    assert rows["Request RDO (own)"]["wire"] == "4184b12c"
    assert result["verdict"] == "pass"


def test_a_request_without_ps_rdy_is_inconclusive(tmp_path):
    capture = CaptureFile().conn().source_caps().request().control("Accept").status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    assert "no PS_RDY after an Accept" in rows_by_check(result)["Request RDO (own)"]["note"]
    assert result["verdict"] == "inconclusive"


def test_a_late_start_skips_the_pdo_rows(tmp_path):
    capture = CaptureFile().conn().request().control("Accept").control("PS_RDY").status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    rows = rows_by_check(result)
    assert any("started late" in n for n in result["sanity"])
    assert rows["Requested PDO"]["result"] == SKIPPED
    assert result["verdict"] == "inconclusive"


# ----- sanity rules -----

def test_only_connection_records_is_no_pd_traffic(tmp_path):
    result = run(tmp_path, CaptureFile().conn().conn("disconnect").status(5000).footer(), sink_registers())
    assert result["verdict"] == "no negotiation"
    assert "no PD traffic" in result["sanity"][0] and "pd_monitor" in result["sanity"][0]
    assert [r["check"] for r in result["rows"]] == ["VBUS (meter, last status)"]


def test_no_capabilities_and_flat_vbus_is_no_negotiation(tmp_path):
    capture = CaptureFile().status(20400).identity_ack().status(20500).footer()
    result = run(tmp_path, capture, sink_registers())
    assert result["verdict"] == "no negotiation"
    assert "check the stimulus" in result["sanity"][0]


def test_vbus_outside_the_allowance_is_a_mismatch(tmp_path):
    result = run(tmp_path, sink_capture(vbus_mv=19800), sink_registers(vbus_raw=209))
    row = rows_by_check(result)["VBUS (meter, last status)"]
    assert row["result"] == MISMATCH and "allowed difference 296 mV" in row["note"]


def test_no_partner_ack_reports_what_0x48_holds(tmp_path):
    capture = CaptureFile().conn().contract().status(20480).footer()
    rows = rows_by_check(run(tmp_path, capture, sink_registers()))
    assert rows["0x48"]["result"] == REPORT and "earlier exchange" in rows["0x48"]["note"]


# ----- the board's role -----

def test_status_wins_over_a_given_role(tmp_path):
    result = run(tmp_path, sink_capture(), sink_registers(), role="source")
    assert result["role"] == "sink" and "using STATUS" in result["sanity"][0]


def test_a_given_role_is_used_without_status(tmp_path):
    regs = sink_registers()
    del regs[0x1A]
    regs[0x32] = bytes(63)
    assert run(tmp_path, sink_capture(), regs, role="sink")["role_from"] == "given"
    with pytest.raises(CrosscheckError, match="give role"):
        run(tmp_path, sink_capture(), regs)


# ----- inputs and the command line -----

def test_files_in_other_formats_are_refused(tmp_path):
    bad = tmp_path / "bad.jsonl"
    bad.write_text(json.dumps({"type": "header", "format": "other", "version": 1}) + "\n")
    regs = write_registers(tmp_path / "registers.json", sink_registers())
    with pytest.raises(CrosscheckError, match="not a km003c-capture"):
        crosscheck.crosscheck(bad, regs)
    (tmp_path / "r.json").write_text('{"format": "tps25751-registers", "version": 2}')
    with pytest.raises(CrosscheckError, match="not a tps25751-registers"):
        crosscheck.crosscheck(sink_capture().write(tmp_path / "c.jsonl"), tmp_path / "r.json")
    with pytest.raises(CrosscheckError, match="no-such"):
        crosscheck.crosscheck(tmp_path / "no-such.jsonl", regs)


def test_the_command_line_exit_status(tmp_path, capsys):
    capture = sink_capture().write(tmp_path / "c.jsonl")
    good = write_registers(tmp_path / "good.json", sink_registers())
    assert crosscheck.main([str(capture), str(good)]) == 0
    out = capsys.readouterr().out
    assert out.startswith("Board role: sink") and "| PDO 4 | 000641d6 | 000641d6 |" in out
    assert out.rstrip().endswith("Verdict: pass")
    broken = sink_registers()
    broken[0x34] = le(0x0004B12C) + broken[0x34][4:]
    assert crosscheck.main([str(capture), str(write_registers(tmp_path / "bad.json", broken)), "--json"]) == 1
    assert json.loads(capsys.readouterr().out)["verdict"] == "fail"
    assert crosscheck.main([str(tmp_path / "none.jsonl"), str(good)]) == 2


# ----- the MCP tool -----

def test_the_tool_reads_files_and_never_the_serial_port(bench, tmp_path):
    capture = sink_capture().write(tmp_path / "c.jsonl")
    regs = write_registers(tmp_path / "r.json", sink_registers())
    result = bench.tools.bench_crosscheck(str(capture), str(regs))
    assert result["verdict"] == "pass" and result["table"].endswith("Verdict: pass")
    assert bench.requests == []
    assert bench.tools.bench_crosscheck(str(capture), str(regs), role="drp")["kind"] == "ValueError"
    missing = bench.tools.bench_crosscheck(str(tmp_path / "none.jsonl"), str(regs))
    assert missing["kind"] == "CrosscheckError"
