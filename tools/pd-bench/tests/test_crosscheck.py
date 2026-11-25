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

    def __init__(self, provenance=None):
        self.records = [{"type": "header", "format": "km003c-capture", "version": 1,
                         "meter": {"serial": "000000", "fw_version": "2.0.0"},
                         "pd_monitor": {"enabled": True, "outcome": "accepted"},
                         "provenance": provenance}]
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

    def wire_hex(self, text, sop="SOP"):
        """A message given as its wire hex, with spaces allowed, as the meter summary prints it."""
        data = bytes.fromhex(text.replace(" ", ""))
        head = int.from_bytes(data[:2], "little")
        objects = [int.from_bytes(data[o:o + 4], "little") for o in range(2, len(data), 4)]
        return self.pd(head, objects, sop=sop)

    def source_caps(self, pdos=APPLE_PDOS, **kw):
        return self.pd(header(1, len(pdos), source=True, dfp=True), pdos, **kw)

    def request(self, rdo=APPLE_RDO, from_source=False):
        return self.pd(header(2, 1, source=from_source, dfp=from_source), [rdo])

    def control(self, name, from_source=True):
        return self.pd(header(CTRL[name], source=from_source, dfp=from_source))

    def contract(self, pdos=APPLE_PDOS, rdo=APPLE_RDO):
        """Source_Capabilities from the Source, whichever side the board is, then the Sink's Request."""
        self.source_caps(pdos)
        return self.request_accepted(rdo)

    def request_accepted(self, rdo=APPLE_RDO):
        """A Request, Accept, and PS_RDY with no Source_Capabilities, as aneg's ANeg produces them."""
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


def run(tmp_path, capture, regs, role=None, stimulus=None):
    return crosscheck.crosscheck(capture.write(tmp_path / "capture.jsonl"),
                                 write_registers(tmp_path / "registers.json", regs), role, stimulus)


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
    # The 0x34 bits 41:32 mapping is confirmed only with the board sinking.
    assert (rows["PDO 1 bits 29:20"]["result"], rows["PDO 1 bits 29:20"]["wire"]) == (REPORT, "0a0")
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


def test_a_request_without_ps_rdy_is_no_contract(tmp_path):
    # As the fixture generator: a negotiation that did not complete is no contract.
    capture = CaptureFile().conn().source_caps().request().control("Accept").status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    assert "Request RDO (own)" not in rows_by_check(result)
    assert any("no completed contract" in n and "Accept but no PS_RDY" in n for n in result["sanity"])
    assert result["verdict"] == "inconclusive"


@pytest.mark.parametrize("ending, outcome", [("Reject", "Reject"), ("Soft_Reset", "abandoned by a Soft_Reset")])
def test_a_rejected_or_abandoned_request_is_no_contract(tmp_path, ending, outcome):
    capture = CaptureFile().conn().source_caps().request().control(ending).status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    assert "Request RDO (own)" not in rows_by_check(result)
    assert any(outcome in n for n in result["sanity"])


def test_a_failed_request_after_the_contract_is_noted(tmp_path):
    capture = CaptureFile().conn().contract().request(0x3184B12C).control("Reject") \
        .identity_ack().status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    assert rows_by_check(result)["Request RDO (own)"]["wire"] == "4184b12c"
    assert any("after the final contract" in n and "Reject" in n for n in result["sanity"])
    assert result["verdict"] == "pass"


def test_the_boards_own_source_capabilities_are_not_compared(tmp_path):
    # A sink that also advertises Source_Capabilities sends them with power role Sink.
    capture = CaptureFile().conn().contract().pd(header(1, 1, source=False), [0x0001912C]) \
        .identity_ack().status(20480).footer()
    rows = rows_by_check(run(tmp_path, capture, sink_registers()))
    assert rows["PDO 1"]["wire"] == "0981912c" and rows["PDO 1"]["result"] == MATCH


def test_a_late_start_takes_the_pdos_from_the_snapshot(tmp_path):
    capture = CaptureFile().conn().request_accepted().status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    rows = rows_by_check(result)
    assert any("started late" in n for n in result["sanity"])
    row = rows["Requested PDO (position 4, from 0x30)"]
    assert row["result"] == MATCH and "not from the wire" in row["note"] and "started late" in row["note"]
    assert result["verdict"] == "inconclusive"


