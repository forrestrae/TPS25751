# USB PD capture bundles

Each bundle pairs a USB PD capture from a ChargerLAB KM003C with the TPS25751 registers
that describe the same negotiation. The native tests use them to check that each
register class decodes to the words that crossed the wire, bit for bit. The plan behind
them is Phase 5 of [km003c-pd-analyzer-toolset.md](../../../docs/plans/km003c-pd-analyzer-toolset.md).

## Index

| Bundle | Wire | Registers | Covers | Notes |
|---|---|---|---|---|
| `2026-11-23-apple-140w-tps25751` | Real: cold attach, Apple 140 W charger as source, the TPS25751 board as sink, KM003C serial 053893 firmware 2.0.0 | Synthesized from bench words in the native tests | 0x30, 0x34, 0x35 | No partner Discover Identity ACK on the wire, so no 0x48 check. Phase 7 replaces the registers with a bench `SNAP`. |

## Bundle layout

```
<yyyy-mm-dd>-<source>-<sink>/
  capture.jsonl     km003c-capture version 1, as written by the km003c server or km003c-capture
  registers.json    tps25751-registers version 1 (below)
  provenance.json   source, sink, cable, stimulus, meter, operator, and how registers.json was made
```

A bundle whose registers were not read from the board at the time of the capture says
so in `provenance.json`: `registers.synthesized` is true, `registers_synthesized_from`
names the files the bytes came from, and `registers.omitted` lists the registers left
out and why. A bundle never invents register bytes.

## registers.json

The bench console's `SNAP` command (Phase 6) emits this format directly. One object per
register, in any order:

```json
{
  "format": "tps25751-registers",
  "version": 1,
  "read_utc": "2026-11-23T14:39:06.000Z",
  "registers": [
    {"address": "0x34", "name": "ACTIVE_PDO_CONTRACT", "length": 6, "data": "d64106009800"}
  ]
}
```

| Field | Meaning |
|---|---|
| `format`, `version` | `tps25751-registers` and `1`. The generator refuses anything else. |
| `read_utc` | When the registers were read, ISO 8601 UTC; `null` when synthesized. Optional. |
| `address` | Register address as a hex string, `0x00` to `0xFF`. Each address appears once. |
| `name` | TRM register name. Optional and informative. |
| `length` | The I2C length byte the controller returned before the data, 1 to 255. |
| `data` | The data bytes that followed the length byte, as lowercase hex, exactly `length` bytes, in the order the library's register classes consume them. |

The cross-check needs 0x30 to 0x35, 0x37, 0x48, STATUS (0x1A), and ADC_RESULTS (0x6A).
`SNAP` also returns 0x03, 0x14, 0x26, 0x3F, 0x40, 0x42, and 0x69; the format takes any
address.

## Generated headers

`test/support/pd_capture_fixture.py` (Python standard library only) turns each bundle
into `test/support/native/fixtures/PdCapture_<bundle>.h`, with every non-alphanumeric
character of the bundle name replaced by `_`. The header's namespace has the same name.
It holds:

- `kSourceCapabilitiesPdos`: the PDOs of the last Source_Capabilities on SOP before the
  final Request, with the message header.
- `kRequestRdo`: the final Request on SOP, which is the last one the Source answered
  with Accept, or the last Request when none was accepted (`kRequestAccepted`).
- `kIdentityAck<Sop><Sender>Vdos`: the latest Discover Identity ACK per SOP and sender,
  with its message and VDM headers. The sender comes from the message header: power and
  data role on SOP (`SinkUfp`, `SourceDfp`), Cable Plug on SOP' and SOP'' (`CablePlug`,
  `Port`).
- `kRegister0x<addr>`: each register's data bytes, with `kRegister0x<addr>Length`.

The generator reads the `wire` hex of each `pd` record itself, so a decode error in the
meter server cannot reach the fixtures. Headers are committed, so `pio test -e native`
needs no Python. Each header records the SHA-256 of its three input files.

```bash
python3 test/support/pd_capture_fixture.py          # write the headers
python3 test/support/pd_capture_fixture.py --check  # exit 1 if a header is stale or missing
```

`--check` writes nothing. It also fails when a `PdCapture_*.h` has no bundle or when this
index does not list a bundle by its directory name in backticks. A malformed bundle
exits 2.

## Adding a bundle

1. Create the bundle directory with the three files. Copy `capture.jsonl` unchanged.
2. Run the generator from the library root and commit the header with the bundle.
3. Add a row to the index above.
4. Assert the wire words in the native suites by hand from the `usb-pd-docs` tables,
   not from the generated header, so the generator cannot hide a decode error.
