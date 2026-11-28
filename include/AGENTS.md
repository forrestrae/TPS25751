# Register classes and public headers

Guidance for the headers in this directory and their sources in `src/`. `CLAUDE.md` is a
symlink to this file; edit `AGENTS.md`.

Each header documents its own register: the TRM table and page, every field, the units,
the reset value, and what hardware showed that the TRM does not say. Read the header, not
a summary of it. This file holds only what no single header can: the index, and the facts
that span more than one register.

Before writing or changing a register class, read
[STANDARDS.md](../docs/engineering/STANDARDS.md): it names the class to copy and lists
every file a new register touches.

## Register index

`TPS25751RegisterAddress.h` is the authority for addresses and sizes. The class for a
register is `TPS25751<Name>` in `TPS25751<Name>.h`.

| Group | Registers (address, class suffix) |
|---|---|
| Identity and boot | 0x03 `Mode`, 0x06 `CustomerUse`, 0x0F `Version`, 0x2D `BootFlags` |
| 4CC tasks | 0x08 `Command`, 0x09 `Data` |
| Interrupts | 0x14 `InterruptEvent`, 0x16 `InterruptMask`, 0x18 `InterruptClear` |
| Port and power state | 0x1A `Status`, 0x26 `PowerPathStatus`, 0x3F `PowerStatus`, 0x40 `PDStatus`, 0x69 `TypeCState`, 0x72 `GPIOStatus`, 0x6A `ADCResults` |
| Configuration | 0x27 `GlobalSystemConfig`, 0x28 `PortConfig`, 0x29 `PortControl`, 0x37 `AutoNegotiateSink`, 0x42 `PD3Config`, 0x5C `IOConfig`, 0x70 `SleepControl`, 0x98 `LiquidDetectionConfig` |
| Capabilities and contract | 0x30 `ReceivedSourceCaps`, 0x31 `ReceivedSinkCaps`, 0x32 `TransmitSourceCaps`, 0x33 `TransmitSinkCaps`, 0x34 `ActivePDOContract`, 0x35 `ActiveRDOContract` |
| USB PD data blocks | 0x48 `ReceivedSOPIdentity`, 0x77 `TransmitSourceCapsExtended`, 0x78 `TransmitSourceInfo`, 0x7A `TransmitPPSStatus`, 0x7B `TransmitBatteryStatus`, 0x7D `TransmitBatteryCapabilities`, 0x7E `TransmitSinkCapsExtended` |

Writable through a `write<Name>()` method: 0x16, 0x18, 0x27, 0x28, 0x29, 0x32, 0x33, 0x37, 0x42,
0x70, 0x77, 0x78, 0x7A, 0x7B, 0x7D, 0x7E, and 0x98. `TPS25751.h` is the authority.
`CustomerUse` and `IOConfig` are read-only at runtime and have no setters.

## Facts that span registers

- PD3_CONFIGURATION (0x42) gates the controller's replies from six data blocks. With the
  bit clear the controller answers Not_Supported whatever the block holds: bit 8 for
  0x77, bit 10 for 0x7D, bit 11 for 0x7B, bit 17 for 0x7E, bit 18 for 0x78, and bit 20
  for 0x7A.
- The battery counts in 0x77 and 0x7E should agree with the populated entries of 0x7B and
  0x7D. No single register's validation can check that, and the reset values read on hardware
  disagree.
- An RDO's layout depends on the PDO it requests. Decode the own RDO of 0x35 with
  `rdoKindFor(activePdo())` from 0x34, and the requested RDO with `rdoKindFor()` of this
  device's 0x32 PDO at the RDO's Object Position. The requested RDO is a partner's
  request only while STATUS (0x1A) reports the port role as Source.
- The PDO offset is not the same in every capability register: PDO 1 is at byte 1 in
  0x30, 0x31, and 0x33, and at byte 3 in 0x32.
- A fixed PDO's port flags exist only in PDO 1. `TPS25751Pdo` takes the position and the
  sender's role for that reason.
- INT_MASK1 (0x16) and INT_CLEAR1 (0x18) mirror the bit map of INT_EVENT1 (0x14). A
  source added to one must be added to all three.
- A write to GLOBAL_SYSTEM_CONFIGURATION (0x27) disconnects and reconnects the port.
- ADC_RESULTS (0x6A) is not updated in Unconnected Sleep, and its power-path current
  needs PORT_CONTROL (0x29) Enable Current Monitor.
- 0x77 and 0x7E hold bytes 10 onward of the USB PD data block: TRM byte n is
  specification byte n + 10.

## Classes that are not registers

`TPS25751PatchLoader`, `TPS25751EepromUpdater`, `TPS25751CommandExecutor`,
`TPS25751HostExecutor`, `TPS25751DownstreamDevice`, `TPS25751BenchProtocol`,
`TPS25751BenchConsole`, and `TPS25751AppConfigImage` do not inherit `TPS25751Register` and
are not in the factory. STANDARDS.md does not govern them; their headers and ADR-007,
ADR-011, ADR-014, ADR-015, and ADR-016 in [ARCHITECTURE.md](../docs/engineering/ARCHITECTURE.md) do.

`TPS25751BenchConsole.h` is the one header `TPS25751.h` does not include; firmware that
wants the console includes it. `TPS25751BenchProtocol.h` holds the line protocol, the
`CMD` task allowlist, the `WR` register allowlist, and the snapshot register list, with no
I2C and no clock. A change to any of them is a protocol change: the host side in
`tools/pd-bench` and its README change with it.