def test_aneg_compares_the_contract_against_the_snapshot_and_can_pass(tmp_path):
    # Step down to 15 V (position 3), then back to 20 V (position 4): no Source_Capabilities.
    capture = CaptureFile(provenance={"stimulus": "aneg"}).conn().request_accepted(0x3184B12C) \
        .request_accepted(APPLE_RDO).status(20480).footer()
    result = run(tmp_path, capture, sink_registers())
    rows = rows_by_check(result)
    assert result["stimulus"] == "aneg"
    assert rows["Request RDO (own)"]["wire"] == "4184b12c" and rows["Request RDO (own)"]["result"] == MATCH
    assert rows["Requested PDO (position 4, from 0x30)"]["result"] == MATCH
    assert rows["PDO 1 bits 29:20, from 0x30"]["result"] == MATCH
    assert "aneg sends none" in rows["PDO 1 bits 29:20, from 0x30"]["note"]
    assert not any("started late" in n for n in result["sanity"])
    assert result["verdict"] == "pass"
    # The same capture without the header's provenance needs the stimulus given.
    plain = CaptureFile().conn().request_accepted(0x3184B12C).request_accepted().status(20480).footer()
    assert run(tmp_path, plain, sink_registers())["verdict"] == "inconclusive"
    assert run(tmp_path, plain, sink_registers(), stimulus="aneg")["verdict"] == "pass"


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


def test_vbus_is_skipped_without_a_plug(tmp_path):
    regs = sink_registers()
    regs[0x1A] = bytes(5)
    result = run(tmp_path, sink_capture(vbus_mv=0), regs)
    assert "VBUS (meter, last status)" not in rows_by_check(result)
    assert any("no plug" in n for n in result["sanity"])


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


def test_a_data_message_shorter_than_its_header_is_unreadable(tmp_path):
    capture = sink_capture()
    request = next(r for r in capture.records if r.get("wire", "").startswith("8210"))
    request["wire"] = "8210"  # the header declares one data object
    with pytest.raises(CrosscheckError, match=r"declares 1 data objects \(6 bytes\), wire has 2"):
        run(tmp_path, capture, sink_registers())
    assert crosscheck.main([str(tmp_path / "capture.jsonl"), str(tmp_path / "registers.json")]) == 2


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
    short = sink_capture()
    next(r for r in short.records if r.get("wire", "").startswith("8210"))["wire"] = "8210"
    unreadable = bench.tools.bench_crosscheck(str(short.write(tmp_path / "short.jsonl")), str(regs))
    assert unreadable["kind"] == "CrosscheckError" and "declares 1 data objects" in unreadable["error"]
    aneg = CaptureFile().conn().request_accepted().status(20480).footer().write(tmp_path / "aneg.jsonl")
    assert bench.tools.bench_crosscheck(str(aneg), str(regs), stimulus="aneg")["verdict"] == "pass"


# ----- the board as Source, then a PR_Swap (iPhone session, 2026-11-25) -----

# The SOP messages of that capture (cap-port_reset-120508.jsonl), as the meter summary
# printed them, without the GoodCRCs, which the builder adds. The board attaches as
# Source/DFP and sends its Source_Capabilities, the iPhone requests 5 V, the board sends
# PR_Swap and the iPhone accepts, then the iPhone is Source and the board requests from it.
IPHONE_SESSION = [
    "a151 2c910123 2cd10200 2cb10400 f4410600 f4b104e0",   # board Source_Capabilities, PDO 1 0x2301912C
    "8210 2cb18417",                                       # iPhone Request, position 1
    "a303", "a605",                                        # board Accept, PS_RDY
    "8802", "a447 2c910132 2cd10200 2cb10400 2c410600",    # Get_Sink_Cap, iPhone Sink_Capabilities
    "aa09", "8304",                                        # board PR_Swap, iPhone Accept
    "a60a", "8607",                                        # PS_RDY from each side
    "8119 96900137", "a210 96588211", "830b", "860d",      # iPhone caps, board Request, Accept, PS_RDY
    "af14 01a800ff",                                       # board Discover Identity REQ
    "8f43 41a000ff ac0500d5 00000000 66210c71",            # iPhone ACK, VID 0x05AC
    "811f 96900127", "a210 96588211", "8301", "8603",      # the final contract
    "8f1d 01a000ff",                                       # iPhone Discover Identity REQ
    "af4a 41a000ff 51044000 00000000 11090000",            # the board's own ACK, VID 0x0451
]
# 0x32 as read in that session: 5 PDOs, Power Path bytes aa 2a, PDO 1 0x2001912C.
IPHONE_0X32 = bytes.fromhex("05aa2a2c9101202cd102002cb10400f4410600f4b104e0") + bytes(63 - 23)


def iphone_registers():
    final_pdo, final_rdo = 0x27019096, 0x11825896
    return {
        0x1A: bytes([0x01, 0, 0, 0, 0]),                    # plug present, port role Sink
        0x30: bytes([1]) + le(final_pdo) + bytes(48),
        0x32: IPHONE_0X32,
        0x34: le(final_pdo) + ((final_pdo >> 20) & 0x3FF).to_bytes(2, "little"),
        0x35: le(final_rdo) + bytes(8) + le(final_rdo),
        0x48: bytes([(1 << 6) | 3]) + le(0xD50005AC, 0x00000000, 0x710C2166) + bytes(12),
        0x6A: bytes([0, 0, 0, 52]) + bytes(9),               # 52 x 98 = 5096 mV
    }


