"""Register names, the snapshot file format, and the few fields the server decodes itself.

The server leaves register decoding to the library (``bench_read`` with
``decode=True``). It reads only what it needs to act: the task return code, and
for ``bench_renegotiate`` the voltages in 0x30, 0x34, and 0x37.
"""

from __future__ import annotations

import json
import os
from pathlib import Path

# TRM SLVUCR8B register names, as test/fixtures/pd-captures uses them.
REGISTER_NAMES = {
    0x03: "MODE",
    0x0F: "VERSION",
    0x14: "INT_EVENT1",
    0x1A: "STATUS",
    0x26: "POWER_PATH_STATUS",
    0x27: "GLOBAL_SYSTEM_CONFIGURATION",
    0x28: "PORT_CONFIGURATION",
    0x29: "PORT_CONTROL",
    0x2D: "BOOT_FLAGS",
    0x30: "RECEIVED_SOURCE_CAPABILITIES",
    0x31: "RECEIVED_SINK_CAPABILITIES",
    0x32: "TRANSMIT_SOURCE_CAPABILITIES",
    0x33: "TRANSMIT_SINK_CAPABILITIES",
    0x34: "ACTIVE_PDO_CONTRACT",
    0x35: "ACTIVE_RDO_CONTRACT",
    0x37: "AUTONEGOTIATE_SINK",
    0x3F: "POWER_STATUS",
    0x40: "PD_STATUS",
    0x42: "PD3_CONFIGURATION",
    0x48: "RECEIVED_SOP_IDENTITY",
    0x69: "TYPEC_STATE",
    0x6A: "ADC_RESULTS",
}

SNAPSHOT_FORMAT = "tps25751-registers"
SNAPSHOT_VERSION = 1

# Standard Task Return Code, bits 3:0 of DATA byte 1 (SLVUCR8B Table 4-1, p.61).
TASK_RESULTS = {
    0x0: "completed",
    0x1: "timed out or aborted",
    0x3: "rejected",
    0x4: "rejected, Rx buffer locked",
}

# Tasks whose DATA byte 1 is the Standard Task Return Code. MBRd returns the
# message size there instead (Table 4-12, p.73).
STANDARD_RETURN_TASKS = {"GSrC", "GSkC", "ANeg", "SSrC", "GPPI", "SRDY", "SRYR", "DBfg", "I2Cr",
                         "SWSk", "SWSr", "SWDF", "SWUF"}


def register_entry(address: int, length: int, data_hex: str) -> dict:
    """One register in the ``tps25751-registers`` form."""
    entry = {"address": f"0x{address:02X}"}
    name = REGISTER_NAMES.get(address)
    if name:
        entry["name"] = name
    entry["length"] = length
    entry["data"] = data_hex
    return entry


def parse_register_tokens(tokens: dict[str, str]) -> tuple[int, int, str]:
    """``(address, length byte, data hex)`` from a ``reg=0x34 len=6 data=...`` line."""
    try:
        address = int(tokens["reg"], 16)
        length = int(tokens["len"])
        data = tokens.get("data", "")
    except (KeyError, ValueError) as exc:
        raise ValueError(f"malformed register line: {tokens!r}") from exc
    if len(data) % 2 or data != data.lower() or any(c not in "0123456789abcdef" for c in data):
        raise ValueError(f"register 0x{address:02X}: data is not lowercase hex: {data!r}")
    return address, length, data


def snapshot_document(read_utc: str, registers: list[dict]) -> dict:
    """The ``registers.json`` document for a snapshot."""
    return {"format": SNAPSHOT_FORMAT, "version": SNAPSHOT_VERSION, "read_utc": read_utc,
            "registers": registers}


