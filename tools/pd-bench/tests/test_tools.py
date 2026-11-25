"""The six tools against the simulated console."""

import importlib.util
import json
from pathlib import Path

import pytest

from fakes import AUTONEG_RESET, CONSOLE_PORT, SILENT, SNAPSHOT_ORDER
from tps25751_pd_bench import registers

REPO_ROOT = Path(__file__).resolve().parents[3]


# ----- bench_hello -----

def test_hello(bench):
    assert bench.tools.bench_hello() == {
        "port": CONSOLE_PORT,
        "proto": 1,
        "lib": "3.1.0",
        "build": "Nov 23 2026 10:11:12",
        "uptime_ms": 123456,
        "mode": "APP",
        "version": "f8091166",
        "role": "sink",
        "plug": True,
        "patch_bundle": False,
        "patch_pending": False,
        "patch_source": "eeprom",
        "swaps": False,
    }


def test_hello_is_fresh_on_every_call(bench):
    bench.tools.bench_hello()
    bench.console.role = "source"
    assert bench.tools.bench_hello()["role"] == "source"
    assert bench.requests == ["HELLO", "HELLO"]


def test_hello_reports_a_pending_patch_without_loading_it(bench):
    # HELLO is read-only: a controller in PTCH mode is reported, not patched.
    bench.console.mode = "PTCH"
    bench.console.patch_bundle = True
    result = bench.tools.bench_hello()
    assert (result["mode"], result["patch_bundle"], result["patch_pending"]) == ("PTCH", True, True)
    assert "bench_renegotiate('restart_cold')" in result["warning"]
    assert "patch_reload" not in result
    assert bench.requests == ["HELLO"]

    # Without a bundle nothing is pending.
    bench.console.patch_bundle = False
    result = bench.tools.bench_hello()
    assert result["patch_pending"] is False and "warning" not in result


def test_hello_reports_a_silent_controller(bench):
    bench.console.script["HELLO"] = [[
        "proto=1 lib=unknown build=Nov__3_2026_09:00:00 uptime_ms=900",
        "mode=? version=? role=? plug=?",
        "patch_bundle=1 patch_pending=0 patch_source=? swaps=1",
        "OK",
    ]]
    result = bench.tools.bench_hello()
    assert result["build"] == "Nov  3 2026 09:00:00"
    assert (result["mode"], result["role"], result["plug"]) == ("?", "?", None)
    assert (result["patch_bundle"], result["swaps"]) == (True, True)
    assert "does not answer on I2C" in result["warning"]


# ----- bench_read -----

def test_read_raw(bench):
    assert bench.tools.bench_read("0x34") == {
        "address": "0x34", "name": "ACTIVE_PDO_CONTRACT", "length": 6, "data": "d64106009800"}
    assert bench.tools.bench_read(0x35)["name"] == "ACTIVE_RDO_CONTRACT"
    assert bench.requests[-2:] == ["RD 0x34", "RD 0x35"]


def test_read_decoded(bench):
    result = bench.tools.bench_read("0x1A", decode=True)
    assert result["data"] == "0d00000000"
    assert result["decode"] == ["=== Status Register (0x1A) ===", "  Plug Present: Yes", ""]
    assert bench.requests[-1] == "DEC 0x1a"


def test_read_decoded_without_a_register_class(bench):
    bench.console.registers[0x73] = bytes.fromhex("0102")
    result = bench.tools.bench_read("0x73", decode=True)
    assert result == {"address": "0x73", "length": 2, "data": "0102",
                      "decode_error": "the library has no register class for this address"}


def test_read_errors(bench):
    assert bench.tools.bench_read("0x100")["kind"] == "ValueError"
    assert bench.requests == []                     # refused before the port was touched

    failed = bench.tools.bench_read("0xEE")
    assert failed["kind"] == "BenchCommandError" and failed["code"] == "I2C"
    assert bench.tools.bench_read("0xEE", decode=True)["code"] == "I2C"


def test_log_lines_and_events_come_back_with_the_result(bench):
    bench.console.noise = ["[bench] noise", "! PLUG removed"]
    result = bench.tools.bench_read("0x34")
    assert result["data"] == "d64106009800"
    assert result.get("log") is None or "[bench] noise" in result["log"]
    decoded = bench.tools.bench_read("0x1A", decode=True)
    assert "[bench] noise" in decoded["log"] and "PLUG removed" in decoded["events"]


# ----- bench_task -----

def test_task(bench):
    result = bench.tools.bench_task("GSkC")
    assert result["task"] == "GSkC"
    assert (result["return_code"], result["task_result"], result["task_ok"]) == (0, "completed", True)
    assert result["data"] == "00" * 64
    assert result["sent_utc"] <= result["finished_utc"]
    assert bench.requests[-1] == "CMD GSkC"


