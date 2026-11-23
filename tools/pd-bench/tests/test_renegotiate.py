"""bench_renegotiate: role checks, the six methods, and the 0x37 restore of aneg."""

from fakes import AUTONEG_RESET, SILENT
from tps25751_pd_bench import registers
from tps25751_pd_bench.server import ANEG_SETTLE_S

ORIGINAL_0X37 = AUTONEG_RESET.hex()


def after_hello(bench) -> list[str]:
    assert bench.requests[0] == "HELLO"
    return bench.requests[1:]


def changes_board(requests: list[str]) -> list[str]:
    return [r for r in requests if r.startswith(("WR", "CMD", "RESTART"))]


# ----- refusals -----

def test_unknown_method(bench):
    result = bench.tools.bench_renegotiate("hard_reset")
    assert result["kind"] == "ValueError" and "gsrc" in result["error"]
    assert bench.requests == []


def test_sink_methods_are_refused_when_the_board_is_source(bench):
    bench.console.role = "source"
    for method in ("gsrc", "aneg"):
        result = bench.tools.bench_renegotiate(method)
        assert result["kind"] == "BenchRefusedError"
        assert f"{method} needs the board to be sink and it is source; use ssrc" in result["error"]
        assert result["role"] == "source" and result["steps"] == []
    assert changes_board(bench.requests) == []


def test_source_method_is_refused_when_the_board_is_sink(bench):
    result = bench.tools.bench_renegotiate("ssrc")
    assert result["kind"] == "BenchRefusedError"
    assert "ssrc needs the board to be source and it is sink; use gsrc or aneg" in result["error"]
    assert changes_board(bench.requests) == []


def test_role_methods_need_a_port_partner(bench):
    bench.console.plug = False
    for method in ("gsrc", "aneg", "ssrc"):
        result = bench.tools.bench_renegotiate(method)
        assert result["kind"] == "BenchRefusedError" and "no plug" in result["error"]
    assert changes_board(bench.requests) == []


def test_the_role_is_read_fresh_for_every_call(bench):
    assert "error" not in bench.tools.bench_renegotiate("gsrc")
    bench.console.role = "source"       # a role swap between two calls
    assert bench.tools.bench_renegotiate("gsrc")["kind"] == "BenchRefusedError"
    assert "error" not in bench.tools.bench_renegotiate("ssrc")


def test_only_restarts_run_outside_app_mode(bench):
    bench.console.mode = "PTCH"
    for method in ("gsrc", "aneg", "ssrc", "port_reset"):
        result = bench.tools.bench_renegotiate(method)
        assert result["kind"] == "BenchRefusedError" and "MODE is PTCH" in result["error"]
    assert changes_board(bench.requests) == []
    assert "error" not in bench.tools.bench_renegotiate("restart")


def test_role_unknown_is_refused(bench):
    bench.console.script["HELLO"] = [[
        "proto=1 lib=3.1.0 build=x uptime_ms=1", "mode=APP version=? role=? plug=?",
        "patch_bundle=0 patch_source=? swaps=0", "OK"]]
    result = bench.tools.bench_renegotiate("gsrc")
    assert result["kind"] == "BenchRefusedError"


def test_restart_cold_is_refused_without_a_way_back_to_app(bench):
    bench.console.patch_source = "i2c"
    result = bench.tools.bench_renegotiate("restart_cold")
    assert result["kind"] == "BenchRefusedError" and "PTCH" in result["error"]
    assert changes_board(bench.requests) == []

    # With a bundle compiled in, or a configuration from the EEPROM, it runs.
    bench.console.patch_bundle = True
    assert "error" not in bench.tools.bench_renegotiate("restart_cold")
    bench.console.patch_bundle, bench.console.patch_source = False, "eeprom"
    assert "error" not in bench.tools.bench_renegotiate("restart_cold")


# ----- gsrc, ssrc, restarts -----