def iphone_capture():
    capture = CaptureFile(provenance={"stimulus": "port_reset"}).conn()
    for message in IPHONE_SESSION:
        capture.wire_hex(message)
    return capture.status(5141).footer()


def test_after_a_power_swap_each_capabilities_meets_its_own_register(tmp_path):
    result = run(tmp_path, iphone_capture(), iphone_registers())
    rows = rows_by_check(result)
    assert result["role"] == "sink"
    # The final contract: the iPhone's capabilities against 0x30, the board's Request
    # against 0x35 bits 31:0, its PDO against 0x34.
    assert (rows["PDO 1"]["wire"], rows["PDO 1"]["where"], rows["PDO 1"]["result"]) == \
        ("27019096", "0x30 bytes 1..4", MATCH)
    assert rows["Request RDO (own)"]["wire"] == "11825896" and rows["Request RDO (own)"]["result"] == MATCH
    assert rows["Requested PDO (position 1)"]["result"] == MATCH
    # The board's own capabilities from before the swap, against 0x32.
    assert rows["Before PR_Swap: Source_Capabilities PDO count"]["result"] == MATCH
    assert rows["Before PR_Swap: PDO 1"]["where"] == "0x32 bytes 3..6"
    # The iPhone's Request of the first contract (0x1784B12C) is compared with nothing.
    assert not any(r["wire"] == "1784b12c" for r in result["rows"])
    # Its Discover Identity ACK came after the swap, from the Source: the partner.
    assert rows["Identity VDO 1"]["wire"] == "d50005ac" and rows["Identity VDO 1"]["result"] == MATCH
    assert rows["SOP Discover Identity ACK (sink/dfp)"]["result"] == REPORT  # the board's own
    swap = next(n for n in result["sanity"] if n.startswith("PR_Swap"))
    assert "from the board" in swap and "was source before it and sink after it" in swap
    assert any("Before PR_Swap" in n and "not compared" in n for n in result["sanity"])
    assert result["verdict"] == "pass"


def test_the_boards_pdo1_flags_are_reported_not_failed(tmp_path):
    rows = rows_by_check(run(tmp_path, iphone_capture(), iphone_registers()))
    pdo1 = rows["Before PR_Swap: PDO 1"]
    assert (pdo1["wire"], pdo1["register"], pdo1["result"]) == ("2301912c", "2001912c", MATCH)
    assert "29:23" in pdo1["note"]
    dual_role_data = rows["Before PR_Swap: PDO 1 bit 25 Dual-Role Data"]
    assert (dual_role_data["wire"], dual_role_data["register"], dual_role_data["result"]) == ("1", "0", REPORT)
    unchunked = rows["Before PR_Swap: PDO 1 bit 24 Unchunked Extended Messages Supported"]
    assert unchunked["result"] == REPORT and "0x42) bit 4" in unchunked["note"]
    assert not any("bit 29" in check or "bit 23" in check for check in rows)
    # PDOs 2 to 5, the SPR AVS APDO included, match exactly.
    assert [rows[f"Before PR_Swap: PDO {p}"]["wire"] for p in range(2, 6)] == \
        ["0002d12c", "0004b12c", "000641f4", "e004b1f4"]
    assert all(rows[f"Before PR_Swap: PDO {p}"]["result"] == MATCH for p in range(2, 6))


def test_a_board_pdo1_difference_outside_the_flags_fails(tmp_path):
    regs = iphone_registers()
    # Maximum Current 3.0 A in the wire's PDO 1, 2.9 A (0x122) in 0x32.
    regs[0x32] = regs[0x32][:3] + le(0x20019122) + regs[0x32][7:]
    result = run(tmp_path, iphone_capture(), regs)
    assert rows_by_check(result)["Before PR_Swap: PDO 1"]["result"] == MISMATCH
    assert result["verdict"] == "fail"


def test_a_sourcing_board_compares_its_own_capabilities_with_0x32(tmp_path):
    # Without the swap: the board stays Source and its first contract is the final one.
    capture = CaptureFile().conn()
    for message in IPHONE_SESSION[:4]:
        capture.wire_hex(message)
    regs = iphone_registers()
    regs[0x1A] = bytes([0x21, 0, 0, 0, 0])
    regs[0x34] = le(0x2301912C) + ((0x2301912C >> 20) & 0x3FF).to_bytes(2, "little")
    regs[0x35] = le(0) + bytes(8) + le(0x1784B12C)
    result = run(tmp_path, capture.status(5141).footer(), regs)
    rows = rows_by_check(result)
    assert rows["PDO 1"]["where"] == "0x32 bytes 3..6" and rows["PDO 1"]["result"] == MATCH
    assert rows["PDO 1 bit 25 Dual-Role Data"]["result"] == REPORT
    assert rows["Request RDO (partner)"]["result"] == MATCH
    assert not any(n.startswith("PR_Swap") for n in result["sanity"])
