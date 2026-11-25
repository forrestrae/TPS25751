# Comparison rules

The rules `tps25751-bench` `bench_crosscheck` applies, written out so the comparison can
be made by hand from `km003c` `pd_capture_read` with `mode` `facts` and a
`bench_snapshot` result, and the field checks that follow a raw match.

## Reading the snapshot

Each register is lowercase hex data without the length byte. Bit n of a register is bit
`n % 8` of data byte `n // 8`, and a 32-bit word at byte offset k is little-endian: word
= byte k + 256 x byte k+1 + 65536 x byte k+2 + 16777216 x byte k+3. The `facts` hex
fields (`pdos[].hex`, `rdo.hex`, `vdos[].hex`) are already words, most significant digit
first, so compare words, not byte strings.

The board's power role is STATUS (0x1A) bit 5: 0 sink, 1 source. STATUS bit 0 is plug
present.

## Which objects to use

- The final contract is `facts.contract`; `request_index` selects the entry in
  `facts.requests`. That entry names its Source_Capabilities by `source_capabilities_i`,
  the `i` of an entry in `facts.source_capabilities`. Retransmissions are already
  counted once.
- With no `contract`, there is no completed negotiation (a Request that was rejected,
  waited, abandoned by a Soft_Reset, or never reached PS_RDY does not count): compare the
  latest Source_Capabilities only, and the verdict is at best inconclusive.
- Only Source_Capabilities sent with the Source power role count (`sender` `source/...`).
  A board that sinks never has its own capabilities compared against 0x30.
- STATUS gives the board's role at the snapshot. Each PR_Swap on SOP that the other port
  accepted flips it for everything before that Accept; the summary shows them as
  `PR_Swap` and `Accept` lines. A message sent with the board's role at its time is the
  board's. So when the board attaches as Source and then swaps to Sink, as with an
  iPhone, the final contract has the partner as Source: its Source_Capabilities go
  against 0x30 and the board's Request against 0x35 bits 31:0. The board's own
  Source_Capabilities, from the first contract, go against 0x32 in separate rows marked
  "Before PR_Swap". That first contract's Request is not compared: 0x34 and 0x35 hold the
  final contract. The same role rule decides whether a Discover Identity ACK on SOP is
  the partner's or the board's own.
- With no Source_Capabilities before the contract's Request (`aneg` sends none; any other
  stimulus means a late start), take the PDOs from the snapshot instead: 0x30 when the
  board sinks, 0x32 when it sources. Those rows compare register with register and say so.
  For `aneg` the verdict can still be pass; for a late start it is inconclusive.
- An EPR contract (`msg` EPR_Source_Capabilities, or an EPR_Request) is not compared by
  these rules; report it.
- Discover Identity ACKs are in `facts.discover_identity_acks`, keyed by SOP and then by
  sender (`source/dfp`, `sink/ufp`, `cable`, `port`). The partner's ACK is the one on SOP
  whose sender's power role is opposite to the board's: `source/...` when the board is
  sink, `sink/...` when it is source. The one whose power role equals the board's is the
  board's own ACK.

## Rows