def test_gsrc(bench):
    result = bench.tools.bench_renegotiate("gsrc")
    assert after_hello(bench) == ["CMD GSrC"]
    assert result["method"] == "gsrc" and result["role"] == "sink"
    assert "Get_Source_Cap" in result["expect"]
    assert len(result["steps"]) == 1
    step = result["steps"][0]
    assert step["step"] == "CMD GSrC" and step["task_ok"] is True
    # Times for placing a capture mark: the call, and each step's send and finish.
    assert result["started_utc"] <= step["sent_utc"] <= step["finished_utc"] <= result["finished_utc"]
    assert "data" not in step


def test_gsrc_reports_a_task_the_controller_rejected(bench):
    bench.console.script["CMD GSrC"] = [["task=GSrC status=success rc=0x03", "data=03" + "00" * 63, "OK"]]
    step = bench.tools.bench_renegotiate("gsrc")["steps"][0]
    assert (step["task_ok"], step["task_result"]) == (False, "rejected")


def test_ssrc(bench):
    bench.console.role = "source"
    result = bench.tools.bench_renegotiate(" SSrC ".lower())
    assert after_hello(bench) == ["CMD SSrC"]
    assert result["role"] == "source" and result["steps"][0]["step"] == "CMD SSrC"


def test_restart_and_restart_cold(bench):
    warm = bench.tools.bench_renegotiate("restart")
    assert after_hello(bench) == ["RESTART warm"]
    steps = warm["steps"][0]["restart_steps"]
    assert [s["step"] for s in steps] == ["issue", "nak", "back", "done"]
    assert steps[0]["task"] == "Gaid" and steps[2]["evidence"] == "data_cleared"

    cold = bench.tools.bench_renegotiate("restart_cold")
    assert bench.requests[-1] == "RESTART cold"
    assert cold["steps"][0]["restart_steps"][0]["task"] == "GAID"


def test_restart_failure_keeps_the_console_reply(bench):
    bench.console.script["RESTART warm"] = [[
        "step=issue task=Gaid status=success t_ms=5000",
        "ERR TIMEOUT no restart observed within 10000 ms"]]
    result = bench.tools.bench_renegotiate("restart")
    assert result["code"] == "TIMEOUT"
    assert result["reply_lines"] == ["step=issue task=Gaid status=success t_ms=5000"]
    assert result["finished_utc"]


# ----- port_reset -----

def test_port_reset_rewrites_0x28_with_its_own_value(bench):
    current = bench.console.registers[0x28].hex()
    result = bench.tools.bench_renegotiate("port_reset")
    requests = after_hello(bench)
    assert requests[:2] == ["RD 0x28", f"WR 0x28 {current}"]
    assert result["steps"][0]["step"] == "WR 0x28 same value" and result["steps"][0]["match"] is True
    # The port did not disconnect in this simulation, and the result says so.
    assert result["observed_disconnect"] is False
    assert result["status_changes"] == [{"after_s": 0.0, "plug": True}]
    assert set(requests[2:]) == {"RD 0x1a"}


def test_port_reset_reports_the_disconnect_it_sees(bench):
    # STATUS reads unplugged for three polls after the write, then plugged again.
    unplugged = ["reg=0x1a len=5 data=0000000000", "OK"]
    bench.console.script["RD 0x1a"] = [unplugged, unplugged, unplugged]
    result = bench.tools.bench_renegotiate("port_reset")
    assert result["observed_disconnect"] is True
    assert [change["plug"] for change in result["status_changes"]] == [False, True]
    assert result["status_changes"][1]["after_s"] > 0.2


def test_port_reset_tolerates_status_reads_that_fail(bench):
    bench.console.script["RD 0x1a"] = [["ERR I2C register read failed"]]
    result = bench.tools.bench_renegotiate("port_reset")
    assert [change["plug"] for change in result["status_changes"]] == [None, True]
    assert "error" not in result


# ----- aneg -----

def expected_temporary(max_mv: int) -> str:
    return registers.with_host_max_voltage(AUTONEG_RESET, max_mv).hex()