def check_snapshot_registers(registers: list[dict]) -> None:
    """Raise if a register cannot be stored as ``tps25751-registers`` version 1.

    The format takes the length byte as read, 1 to 255, followed by exactly that
    many data bytes.
    """
    for entry in registers:
        length = entry["length"]
        if not 1 <= length <= 255 or len(entry["data"]) != 2 * length:
            raise ValueError(
                f"register {entry['address']}: length byte {length} with {len(entry['data']) // 2} "
                "data bytes cannot be stored in the tps25751-registers format")


def write_snapshot(path: str, document: dict) -> tuple[str, bool]:
    """Write a snapshot as ``registers.json``; returns the absolute path and whether it replaced a file."""
    target = Path(path).expanduser()
    if target.is_dir():
        target = target / "registers.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    replaced = target.exists()
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, target)
    return str(target.resolve()), replaced


def task_result(code: str, return_code: int) -> str | None:
    """Meaning of a task's return code, for the tasks that use the standard one."""
    if code not in STANDARD_RETURN_TASKS:
        return None
    return TASK_RESULTS.get(return_code & 0x0F, f"error {return_code & 0x0F:#x}")


# ----- fields bench_renegotiate needs -----

def _bits(data: bytes, position: int, width: int) -> int:
    """A little-endian bit field: bit n of a register is bit n % 8 of byte n // 8."""
    return (int.from_bytes(data, "little") >> position) & ((1 << width) - 1)


def _set_bits(data: bytes, position: int, width: int, value: int) -> bytes:
    mask = ((1 << width) - 1) << position
    word = (int.from_bytes(data, "little") & ~mask) | ((value << position) & mask)
    return word.to_bytes(len(data), "little")


def fixed_pdo_voltage_mv(pdo: int) -> int | None:
    """Voltage of a Fixed Supply PDO in mV, or None for another PDO type.

    Bits 31:30 are 00b for Fixed Supply and bits 19:10 the voltage in 50 mV units
    (USB PD R3.3 Tables 6.6 and 6.9).
    """
    if pdo == 0 or (pdo >> 30) & 0x3 != 0:
        return None
    return ((pdo >> 10) & 0x3FF) * 50


def source_caps_pdos(data: bytes) -> list[int]:
    """The PDO words of RECEIVED_SOURCE_CAPABILITIES (0x30): a count in bits 2:0, PDO 1 at byte 1."""
    count = min(data[0] & 0x07, 7) if data else 0
    return [int.from_bytes(data[1 + 4 * i:5 + 4 * i], "little") for i in range(count)
            if len(data) >= 5 + 4 * i]


def active_pdo(data: bytes) -> int:
    """The active PDO word, bits 31:0 of ACTIVE_PDO_CONTRACT (0x34)."""
    return int.from_bytes(data[:4], "little")


# AUTONEGOTIATE_SINK (0x37), SLVUCR8B p.40.
AUTO_COMPUTE_SINK_MAX_VOLTAGE_BIT = 5   # 0 = provided by host, 1 = computed by the controller
AUTO_NEG_MAX_VOLTAGE_SHIFT = 32         # bits 41:32, 50 mV per LSB
AUTO_NEG_MAX_VOLTAGE_BITS = 10


def auto_negotiate_max_voltage(data: bytes) -> tuple[bool, int]:
    """``(computed by the controller, host-provided maximum in mV)`` from 0x37."""
    computed = bool(_bits(data, AUTO_COMPUTE_SINK_MAX_VOLTAGE_BIT, 1))
    return computed, _bits(data, AUTO_NEG_MAX_VOLTAGE_SHIFT, AUTO_NEG_MAX_VOLTAGE_BITS) * 50


def with_host_max_voltage(data: bytes, millivolts: int) -> bytes:
    """0x37 with the maximum voltage provided by the host and set to ``millivolts``."""
    changed = _set_bits(data, AUTO_COMPUTE_SINK_MAX_VOLTAGE_BIT, 1, 0)
    return _set_bits(changed, AUTO_NEG_MAX_VOLTAGE_SHIFT, AUTO_NEG_MAX_VOLTAGE_BITS, millivolts // 50)