def test_task_with_input_data(bench):
    bench.tools.bench_task("I2Cr", "6B 1b 02")
    assert bench.requests[-1] == "CMD I2Cr 6b1b02"


def test_task_return_code_meanings(bench):
    bench.console.script["CMD SSrC"] = [["task=SSrC status=success rc=0x03", "data=03" + "00" * 63, "OK"]]
    rejected = bench.tools.bench_task("SSrC")
    assert (rejected["task_result"], rejected["task_ok"]) == ("rejected", False)

    # MBRd returns the message size in DATA byte 1, not a return code (TRM Table 4-12).
    bench.console.script["CMD MBRd 000001"] = [["task=MBRd status=success rc=0x18", "data=18" + "00" * 63, "OK"]]
    message = bench.tools.bench_task("MBRd", "000001")
    assert message["return_code"] == 0x18 and "task_result" not in message

    assert registers.task_result("GSrC", 0x01) == "timed out or aborted"
    assert registers.task_result("GPPI", 0x04) == "rejected, Rx buffer locked"
    assert registers.task_result("ANeg", 0x15) == "error 0x5"


def test_task_refused_by_the_console(bench):
    result = bench.tools.bench_task("PBMs", "640000003031")
    assert result["kind"] == "BenchCommandError" and result["code"] == "REFUSED"


def test_task_argument_checks(bench):
    assert bench.tools.bench_task("GSr")["kind"] == "ValueError"
    assert bench.tools.bench_task("GS C")["kind"] == "ValueError"
    assert bench.tools.bench_task("GPPI", "abc")["kind"] == "ValueError"
    assert bench.tools.bench_task("GPPI", "00" * 65)["kind"] == "ValueError"
    assert bench.requests == []


def test_task_restart_reports_the_steps(bench):
    bench.console.script["CMD Gaid"] = [[
        "step=issue task=Gaid status=timeout t_ms=5105",
        "step=back evidence=data_cleared t_ms=6305",
        "step=done mode=APP t_ms=6305", "OK"]]
    result = bench.tools.bench_task("Gaid")
    assert [s["step"] for s in result["restart_steps"]] == ["issue", "back", "done"]
    assert result["restart_steps"][1] == {"step": "back", "evidence": "data_cleared", "t_ms": 6305}


def test_task_restart_reports_a_patch_step_that_sent_nothing(bench):
    # The controller reached APP by itself while the patch loader waited: the
    # console reports the loader's status with sent=0 and answers OK.
    bench.console.script["CMD GAID"] = [[
        "step=issue task=GAID status=timeout t_ms=5105",
        "step=back evidence=data_cleared t_ms=6305",
        "step=mode t_ms=6305 mode=PTCH waited_ms=0",
        "step=patch result=CommandTimeout t_ms=14305 sent=0",
        "step=done mode=APP t_ms=14305", "OK"]]
    result = bench.tools.bench_task("GAID")
    assert "kind" not in result
    assert result["restart_steps"][3] == {
        "step": "patch", "result": "CommandTimeout", "t_ms": 14305, "sent": False}
    assert result["restart_steps"][4]["mode"] == "APP"


def test_task_restart_reports_a_patch_step_that_loaded(bench):
    bench.console.script["CMD GAID"] = [[
        "step=patch result=Success t_ms=9000 sent=1", "step=done mode=APP t_ms=9000", "OK"]]
    result = bench.tools.bench_task("GAID")
    assert result["restart_steps"][0] == {"step": "patch", "result": "Success", "t_ms": 9000, "sent": True}


# ----- bench_write -----

def test_write(bench):
    result = bench.tools.bench_write("0x42", "1A 00 08 10")
    assert result["address"] == "0x42" and result["name"] == "PD3_CONFIGURATION"
    assert (result["data"], result["written"], result["match"]) == ("1a000810", "1a000810", True)
    assert bench.requests[-1] == "WR 0x42 1a000810"


def test_write_refusals_and_errors(bench):
    refused = bench.tools.bench_write("0x33", "00")
    assert refused["code"] == "REFUSED"
    short = bench.tools.bench_write("0x42", "1a0008")
    assert short["code"] == "BADARG" and "4 bytes, got 3" in short["error"]
    assert bench.tools.bench_write("0x42", "")["kind"] == "ValueError"
    assert bench.tools.bench_write("0x42", "xyz")["kind"] == "ValueError"


def test_write_reports_a_readback_that_differs(bench):
    bench.console.script["WR 0x42 ffffffff"] = [["reg=0x42 len=4 data=1a1f0f1d match=0", "OK"]]
    result = bench.tools.bench_write(0x42, "ffffffff")
    assert (result["match"], result["data"], result["written"]) == (False, "1a1f0f1d", "ffffffff")