def test_aneg_changes_0x37_runs_aneg_restores_and_runs_aneg_again(bench):
    result = bench.tools.bench_renegotiate("aneg")

    # Contract at 20 V; the source offers 5, 9, 15, and 20 V; so the temporary maximum is 15 V.
    assert result["contract_mv"] == 20000 and result["temporary_max_voltage_mv"] == 15000
    temporary = expected_temporary(15000)
    assert after_hello(bench) == [
        "RD 0x37", "RD 0x34", "RD 0x30",
        f"WR 0x37 {temporary}", "CMD ANeg",
        f"WR 0x37 {ORIGINAL_0X37}", "CMD ANeg",
    ]
    assert [s["step"] for s in result["steps"]] == [
        "WR 0x37 temporary", "CMD ANeg", "WR 0x37 restore", "CMD ANeg"]
    assert result["restored_0x37"] is True
    assert result["original_0x37"] == ORIGINAL_0X37 and result["temporary_0x37"] == temporary
    assert bench.console.registers[0x37] == AUTONEG_RESET
    assert "error" not in result
    # The temporary contract had time to reach PS_RDY before the restore.
    assert ANEG_SETTLE_S in bench.clock.sleeps


def test_aneg_changes_only_the_maximum_voltage_fields(bench):
    bench.tools.bench_renegotiate("aneg")
    temporary = bytes.fromhex(bench.requests[4].split()[2])
    before = int.from_bytes(AUTONEG_RESET, "little")
    after = int.from_bytes(temporary, "little")
    # Bit 5 (Auto Compute Sink Max Voltage) cleared, bits 41:32 set to 15 V in 50 mV units.
    changed_mask = (1 << 5) | (0x3FF << 32)
    assert (before ^ after) & ~changed_mask == 0
    assert (after >> 5) & 1 == 0
    assert (after >> 32) & 0x3FF == 300
    assert registers.auto_negotiate_max_voltage(AUTONEG_RESET) == (True, 20000)
    assert registers.auto_negotiate_max_voltage(temporary) == (False, 15000)


def test_aneg_restores_0x37_when_the_first_aneg_fails(bench):
    bench.console.script["CMD ANeg"] = [["ERR TIMEOUT COMMAND did not clear within 2000 ms"]]
    result = bench.tools.bench_renegotiate("aneg")

    requests = after_hello(bench)
    assert requests[-4:] == [
        f"WR 0x37 {expected_temporary(15000)}", "CMD ANeg",   # the second step fails
        f"WR 0x37 {ORIGINAL_0X37}", "CMD ANeg",               # the restore and the second ANeg still run
    ]
    assert result["code"] == "TIMEOUT" and result["kind"] == "BenchCommandError"
    assert result["restored_0x37"] is True
    assert bench.console.registers[0x37] == AUTONEG_RESET
    assert [s["step"] for s in result["steps"]] == ["WR 0x37 temporary", "WR 0x37 restore", "CMD ANeg"]


def test_aneg_restores_0x37_when_the_first_aneg_never_answers(bench):
    bench.console.script["CMD ANeg"] = [SILENT]
    result = bench.tools.bench_renegotiate("aneg")
    assert result["kind"] == "BenchTimeoutError"
    assert result["restored_0x37"] is True
    assert bench.console.registers[0x37] == AUTONEG_RESET
    assert after_hello(bench)[-2:] == [f"WR 0x37 {ORIGINAL_0X37}", "CMD ANeg"]


def test_aneg_restores_0x37_when_the_reply_to_the_temporary_write_is_lost(bench):
    # The write reached the controller and its reply did not reach the server.
    def lost(console):
        console.registers[0x37] = bytes.fromhex(expected_temporary(15000))
        return SILENT

    bench.console.script[f"WR 0x37 {expected_temporary(15000)}"] = [lost]
    result = bench.tools.bench_renegotiate("aneg")
    assert result["kind"] == "BenchTimeoutError"
    assert result["restored_0x37"] is True
    assert bench.console.registers[0x37] == AUTONEG_RESET


def test_aneg_reports_a_restore_that_failed(bench):
    restore = f"WR 0x37 {ORIGINAL_0X37}"
    bench.console.script[restore] = [["ERR I2C register write failed"], ["ERR I2C register write failed"]]
    result = bench.tools.bench_renegotiate("aneg")

    assert result["restored_0x37"] is False
    assert "NOT restored" in result["restore_error"]
    # The result carries what a human needs to put the register back.
    assert f'bench_write(0x37, "{ORIGINAL_0X37}")' in result["restore_error"]
    assert bench.requests.count(restore) == 2           # one retry
    assert bench.requests[-1] == restore                # no second ANeg on a register in doubt