| Wire | Register bits | Board role | Result rule |
|---|---|---|---|
| Source_Capabilities PDO count (`len(pdos)`) | 0x30 bits 2:0 | sink | equal |
| Source_Capabilities PDO count | 0x32 bits 2:0 | source | equal |
| PDO p, p from 1 to the larger count | 0x30 word at byte 1 + 4(p - 1) | sink | equal; a PDO present on one side only is a mismatch |
| PDO p | 0x32 word at byte 3 + 4(p - 1) | source | as above. 0x32 holds PDO 1's Power Path in bits 9:8 and reserved bits 23:10, so PDO 1 starts at byte 3. For PDO 1 only bits 31:30 and 22:0 must be equal |
| PDO 1 bits 29:23, one row per differing flag | 0x32 bits 53:47 | source | report only: the controller sets these flags at runtime. Likely sources, inferred and not TRM text: bit 25 Dual-Role Data from DR_Swap support (0x29 bits 15:12), bit 24 Unchunked Extended Messages from PD3_CONFIGURATION (0x42) bit 4, bit 29 Dual-Role Power from the DRP state machine (0x28) and PR_Swap support (0x29). The TRM says 0x29 bit 19 sets bit 27, Unconstrained Power |
| Request RDO | 0x35 bits 31:0 (bytes 0 to 3) | sink | equal. Bits 31:0 are the RDO this device sent as Sink |
| Request RDO | 0x35 bits 127:96 (bytes 12 to 15) | source | equal. Bits 127:96 are the partner's request only while STATUS reports Source |
| none | 0x35 bits 127:96 | sink | report only. On the sink bench they read the same word as bits 31:0 |
| PDO at the RDO's Object Position (RDO bits 31:28) | 0x34 bits 31:0 | either | equal; a position naming no PDO is a mismatch |
| PDO 1 bits 29:20 | 0x34 bits 41:32 | sink | equal. The TRM does not state this mapping; two chargers confirmed it on hardware with the board sinking |
| 0x32 PDO 1 bits 29:20 | 0x34 bits 41:32 | source | report only. One hardware observation (2026-11-25) found them equal with the board sourcing, while the PDO 1 sent on the wire differed in its runtime flags; the wire's bits 29:20 go in the note |
| Partner's ACK, `ACK` | 0x48 bits 7:6 | either | value 1 (Responder ACK received) |
| Partner's ACK, `vdo_count` | 0x48 bits 2:0 | either | equal |
| Partner's ACK, VDO k from 1 (`vdos[k-1].hex`) | 0x48 bits 8 + 32(k - 1) + 31 to 8 + 32(k - 1), that is the word at byte 1 + 4(k - 1) | either | equal |
| No partner ACK on the wire | 0x48 | either | report only: it may hold an earlier exchange |
| Board's own ACK on SOP, every SOP' and SOP'' ACK | none | either | report only, with the VDOs |
| Last `status` VBUS of the capture, or `measure` | 0x6A byte 3 x 98 mV | either | at most 98 mV plus 1% of the meter reading apart. Skipped when STATUS bit 0 reads no plug: 0x6A is not updated in Unconnected Sleep |

The cable on this bench answers Discover Identity with a PD 2.0 header and SVDM version
1.0, so its ID Header's connector type (bits 22:21) reads 00b. That is the cable, not a
decode error.

## Field checks after a raw match

For each compared word, read its fields three ways: the `facts` decode, the library's
`bench_read` `decode` lines for the register, and the `usb-pd-docs` table (call
`get_register` with the table number, or `explain_bitfield` for one field). Choose the
table from the PDO type bits (31:30, and 29:28 for an APDO; Tables 6.6 and 6.7).

| Word | `usb-pd-docs` table | Fields to check |
|---|---|---|
| PDO 1, Fixed 5 V, from a source | 6.8 | Voltage, Maximum Current, Peak Current, and the flags: Dual-Role Power, USB Suspend Supported, Unconstrained Power, USB Communications Capable, Dual-Role Data, Unchunked Extended Messages Supported, EPR Capable |
| PDO 1, Fixed 5 V, from a sink | 6.9 | Voltage, Operational Current, the flags, Fast Role Swap required current |
| Fixed PDO above 5 V | 6.10 | Voltage, Maximum Current, Peak Current; bits 29:22 are reserved outside PDO 1 |
| Battery, Variable | 6.11, 6.12 | Voltages and power or current |
| SPR PPS APDO | 6.13 (source), 6.14 (sink) | Minimum and maximum voltage (100 mV), maximum current (50 mA), PPS Power Limited |
| SPR AVS APDO | 6.15 | Peak Current, maximum currents for 15 V and 20 V |
| RDO for a Fixed or Variable PDO | 6.19 | Object Position, Operating Current, Maximum Operating Current, Capability Mismatch, USB Communications Capable, No USB Suspend, Unchunked Extended Messages Supported, EPR Capable |
| RDO for a Battery, PPS, or AVS PDO | 6.20, 6.21, 6.22 | The table's fields; the RDO kind follows the PDO it requests |
| 0x34 bits 41:32 | 6.8 | The PDO 1 flags and Peak Current the library reads from them (`firstPdoControl()`) |
| 0x48 VDO 1, ID Header | 6.34 | USB Host, USB Device, Product Type (UFP), Modal Operation, Product Type (DFP), Connector Type, USB Vendor ID |
| 0x48 VDO 2, Cert Stat | 6.38 | XID |
| 0x48 VDO 3, Product | 6.39 | USB Product ID, bcdDevice |

Decode the 0x35 RDO with the kind of the PDO it requests: the own RDO with the PDO in
0x34, the partner's RDO with the board's 0x32 PDO at the RDO's Object Position. The
library's `debugPrint()` prints only the common RDO fields; the facts and table decodes
cover the rest.

A field the library does not print is not a disagreement; say it was not checked.