# ----- bench_snapshot -----

def test_snapshot_format(bench):
    result = bench.tools.bench_snapshot()
    assert result["format"] == "tps25751-registers" and result["version"] == 1
    assert result["read_utc"].endswith("Z")
    assert [int(r["address"], 16) for r in result["registers"]] == SNAPSHOT_ORDER
    assert result["int_event1_cleared"] is True
    assert result["uptime_ms"] == 123999

    by_address = {r["address"]: r for r in result["registers"]}
    assert by_address["0x34"] == {"address": "0x34", "name": "ACTIVE_PDO_CONTRACT", "length": 6,
                                  "data": "d64106009800"}
    # Addresses are uppercase hex; data is lowercase, as the fixture generator requires.
    assert "0x6A" in by_address and by_address["0x1A"]["name"] == "STATUS"
    for register in result["registers"]:
        assert len(register["data"]) == 2 * register["length"]
        assert register["data"] == register["data"].lower()
    assert "written_to" not in result


def test_snapshot_keeps_the_length_byte_as_read(bench):
    # A controller that reports 15 bytes for 0x27, where the library expects 14.
    bench.console.registers[0x37] = AUTONEG_RESET + b"\x00"
    result = bench.tools.bench_snapshot()
    autoneg = next(r for r in result["registers"] if r["address"] == "0x37")
    assert autoneg["length"] == 25 and len(autoneg["data"]) == 50


def test_snapshot_writes_registers_json(bench, tmp_path):
    target = tmp_path / "bundle" / "registers.json"
    result = bench.tools.bench_snapshot(str(target))
    assert result["written_to"] == str(target.resolve())
    assert result["replaced_existing_file"] is False

    document = json.loads(target.read_text())
    # The file holds the format's four keys and nothing else.
    assert list(document) == ["format", "version", "read_utc", "registers"]
    assert document["registers"] == result["registers"]
    assert target.read_text().endswith("\n")
    assert not list(target.parent.glob("*.tmp"))

    again = bench.tools.bench_snapshot(str(target))
    assert again["replaced_existing_file"] is True

    # A directory gets registers.json inside it.
    in_directory = bench.tools.bench_snapshot(str(tmp_path))
    assert in_directory["written_to"] == str((tmp_path / "registers.json").resolve())


def test_snapshot_file_is_accepted_by_the_fixture_generator(bench, tmp_path):
    """The library's own reader of tps25751-registers version 1 takes the file as written."""
    generator = REPO_ROOT / "test" / "support" / "pd_capture_fixture.py"
    if not generator.exists():
        pytest.skip("not inside the library checkout")
    spec = importlib.util.spec_from_file_location("pd_capture_fixture", generator)
    module = importlib.util.module_from_spec(spec)
    import sys
    sys.modules[spec.name] = module
    try:
        spec.loader.exec_module(module)
        target = tmp_path / "registers.json"
        bench.tools.bench_snapshot(str(target))
        parsed = module.parse_registers(target)
    finally:
        sys.modules.pop(spec.name, None)

    assert [reg.address for reg in parsed] == sorted(SNAPSHOT_ORDER)
    contract = next(reg for reg in parsed if reg.address == 0x34)
    assert (contract.length, contract.data.hex()) == (6, "d64106009800")


def test_snapshot_with_a_failed_register_is_an_error_with_the_partial_reply(bench):
    bench.console.script["SNAP"] = [["uptime_ms=1", "reg=0x03 len=4 data=41505020", "reg=0x30 error=I2C",
                                     "int_clear=ok", "ERR I2C 1 of 17 registers did not answer"]]
    result = bench.tools.bench_snapshot()
    assert result["code"] == "I2C" and "1 of 17" in result["error"]
    assert "reg=0x30 error=I2C" in result["reply_lines"]


def test_snapshot_refuses_to_store_a_zero_length_register(bench, tmp_path):
    bench.console.registers[0x48] = b""
    target = tmp_path / "registers.json"
    result = bench.tools.bench_snapshot(str(target))
    assert result["kind"] == "ValueError" and "0x48" in result["error"]
    assert not target.exists()
    # Without a path the snapshot is still returned as read.
    assert bench.tools.bench_snapshot()["registers"][14]["length"] == 0


def test_snapshot_reports_a_clear_that_did_not_happen(bench):
    lines = bench.console.reply_lines("SNAP")
    bench.console.script["SNAP"] = [[line.replace("int_clear=ok", "int_clear=failed") for line in lines]]
    assert bench.tools.bench_snapshot()["int_event1_cleared"] is False