def test_aneg_retries_the_restore_once(bench):
    restore = f"WR 0x37 {ORIGINAL_0X37}"
    bench.console.script[restore] = [["ERR I2C register write failed"]]
    result = bench.tools.bench_renegotiate("aneg")
    assert result["restored_0x37"] is True and "restore_error" not in result
    assert bench.console.registers[0x37] == AUTONEG_RESET


def test_aneg_reports_a_restore_that_reads_back_differently(bench):
    restore = f"WR 0x37 {ORIGINAL_0X37}"
    other = "reg=0x37 len=24 data=" + expected_temporary(15000) + " match=0"
    bench.console.script[restore] = [[other, "OK"], [other, "OK"]]
    result = bench.tools.bench_renegotiate("aneg")
    assert result["restored_0x37"] is False
    assert "read back differently" in result["restore_error"]


def test_aneg_reports_a_failed_second_aneg(bench):
    ok = ["task=ANeg status=success rc=0x00", "data=" + "00" * 64, "OK"]
    bench.console.script["CMD ANeg"] = [ok, ["ERR I2C the controller did not answer during the task"]]
    result = bench.tools.bench_renegotiate("aneg")
    assert result["restored_0x37"] is True
    assert "second ANeg failed" in result["second_aneg_error"]
    assert result["steps"][-1]["step"] == "CMD ANeg" and "error" in result["steps"][-1]


def test_aneg_is_refused_at_the_lowest_fixed_voltage(bench):
    bench.console.registers[0x34] = bytes.fromhex("2c9181090000")  # the 5 V PDO, 0x0981912C
    result = bench.tools.bench_renegotiate("aneg")
    assert result["kind"] == "BenchRefusedError" and "no lower Fixed PDO" in result["error"]
    assert changes_board(bench.requests) == []


def test_aneg_is_refused_for_a_contract_that_is_not_fixed_supply(bench):
    bench.console.registers[0x34] = bytes.fromhex("f4b104e00000")  # SPR AVS APDO 0xE004B1F4
    result = bench.tools.bench_renegotiate("aneg")
    assert result["kind"] == "BenchRefusedError" and "0xE004B1F4" in result["error"]
    assert changes_board(bench.requests) == []


def test_aneg_with_a_given_maximum(bench):
    result = bench.tools.bench_renegotiate("aneg", max_voltage_mv=9000)
    assert result["temporary_max_voltage_mv"] == 9000
    assert f"WR 0x37 {expected_temporary(9000)}" in bench.requests
    assert "RD 0x30" not in bench.requests              # the source's PDOs are not needed

    for bad in (20000, 4950, 9010, 25000):
        refused = bench.tools.bench_renegotiate("aneg", max_voltage_mv=bad)
        assert refused["kind"] == "BenchRefusedError", bad


def test_aneg_read_failure_before_any_write(bench):
    bench.console.script["RD 0x37"] = [["ERR I2C register read failed"]]
    result = bench.tools.bench_renegotiate("aneg")
    assert result["code"] == "I2C"
    assert changes_board(bench.requests) == []
    assert "restored_0x37" not in result


# ----- the register helpers behind aneg -----

def test_fixed_pdo_voltage():
    assert registers.fixed_pdo_voltage_mv(0x0981912C) == 5000
    assert registers.fixed_pdo_voltage_mv(0x0002D12C) == 9000
    assert registers.fixed_pdo_voltage_mv(0x0004B12C) == 15000
    assert registers.fixed_pdo_voltage_mv(0x000641D6) == 20000
    assert registers.fixed_pdo_voltage_mv(0xE004B1F4) is None   # APDO
    assert registers.fixed_pdo_voltage_mv(0) is None


def test_source_caps_pdos():
    from fakes import SOURCE_CAPS_140W
    assert registers.source_caps_pdos(SOURCE_CAPS_140W) == [0x0981912C, 0x0002D12C, 0x0004B12C, 0x000641D6]
    assert registers.source_caps_pdos(b"") == []
    assert registers.source_caps_pdos(bytes([7]) + bytes(4)) == [0]  # a count the data does not cover
